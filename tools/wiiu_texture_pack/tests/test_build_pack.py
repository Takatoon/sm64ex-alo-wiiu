from __future__ import annotations

import importlib.util
import io
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

import numpy as np
from PIL import Image


MODULE_PATH = Path(__file__).resolve().parents[1] / "build_pack.py"
SPEC = importlib.util.spec_from_file_location("wiiu_texture_pack_build", MODULE_PATH)
assert SPEC and SPEC.loader
build = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = build
SPEC.loader.exec_module(build)


def png_bytes(image: Image.Image) -> bytes:
    stream = io.BytesIO()
    image.save(stream, "PNG")
    return stream.getvalue()


class BuildPackTests(unittest.TestCase):
    def make_assets(self, root: Path, entries: dict[str, list[int]]) -> Path:
        path = root / "assets.json"
        path.write_text(json.dumps(entries), encoding="utf-8")
        return path

    def make_zip(self, root: Path, entries: list[tuple[str, bytes]]) -> Path:
        path = root / "input.zip"
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in entries:
                archive.writestr(name, data)
        return path

    def run_build(
        self, root: Path, source: Path, assets: Path, base_zip: Path | None = None
    ):
        output = root / "output.zip"
        report = root / "report.json"
        result = build.build_pack(source, assets, output, report, base_zip=base_zip)
        return output, report, result

    def test_rejects_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_zip(root, [("../gfx/actors/bad.png", b"bad")])
            assets = self.make_assets(root, {})
            with self.assertRaisesRegex(build.PackError, r"\.\."):
                self.run_build(root, source, assets)

    def test_rejects_duplicate_and_case_collision(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = png_bytes(Image.new("RGBA", (2, 2), "red"))
            source = self.make_zip(
                root,
                [
                    ("gfx/actors/a.png", image),
                    ("gfx/actors/A.png", image),
                    ("gfx/actors/a.png", image),
                ],
            )
            assets = self.make_assets(root, {"actors/a.png": [2, 2]})
            with self.assertRaisesRegex(build.PackError, "collision|duplicate"):
                self.run_build(root, source, assets)

    def test_requires_known_dimensions(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = png_bytes(Image.new("RGBA", (8, 8), "red"))
            source = self.make_zip(root, [("gfx/textures/unknown.png", image)])
            assets = self.make_assets(root, {})
            with self.assertRaisesRegex(build.PackError, "no original dimensions"):
                self.run_build(root, source, assets)

    def test_assets_record_without_width_and_height_stays_unresolved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = png_bytes(Image.new("RGBA", (8, 8), "red"))
            source = self.make_zip(root, [("gfx/levels/special.png", image)])
            assets = self.make_assets(
                root, {"levels/special.png": [128, {"us": [1234]}]}
            )
            with self.assertRaisesRegex(build.PackError, "no original dimensions"):
                self.run_build(root, source, assets)

    def test_scales_from_original_dimensions_and_uses_store(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source_image = Image.new("RGB", (64, 32), (20, 40, 60))
            source = self.make_zip(
                root, [("gfx/actors/test/texture.rgba16.png", png_bytes(source_image))]
            )
            assets = self.make_assets(root, {"actors/test/texture.rgba16.png": [8, 4]})
            output, report_path, report = self.run_build(root, source, assets)

            self.assertEqual(report["entries"][0]["input_size"], [64, 32])
            self.assertEqual(report["entries"][0]["output_size"], [24, 12])
            with zipfile.ZipFile(output) as archive:
                info = archive.infolist()[0]
                self.assertEqual(info.compress_type, zipfile.ZIP_STORED)
                with archive.open(info) as stream:
                    with Image.open(stream) as converted:
                        self.assertEqual(converted.size, (24, 12))
                        self.assertEqual(converted.mode, "RGBA")
            self.assertEqual(json.loads(report_path.read_text())["profile"], "full-720")

    def test_does_not_upscale_small_input(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_zip(
                root,
                [("gfx/textures/small.png", png_bytes(Image.new("RGBA", (8, 4), "red")))],
            )
            assets = self.make_assets(root, {"textures/small.png": [8, 8]})
            _, _, report = self.run_build(root, source, assets)
            entry = report["entries"][0]
            self.assertEqual(entry["output_size"], [8, 4])
            self.assertEqual(entry["profile_action"], "preserve-source-below-target")

    def test_downscale_preserves_aspect_ratio(self):
        self.assertEqual(build.fit_without_upscale((512, 256), (192, 192)), (192, 96))

    def test_half_source_profile_halves_x8_texture(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_zip(
                root,
                [("gfx/textures/x8.png", png_bytes(Image.new("RGBA", (64, 32), "red")))],
            )
            assets = self.make_assets(root, {"textures/x8.png": [8, 4]})
            output = root / "half.zip"
            report_path = root / "half-report.json"
            report = build.build_pack(
                source,
                assets,
                output,
                report_path,
                profile="half-source",
            )
            entry = report["entries"][0]
            self.assertEqual(entry["output_size"], [32, 16])
            self.assertEqual(entry["profile_action"], "resize-half-source")
            self.assertEqual(report["source_dimension_multiplier"], 0.5)
            self.assertEqual(report["minimum_scale_factor"], 3)

    def test_half_source_profile_uses_3x_floor_and_does_not_upscale(self):
        self.assertEqual(build.half_source_with_minimum((32, 16), (24, 12)), (24, 12))
        self.assertEqual(build.half_source_with_minimum((24, 12), (24, 12)), (24, 12))
        self.assertEqual(build.half_source_with_minimum((16, 8), (24, 12)), (16, 8))

    def test_quarter_source_profile_reduces_x16_to_x4(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_zip(
                root,
                [("gfx/textures/x16.png", png_bytes(Image.new("RGBA", (128, 64), "red")))],
            )
            assets = self.make_assets(root, {"textures/x16.png": [8, 4]})
            output = root / "quarter.zip"
            report_path = root / "quarter-report.json"
            report = build.build_pack(
                source,
                assets,
                output,
                report_path,
                profile="quarter-source",
            )
            entry = report["entries"][0]
            self.assertEqual(entry["output_size"], [32, 16])
            self.assertEqual(entry["profile_action"], "resize-quarter-source")
            self.assertEqual(report["source_dimension_multiplier"], 0.25)
            self.assertEqual(report["minimum_scale_factor"], 3)

    def test_quarter_source_profile_uses_3x_floor_and_does_not_upscale(self):
        self.assertEqual(build.quarter_source_with_minimum((64, 32), (24, 12)), (24, 12))
        self.assertEqual(build.quarter_source_with_minimum((32, 16), (24, 12)), (24, 12))
        self.assertEqual(build.quarter_source_with_minimum((24, 12), (24, 12)), (24, 12))
        self.assertEqual(build.quarter_source_with_minimum((16, 8), (24, 12)), (16, 8))

    def test_max_6x_profile_optimizes_castle_grounds_only(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            x8_path = "gfx/textures/x8.rgba16.png"
            x4_path = "gfx/textures/x4.rgba16.png"
            castle_path = "gfx/levels/castle_grounds/0.rgba16.png"
            outside_path = "gfx/textures/outside/castle_grounds.00000.rgba16.png"
            irregular_path = "gfx/textures/outside/castle_grounds.0B000.rgba16.png"
            source = self.make_zip(
                root,
                [
                    (x8_path, png_bytes(Image.new("RGBA", (64, 64), "red"))),
                    (x4_path, png_bytes(Image.new("RGBA", (32, 32), "blue"))),
                    (castle_path, png_bytes(Image.new("RGBA", (64, 64), "green"))),
                    (outside_path, png_bytes(Image.new("RGBA", (64, 64), "yellow"))),
                    (irregular_path, png_bytes(Image.new("RGBA", (256, 128), "black"))),
                ],
            )
            base_zip = root / "base.zip"
            with zipfile.ZipFile(base_zip, "w") as archive:
                archive.writestr(x8_path, png_bytes(Image.new("RGBA", (8, 8))))
                archive.writestr(x4_path, png_bytes(Image.new("RGBA", (8, 8))))
                archive.writestr(castle_path, png_bytes(Image.new("RGBA", (8, 8))))
                archive.writestr(outside_path, png_bytes(Image.new("RGBA", (8, 8))))
                archive.writestr(
                    irregular_path, png_bytes(Image.new("RGBA", (16, 32)))
                )
            assets = self.make_assets(root, {})
            output = root / "max-6x.zip"
            report_path = root / "max-6x-report.json"

            report = build.build_pack(
                source,
                assets,
                output,
                report_path,
                profile="max-6x",
                base_zip=base_zip,
            )

            entries = {entry["path"]: entry for entry in report["entries"]}
            self.assertEqual(entries[x8_path]["output_size"], [48, 48])
            self.assertEqual(entries[x4_path]["output_size"], [32, 32])
            self.assertEqual(entries[castle_path]["output_size"], [32, 32])
            self.assertEqual(entries[outside_path]["output_size"], [32, 32])
            self.assertEqual(entries[irregular_path]["output_size"], [96, 48])
            self.assertEqual(
                entries[x4_path]["profile_action"], "preserve-source-below-target"
            )
            self.assertEqual(
                entries[castle_path]["profile_action"],
                "resize-half-source-castle-grounds",
            )
            self.assertEqual(
                entries[outside_path]["profile_action"],
                "resize-half-source-castle-grounds",
            )
            self.assertEqual(
                entries[irregular_path]["profile_action"],
                "resize-linear-premultiplied",
            )
            self.assertEqual(report["scale_factor"], 6)

    def test_transparent_edge_does_not_leak_hidden_colour(self):
        image = Image.new("RGBA", (2, 1))
        image.putdata([(255, 0, 0, 255), (0, 255, 0, 0)])
        resized = build.resize_linear_premultiplied(image, (12, 6))
        pixels = np.asarray(resized)
        partially_transparent = pixels[(pixels[..., 3] > 0) & (pixels[..., 3] < 255)]
        self.assertGreater(len(partially_transparent), 0)
        self.assertTrue(np.all(partially_transparent[:, 1] <= 1))

    def test_base_zip_supplies_dimensions_and_filters_other_forks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_zip(
                root,
                [
                    ("gfx/actors/current.png", png_bytes(Image.new("RGBA", (32, 16), "red"))),
                    ("gfx/actors/other-fork.png", png_bytes(Image.new("RGBA", (32, 32), "blue"))),
                ],
            )
            base_zip = root / "base.zip"
            with zipfile.ZipFile(base_zip, "w") as archive:
                archive.writestr(
                    "gfx/actors/current.png", png_bytes(Image.new("RGBA", (4, 2), "red"))
                )
                archive.writestr(
                    "gfx/textures/base-only.png",
                    png_bytes(Image.new("RGBA", (8, 8), "green")),
                )
            assets = self.make_assets(root, {})
            output, _, report = self.run_build(root, source, assets, base_zip)
            self.assertEqual(report["entries"][0]["output_size"], [12, 6])
            self.assertEqual(
                report["unsupported_pack_entries"], ["gfx/actors/other-fork.png"]
            )
            self.assertEqual(
                report["inventory"]["pack_replacement_assets"],
                ["gfx/actors/current.png"],
            )
            self.assertEqual(
                report["inventory"]["base_only_assets"],
                ["gfx/textures/base-only.png"],
            )
            self.assertEqual(report["summary"]["base_only_asset_count"], 1)
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(archive.namelist(), ["gfx/actors/current.png"])

    def test_removes_build_only_skybox_sources_but_keeps_runtime_tiles(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source_skybox = "gfx/textures/skyboxes/water.png"
            runtime_tile = "gfx/textures/skybox_tiles/water.0.rgba16.png"
            image = png_bytes(Image.new("RGBA", (32, 32), "blue"))
            source = self.make_zip(
                root,
                [(source_skybox, image), (runtime_tile, image)],
            )
            base_zip = root / "base.zip"
            with zipfile.ZipFile(base_zip, "w") as archive:
                # Old base.zip files included both paths. The converter must
                # still reject the build-only panorama when one is reused.
                archive.writestr(source_skybox, image)
                archive.writestr(runtime_tile, image)
            assets = self.make_assets(root, {})

            output, _, report = self.run_build(root, source, assets, base_zip)

            with zipfile.ZipFile(output) as archive:
                self.assertEqual(archive.namelist(), [runtime_tile])
            self.assertIn(source_skybox, report["skipped_entries"])
            self.assertEqual(
                report["inventory"]["pack_replacement_assets"], [runtime_tile]
            )

    def test_hybrid_profile_preserves_only_allowlisted_hud(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            hud_path = "gfx/textures/segment2/hud.png"
            level_path = "gfx/levels/test/ground.png"
            source = self.make_zip(
                root,
                [
                    (hud_path, png_bytes(Image.new("RGBA", (32, 16), "red"))),
                    (level_path, png_bytes(Image.new("RGBA", (32, 32), "blue"))),
                ],
            )
            assets = self.make_assets(
                root,
                {
                    "textures/segment2/hud.png": [4, 2],
                    "levels/test/ground.png": [4, 4],
                },
            )
            ui_paths = root / "ui_paths.txt"
            ui_paths.write_text(hud_path + "\n", encoding="utf-8")
            output = root / "output.zip"
            report_path = root / "report.json"
            report = build.build_pack(
                source,
                assets,
                output,
                report_path,
                profile="full-720-hd-hud",
                ui_paths=ui_paths,
            )
            by_path = {entry["path"]: entry for entry in report["entries"]}
            self.assertEqual(by_path[hud_path]["output_size"], [32, 16])
            self.assertEqual(by_path[hud_path]["profile_action"], "preserve-source-hd")
            self.assertEqual(by_path[level_path]["output_size"], [12, 12])
            self.assertEqual(report["summary"]["preserved_hd_hud_count"], 1)

    def test_clean_profile_copies_only_release_paths_byte_for_byte(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            retained_path = "gfx/actors/current.png"
            retained_png = png_bytes(Image.new("P", (7, 5), 1))
            source = self.make_zip(
                root,
                [
                    (retained_path, retained_png),
                    ("gfx/actors/other-fork.png", png_bytes(Image.new("RGBA", (8, 8)))),
                    ("readme.txt", b"not needed at runtime"),
                ],
            )
            base_zip = root / "base.zip"
            with zipfile.ZipFile(base_zip, "w") as archive:
                archive.writestr(
                    retained_path, png_bytes(Image.new("RGBA", (4, 4), "red"))
                )
            assets = self.make_assets(root, {})
            output = root / "clean.zip"
            report_path = root / "clean-report.json"
            report = build.build_pack(
                source,
                assets,
                output,
                report_path,
                profile="clean-ex-alo",
                base_zip=base_zip,
            )
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(archive.namelist(), [retained_path])
                self.assertEqual(archive.read(retained_path), retained_png)
            self.assertEqual(report["entries"][0]["output_mode"], "P")
            self.assertEqual(report["entries"][0]["profile_action"], "copy-source-clean")
            self.assertEqual(report["summary"]["copied_clean_png_count"], 1)
            self.assertEqual(report["summary"]["removed_input_entry_count"], 2)

    def test_two_builds_are_identical(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = self.make_zip(
                root,
                [
                    ("notes.txt", b"ignored"),
                    ("gfx/textures/b.png", png_bytes(Image.new("RGBA", (4, 4), "blue"))),
                    ("gfx/actors/a.png", png_bytes(Image.new("RGBA", (4, 4), "red"))),
                ],
            )
            assets = self.make_assets(
                root, {"textures/b.png": [4, 4], "actors/a.png": [4, 4]}
            )
            first_output, first_report, _ = self.run_build(root, source, assets)
            first_zip_bytes = first_output.read_bytes()
            first_report_bytes = first_report.read_bytes()
            first_output.unlink()
            first_report.unlink()
            second_output, second_report, _ = self.run_build(root, source, assets)
            self.assertEqual(first_zip_bytes, second_output.read_bytes())
            self.assertEqual(first_report_bytes, second_report.read_bytes())


if __name__ == "__main__":
    unittest.main()
