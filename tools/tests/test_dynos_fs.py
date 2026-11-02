import pathlib
import subprocess
import tempfile
import unittest
import zipfile

ROOT=pathlib.Path(__file__).resolve().parents[2]


class DynosFilesystemTest(unittest.TestCase):
    def test_texture_pack_policy_and_live_file_handles(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=pathlib.Path(temporary)
            for pack,files in [('base',{'gfx/shared.bin':b'0','gfx/base_only.bin':b'0'}),
                               ('A',{'gfx/shared.bin':b'A','gfx/only_a.bin':b'A'}),
                               ('B',{'gfx/shared.bin':b'B'}),
                               ('models',{'dynos/packs/Zip/mario_geo.bin':b'M'})]:
                with zipfile.ZipFile(folder/(pack+'.zip'),'w',compression=zipfile.ZIP_DEFLATED) as z:
                    for name,data in files.items():z.writestr(name,data)
            loose=folder/'0.v1/gfx/extra.bin';loose.parent.mkdir(parents=True);loose.write_bytes(b'F')
            exe=folder/'mods-fs'
            subprocess.run(['gcc','-Iinclude','-Isrc','-D_LANGUAGE_C=1','-Wno-cpp',
                            'src/pc/fs/fs.c','src/pc/fs/fs_packtype_dir.c',
                            'src/pc/fs/fs_packtype_zip.c','src/pc/fs/dirtree.c',
                            'src/pc/platform.c','tools/tests/mods_fs_test.c','-o',str(exe)],cwd=ROOT,check=True)
            subprocess.run([str(exe),str(folder)],check=True,cwd=ROOT)

    def test_scoped_model_layouts_and_no_sd_root_walk(self):
        for legacy in (False,True):
            with self.subTest(legacy=legacy),tempfile.TemporaryDirectory() as temporary:
                folder=pathlib.Path(temporary)
                resources=folder/'sm64ex_res' if legacy else folder
                physical={
                    resources/'dynos/packs/Loose/mario_geo.bin':b'L',
                    resources/'dynos/packs/Sea/manta_seg5_geo_05008D14.bin':b'M',
                    resources/'dynos/packs/Loose/main.lua':b'S',
                    resources/'dynos/packs/Loose/sound/jump.ogg':b'V',
                    folder/'unrelated/toad_geo.bin':b'IGNORE',
                    resources/'outside/luigi_geo.bin':b'IGNORE',
                    folder/'sm64config.txt':b'CONFIG',
                }
                if legacy:
                    physical[folder/'dynos/packs/Loose/mario_geo.bin']=b'WRONG_ROOT'
                    physical[folder/'dynos/packs/OnlyRoot/wario_geo.bin']=b'IGNORE'
                    physical[folder/'dynos/packs/Loose/main.lua']=b'WRONG_ROOT'
                    physical[folder/'dynos/packs/Loose/sound/jump.ogg']=b'WRONG_ROOT'
                    physical[folder/'gfx/only_root.bin']=b'WRONG_ROOT'
                for file,data in physical.items():file.parent.mkdir(parents=True,exist_ok=True);file.write_bytes(data)
                archives=resources if legacy else folder/'mods';archives.mkdir(exist_ok=True)
                with zipfile.ZipFile(archives/'Actors.zip','w') as z:
                    z.writestr('Classic/luigi_geo.bin',b'Z')
                    z.writestr('dynos/packs/Loose/mario_geo.bin',b'ZIP_SHADOW')
                    z.writestr('../escape/wario_geo.bin',b'IGNORE')
                with zipfile.ZipFile(archives/'Texture.zip','w') as z:z.writestr('gfx/shared.bin',b'T')
                with zipfile.ZipFile(resources/'base.zip','w') as z:
                    z.writestr('gfx/base_only.bin',b'B');z.writestr('ignored/wario_geo.bin',b'IGNORE')
                exe=folder/'model-layout'
                flags=['-DWIIU_LEGACY_PATHS=1'] if legacy else []
                subprocess.run(['gcc','-Iinclude','-Isrc','-D_LANGUAGE_C=1','-Wno-cpp',
                                '-DFS_BASEDIR="sm64ex_res"',*flags,
                                'src/pc/fs/fs.c','src/pc/fs/fs_packtype_dir.c','src/pc/fs/fs_packtype_zip.c',
                                'src/pc/fs/dirtree.c','src/pc/platform.c','tools/tests/dynos_paths_test.c',
                                '-o',str(exe)],cwd=ROOT,check=True)
                subprocess.run([str(exe),str(folder)],cwd=ROOT,check=True)

    def test_archive_root_with_reused_heap_memory(self):
        with tempfile.TemporaryDirectory() as temporary:
            exe=pathlib.Path(temporary)/'dirtree-test'
            subprocess.run(['gcc','-Iinclude','-Isrc','-D_LANGUAGE_C=1','-Wno-cpp',
                            'tools/tests/dynos_dirtree_test.c','-o',str(exe)],
                           cwd=ROOT,check=True)
            subprocess.run([str(exe)],check=True,cwd=ROOT)

    def test_discovery_and_reading_loose_and_compressed_zip(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=pathlib.Path(temporary)
            loose=folder/'dynos/packs/Loose/mario_geo.bin'
            loose.parent.mkdir(parents=True);loose.write_bytes(b'MODEL_BINARY')
            decoy=folder/'unrelated/luigi_geo.bin';decoy.parent.mkdir();decoy.write_bytes(b'IGNORE')
            archive=folder/'test.zip'
            with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as z:
                z.writestr('ZipMario/mario_geo.bin',b'MODEL_BINARY')
            exe=folder/'fs-test'
            subprocess.run(['gcc','-Iinclude','-Isrc','-D_LANGUAGE_C=1','-Wno-cpp',
                            'src/pc/fs/fs.c','src/pc/fs/fs_packtype_dir.c',
                            'src/pc/fs/fs_packtype_zip.c','src/pc/fs/dirtree.c',
                            'src/pc/platform.c','tools/tests/dynos_fs_test.c','-o',str(exe)],
                           cwd=ROOT,check=True)
            subprocess.run([str(exe),str(folder),str(archive)],check=True,cwd=ROOT)


if __name__=='__main__':unittest.main()
