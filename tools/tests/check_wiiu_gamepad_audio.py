"""Exercise the real DRC filter/capture on a host and compile with Wii U WUT."""
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

ROUTING = r'''
#include <assert.h>
#include <string.h>
#include "src/pc/audio/audio_wiiu_gamepad.c"
static AXDeviceFinalMixCallback callback;
static unsigned registrations;
int AXGetDeviceFinalMixCallback(unsigned device, AXDeviceFinalMixCallback *out) {
    assert(device == AX_DEVICE_TYPE_DRC); *out = callback; return 0;
}
int AXRegisterDeviceFinalMixCallback(unsigned device, AXDeviceFinalMixCallback cb) {
    assert(device == AX_DEVICE_TYPE_DRC); callback = cb; ++registrations; return 0;
}
int main(void) {
    int32_t samples[8][144]; int32_t *channels[8];
    for (int i=0;i<8;++i) channels[i]=samples[i];
    struct GamePadFinalMix mix={channels,4,96,2,4};
    int16_t alert[192];
    for (int i=0;i<96;++i) {alert[2*i]=i*100;alert[2*i+1]=-i*100;}
    wiiu_gamepad_audio_init(false);
    assert(callback && registrations==1);
    memset(samples,0x12,sizeof(samples));
    callback(&mix); assert(samples[0][0]==0x12121212); // Mirror unaltered
    wiiu_gamepad_audio_set_mode(true);
    callback(&mix); // acknowledge mode transition
    wiiu_gamepad_audio_queue(alert,96);callback(&mix);
    for (int i=0;i<96;++i) {
        assert(samples[0][i]==alert[2*i] && samples[1][i]==alert[2*i+1]);
        for (int ch=2;ch<8;++ch) assert(samples[ch][i]==0);
    }
    callback(&mix); // no repeated audio on underrun
    for (int ch=0;ch<8;++ch)for(int i=0;i<96;++i)assert(samples[ch][i]==0);
    mix.samples=144;wiiu_gamepad_audio_queue(alert,96);callback(&mix);
    for (int i=0;i<143;++i)assert(samples[0][i]==i*200/3);
    assert(sRead==sWritten);
    mix.samples=96;
    for(int round=0;round<100;++round){
        wiiu_gamepad_audio_queue(alert,96);callback(&mix);
        for(int i=0;i<96;++i)assert(samples[0][i]==alert[2*i]);
    }
    wiiu_gamepad_audio_queue(alert,96);
    wiiu_gamepad_audio_set_foreground(false);callback(&mix);
    for(int i=0;i<96;++i)assert(samples[0][i]==0);
    wiiu_gamepad_audio_set_foreground(true);callback(&mix);
    for(int i=0;i<96;++i)assert(samples[0][i]==0); // no stale alarm after HOME
    wiiu_gamepad_audio_queue(alert,4097); // oversize blocks are rejected before reading
    callback(&mix);assert(sRead==sWritten);
    wiiu_gamepad_audio_set_mode(false);
    memset(samples,0x23,sizeof(samples));callback(&mix);
    assert(samples[0][0]==0x23232323);
    wiiu_gamepad_audio_shutdown();assert(callback==0 && registrations==2);
}
'''

CAPTURE = r'''
#include <assert.h>
#include "src/pc/mixer.c"
int main(void) {
    int16_t game[64]={0},alert[64];
    const uint16_t left=0x4c0,right=0x600;
    mixer_gamepad_capture_begin(game,alert,32);
    mixer_gamepad_chunk(game,16,left,right);
    for(int i=0;i<16;++i){BUF_S16(left)[i]=300;BUF_S16(right)[i]=-500;}
    mixer_gamepad_note_begin(false);
    for(int i=0;i<16;++i){BUF_S16(left)[i]+=100;BUF_S16(right)[i]-=100;}
    mixer_gamepad_note_end();
    for(int i=0;i<64;++i)assert(alert[i]==0); // ordinary SFX/music excluded
    mixer_gamepad_note_begin(true);
    for(int i=0;i<16;++i){BUF_S16(left)[i]+=1200;BUF_S16(right)[i]+=700;}
    mixer_gamepad_note_end();
    for(int i=0;i<16;++i){
        assert(alert[2*i]==1200 && alert[2*i+1]==700);
        assert(BUF_S16(left)[i]==1600 && BUF_S16(right)[i]==100); // TV mix unchanged
    }
    for(int i=32;i<64;++i)assert(alert[i]==0);
    mixer_gamepad_chunk(game+32,16,left,right);
    mixer_gamepad_note_begin(true);
    for(int i=0;i<16;++i){BUF_S16(left)[i]+=500;BUF_S16(right)[i]-=200;}
    mixer_gamepad_note_end();
    for(int i=16;i<32;++i)assert(alert[2*i]==500 && alert[2*i+1]==-200);
    mixer_gamepad_capture_end();
    mixer_gamepad_note_begin(true);mixer_gamepad_note_end(); // disabled safely

    // Run the actual envelope mixer with/without capture. Full TV DMEM and
    // envelope state must stay byte-identical, not just the observed samples.
    memset(&rspa,0,sizeof(rspa));
    rspa.in=0;rspa.out=left;rspa.dry_right=right;rspa.nbytes=32;
    rspa.vol[0]=rspa.vol[1]=4096;rspa.target[0]=rspa.target[1]=4096;
    rspa.rate[0]=rspa.rate[1]=65536;rspa.vol_dry=32767;
    for(int i=0;i<16;++i){BUF_S16(0)[i]=1000+i*30;BUF_S16(left)[i]=100;BUF_S16(right)[i]=-100;}
    unsigned char original[sizeof(rspa)],reference[sizeof(rspa)];
    memcpy(original,&rspa,sizeof(rspa));
    ENVMIX_STATE baseline={0},captured={0};
    aEnvMixerImpl(A_INIT,baseline);memcpy(reference,&rspa,sizeof(rspa));
    memcpy(&rspa,original,sizeof(rspa));
    mixer_gamepad_capture_begin(game,alert,16);mixer_gamepad_chunk(game,16,left,right);
    mixer_gamepad_note_begin(true);aEnvMixerImpl(A_INIT,captured);mixer_gamepad_note_end();
    assert(memcmp(reference,&rspa,sizeof(rspa))==0);
    assert(memcmp(baseline,captured,sizeof(baseline))==0);
    assert(alert[0]>0 && alert[1]>0);
    mixer_gamepad_capture_end();
}
'''


def run_host(directory):
    includes = directory / 'include'
    for name, content in {
        'coreinit/cache.h': '#define OSMemoryBarrier() __sync_synchronize()\n',
        'sndcore2/device.h': '''#include <stdint.h>
typedef void (*AXDeviceFinalMixCallback)(void*);
#define AX_DEVICE_TYPE_DRC 1
#define WUT_CHECK_OFFSET(...)
#define WUT_CHECK_SIZE(...)
int AXGetDeviceFinalMixCallback(unsigned,AXDeviceFinalMixCallback*);
int AXRegisterDeviceFinalMixCallback(unsigned,AXDeviceFinalMixCallback);
''',
        'whb/log.h': '#define WHBLogPrint(...) ((void)0)\n#define WHBLogPrintf(...) ((void)0)\n',
    }.items():
        path = includes / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    classifier = (ROOT/'src/audio/external.c').read_text()
    classifier = classifier[classifier.index('s32 audio_channel_is_drowning_alert('):]
    classifier = classifier[:classifier.index('\n}\n')+3]
    classifier = '''
#include <assert.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint32_t u32; typedef int32_t s32;
#include "include/sounds.h"
#define FALSE 0
#define SEQ_PLAYER_SFX 2
struct SequenceChannel {int id;} moving,other;
static u8 sMaxChannelsForSoundBank[10]={1,1,1,1,1,1,1,1,1,1};
static u8 sUsedChannelsForSoundBank[10]={1,1,1,1,1,1,1,1,1,1};
static u8 sCurrentSound[10][4];
static struct {u32 soundBits;} sSoundBanks[10][40];
static struct {struct SequenceChannel *channels[16];} gSequencePlayers[3];
''' + classifier + '''
int main(void){
    gSequencePlayers[SEQ_PLAYER_SFX].channels[1]=&moving;
    sCurrentSound[1][0]=0;
    sSoundBanks[1][0].soundBits=SOUND_MOVING_ALMOST_DROWNING;
    assert(audio_channel_is_drowning_alert(&moving));
    assert(!audio_channel_is_drowning_alert(&other));
    sSoundBanks[1][0].soundBits=SOUND_MOVING_AIM_CANNON;
    assert(!audio_channel_is_drowning_alert(&moving));
    sCurrentSound[1][0]=0xff;
    assert(!audio_channel_is_drowning_alert(&moving));
}
'''
    for name, source in (('routing', ROUTING), ('capture', CAPTURE), ('classifier', classifier)):
        file = directory / f'{name}.c'
        file.write_text(source)
        output = directory / name
        subprocess.run(['gcc', '-std=c11', '-O1', '-g', '-fsanitize=undefined',
                        '-DTARGET_WII_U=1', '-DWIIU_MINIMAP=1', '-DVERSION_US=1', '-DNO_SEGMENTED_MEMORY',
                        '-D_LANGUAGE_C', '-DF3DEX_GBI_2E',
                        '-I'+str(includes), '-I.', '-Iinclude', '-Isrc', str(file),
                        '-o', str(output)], cwd=ROOT, check=True)
        subprocess.run([str(output)], check=True, timeout=20)
    print('PASS: DRC-only routing, 32/48kHz, ring wrap, underrun, HOME, mirror restore; isolated alert capture leaves TV mix unchanged', flush=True)


def check_wut():
    sources = ['src/pc/audio/audio_wiiu_gamepad.c', 'src/pc/audio/audio_sdl2.c',
               'src/pc/pc_main.c', 'src/pc/mixer.c', 'src/audio/external.c',
               'src/audio/playback.c', 'src/audio/synthesis.c']
    targets = ['build/us_wiiu/' + source.removesuffix('.c') + '.o' for source in sources]
    command = ['make', '-n', 'TARGET_WII_U=1', 'VERSION=us', 'EXTERNAL_DATA=1',
               'HIGH_FPS_PC=1', 'WIIU_MINIMAP=1', 'DYNOS=1', 'EXT_DEBUG_MENU=1']
    for source in sources:
        command.extend(['-W', source])
    result = subprocess.run(command + targets, cwd=ROOT, capture_output=True, text=True, check=True)
    checked = 0
    for line in result.stdout.splitlines():
        if not line.startswith('powerpc-eabi-gcc '):
            continue
        tokens = iter(shlex.split(line))
        syntax = []
        for token in tokens:
            if token in ('-o', '-MF'):
                next(tokens)
            elif token not in ('-c', '-MMD'):
                syntax.append(token)
        subprocess.run(syntax + ['-fsyntax-only'], cwd=ROOT, check=True)
        checked += 1
    assert checked == len(targets), (checked, len(targets))
    print('PASS: actual Wii U compiler, WUT callback ABI and modified audio sources', flush=True)


def main():
    with tempfile.TemporaryDirectory(prefix='sm64-gamepad-audio-') as temporary:
        run_host(Path(temporary))
    check_wut()


if __name__ == '__main__':
    main()
