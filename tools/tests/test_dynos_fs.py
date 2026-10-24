import pathlib
import subprocess
import tempfile
import unittest
import zipfile

ROOT=pathlib.Path(__file__).resolve().parents[2]


class DynosFilesystemTest(unittest.TestCase):
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
