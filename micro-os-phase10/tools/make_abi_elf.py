#!/usr/bin/env python3
"""Compile the strictly validated native log-ABI C sample and package 184-byte ELF.

AArch64 Clang -Os emits a 40-byte function, including a bounded stack frame and
one indirect call BLR X1 (callback pointer provided by native runner). The
OS ELF loader accepts only this exact instruction sequence plus a MOVZ constant.
"""
import argparse
import base64
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path
from make_c_elf import elf_text

EXPECTED = (0xa9be7bfd, 0xf9000bf3, 0x910003fd, 0x2a0003f3,
            0xd63f0020, None, 0x1b132260, 0xf9400bf3,
            0xa8c27bfd, 0xd65f03c0)

def validate(code):
    if len(code) != 40:
        raise ValueError(f'expected 40 bytes of code, got {len(code)}')
    instr = struct.unpack('<10I', code)
    for n, (got, want) in enumerate(zip(instr, EXPECTED)):
        if n == 5:
            if got & 0xffe0001f != 0x52800008:
                raise ValueError('invalid immediate instruction in ABI function')
        elif got != want:
            raise ValueError(f'unexpected instruction #{n}: 0x{got:08x}')
    return (instr[5] >> 5) & 0xffff

def package(code):
    image=bytearray(184)
    image[:16] = b'\x7fELF' + bytes([2,1,1]) + bytes(9)
    struct.pack_into('<HHIQQQIHHHHHH',image,16,3,183,1,136,64,0,0,64,56,1,0,0,0)
    struct.pack_into('<IIQQQQQQ',image,64,1,5,0,0,0,184,184,16)
    image[136:176] = code
    return bytes(image)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path,nargs='?',default=Path('examples/log.c'))
    p.add_argument('--name',default='log.elf')
    p.add_argument('--value',type=int,default=9)
    p.add_argument('--compiler',default='clang')
    p.add_argument('--output',type=Path)
    args=p.parse_args()
    if args.value<0 or args.value>10000:p.error('--value must be 0..10000')
    if '/' in args.name or not args.name.endswith('.elf') or len(args.name)>22:
        p.error('short --name ending in .elf required')
    cc=shutil.which(args.compiler)
    if not cc:p.error('clang required (sudo apt install clang)')
    with tempfile.TemporaryDirectory() as td:
        path=Path(td)/'native.o'
        subprocess.run([cc,'--target=aarch64-none-elf','-Os',
            '-ffreestanding','-fno-builtin','-fno-stack-protector',
            '-fno-pic','-c',str(args.source.resolve()),'-o',str(path)],check=True)
        code=elf_text(path.read_bytes())
    k=validate(code)
    b=package(code)
    if args.output:args.output.write_bytes(b)
    encoded=base64.b64encode(b).decode('ascii')
    assert len(b)==184 and len(encoded)==248
    print(f'Validated Clang native function: {len(code)} bytes, K={k}')
    print(f'ELF64: {len(b)} bytes, Base64: {len(encoded)} characters')
    print('Paste this single complete command INTO Micro-OS:')
    print(f'write /apps/{args.name} {encoded}')
    print('Then run:')
    print(f'elfrun /apps/{args.name} {args.value}')
    print('Expected on ARM64:')
    print(f'[CONSOLE] native app printed: {args.value}')
    print(f'[ABI] print_u32({args.value}) sent to console service')
    print(f'native C returned {args.value*args.value+k} (AArch64 ABI callback)')
    print('Use sync to persist the ELF. Note: no dynamic processes or arbitrary code.')

if __name__=='__main__':
    try:main()
    except (ValueError, subprocess.CalledProcessError) as e:
        raise SystemExit(f'Build rejected: {e}')
