#!/usr/bin/env python3
"""Compile a restricted two-operation native C program for Micro-OS v1.1.

The preallocated runner's callback supports DEC (op 1) and HEX (op 2).
The ELF validator accepts precisely this 60-byte Clang -Oz instruction
sequence, not arbitrary ELF or relocations. Output fits MicroFS 255B files.
"""
import argparse
import base64
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
from make_c_elf import elf_text

EXPECTED = (
    0xa9be7bfd, 0xa9014ff4, 0x2a0003f4, 0xaa0103f3,
    0x52800020, 0x2a1403e1, 0x910003fd, 0xd63f0260,
    0x52800040, 0x2a1403e1, 0xd63f0260, 0x2a1403e0,
    0xa9414ff4, 0xa8c27bfd, 0xd65f03c0,
)

def package(source: Path, compiler: str = 'clang') -> bytes:
    cc=shutil.which(compiler)
    if not cc: raise ValueError(f'{compiler} is not installed')
    with tempfile.TemporaryDirectory() as tmp:
        out=Path(tmp)/'multi.o'
        subprocess.run([cc,'--target=aarch64-none-elf','-Oz','-ffreestanding',
            '-fno-builtin','-fno-stack-protector','-fno-pic',
            '-c',str(source.resolve()),'-o',str(out)],check=True)
        code=elf_text(out.read_bytes())
    if len(code)!=60 or struct.unpack('<15I',code)!=EXPECTED:
        raise ValueError('compiler produced a different instruction sequence; loader would reject it')
    image=bytearray(184)
    image[:16]=b'\x7fELF'+bytes([2,1,1])+bytes(9)
    struct.pack_into('<HHIQQQIHHHHHH',image,16,3,183,1,124,64,0,0,64,56,1,0,0,0)
    struct.pack_into('<IIQQQQQQ',image,64,1,5,0,0,0,184,184,16)
    image[124:184]=code
    return bytes(image)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path,nargs='?',default=Path('examples/multi.c'))
    p.add_argument('--name',default='multi.elf')
    p.add_argument('--value',type=int,default=42)
    p.add_argument('--compiler',default='clang')
    p.add_argument('--output',type=Path)
    args=p.parse_args()
    if not 0<=args.value<=10000: p.error('--value must be 0..10000')
    if '/' in args.name or not args.name.endswith('.elf') or len(args.name)>22:
        p.error('short --name ending in .elf required')
    elf=package(args.source,args.compiler)
    if args.output: args.output.write_bytes(elf)
    b64=base64.b64encode(elf).decode('ascii')
    assert len(b64)==248
    print('Validated C-compiled two-service AArch64 ELF (60 byte .text).')
    print('ELF: 184 bytes; Base64: 248 characters, fits MicroFS.')
    print('Paste this entire command INTO Micro-OS:')
    print(f'write /apps/{args.name} {b64}')
    print('Run:')
    print(f'elfrun /apps/{args.name} {args.value}')
    print('Expected console output (order relative to shell text may vary):')
    print(f'[CONSOLE] decimal: {args.value}')
    print(f'[CONSOLE] hexadecimal: 0x{args.value:08X}')
    print(f'native C returned {args.value} (two AArch64 ABI calls)')
    print('Then use sync to persist the file.')

if __name__=='__main__':
    try: main()
    except (ValueError,subprocess.CalledProcessError) as exc:
        raise SystemExit(f'Build rejected: {exc}')
