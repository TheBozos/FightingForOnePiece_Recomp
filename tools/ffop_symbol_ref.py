"""llvm-objcopy options giving a stripped EE executable the names of its symbol map (function symbols) and a start
at every jal target (untyped, unnamed for the matcher), so bozo_symbolmap.py can take it as a reference.

    python3 -I tools/ffop_symbol_ref.py <elf> <map.txt> <out.rsp>
"""
import struct
import sys

elf_path, map_path, rsp_path = sys.argv[1:4]
d = open(elf_path, 'rb').read()
shoff = struct.unpack_from('<I', d, 32)[0]
shentsize, shnum, shstrndx = struct.unpack_from('<HHH', d, 46)
secs = [struct.unpack_from('<10I', d, shoff + i * shentsize) for i in range(shnum)]
strtab = secs[shstrndx]
def sname(s):
    o = strtab[4] + s[0]
    return d[o:d.index(b'\0', o)].decode()
text = next(s for s in secs if sname(s) == '.text')
addr, off, size = text[3], text[4], text[5]
starts = set()
for k in range(size // 4):
    w = struct.unpack_from('<I', d, off + 4 * k)[0]
    if w >> 26 == 3:
        t = ((addr + 4 * k + 4) & 0xF0000000) | ((w & 0x03FFFFFF) << 2)
        if addr <= t < addr + size:
            starts.add(t)
named = {}
for line in open(map_path):
    line = line.split('#', 1)[0].split()
    if len(line) == 2:
        named.setdefault(int(line[1], 16), line[0])
with open(rsp_path, 'w') as f:
    for a, n in sorted(named.items()):
        f.write(f'--add-symbol={n}=0x{a:x},function,global\n')
    for a in sorted(starts - set(named)):
        f.write(f'--add-symbol=__bound_{a:x}=0x{a:x},local\n')
print(len(named), 'named', len(starts - set(named)), 'bounds')
