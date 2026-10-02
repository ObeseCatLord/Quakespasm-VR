#!/usr/bin/env python3
"""Build the controlled voice fixture from an existing voice-enabled SDL3 Meson build."""
import argparse
from pathlib import Path
import json, shlex, subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', required=True, type=Path)
parser.add_argument('--output-dir', required=True, type=Path)
args = parser.parse_args()
build = args.build_dir.resolve()
root = args.output_dir.resolve()
root.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent / 'voice_pcm_native_fixture.c'
entries=json.loads((build/'compile_commands.json').read_text())
entry=next(x for x in entries if x['file'].endswith('/Quake/voice.c'))
arguments=entry.get('arguments') or shlex.split(entry['command'])
if any(flag.startswith('-DNDEBUG') for flag in arguments):
 raise SystemExit('requires assertion-enabled DEBUG compiler flags')
if '-DUSE_VOICECHAT' not in arguments or '-DUSE_SDL3' not in arguments:
 raise SystemExit('requires a voice-enabled SDL3 Meson build')
original=entry['file']
obj=root/'voice-pcm.o'
for flag in ['-o','-MF','-MQ']:
 if flag in arguments:
  at=arguments.index(flag);arguments[at+1]=str(obj if flag!='-MF' else root/'voice-pcm.d')
arguments[arguments.index(original)]=str(source)
(root/'compile-argv.json').write_text(json.dumps(arguments,indent=2)+'\n')
subprocess.run(arguments,cwd=entry['directory'],check=True)
line=subprocess.check_output(['ninja','-C',str(build),'-t','commands','vkquake'],text=True).splitlines()[-1]
link=shlex.split(line)
exclude=['main_sdl','sv_main','cl_demo','cl_parse','voice']
for owner in exclude:
 if sum(x.endswith('Quake_'+owner+'.c.o') for x in link) != 1:
  raise SystemExit('unexpected native object graph for '+owner)
link=[x for x in link if not any(x.endswith('Quake_'+s+'.c.o') for s in exclude)]
link[link.index('-o')+1]=str(root/'voice-pcm')
link.insert(link.index('-Wl,--start-group'),str(obj))
link += ['-Wl,--wrap=Loop_Init','-Wl,--wrap=NET_CanSendMessage','-Wl,--wrap=NET_SendUnreliableMessage','-Wl,--wrap=R_TranslateNewPlayerSkin']
if '-DUSE_STEAMAUDIO' in arguments:
 link += ['-Wl,--wrap=SA_SetSource']
(root/'link-argv.json').write_text(json.dumps(link,indent=2)+'\n')
subprocess.run(link,cwd=build,check=True)
print(root/'voice-pcm')
