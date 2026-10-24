from pathlib import Path
import subprocess
import tempfile
import unittest
from test_dynos_model import geometry
ROOT=Path(__file__).resolve().parents[2]
class DynosNpcTest(unittest.TestCase):
    def test_runtime_npc_fallback_rebinding_and_shortcut_persistence(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            for pack,data in [('A broken',b'bad'),('B valid',geometry(parents=[-1,0,0])),('C unused',b'bad')]:
                file=folder/'dynos/packs'/pack/'actors/toad_geo.bin';file.parent.mkdir(parents=True);file.write_bytes(data)
            player=folder/'dynos/packs/Player/mario_geo.bin';player.parent.mkdir(parents=True);player.write_bytes(geometry())
            includes=['-I.','-Iinclude','-Isrc','-DDYNOS=1','-DEXTERNAL_DATA=1','-DUSE_SYSTEM_MALLOC=1']
            objects=[]
            for source in ['src/pc/fs/fs.c','src/pc/fs/fs_packtype_dir.c','src/pc/fs/fs_packtype_zip.c','src/pc/fs/dirtree.c','src/pc/platform.c']:
                obj=folder/(Path(source).stem+'.o');objects.append(str(obj))
                subprocess.run(['gcc',*includes,'-D_LANGUAGE_C=1','-c',source,'-o',str(obj)],cwd=ROOT,check=True)
            exe=folder/'runtime'
            subprocess.run(['g++','-std=c++17',*includes,'-D_LANGUAGE_C_PLUS_PLUS=1','-DF3DEX_GBI_2=1','-DF3DEX_GBI_2E=1',
                            'src/pc/dynos/dynos.cpp','src/pc/dynos/dynos_graph.cpp','src/pc/dynos/dynos_model.cpp',
                            'tools/tests/dynos_runtime_test.cpp',*objects,'-o',str(exe)],cwd=ROOT,check=True)
            result=subprocess.run([str(exe),str(folder)],cwd=ROOT,check=True,capture_output=True,text=True)
            self.assertIn('A broken/actors/toad_geo.bin reason=',result.stdout)
            self.assertIn('DYNOS_NPC_BOUND actor=toad_geo path=dynos/packs/B valid/',result.stdout)
            self.assertNotIn('C unused/actors/toad_geo.bin',result.stdout)
    def test_native_actor_hierarchy_switches_lods_and_cycles(self):
        with tempfile.TemporaryDirectory() as temporary:
            exe=Path(temporary)/'graph'
            subprocess.run(['g++','-std=c++17','-I.','-Iinclude','-Isrc','-D_LANGUAGE_C_PLUS_PLUS=1',
                            'src/pc/dynos/dynos_graph.cpp','tools/tests/dynos_graph_test.cpp','-o',str(exe)],cwd=ROOT,check=True)
            subprocess.run([str(exe)],check=True)
    def test_physical_shortcut_edges_and_held_buttons(self):
        with tempfile.TemporaryDirectory() as temporary:
            exe=Path(temporary)/'shortcut'
            subprocess.run(['gcc','-Isrc','tools/tests/dynos_shortcut_test.c','-o',str(exe)],cwd=ROOT,check=True)
            subprocess.run([str(exe)],check=True)
if __name__=='__main__':unittest.main()
