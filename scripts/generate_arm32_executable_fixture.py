#!/usr/bin/env python3
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "fixtures" / "arm32_vm_probe"
BASE = 0x10000
CODE_OFFSET = 0x1000

words = [
    0xE3A0000A,       # mov r0, #10
    0xE3A01001,       # mov r1, #1
    0xEB00000A,       # bl candidate_arm32_vm (false HI predicate case)
    0xE350000F,       # cmp r0, #15
    0x13A04001,       # movne r4, #1
    0x03A04000,       # moveq r4, #0
    0xE3A00014,       # mov r0, #20
    0xE3A01001,       # mov r1, #1
    0xEB000004,       # bl candidate_arm32_vm (true HI predicate case)
    0xE3500016,       # cmp r0, #22
    0x13A00001,       # movne r0, #1
    0x01A00004,       # moveq r0, r4
    0xE3A07001,       # mov r7, #1
    0xEF000000,       # svc #0
    0xE0800001,       # candidate: add r0, r0, r1
    0xE0200001,       # eor r0, r0, r1
    0xE1800001,       # orr r0, r0, r1
    0xE0400001,       # sub r0, r0, r1
    0xE2800005,       # add r0, r0, #5
    0xE3500014,       # cmp r0, #20
    0x82400003,       # subhi r0, r0, #3
    0xE0000190,       # mul r0, r0, r1
    0xE3A03003,       # mov r3, #3
    0xE2533001,       # loop: subs r3, r3, #1
    0x1AFFFFFD,       # bne loop
    0xE2900000,       # adds r0, r0, #0
    0xE2500000,       # subs r0, r0, #0
    0xE3100001,       # tst r0, #1
    0xE1C02001,       # bic r2, r0, r1
    0xE1E02001,       # mvn r2, r1
    0xE12FFF1E,       # bx lr
]
code = b"".join(struct.pack("<I", word) for word in words)
candidate_offset = 14 * 4
candidate_size = 17 * 4

strtab = b"\0_start\0candidate_arm32_vm\0"
start_name = 1
candidate_name = strtab.index(b"candidate")
shstr = b"\0.text\0.symtab\0.strtab\0.shstrtab\0"
section_data_offset = 0x3000
symtab_offset = section_data_offset
symtab = b"".join([
    bytes(16),
    struct.pack("<IIIBBH", start_name, BASE, candidate_offset, 0x12, 0, 1),
    struct.pack("<IIIBBH", candidate_name, BASE + candidate_offset, candidate_size, 0x12, 0, 1),
])
strtab_offset = symtab_offset + len(symtab)
shstr_offset = strtab_offset + len(strtab)
shoff = (shstr_offset + len(shstr) + 3) & ~3

header = bytearray(52)
header[:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
struct.pack_into("<HHIIIIIHHHHHH", header, 16, 2, 40, 1, BASE, 52, shoff, 0x05000200,
                 52, 32, 1, 40, 5, 4)
phdr = struct.pack("<IIIIIIII", 1, CODE_OFFSET, BASE, BASE, len(code), len(code), 5, 0x1000)

blob = bytearray(shoff + 5 * 40)
blob[:52] = header
blob[52:84] = phdr
blob[CODE_OFFSET:CODE_OFFSET + len(code)] = code
blob[symtab_offset:symtab_offset + len(symtab)] = symtab
blob[strtab_offset:strtab_offset + len(strtab)] = strtab
blob[shstr_offset:shstr_offset + len(shstr)] = shstr

def shdr(index, name, kind, flags, addr, offset, size, link=0, info=0, align=1, entsize=0):
    struct.pack_into("<IIIIIIIIII", blob, shoff + index * 40, name, kind, flags, addr, offset,
                     size, link, info, align, entsize)

shdr(1, shstr.index(b".text"), 1, 0x6, BASE, CODE_OFFSET, len(code), align=4)
shdr(2, shstr.index(b".symtab"), 2, 0, 0, symtab_offset, len(symtab), 3, 1, 4, 16)
shdr(3, shstr.index(b".strtab"), 3, 0, 0, strtab_offset, len(strtab))
shdr(4, shstr.index(b".shstrtab"), 3, 0, 0, shstr_offset, len(shstr))

OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_bytes(blob)
OUT.chmod(0o755)
print(OUT)
