"""Check NPC BINs against animation hierarchies in this port's original geos."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]

def native_signature(folder,root):
    source=subprocess.run(['gcc','-E','-P','-x','c','-DVERSION_US',str(ROOT/'actors'/folder/'geo.inc.c')],capture_output=True,text=True,check=True).stdout
    arrays=dict(re.findall(r'const\s+GeoLayout\s+(\w+)\[\]\s*=\s*\{(.*?)\};',source,re.S))
    def tree(name,active=()):
        if name in active:raise ValueError('native geo cycle')
        result=[];stack=[result];last=None
        for op,args in re.findall(r'GEO_(\w+)\s*\(([^)]*)\)',arrays[name]):
            if op=='OPEN_NODE':stack.append(last[1]);last=None
            elif op=='CLOSE_NODE':
                # Native geos may reopen the last node or contain redundant closes.
                if len(stack)>1:stack.pop()
                last=stack[-1][-1] if stack[-1] else None
            elif op in ('BRANCH','BRANCH_AND_LINK'):
                stack[-1].extend(tree(args.split(',')[-1].strip(),active+(name,)));last=None
            elif op not in ('END','RETURN'):
                last=[op,[]];stack[-1].append(last)
        return result
    def signature(nodes,parent=-1,base=0):
        result=[];i=0
        while i<len(nodes):
            op,children=nodes[i];joint=parent
            if op=='ANIMATED_PART':joint=base+len(result);result.append(parent)
            if op=='SWITCH_CASE':
                alternatives=[signature([child],joint,base+len(result)) for child in children]
                if joint==-1 and base==0 and not result:alternatives=[a for a in alternatives if a] or [[]]
                if not alternatives or any(a!=alternatives[0] for a in alternatives):raise ValueError('native switch hierarchy differs')
                sub=alternatives[0]
            else:sub=signature(children,joint,base+len(result))
            if op=='RENDER_RANGE':
                while i+1<len(nodes) and nodes[i+1][0]=='RENDER_RANGE':
                    i+=1
                    if signature(nodes[i][1],parent,base+len(result))!=sub:raise ValueError('native LOD hierarchy differs')
            result+=sub;i+=1
        return result
    return signature(tree(root))

if __name__=='__main__':
    exe=ROOT/'build/dynos-reader'
    subprocess.run(['g++','-std=c++17','-O1','-ffast-math','-D_LANGUAGE_C_PLUS_PLUS=1','-DF3DEX_GBI_2=1','-DF3DEX_GBI_2E=1','-Iinclude','-Isrc',
                    'src/pc/dynos/dynos_model.cpp','tools/tests/dynos_model_test.cpp','-o',str(exe)],cwd=ROOT,check=True)
    targets={'peach_geo':('peach','peach_geo'),'toad_geo':('toad','toad_geo'),
             'bowser_geo':('bowser','bowser_geo'),'bowser2_geo':('bowser','bowser_geo_no_shadow')}
    failed=False
    with tempfile.TemporaryDirectory() as temporary:
        for argument in sys.argv[1:]:
            path=Path(argument).resolve();folder,root=targets[path.stem]
            parents=native_signature(folder,root)
            expected=Path(temporary)/'hierarchy.txt';expected.write_text(' '.join(map(str,parents)))
            result=subprocess.run([str(exe),str(path),str(expected)],capture_output=True,text=True)
            print(('PASS' if result.returncode==0 else 'REJECT'),path.name,'native_joints='+str(len(parents)),(result.stdout+result.stderr).strip(),flush=True)
            failed|=result.returncode!=0
    sys.exit(int(failed))
