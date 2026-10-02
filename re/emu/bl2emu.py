"""Run original BL2.OVL game-logic functions headless under Unicorn.

The image is loaded at segment 0x1000 (same addresses as the Ghidra project), relocated,
and individual near functions are called directly. Rendering/sound routines are stubbed
out; BIOS keyboard (INT 16h) and tick count (INT 1Ah) are faked.
"""
import os, struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE, UC_HOOK_INTR
from unicorn.x86_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, '..', '..', 'original', 'BL2.OVL')

CS = 0x1000
DS = CS + 0x0f02
SENTINEL = 0xfff0          # return address inside CS; reaching it ends a call
STACK_TOP = 0xfffe          # SS = DS, small model
HEAP = 0x5000              # scratch area in DGROUP (beyond BSS end 0x4f94)

# Routines replaced with an immediate `ret`. Each one was checked: it writes nothing the
# game logic reads (only video memory, palette, projection buffers or the speaker).
STUBS = {
    0x660f: 'project_piece',     # vertex projection into screen buffers
    0x2125: 'draw_piece', 0x7449: 'flip', 0x2712: 'draw_text',
    0x179d: 'draw_cubes_played', 0x17be: 'draw_hiscore', 0x17d8: 'draw_level',
    0x19c0: 'draw_score', 0x1989: 'draw_rot_keys', 0x180e: 'draw_depth_gauge',
    0x367f: 'redraw_pit',        # redraws settled cubes; reads pit only
    0x38b3: 'palette_flash',
    0x8be5: 'play_sound',        # blocking speaker effects (timing modelled in the core)
    0x44ca: 'screen_layout',     # sets screen size/colours; harness sets 41cd/40b9 itself
    0x99a4: 'set_clip', 0x2a6d: 'draw_pit_frame', 0x2fa9: 'draw_pit_grid', 0x23f4: 'draw_panel',
    0x7c3b: 'find_hiscore_table',   # file I/O; only loads the hall of fame table
    0x3d2d: 'game_over',         # waits for Enter/Esc; harness stops at game over instead
}


class BL2:
    def __init__(self):
        raw = open(EXE, 'rb').read()
        hdr = struct.unpack('<H', raw[8:10])[0] * 16
        img = bytearray(raw[hdr:])
        nrel = struct.unpack('<H', raw[6:8])[0]
        rtab = struct.unpack('<H', raw[0x18:0x1a])[0]
        for k in range(nrel):
            off, seg = struct.unpack('<HH', raw[rtab + 4 * k: rtab + 4 * k + 4])
            p = seg * 16 + off
            v = struct.unpack('<H', img[p:p + 2])[0]
            img[p:p + 2] = struct.pack('<H', (v + CS) & 0xffff)
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_16)
        mu.mem_map(0, 0x100000)
        mu.mem_write(CS * 16, bytes(img))
        # zero BSS explicitly (beyond file image)
        self.bios_ticks = 0
        self.keys = []            # pending BIOS key words (scan<<8 | ascii)
        self.on_poll = None       # callback when the game polls an empty keyboard
        self.trace = []
        for a in STUBS:
            mu.mem_write(CS * 16 + a, b'\xc3')
        mu.hook_add(UC_HOOK_INTR, self._intr)
        # Python replacements for RTL/hardware routines: addr -> fn(self, args) -> ax
        self.heap = HEAP
        self.pyfuncs = {
            0xb880: lambda e, a: e.alloc(a[0]),                      # malloc(n)
            0xdd29: lambda e, a: e.alloc(a[0] * a[1], zero=True),     # calloc(n, size)
            0x5b5a: lambda e, a: e.cpu_class,                         # cpu_speed_class
            0x5237: lambda e, a: e.keys.clear(),                      # flush_keys
        }
        self.cpu_class = 14
        for a in self.pyfuncs:
            mu.hook_add(UC_HOOK_CODE, self._py, begin=CS * 16 + a, end=CS * 16 + a)
        mu.hook_add(UC_HOOK_CODE, self._stop, begin=CS * 16 + SENTINEL, end=CS * 16 + SENTINEL)

    # ---- memory helpers (DGROUP offsets) ----
    def rb(self, off, n=1): return bytes(self.mu.mem_read(DS * 16 + off, n))
    def r8(self, off): return self.rb(off)[0]
    def r16(self, off): return struct.unpack('<h', self.rb(off, 2))[0]
    def ru16(self, off): return struct.unpack('<H', self.rb(off, 2))[0]
    def r32(self, off): return struct.unpack('<i', self.rb(off, 4))[0]
    def wb(self, off, data): self.mu.mem_write(DS * 16 + off, bytes(data))
    def w8(self, off, v): self.wb(off, [v & 0xff])
    def w16(self, off, v): self.wb(off, struct.pack('<H', v & 0xffff))
    def w32(self, off, v): self.wb(off, struct.pack('<I', v & 0xffffffff))

    # ---- interrupts ----
    def _intr(self, mu, intno, _):
        ax = mu.reg_read(UC_X86_REG_AX); ah = ax >> 8
        if intno == 0x1a and ah == 0:
            mu.reg_write(UC_X86_REG_CX, (self.bios_ticks >> 16) & 0xffff)
            mu.reg_write(UC_X86_REG_DX, self.bios_ticks & 0xffff)
            mu.reg_write(UC_X86_REG_AX, 0)
        elif intno == 0x16 and ah in (0, 0x10):
            k = self.keys.pop(0) if self.keys else 0
            mu.reg_write(UC_X86_REG_AX, k)
        elif intno == 0x16 and ah in (1, 0x11):
            if not self.keys and self.on_poll:
                self.on_poll(self)
            fl = mu.reg_read(UC_X86_REG_EFLAGS)
            if self.keys:
                mu.reg_write(UC_X86_REG_AX, self.keys[0]); fl &= ~0x40
            else:
                fl |= 0x40
            mu.reg_write(UC_X86_REG_EFLAGS, fl)
        else:
            raise RuntimeError(f'unhandled int {intno:02x} ax={ax:04x} at {mu.reg_read(UC_X86_REG_IP):04x}')

    def alloc(self, n, zero=True):
        p = self.heap
        self.heap = (self.heap + n + 1) & ~1
        assert self.heap < 0xe000, 'fake heap exhausted'
        if zero: self.wb(p, bytes(n))
        return p

    def _py(self, mu, addr, size, _):
        sp = mu.reg_read(UC_X86_REG_SP)
        ret = self.ru16(sp)
        args = [self.ru16(sp + 2 + 2 * k) for k in range(4)]
        ax = self.pyfuncs[addr - CS * 16](self, args)
        mu.reg_write(UC_X86_REG_AX, (ax or 0) & 0xffff)
        mu.reg_write(UC_X86_REG_SP, sp + 2)
        mu.reg_write(UC_X86_REG_IP, ret)

    def _stop(self, mu, addr, size, _):
        mu.emu_stop()

    # ---- calling ----
    def call(self, addr, *args):
        mu = self.mu
        for seg in (UC_X86_REG_DS, UC_X86_REG_SS, UC_X86_REG_ES):
            mu.reg_write(seg, DS)
        mu.reg_write(UC_X86_REG_CS, CS)
        sp = STACK_TOP
        for a in reversed(args):
            sp -= 2; self.w16(sp, a)
        sp -= 2; self.w16(sp, SENTINEL)
        mu.reg_write(UC_X86_REG_SP, sp)
        mu.reg_write(UC_X86_REG_BP, 0)
        mu.emu_start(CS * 16 + addr, CS * 16 + SENTINEL)
        return mu.reg_read(UC_X86_REG_AX), mu.reg_read(UC_X86_REG_DX)


TICK_HZ = 1193182 / 65536      # 18.2065 Hz BIOS tick


class Game(BL2):
    """Drive the original play loop with a frame clock and scripted keys.

    A frame starts at each call of anim_move_step (5d3a), which every game loop runs once
    per iteration. `fps` frames per second map to BIOS ticks; each elapsed tick runs the
    INT 1Ch handler's effect (countdown--).
    """

    def __init__(self, fps=30, cpu_class=14):
        super().__init__()
        self.cpu_class = cpu_class
        self.fps = fps
        self.frame = 0
        self.script = {}          # frame -> [key words]
        self.max_frames = 1 << 30
        self.stopped = False
        self.log = []
        self.mu.hook_add(UC_HOOK_CODE, self._frame, begin=CS * 16 + 0x5d3a, end=CS * 16 + 0x5d3a)

    def ticks_at(self, f):
        return int(f * TICK_HZ / self.fps)

    def _frame(self, mu, addr, size, _):
        self.frame += 1
        if self.frame > self.max_frames:
            self.stopped = True
            mu.emu_stop()
            return
        for _ in range(self.ticks_at(self.frame) - self.ticks_at(self.frame - 1)):
            self.bios_ticks += 1
            c = self.r16(0x4e6c)
            if c > 0: self.w16(0x4e6c, c - 1)
            self.w16(0x44c4, self.ru16(0x44c4) + 1)
        for k in self.script.pop(self.frame, []):
            if len(self.keys) < 15: self.keys.append(k)      # BIOS buffer capacity
        self.log.append(self.snapshot())

    def snapshot(self):
        pose = struct.unpack('<9h', self.rb(0x0de4, 18))
        return '%d %d %s %d %d %d %d' % (self.frame, self.r16(0x0db2), ' '.join(map(str, pose)),
                                         self.r16(0x4e6c), self.r32(0x40a7), self.r32(0x41df), self.r16(0x40b7))

    def setup(self, len_=5, wid=5, dep=12, blockset=0, level=0, rotspeed=1, mode=0):
        p = 0x6000 - 0x40
        self.wb(p, struct.pack('<10h', len_, wid, dep, blockset, level, rotspeed, 0, 0, 2, 0))
        self.w16(0x0da4, mode); self.w16(0x44a8, 1); self.w16(0x41cd, 640); self.w16(0x40b9, 350)
        self.call(0x427d, p)

    def pit(self):
        L, W, D = self.r16(0x40ad), self.r16(0x40af), self.r16(0x40b1)
        cols = self.ru16(0x40b3)
        out = []
        for x in range(L):
            row = self.ru16(cols + 2 * x)
            out.append([self.rb(self.ru16(row + 2 * y), D) for y in range(W)])
        return out
