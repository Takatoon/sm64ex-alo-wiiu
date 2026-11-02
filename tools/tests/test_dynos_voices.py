"""Static Lua extraction plus real Ogg decoding/mixing with synthetic tones."""
from pathlib import Path
import subprocess
import tempfile
import unittest
import zipfile
ROOT=Path(__file__).resolve().parents[2]
class DynosVoiceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.folder=Path(cls.temp.name)
        cls.parser=cls.folder/'manifest'
        subprocess.run(['g++','-std=c++17','-Isrc','-Iinclude','src/pc/dynos/dynos_voice_manifest.cpp',
                        'tools/tests/dynos_voice_manifest_test.cpp','-o',str(cls.parser)],cwd=ROOT,check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def parse(self,lua,model='mario_geo',accepted=True):
        file=self.folder/'main.lua';file.write_text(lua)
        r=subprocess.run([str(self.parser),str(file),model],capture_output=True,text=True)
        self.assertEqual(r.returncode,0 if accepted else 2,r.stdout+r.stderr)
        return r.stdout
    def test_association_does_not_borrow_another_players_table(self):
        lua="""local M = smlua_model_util_get_id('mario_geo')
local L = smlua_model_util_get_id('luigi_geo')
local MARIO = {[CHAR_SOUND_HERE_WE_GO]='mario.ogg'}
local LUIGI = {[CHAR_SOUND_HERE_WE_GO]='luigi.ogg'}
_G.charSelect.character_add_voice(M, MARIO)
_G.charSelect.character_add_voice(L, LUIGI)"""
        self.assertIn('mario.ogg',self.parse(lua));self.assertIn('luigi.ogg',self.parse(lua,'luigi_geo'))
        self.parse(lua,'wario_geo',False)
    def test_arrays_aliases_and_static_audio_load_calls(self):
        result=self.parse("""local jump='jump.ogg'
local M=smlua_model_util_get_id('mario_geo')
local V={[CHAR_SOUND_YAH_WAH_HOO]={jump,audio_sample_load('hoo.ogg')},[CHAR_SOUND_DYING]='die.ogg'}
local alias=V
charSelect.character_add_voice(M,alias)""")
        self.assertIn('jump.ogg\thoo.ogg',result);self.assertIn('die.ogg',result)
    def test_single_table_without_registration(self):
        self.assertIn('jump.ogg',self.parse("local V={[CHAR_SOUND_YAH_WAH_HOO]='jump.ogg'}"))
    def test_comments_strings_and_long_comments_are_not_code(self):
        self.assertIn('jump.ogg',self.parse("""--[=[ local BAD={[CHAR_SOUND_DYING]='evil.ogg'} ]=]
local description="character_add_voice(fake, x)"
-- [CHAR_SOUND_DYING]='another.ogg'
local V={[CHAR_SOUND_YAH_WAH_HOO]='jump.ogg'}"""))
    def test_dynamic_expressions_are_not_executed_or_treated_as_prefixes(self):
        self.parse("local V={[CHAR_SOUND_DYING]='die'..suffix}",accepted=False)
        self.parse("local V={[CHAR_SOUND_DYING]=choose_file()}",accepted=False)
        self.parse("local V={[CHAR_SOUND_DYING]=os.execute('anything')}",accepted=False)
    def test_traversal_absolute_paths_and_control_characters_rejected(self):
        for path in ['../outside.ogg','/outside.ogg','SD:/file.ogg','sound/../file.ogg','x\\\\bad.ogg','x\\n.ogg']:
            with self.subTest(path=path):self.parse("local V={[CHAR_SOUND_DYING]='"+path+"'}",accepted=False)
    def test_ambiguous_tables_or_registrations_rejected(self):
        self.parse("local A={[CHAR_SOUND_DYING]='a.ogg'} local B={[CHAR_SOUND_DYING]='b.ogg'}",accepted=False)
        self.parse("local V={[CHAR_SOUND_DYING]='a.ogg'} character_add_voice(smlua_model_util_get_id('mario_geo'),V) character_add_voice(smlua_model_util_get_id('mario_geo'),V)",accepted=False)
    def test_bounds_and_malformed_tables(self):
        for lua in ["local V={[CHAR_SOUND_DYING]='x.ogg'",'local V='+('{'*40)+("'x.ogg'")+('}'*40),'x'*(1024*1024+1),"--[[unterminated"]:
            with self.subTest(length=len(lua)):self.parse(lua,accepted=False)
    def test_real_ogg_decode_resample_mix_interrupt_and_memory_fallback(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);includes=['-I.','-Iinclude','-Isrc','-DDYNOS=1','-DEXTERNAL_DATA=1']
            objects=[]
            for source in ['src/pc/fs/fs.c','src/pc/fs/fs_packtype_dir.c','src/pc/fs/fs_packtype_zip.c','src/pc/fs/dirtree.c','src/pc/platform.c']:
                obj=folder/(Path(source).stem+'.o');objects.append(str(obj))
                subprocess.run(['gcc',*includes,'-D_LANGUAGE_C=1','-c',source,'-o',str(obj)],cwd=ROOT,check=True)
            exe=folder/'voices'
            subprocess.run(['g++','-std=c++17','-O2','-ffast-math',*includes,'-D_LANGUAGE_C_PLUS_PLUS=1',
                            'src/pc/dynos/dynos_voice.cpp','src/pc/dynos/dynos_voice_manifest.cpp',
                            'tools/tests/dynos_voice_test.cpp',*objects,'-lvorbisfile','-lvorbisenc','-lvorbis','-logg','-o',str(exe)],cwd=ROOT,check=True)
            subprocess.run([str(exe),str(folder)],cwd=ROOT,check=True)
            with zipfile.ZipFile(folder/'voices.zip','w',compression=zipfile.ZIP_DEFLATED) as z:
                z.writestr('Packed/main.lua',"local V={[CHAR_SOUND_HERE_WE_GO]='jump.ogg'}")
                z.writestr('Packed/sound/jump.ogg',(folder/'dynos/packs/Voice/sound/jump.ogg').read_bytes())
            subprocess.run([str(exe),str(folder),'zip'],cwd=ROOT,check=True)
if __name__=='__main__':unittest.main()
