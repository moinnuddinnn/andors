#!/usr/bin/env python3
"""Create a 128-byte ET_DYN ELF64 AArch64 binary: movz w0,#imm16; ret.
Its base64 string (172 ASCII bytes) fits Micro-OS's 255-byte text file.
This intentionally does NOT produce general purpose ELF programs.
"""
import argparse, base64, struct
parser=argparse.ArgumentParser()
parser.add_argument('value',type=int,nargs='?',default=42)
a=parser.parse_args()
if not 0 <= a.value <= 65535: parser.error('value must be 0..65535')
elf=bytearray(128)
elf[:16]=b'\x7fELF'+bytes([2,1,1])+bytes(9)
struct.pack_into('<HHIQQQIHHHHHH',elf,16,
                 3,183,1,120,64,0,0,64,56,1,0,0,0)
struct.pack_into('<IIQQQQQQ',elf,64,1,5,0,0,0,128,128,16)
struct.pack_into('<II',elf,120,0x52800000 | (a.value << 5),0xd65f03c0)
encoded=base64.b64encode(elf).decode('ascii')
assert len(encoded)==172
print('write /apps/return%d.elf %s' % (a.value,encoded))
print('elfrun /apps/return%d.elf' % a.value)
