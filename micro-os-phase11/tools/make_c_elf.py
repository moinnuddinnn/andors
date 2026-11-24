#!/usr/bin/env python3
"""Compile a tiny C function with Clang and package its actual .text bytes.

This is a restricted AArch64 C-to-ELF demonstrator: accepts only machine
instructions `movz wN, #K; madd w0,w0,w0,wN; ret`. No relocs/branches/syscalls.
A 160-byte ELF -> 216 base64 characters; MicroFS stores the encoded text.
"""
import argparse
import base64
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


def elf_text(obj):
    if obj[:4] != b'\x7fELF' or obj[4:7] != b'\x02\x01\x01':
        raise ValueError('compiler did not produce ELF64 little-endian')
    shoff = struct.unpack_from('<Q', obj, 40)[0]
    entsz, count, names = struct.unpack_from('<HHH', obj, 58)
    if not (0 < count < 256 and 0 < entsz <= 256 and names < count):
        raise ValueError('unexpected section header table')
    if shoff + count*entsz > len(obj):
        raise ValueError('section table extends past file')
    st_base=shoff+names*entsz
    st_off,st_size=struct.unpack_from('<QQ',obj,st_base+24)
    if st_off+st_size>len(obj): raise ValueError('bad section names')
    names_data=obj[st_off:st_off+st_size]
    for i in range(count):
        sh=shoff+i*entsz
        noff=struct.unpack_from('<I', obj, sh)[0]
        if noff>=len(names_data): continue
        name=names_data[noff:].split(b'\0',1)[0]
        if name==b'.text':
            off,sz=struct.unpack_from('<QQ',obj,sh+24)
            if off+sz>len(obj): raise ValueError('invalid .text section')
            return obj[off:off+sz]
    raise ValueError('no .text section')


def validate(code):
    if len(code)!=12: raise ValueError(f'expected 12 bytes; compiler generated {len(code)}')
    a,b,c=struct.unpack('<III',code)
    reg=a&31
    if ((a&0xffe00000)!=0x52800000 or reg==31 or
        b!=(0x1b000000|(reg<<10)) or c!=0xd65f03c0):
        raise ValueError('compiler output outside allowed instruction subset. '
                         'Use the provided square.c and Clang -O2.')
    return (a>>5)&65535


def package(code):
    elf=bytearray(160)
    elf[:16]=b'\x7fELF'+bytes([2,1,1])+bytes(9)
    struct.pack_into('<HHIQQQIHHHHHH',elf,16,3,183,1,128,64,0,0,64,56,1,0,0,0)
    struct.pack_into('<IIQQQQQQ',elf,64,1,5,0,0,0,160,160,16)
    elf[128:140]=code
    return bytes(elf)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path,nargs='?',default=Path('examples/square.c'))
    p.add_argument('--name',default='square.elf')
    p.add_argument('--value',type=int,default=9,help='test input argument, 0..10000')
    p.add_argument('--compiler',default='clang')
    p.add_argument('--output',type=Path,help='optional host-side ELF output file')
    args=p.parse_args()
    if not 0 <= args.value <= 10000: p.error('--value must be 0..10000')
    if '/' in args.name or len(args.name)>22 or not args.name.endswith('.elf'):
        p.error('--name must be a short filename ending in .elf')
    if not args.source.is_file(): p.error(f'source not found: {args.source}')
    compiler=shutil.which(args.compiler)
    if not compiler: p.error('Clang is missing. Install it: sudo apt install clang')
    with tempfile.TemporaryDirectory() as td:
        objpath=Path(td)/'program.o'
        subprocess.run([compiler,'--target=aarch64-none-elf','-Os',
            '-ffreestanding','-fno-builtin','-fno-stack-protector','-fno-pic',
            '-c',str(args.source.resolve()),'-o',str(objpath)],check=True)
        code=elf_text(objpath.read_bytes())
    const=validate(code)
    elf=package(code)
    if args.output: args.output.write_bytes(elf)
    encoded=base64.b64encode(elf).decode('ascii')
    assert len(encoded)==216
    print('Compiled C function: x*x +', const)
    print('Actual AArch64 instruction bytes:',code.hex())
    print('ELF binary:',len(elf),'bytes; base64:',len(encoded),'characters')
    print('Copy the following command to the Micro-OS prompt (not Ubuntu):')
    print('write /apps/'+args.name+' '+encoded)
    print('Then execute inside Micro-OS:')
    print('elfrun /apps/'+args.name+' '+str(args.value))
    print('Expected result:',args.value*args.value+const)
    print('Use sync to persist the file across reboots.')

if __name__=='__main__':
    try: main()
    except (ValueError, subprocess.CalledProcessError) as exc:
        sys.exit('Build rejected: '+str(exc))
