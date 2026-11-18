"""Stage the pinned extractor output for a Wii U build."""

import argparse
import json
import math
from pathlib import Path
import re
import sys
from tempfile import TemporaryDirectory

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
EXTRACTOR = ROOT / "tools/map-extractor"
OUTPUT = ROOT / "build/minimap"

LEVELS = {
    "Big Boo's Haunt": "LEVEL_BBH",
    "Bob-omb Battlefield": "LEVEL_BOB",
    "Bowser in the Dark World": "LEVEL_BITDW",
    "Bowser in the Dark World Arena": "LEVEL_BOWSER_1",
    "Bowser in the Fire Sea": "LEVEL_BITFS",
    "Bowser in the Fire Sea Arena": "LEVEL_BOWSER_2",
    "Bowser in the Sky": "LEVEL_BITS",
    "Bowser in the Sky Arena": "LEVEL_BOWSER_3",
    "Cool Cool Mountain": "LEVEL_CCM",
    "Cool Cool Mountain Slide": "LEVEL_CCM",
    "Dire Dire Docks": "LEVEL_DDD",
    "Hazy Maze Cave": "LEVEL_HMC",
    "Jolly Roger Bay": "LEVEL_JRB",
    "Jolly Roger Bay Ship": "LEVEL_JRB",
    "Lethal Lava Land": "LEVEL_LLL",
    "Lethal Lava Land Volcano": "LEVEL_LLL",
    "Metal Cap Cavern": "LEVEL_COTMC",
    "Peach's Castle Courtyard": "LEVEL_CASTLE_COURTYARD",
    "Peach's Castle Exterior": "LEVEL_CASTLE_GROUNDS",
    "Peach's Castle Interior 1F": "LEVEL_CASTLE",
    "Peach's Castle Interior 2F": "LEVEL_CASTLE",
    "Peach's Castle Interior BF": "LEVEL_CASTLE",
    "Princess's Secret Slide": "LEVEL_PSS",
    "Rainbow Ride": "LEVEL_RR",
    "Secret Aquarium": "LEVEL_SA",
    "Shifting Sand Land": "LEVEL_SSL",
    "Shifting Sand Land Pyramid": "LEVEL_SSL",
    "Snowman's Land": "LEVEL_SL",
    "Snowman's Land Igloo": "LEVEL_SL",
    "Tall Tall Mountain": "LEVEL_TTM",
    "Tall Tall Mountain Slide": "LEVEL_TTM",
    "Tick Tock Clock": "LEVEL_TTC",
    "Tiny Huge Island": "LEVEL_THI",
    "Tiny Huge Island Cave": "LEVEL_THI",
    "Vanish Cap Under the Moat": "LEVEL_VCUTM",
    "Wet-Dry World": "LEVEL_WDW",
    "Whomp's Fortress": "LEVEL_WF",
    "Wing Cap Tower": "LEVEL_TOTWC",
    "Wing Mario Over the Rainbow": "LEVEL_WMOTR",
}

# Colors are taken from the original 3DS minimap table when it defines one.
BACKGROUNDS = {
    ("Bob-omb Battlefield", 1): 0x0CAE1F,
    ("Cool Cool Mountain", 1): 0x848C94,
    ("Cool Cool Mountain Slide", 2): 0x181818,
    ("Hazy Maze Cave", 1): 0xA06A85,
    ("Jolly Roger Bay", 1): 0x94CEBD,
    ("Rainbow Ride", 1): 0x3152AD,
    ("Peach's Castle Exterior", 1): 0x0EBC00,
    ("Lethal Lava Land", 1): 0xAE1D01,
    ("Whomp's Fortress", 1): 0x8CBDF7,
    ("Peach's Castle Courtyard", 1): 0x52C618,
    ("Bowser in the Dark World", 1): 0x00295A,
    ("Tall Tall Mountain", 1): 0x94B5FF,
}

VARIANTS = {
    "moat_drained": "sCastleGroundsDrainedMap",
    "submarine": "sDddSubmarineMap",
    "ship_sunken": "sJrbSunkenMap",
    "ship_surfaced": "sJrbSurfacedMap",
    "ship_drained": "sJrbShipDrainedMap",
    "arena_full": "sBowser3FullMap",
    "water_0256": "sWdwWater0256Map",
    "water_1024": "sWdwWater1024Map",
    "water_1792": "sWdwWater1792Map",
    "water_2560": "sWdwWater2560Map",
    "water_2816": "sWdwWater2816Map",
    "water_low": "sWdwCityLowMap",
}
LAYERS = {
    "sBbhMaps": ("bbh_outside", "bbh_basement", "bbh_ground", "bbh_upper", "bbh_attic"),
    "sHmcMaps": ("hmc_deep", "hmc_lower", "hmc_middle", "hmc_upper"),
}


def world_bounds(frame, margin):
    x0, x1, y0, y1 = frame
    half = max(x1 - x0, y1 - y0) * margin * 50.0
    if not all(map(math.isfinite, (*frame, half))) or half <= 0:
        raise ValueError(f"Invalid map frame: {frame!r}")
    x = (x0 + x1) * 50.0
    z = -(y0 + y1) * 50.0
    return x - half, x + half, z - half, z + half


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="manifest.json emitted by the extractor")
    args = parser.parse_args()
    if not (EXTRACTOR / "sm64_map_extractor.py").is_file():
        parser.error("Initialize the extractor with git submodule update --init tools/map-extractor")
    sys.path.insert(0, str(EXTRACTOR / "tools"))
    from annotate_map_assets import MapAnnotations, renderer
    from stylize_minimap import PROCESSORS

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    catalog = renderer.map_config.load_catalog()
    expected = {name for level in catalog["levels"].values() for name in level["maps"]}
    entries = {item["name"]: item for item in manifest["maps"]}
    if (manifest.get("failures") or len(entries) != len(manifest["maps"]) or
            entries.keys() != expected or manifest["image_size"] != 480):
        parser.error("Expected a complete 480x480 render from the pinned extractor")
    margin = manifest["frame_margin"]
    if not math.isfinite(margin) or margin <= 0:
        parser.error("Invalid camera margin in extractor manifest")

    frames, targets, layer_entries, variant_entries, rows = {}, {}, {}, {}, []
    all_layers = {name for layer in LAYERS.values() for name in layer}
    level_areas = set()
    for name, item in entries.items():
        if not re.fullmatch(r"[a-z0-9_]+", name) or item["image"] != name + ".png":
            parser.error(f"Unexpected image name for {name}")
        code, area = item["level_code"], item["area"]
        if not isinstance(area, int) or area < 1 or area > 8:
            parser.error(f"Invalid area for {name}")
        targets[name] = (code, area)
        if name.startswith("bbh_"):
            frame = catalog["levels"]["bbh"]["layers"]["frame"]
        elif "frame" in item:
            frame = item["frame"]
        else:
            frame = [item["bounds"][axis] for axis in ("x_min", "x_max", "y_min", "y_max")]
        frames[name] = world_bounds(frame, margin)
        bounds = ", ".join(f"{value:.2f}f" for value in frames[name])
        background = 0x241E2A if name.startswith("bbh_") else BACKGROUNDS.get(
            (item["level"], area), 0x0C0E18)
        initializer = f'{{ "gfx/textures/minimap/{name}.png", 0x{background:06X}, {bounds} }}'
        if name in all_layers:
            layer_entries[name] = initializer
        elif item.get("variant"):
            key = VARIANTS[item["variant"]]
            if key in variant_entries:
                parser.error(f"Duplicate variant {key}")
            variant_entries[key] = initializer
        else:
            key = LEVELS[item["level"]], area
            if key in level_areas:
                parser.error(f"Duplicate base map for {key}")
            level_areas.add(key)
            rows.append(f"    {{ {key[0]}, {area}, {initializer} }},")
    if layer_entries.keys() != all_layers or variant_entries.keys() != set(VARIANTS.values()):
        parser.error("The render does not contain all supported layers and variants")

    class BuildAnnotations(MapAnnotations):
        def __init__(self):
            # Use the actual render frames rather than the older checked-in Wii U table.
            self.root, self.frames, self.targets = ROOT, frames, targets

    annotations = BuildAnnotations()
    header = [
        "/* Generated by tools/prepare_wiiu_minimaps.py. */",
        "static const struct WiiUMinimapEntry sGeneratedMaps[] = {",
        *rows, "};",
    ]
    for key, initializer in variant_entries.items():
        header.append(f"static const struct WiiUMinimapInfo {key} = {initializer};")
    for key, names in LAYERS.items():
        header.append(f"static const struct WiiUMinimapInfo {key}[] = {{")
        header.extend(f"    {layer_entries[name]}," for name in names)
        header.append("};")

    OUTPUT.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(dir=OUTPUT) as directory:
        temporary = Path(directory)
        for name, item in entries.items():
            with Image.open(args.manifest.parent / item["image"]) as source:
                if source.size != (480, 480) or source.format != "PNG":
                    parser.error(f"Invalid source image for {name}")
                image = source.convert("RGBA")
            annotations.draw_doors(image, annotations.marker_segments(name, image.size))
            style = renderer.map_config.map_settings(item["level_code"], name)["style"]
            base = Image.new("RGBA", image.size, (232, 229, 216, 255))
            base.alpha_composite(image)
            painted = PROCESSORS[style["name"]](base.convert("RGB"), style["seed"]).convert("RGBA")
            painted.putalpha(image.getchannel("A"))
            painted.save(temporary / item["image"], optimize=True)
        pack_list = []
        for name in sorted(entries):
            filename = name + ".png"
            (temporary / filename).replace(OUTPUT / filename)
            pack_list.append(f"build/minimap/{filename} gfx/textures/minimap/{filename}")
        for filename, lines in (("wiiu_minimap_generated.inc.h", header), ("pack.lst", pack_list)):
            path = temporary / filename
            path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
            path.replace(OUTPUT / filename)
    print(f"Staged {len(entries)} maps and their coordinate table in {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
