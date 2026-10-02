#!/usr/bin/env python3
"""Prepare original short music signals for the native EOF/format fixture."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import wave

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir', required=True, type=Path,
                    help='new private directory; existing paths are refused')
args = parser.parse_args()
root = args.output_dir.resolve()
root.mkdir(parents=True, exist_ok=False)
music = root / 'music'
music.mkdir()
for name, rate, width, channels in [
    ('native-u8.wav', 11025, 1, 1),
    ('native-mono.wav', 22050, 2, 1),
    ('native-stereo.wav', 48000, 2, 2),
]:
    pcm = bytearray()
    for i in range(int(rate * .04)):
        signal = math.sin(2 * math.pi * 440 * i / rate)
        for channel in range(channels):
            sample = int(signal * (10000 if channel == 0 else 3000))
            pcm.extend(struct.pack('<h', sample) if width == 2 else
                       bytes([128 + int(sample / 256)]))
    with wave.open(str(music / name), 'wb') as output:
        output.setnchannels(channels)
        output.setsampwidth(width)
        output.setframerate(rate)
        output.writeframes(pcm)

receipts = []
for name, rate, codec, options in [
    ('native-flac.flac', 44100, 'flac', []),
    ('native-vorbis.ogg', 44100, 'libvorbis', ['-q:a', '4']),
    ('native-mp3.mp3', 48000, 'libmp3lame', ['-b:a', '128k']),
    ('native-opus.opus', 48000, 'libopus', ['-b:a', '96k']),
]:
    argv = ['ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error', '-n',
            '-i', str(music / 'native-stereo.wav'), '-map', '0:a:0', '-vn',
            '-ar', str(rate), '-ac', '2', '-c:a', codec, '-threads', '1']
    argv += options + [str(music / name)]
    with (root / (name + '.encode.log')).open('w') as log:
        result = subprocess.run(argv, stdout=log, stderr=subprocess.STDOUT)
    receipts.append({'argv': argv, 'exit': result.returncode})
    (root / 'asset-generation.json').write_text(json.dumps(receipts, indent=2))
    if result.returncode:
        raise SystemExit('CPU audio conversion failed; see private encode log')

(root / 'assets.json').write_text(json.dumps({
    file.name: {'bytes': file.stat().st_size,
                'sha256': hashlib.sha256(file.read_bytes()).hexdigest()}
    for file in sorted(music.iterdir())
}, indent=2))
print(music)
