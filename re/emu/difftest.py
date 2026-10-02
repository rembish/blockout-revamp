"""Differential test: original BL2.OVL (emulated) vs port/core on random input scripts.

usage: difftest.py [n_runs] [first_seed]

Covered: movement, diagonals, rotations, hard drop, gravity, landing, layer clears,
scoring, level ups, spawn/game over, practice mode, both CPU classes, any frame rate.
Not covered (code-read only): pause (P), sound toggle (O), abort (Esc), blocking sound
durations, hall of fame.
"""
import os, random, subprocess, sys, tempfile
from bl2emu import Game

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.join(HERE, '..', '..', 'port')
REPLAY = os.path.join(tempfile.gettempdir(), 'bo_replay')
BOTGEN = os.path.join(tempfile.gettempdir(), 'bo_botgen')


def bot_script(rng):
    """A script from port/tests/botgen: a search bot that clears layers."""
    args = [rng.randint(3, 5), rng.randint(3, 5), rng.randint(6, 12), rng.randint(0, 2), rng.randint(0, 9),
            rng.randint(0, 2), rng.choice([0, 0, 3]), rng.choice([30, 60, 70]), rng.choice([0, 1]),
            rng.randint(0, 65535), rng.randint(0, 1 << 20), rng.randint(15, 40), rng.randint(0, 3) and rng.randint(1, 999)]
    out = subprocess.run([BOTGEN] + [str(a) for a in args], capture_output=True, text=True, check=True).stdout
    lines = out.strip().split('\n')
    h = list(map(int, lines[0].split()))
    hdr = dict(zip(['len', 'wid', 'dep', 'set', 'level', 'rot', 'mode', 'fps', 'fast', 'seed', 'bios'], h))
    fl = list(map(int, lines[1].split()))
    hdr['fill'] = [tuple(fl[1 + 3 * i: 4 + 3 * i]) for i in range(fl[0])]
    keys = [(int(a), int(b, 16)) for a, b in (l.split() for l in lines[2:])]
    return hdr, keys

KEYS = [0x4800, 0x5000, 0x4b00, 0x4d00, 0x4700, 0x4900, 0x4f00, 0x5100,
        ord('q'), ord('w'), ord('e'), ord('a'), ord('s'), ord('d'), ord('Q'), ord('D'), 0x20]


def make_script(rng):
    L, W = rng.randint(3, 7), rng.randint(3, 7)
    D = rng.randint(6, 18)
    if rng.random() < 0.15: L, W, D = 3, 3, 6          # small pit: fills up, game over
    hdr = dict(len=L, wid=W, dep=D, set=rng.randint(0, 2),
               level=rng.randint(0, 3) if rng.random() < 0.25 else rng.randint(4, 9),
               rot=rng.randint(0, 2), mode=rng.choice([0, 0, 0, 3]), fps=rng.choice([15, 30, 60, 70]),
               fast=rng.choice([0, 1]), seed=rng.randint(0, 65535), bios=rng.randint(0, 1 << 20))
    fill = []
    if rng.random() < 0.6:          # nearly full bottom layers: forces layer clears
        for z in range(rng.randint(1, min(4, D - 3))):
            holes = set(rng.sample([(x, y) for x in range(L) for y in range(W)], rng.randint(1, 3)))
            fill += [(x, y, z) for x in range(L) for y in range(W) if (x, y) not in holes]
    hdr['fill'] = fill
    keys, f = [], 1
    nframes = rng.randint(2000, 8000)
    density = rng.choice([0.05, 0.2, 0.5])
    while f < nframes:
        if rng.random() < density:
            k = rng.choice(KEYS)
            if hdr['mode'] == 3 and rng.random() < 0.3: k = 0x20
            keys.append((f, k))
        f += 1
    return hdr, keys


def run_original(hdr, keys, max_frames):
    g = Game(fps=hdr['fps'], cpu_class=14 if hdr['fast'] else 1)
    g.call(0xe39d, hdr['seed'])
    g.bios_ticks = hdr['bios']
    for f, k in keys: g.script.setdefault(f, []).append(k)
    g.setup(hdr['len'], hdr['wid'], hdr['dep'], hdr['set'], hdr['level'], hdr['rot'], hdr['mode'])
    cols = g.ru16(0x40b3)
    for x, y, z in hdr['fill']:
        g.w8(g.ru16(g.ru16(cols + 2 * x) + 2 * y) + z, 1)
        g.w8(g.ru16(0x40b5) + z, g.r8(g.ru16(0x40b5) + z) + 1)
    g.max_frames = max_frames
    while not g.stopped:
        over, _ = g.call(0x550a)
        if over or g.stopped: break
        r, _ = g.call(0x55a7)
        if r == 1: break
    return g.log


def run_port(hdr, keys, path, max_frames):
    with open(path, 'w') as fp:
        fp.write('%(len)d %(wid)d %(dep)d %(set)d %(level)d %(rot)d %(mode)d %(fps)d %(fast)d %(seed)d %(bios)d\n' % hdr)
        fp.write('%d %s\n' % (len(hdr['fill']), ' '.join('%d %d %d' % c for c in hdr['fill'])))
        for f, k in keys: fp.write('%d %x\n' % (f, k))
    out = subprocess.run([REPLAY, path, str(max_frames)], capture_output=True, text=True, check=True).stdout.split('\n')
    return [l for l in out if l and not l.startswith('end')]


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    first = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    subprocess.run(['gcc', '-O2', '-o', REPLAY, os.path.join(PORT, 'tests', 'replay.c'),
                    os.path.join(PORT, 'core', 'bo_core.c'), os.path.join(PORT, 'core', 'bo_tables.c')], check=True)
    subprocess.run(['gcc', '-O2', '-o', BOTGEN, os.path.join(PORT, 'tests', 'botgen.c'),
                    os.path.join(PORT, 'core', 'bo_core.c'), os.path.join(PORT, 'core', 'bo_tables.c')], check=True)
    bad = 0
    for run in range(first, first + n):
        rng = random.Random(run)
        hdr, keys = bot_script(rng) if run % 2 else make_script(rng)
        maxf = keys[-1][0] + 400 if keys else 3000
        orig = run_original(hdr, keys, maxf)
        port = run_port(hdr, keys, os.path.join(tempfile.gettempdir(), f'bo_script_{run}.txt'), maxf)
        m = min(len(orig), len(port))
        diff = next((i for i in range(m) if orig[i] != port[i]), None)
        if diff is None and len(port) != len(orig):
            diff = m
        status = 'OK ' if diff is None else 'BAD'
        last = orig[min(m, len(orig)) - 1].split() if orig else ['?'] * 15
        h = {k: v for k, v in hdr.items() if k != 'fill'}
        print(f'{status} run {run}: {h} fill={len(hdr["fill"])} frames={len(orig)}/{len(port)} score={last[12]} cubes={last[13]}', flush=True)
        if diff is not None:
            bad += 1
            for i in range(max(0, diff - 3), min(diff + 2, m)):
                print('   orig', orig[i]); print('   port', port[i])
    print(f'{n - bad}/{n} matched')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
