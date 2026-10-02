"""Helpers for poking at BL2.OVL: image/DS offsets, strings, disassembly."""
import struct, sys, os, re
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, '..', '..', 'original', 'BL2.OVL')
raw = open(EXE, 'rb').read()
HDR = struct.unpack('<H', raw[8:10])[0] * 16
img = bytearray(raw[HDR:])
DSEG = 0x0f02            # load-relative data segment (Ghidra shows it as 1f02)
DSBASE = DSEG * 16

# apply relocations with load segment 0 so segment immediates stay load-relative
nrel, relo = struct.unpack('<H', raw[6:8])[0], struct.unpack('<H', raw[0x18:0x1a])[0]

def ds(off, n=1):
    return bytes(img[DSBASE + off: DSBASE + off + n])

def w(off):  return struct.unpack('<h', ds(off, 2))[0]
def uw(off): return struct.unpack('<H', ds(off, 2))[0]

def cstr(off):
    b = img[DSBASE + off:]
    return b[:b.index(0)].decode('latin1')

def dis(start, end):
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    for i in md.disasm(bytes(img[start:end]), start):
        yield i

def strings(minlen=3):
    out, cur, st = [], b'', None
    for o in range(0, len(img) - DSBASE):
        c = img[DSBASE + o]
        if 32 <= c < 127 or c in (9, 10, 13):
            if st is None: st = o
            cur += bytes([c])
        else:
            if c == 0 and st is not None and len(cur) >= minlen: out.append((st, cur.decode('latin1')))
            cur, st = b'', None
    return out

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'strings':
        for o, s in strings(): print(f'{o:04x} {s!r}')
    elif cmd == 's':
        for a in sys.argv[2:]: print(a, repr(cstr(int(a, 16))))
    elif cmd == 'dis':
        for i in dis(int(sys.argv[2], 16), int(sys.argv[3], 16)):
            op=re.sub(r'0x(1[0-9a-f]{4}|f{4}[0-9a-f]{4})\b', lambda m: hex(int(m.group(1),16)&0xffff), i.op_str) if i.mnemonic in ('call','jmp') or i.mnemonic.startswith('j') else i.op_str
            print(f'{i.address:04x}: {i.mnemonic} {op}')
    elif cmd == 'hex':
        o, n = int(sys.argv[2], 16), int(sys.argv[3], 16)
        b = ds(o, n)
        for k in range(0, n, 16):
            print(f'{o+k:04x}: ' + ' '.join(f'{x:02x}' for x in b[k:k+16]))
