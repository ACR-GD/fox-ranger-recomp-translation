#!/usr/bin/env python3
"""boot_smoke.py - end-to-end check of the recompiled game, headless.

Boots FOX_EN_FINAL.EXE with the deterministic virtual clock, skips the intro
(SPACE at 20 s), opens the menu (ENTER 26 s), starts the game (ENTER 29 s) and
compares the title screen (25 s) and a Stage 1 gameplay frame (41 s) with the
reference images in tests/ref/ (bit-exact: headless runs are deterministic).
Also checks that the AdLib music produced sound.

usage: boot_smoke.py FOXRT GAME_DIR EXE [--update]   (--update rewrites the references)
"""
import math, os, shutil, struct, subprocess, sys, tempfile, wave

def ppm_pixels(p):
    return open(p, 'rb').read().split(b'\n', 3)[3]

def main():
    foxrt, game, exe = sys.argv[1:4]
    update = '--update' in sys.argv
    ref = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ref')
    tmp = tempfile.mkdtemp()
    shots = {'title_25s': 25, 'stage1_41s': 41}
    cmd = [foxrt, '--headless', '--game', game, '--exe', exe, '--seconds', '42',
           '--keys', '20:SPACE,26:ENTER,29:ENTER', '--wav', os.path.join(tmp, 'a.wav'),
           '--shots', ','.join(f'{t}:{os.path.join(tmp, n + ".ppm")}' for n, t in shots.items())]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    ok = r.returncode == 0
    print(f'foxrt exit code {r.returncode}')
    for n in shots:
        got = os.path.join(tmp, n + '.ppm')
        if update:
            shutil.copy(got, os.path.join(ref, n + '.ppm')); print(f'updated {n}'); continue
        a, b = ppm_pixels(got), ppm_pixels(os.path.join(ref, n + '.ppm'))
        diff = sum(1 for i in range(0, min(len(a), len(b)), 3) if a[i:i + 3] != b[i:i + 3])
        print(f'{n}: {diff} of {len(b) // 3} pixels differ from reference')
        ok &= diff == 0 and len(a) == len(b)
    w = wave.open(os.path.join(tmp, 'a.wav')); n = w.getnframes()
    v = struct.unpack(f'<{n * 2}h', w.readframes(n))
    rms = math.sqrt(sum(x * x for x in v) / len(v))
    print(f'audio: {n / w.getframerate():.1f} s, RMS {rms:.0f}')
    ok &= rms > 200
    shutil.rmtree(tmp)
    print('PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)

if __name__ == '__main__':
    main()
