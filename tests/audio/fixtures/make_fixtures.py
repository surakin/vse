#!/usr/bin/env python3
"""Makes the Ogg Vorbis fixtures of tests/audio (Phase 26 Day 432). Needs oggenc (libvorbis 1.3.7) and, for the 5.1 stream, ffmpeg built with libvorbis.
Run from this directory: python3 make_fixtures.py. The content is generated here (sines, seeded noise, clicks): no third-party recordings."""
import math, random, struct, subprocess, wave, os

def write_wav(path, rate, channels, frames):
    with wave.open(path, 'wb') as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(rate)
        data = bytearray()
        for f in frames:
            for v in f:
                data += struct.pack('<h', max(-32767, min(32767, int(v * 32767))))
        w.writeframes(bytes(data))

def sine(rate, seconds, hz, amp=0.5):
    return [amp * math.sin(2 * math.pi * hz * i / rate) for i in range(int(rate * seconds))]

def enc(wav, ogg, *args):
    subprocess.check_call(['oggenc', '-q', '3', '-Q', '-o', ogg] + list(args) + [wav])

random.seed(20261117)
# 1. a mono sine, 44.1 kHz, one second, with tags
write_wav('sine_mono.wav', 44100, 1, [(v,) for v in sine(44100, 1.0, 440)])
enc('sine_mono.wav', 'sine_mono.ogg', '-t', 'Sine', '-a', 'Fixture Generator', '-G', 'test')
# 2. seeded noise, 44.1 kHz, half a second (long blocks only, dense spectrum)
write_wav('noise_mono.wav', 44100, 1, [((random.random() * 2 - 1) * 0.4,) for _ in range(22050)])
enc('noise_mono.wav', 'noise_mono.ogg')
# 3. a stereo mix, 48 kHz: 330 Hz left, 550 Hz right plus a shared 110 Hz (coupled channels)
l = sine(48000, 1.0, 330, 0.3); r = sine(48000, 1.0, 550, 0.3); b = sine(48000, 1.0, 110, 0.2)
write_wav('stereo_mix.wav', 48000, 2, [(l[i] + b[i], r[i] + b[i]) for i in range(len(l))])
enc('stereo_mix.wav', 'stereo_mix.ogg')
# 4. a tone with clicks, 44.1 kHz: forces short and long blocks to mix
t = sine(44100, 1.0, 220, 0.3)
for k in range(0, len(t), 7000):
    for j in range(40):
        t[min(len(t) - 1, k + j)] += 0.7 * (1 if j % 2 else -1)
write_wav('transient_mix.wav', 44100, 1, [(max(-1, min(1, v)),) for v in t])
enc('transient_mix.wav', 'transient_mix.ogg')
# 5. 5.1 at 48 kHz, 0.3 s: each channel its own frequency (FL 220, FR 330, FC 440, LFE 55, BL 660, BR 880), in WAV channel order
freqs = [220, 330, 440, 55, 660, 880]
chans = [sine(48000, 0.3, f, 0.3) for f in freqs]
write_wav('surround51.wav', 48000, 6, [tuple(c[i] for c in chans) for i in range(len(chans[0]))])
subprocess.check_call(['ffmpeg', '-y', '-loglevel', 'error', '-i', 'surround51.wav', '-c:a', 'libvorbis', '-q:a', '3', 'surround51.ogg'])
# 6. streams the engine refuses: three channels (no layout), a 4 kHz rate (below 8 kHz), and FLAC in an Ogg container (not Vorbis)
c3 = [sine(48000, 0.1, f, 0.3) for f in (220, 330, 440)]
write_wav('three_channels.wav', 48000, 3, [tuple(c[i] for c in c3) for i in range(len(c3[0]))])
subprocess.check_call(['ffmpeg', '-y', '-loglevel', 'error', '-i', 'three_channels.wav', '-c:a', 'libvorbis', '-q:a', '3', 'three_channels.ogg'])
write_wav('rate_4000.wav', 4000, 1, [(v,) for v in sine(4000, 0.2, 300)])
subprocess.check_call(['ffmpeg', '-y', '-loglevel', 'error', '-i', 'rate_4000.wav', '-c:a', 'libvorbis', '-q:a', '3', 'rate_4000.ogg'])
subprocess.check_call(['ffmpeg', '-y', '-loglevel', 'error', '-i', 'sine_mono.wav', '-t', '0.1', '-c:a', 'flac', '-f', 'ogg', 'flac_in_ogg.ogg'])
for f in os.listdir('.'):
    if f.endswith('.wav'):
        os.remove(f)
