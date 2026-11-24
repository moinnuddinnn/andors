#!/usr/bin/env python3
"""Build a trusted freestanding AArch64 C ELF bundle for Micro-OS v1.2.

The program must have uint32_t program(uint32_t, uint32_t*) and use only its
stack and supplied scratch[1024], plus read-only position-independent code/data.
This is not a security validator; run only source you trust.
"""
import argparse, base64, re, shutil, struct, subprocess, tempfile, zlib
from pathlib import Path
from make_c_elf import elf_text
MAX=2048
CHUNK=180
SCRIPT='''ENTRY(program)
SECTIONS {
  . = 128;
  .text : { *(.text.program) *(.text .text.*) *(.rodata .rodata.*) }
  .writable : { *(.data .data.* .bss .bss.* COMMON .got .got.* .sdata .sdata.* .sbss .sbss.* .tdata .tdata.* .tbss .tbss.* .init_array .fini_array) }
  ASSERT(SIZEOF(.writable) == 0, "writable globals / GOT unsupported")
  /DISCARD/ : { *(.comment .note* .eh_frame* .llvm_addrsig .ARM.attributes) }
}
'''
def package(source, compiler='clang', linker='ld.lld'):
    cc=shutil.which(compiler); ld=shutil.which(linker)
    if not cc or not ld: raise ValueError('install clang and lld')
    with tempfile.TemporaryDirectory() as d:
        d=Path(d); obj=d/'prog.o'; linked=d/'prog.elf'; script=d/'link.ld'; script.write_text(SCRIPT)
        subprocess.run([cc,'--target=aarch64-none-elf','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-fPIC','-ffunction-sections','-fdata-sections','-fno-unwind-tables','-fno-asynchronous-unwind-tables','-mgeneral-regs-only','-c',str(source.resolve()),'-o',str(obj)],check=True)
        subprocess.run([ld,'-T',str(script),'--no-undefined','--fatal-warnings','-o',str(linked),str(obj)],check=True)
        raw=linked.read_bytes()
        if struct.unpack_from('<Q',raw,24)[0]!=128: raise ValueError('program entry must start at 128; define program with external linkage')
        # Reject additional allocated sections: the bundle carries only .text.
        shoff=struct.unpack_from('<Q',raw,40)[0]; entsz,count,names=struct.unpack_from('<HHH',raw,58)
        names_off,names_sz=struct.unpack_from('<QQ',raw,shoff+names*entsz+24); names_data=raw[names_off:names_off+names_sz]
        for i in range(count):
            pos=shoff+i*entsz; nameoff,typ,flags=struct.unpack_from('<IIQ',raw,pos); size=struct.unpack_from('<Q',raw,pos+32)[0]; name=names_data[nameoff:].split(b'\0',1)[0]
            if size and flags&2 and name!=b'.text': raise ValueError(f'unsupported allocated section {name!r}')
            if size and typ in (4,9): raise ValueError('unresolved relocations')
        code=elf_text(raw)
    size=128+len(code)
    if not 132<=size<=MAX: raise ValueError(f'image {size} bytes outside 132..{MAX}')
    e=bytearray(size); e[:16]=b'\x7fELF'+bytes([2,1,1])+bytes(9)
    struct.pack_into('<HHIQQQIHHHHHH',e,16,3,183,1,128,64,0,0,64,56,1,0,0,0)
    struct.pack_into('<IIQQQQQQ',e,64,1,5,0,0,0,size,size,16); e[128:]=code
    return bytes(e)
def commands(elf,name,value):
    path='/apps/'+name
    # Write manifest last; an incomplete update fails its length/CRC check.
    lines=[f'write {path}.{i//CHUNK} '+base64.b64encode(elf[i:i+CHUNK]).decode() for i in range(0,len(elf),CHUNK)]
    lines += [f'write {path} MOS12 {len(elf)} {zlib.crc32(elf)}',f'nativeexec {path} {value}']
    return '\n'.join(lines)+'\n'
def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('source',type=Path);p.add_argument('--name');p.add_argument('--value',type=int,default=9);p.add_argument('--compiler',default='clang');p.add_argument('--linker',default='ld.lld');p.add_argument('--output',type=Path);p.add_argument('--commands',type=Path);a=p.parse_args()
    name=a.name or a.source.stem+'.elf'
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,15}\.elf',name) or not 0<=a.value<=10000: p.error('short .elf name; value 0..10000')
    try: elf=package(a.source,a.compiler,a.linker)
    except (ValueError,subprocess.CalledProcessError) as e: p.exit(1,f'Build rejected: {e}\n')
    if a.output:a.output.write_bytes(elf)
    text=commands(elf,name,a.value)
    if a.commands:a.commands.write_text(text)
    print(f'ELF {len(elf)} bytes; {((len(elf)+CHUNK-1)//CHUNK)} chunks. Paste ONE command at a time into Micro-OS (wait for prompt).')
    print(text,end='')
if __name__=='__main__':main()
