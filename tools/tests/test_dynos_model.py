"""Compile/run the native reader against valid, compressed and invalid streams."""
import pathlib
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[2]
PARENTS = [-1,0,1,2,2,4,5,6,2,8,9,10,1,12,13,14,1,16,17,18]


def name(value):
    raw = value.encode()
    return bytes([len(raw)]) + raw


def geometry(extra=(),parents=PARENTS):
    words = [11, 4, *extra]
    def joint(i):
        words.extend((0x113, 0, 0, 4))
        for j, parent in enumerate(parents):
            if parent == i:
                joint(j)
        words.append(5)
    if parents:joint(0)
    words.extend((5, 1))
    return b'\x05' + name('mario_geo') + struct.pack('<I',len(words)) + struct.pack('<'+'I'*len(words),*words)


class NativeReaderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.folder = pathlib.Path(cls.temporary.name)
        cls.exe = cls.folder/'reader'
        subprocess.run(['g++','-std=c++17','-O1','-ffast-math','-D_LANGUAGE_C_PLUS_PLUS=1',
                        '-DF3DEX_GBI_2=1','-DF3DEX_GBI_2E=1', '-Iinclude','-Isrc',
                        'src/pc/dynos/dynos_model.cpp','tools/tests/dynos_model_test.cpp',
                        '-o',str(cls.exe)],cwd=ROOT,check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def check(self,data,accepted,message='',expected_hierarchy=None):
        file=self.folder/'model.bin';file.write_bytes(data)
        args=[str(self.exe),str(file)]
        if expected_hierarchy is not None:
            expected=self.folder/'hierarchy.txt';expected.write_text(' '.join(map(str,expected_hierarchy)))
            args.append(str(expected))
        result=subprocess.run(args,capture_output=True,text=True)
        self.assertEqual(result.returncode,0 if accepted else 2,result.stdout+result.stderr)
        if message:self.assertIn(message,result.stderr)

    def test_native_and_compressed_geometry(self):
        data=geometry();self.check(data,True)
        self.check(b'DYNOSBIN'+struct.pack('<Q',len(data))+zlib.compress(data),True)

    def test_truncation_bad_size_and_unknown_geometry(self):
        self.check(geometry()[:-1],False,'truncated')
        self.check(b'DYNOSBIN'+struct.pack('<Q',64*1024*1024),False,'size limit')
        self.check(geometry([255]),False,'geometry opcode')

    def test_bad_reference_and_callback(self):
        # A callback marker is a variable-length word; replace a plain command
        # with a 2-word ASM while retaining a valid surrounding skeleton.
        words=geometry([0x18,0x434e5546,999])
        # The record word count excludes the marker's callback-index payload.
        pos=1+len(name('mario_geo'))
        words=words[:pos]+struct.pack('<I',struct.unpack_from('<I',words,pos)[0]-1)+words[pos+4:]
        self.check(words,False,'callback')
        bad=b'\x05'+name('mario_geo')+struct.pack('<I',3)+struct.pack('<II',2,0x52544e50)+name('missing')+struct.pack('<II',0,1)
        self.check(bad,False,'reference')

    def test_wrong_animation_hierarchy(self):
        bad=b'\x05'+name('mario_geo')+struct.pack('<III',2,11,1)
        self.check(bad,False,'20-joint')

    def test_npc_geometry_matches_its_original_actor(self):
        data=geometry(parents=[-1,0,0])
        self.check(data,False,'20-joint')
        self.check(data,True,expected_hierarchy=[-1,0,0])
        self.check(data,False,'original actor',expected_hierarchy=[-1,0,1])
        self.check(geometry(parents=[]),True,expected_hierarchy=[])

    def test_npc_callbacks_preserve_command_context(self):
        data=geometry([0x18,0x434e5546,38]);pos=1+len(name('mario_geo'))
        data=data[:pos]+struct.pack('<I',struct.unpack_from('<I',data,pos)[0]-1)+data[pos+4:]
        self.check(data,True,expected_hierarchy=PARENTS)
        data=data.replace(struct.pack('<II',0x434e5546,38),struct.pack('<II',0x434e5546,40))
        self.check(data,False,'callback context',expected_hierarchy=PARENTS)

    def test_npc_whole_actor_shadow_state_and_partial_skip(self):
        data=geometry();pos=1+len(name('mario_geo'))+4
        joints=list(struct.unpack('<'+'I'*((len(data)-pos)//4),data[pos:]))[2:-2]
        words=[11,4,0x0e,0x434e5546,15,4,11,4,*joints,5,11,5,5,1]
        def stream(values):
            return b'\x05'+name('mario_geo')+struct.pack('<I',len(values)-1)+struct.pack('<'+'I'*len(values),*values)
        self.check(stream(words),True,expected_hierarchy=PARENTS)
        self.check(stream(words),False,'switch changes')
        words[-2:-2]=[0x113,0,0]
        self.check(stream(words),False,'partial empty actor',expected_hierarchy=PARENTS+[-1])

    def test_callback_context_and_null_callback(self):
        words=geometry([0x18,0x434e5546,7]);pos=1+len(name('mario_geo'))
        words=words[:pos]+struct.pack('<I',struct.unpack_from('<I',words,pos)[0]-1)+words[pos+4:]
        self.check(words,False,'callback context')
        self.check(geometry([0x18,0]),False,'missing geometry callback')

    def test_native_vertex_endianness_and_ambient_padding(self):
        ambient=b'\x13'+name('test_ambient')+bytes([1,2,3,4,5,6])
        vertex=b'\x03'+name('test_vertex')+struct.pack('<IhhhhhhbbbB',1,-3,17,-200,0,-50,31,-128,64,0,255)
        self.check(ambient+vertex+geometry(),True)

    def test_cyclic_display_list(self):
        loop=b'\x04'+name('cycle')+struct.pack('<III',2,0xde000000,0x52544e50)+name('cycle')+struct.pack('<III',0,0xdf000000,0)
        self.check(loop+geometry(),False,'cyclic')

    def test_float_vertices_round_to_native_coordinates(self):
        marker=struct.pack('<8H',0x3346,0x5632,0x5854,0,0,0,0,0)
        vertex=struct.pack('<fffhhhbbbB',-3.4,16.8,-200.1,0,-50,31,-128,64,0,255)
        record=b'\x03'+name('test_vertex')+struct.pack('<I',2)+marker+vertex
        self.check(record+geometry(),True)
        self.check(b'DYNOSBIN'+struct.pack('<Q',len(record+geometry()))+zlib.compress(record+geometry()),True)
        self.check(record[:-1],False,'truncated')

    def test_float_vertices_reject_invalid_coordinates(self):
        marker=struct.pack('<8H',0x3346,0x5632,0x5854,0,0,0,0,0)
        for value in (float('nan'),float('inf'),float('-inf'),32768.,-32769.):
            vertex=struct.pack('<fffhhhbbbB',value,0.,0.,0,0,0,0,0,0,255)
            record=b'\x03'+name('bad_vertex')+struct.pack('<I',2)+marker+vertex
            self.check(record+geometry(),False,'coordinate')

    def test_texture_aliases_resolve_and_reject_cycles(self):
        png=b'\x89PNG\r\n\x1a\n'+b'\0'*8+struct.pack('>II',1,1)+b'\0'*9
        texture=b'\x02'+name('test_texture')+struct.pack('<I',len(png))+png
        alias=b'\x02'+name('test_alias')+struct.pack('<I',0x52584554)+name('test_texture')
        dl=b'\x04'+name('test_alias_dl')+struct.pack('<III',2,0xfd100000,0x52544e50)+name('test_alias')+struct.pack('<III',0,0xdf000000,0)
        self.check(alias+texture+dl+geometry(),True)
        self.check(alias+geometry(),False,'texture alias')
        cycle=b'\x02'+name('test_texture')+struct.pack('<I',0x52584554)+name('test_alias')
        self.check(alias+cycle+geometry(),False,'cyclic texture alias')

    def test_unused_secondary_texture_does_not_replace_primary_uv_bounds(self):
        commands=[0xfc127e24,0xfffffbfd,0xf5000100,0x07000000,
                  0xf4000000,0x07000000,0xf2000000,(1020<<12)|1020,
                  0xf2000000,0x01000000|(124<<12)|124,0xdf000000,0]
        def display(values):
            return b'\x04'+name('test_unused_secondary')+struct.pack('<I',len(values)//2)+struct.pack('<'+'I'*len(values),*values)
        self.check(display(commands)+geometry(),True)
        commands[0]=0xfc227e24 # The combiner samples TEXEL1: different UVs remain unsupported.
        self.check(display(commands)+geometry(),False,'independent secondary')
        self.check(display(commands[2:])+geometry(),False,'independent secondary')

    def test_palette_copies_refresh_native_light_state(self):
        dl=b'\x04'+name('test_palette')+struct.pack('<IIIIIII',3,0xd2090600,0,0xd20c0900,0,0xdf000000,0)
        self.check(dl+geometry(),True)

    def test_real_player_palette_pairs_keep_matching_ambient(self):
        # VC21 Mario: pants then shirt. The ambient source crosses the integer
        # division boundary used to identify the preceding directional source.
        commands=[0xd20c060a,1,0xd20f090a,1,
                  0xd212060a,1,0xd215090a,1,0xdf000000,0]
        dl=b'\x04'+name('test_real_palette')+struct.pack('<I',5)+struct.pack('<10I',*commands)
        self.check(dl+geometry(),True)

    def test_switch_alternative_with_wrong_joint_count(self):
        data=geometry();pos=1+len(name('mario_geo'))+4
        words=list(struct.unpack('<'+'I'*((len(data)-pos)//4),data[pos:]))
        switched=[11,4,0x0e,0x434e5546,7,4,11,4,*words[2:-2],5,11,5,5,1]
        stream=b'\x05'+name('mario_geo')+struct.pack('<I',len(switched)-1)+struct.pack('<'+'I'*len(switched),*switched)
        self.check(stream,False,'switch changes')


if __name__=='__main__':unittest.main()
