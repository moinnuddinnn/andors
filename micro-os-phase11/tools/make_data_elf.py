#!/usr/bin/env python3
"""Build a strictly allowlisted C-to-ELF native data-writer (Micro-OS v0.9).

A 176-byte ELF contains actual AArch64 Clang-generated machine code:
movz wN,#K; madd w0,w0,w0,wN; str w0,[x1]; ret.
The runner provides x1 as the address of a private RW memory page; no syscall,
relocation, process creation, unrestricted memory access or general ELF support.
"""
import argparse, base64, shutil, struct, subprocess, sys, tempfile
from pathlib import Path
from make_c_elf import elf_text


def validate(code):
    if len(code) != 16:
        raise ValueError(f'expected 16 bytes of code, got {len(code)}')
    a,b,c,d=struct.unpack('<4I',code)
    reg=a & 31
    if ((a & 0xffe00000)!=0x52800000 or reg==31 or
        b!=(0x1b000000 | (reg<<10)) or
        c!=0xb9000020 or d!=0xd65f03c0):
        raise ValueError('unsupported instruction sequence; build examples/store.c with Clang -Os')
    return (a>>5)&0xffff


def package(code):
    elf=bytearray(176)
    elf[:16]=b'\x7fELF'+bytes([2,1,1])+bytes(9)
    struct.pack_into('<HHIQQQIHHHHHH',elf,16,3,183,1,144,64,0,0,64,56,1,0,0,0)
    struct.pack_into('<IIQQQQQQ',elf,64,1,5,0,0,0,176,176,16)
    elf[144:160]=code
    return bytes(elf)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path,nargs='?',default=Path('examples/store.c'))
    p.add_argument('--name',default='store.elf')
    p.add_argument('--value',type=int,default=9)
    p.add_argument('--compiler',default='clang')
    p.add_argument('--output',type=Path)
    args=p.parse_args()
    if not (0<=args.value<=10000):p.error('value must be 0..10000')
    if '/' in args.name or len(args.name)>22 or not args.name.endswith('.elf'):
        p.error('name must be short and end in .elf')
    if not args.source.is_file():p.error('C source does not exist')
    cc=shutil.which(args.compiler)
    if not cc:p.error('Clang not found: sudo apt install clang')
    with tempfile.TemporaryDirectory() as td:
        out=Path(td)/'data.o'
        subprocess.run([cc,'--target=aarch64-none-elf','-Os',
            '-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pic',
            '-c',str(args.source.resolve()),'-o',str(out)],check=True)
        code=elf_text(out.read_bytes())
    k=validate(code)
    elf=package(code)
    if args.output:args.output.write_bytes(elf)
    b64=base64.b64encode(elf).decode('ascii')
    assert len(elf)==176 and len(b64)==236
    print(f'C-compiled machine code: {code.hex()}  (K={k})')
    print('ELF64 binary: 176 bytes; Base64: 236 characters')
    print('Paste the FULL command into the Micro-OS prompt:')
    print(f'write /apps/{args.name} {b64}')
    print('Then, inside Micro-OS:')
    print(f'elfrun /apps/{args.name} {args.value}')
    print(f'Expected: native C wrote {args.value*args.value+k} to private memory; returned {args.value*args.value+k}')
    print('Run sync to persist the program.')

if __name__=='__main__':
    try:main()
    except (ValueError,subprocess.CalledProcessError) as exc:
        sys.exit('Build rejected: '+str(exc))
