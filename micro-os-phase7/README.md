# Micro-OS v0.7 — First bounded native AArch64 ELF execution

Microkernel: seL4 Microkit **2.4.0**. Board: **qemu_virt_aarch64**. Language: **C**. Development host: Ubuntu.

**What is new:** a ninth protection domain `native_runner`, a strict ELF64/AArch64 loader and a new `elfrun <path>` shell command. Native instructions come from a **file in MicroFS**; they are not compiled into the boot image. Microkit still preconfigures the runner PD at build time.

## Trust boundary and limitations

This is **not a general ELF loader**. v0.7 deliberately only accepts exactly 128-byte little-endian AArch64 ET_DYN ELF files with one RX PT_LOAD segment at zero, entry offset 120, and the two machine instructions `movz w0, #imm16; ret`. It executes these **actual AArch64 instructions**, but does **not** support loading normal compiled C programs, shared libraries, relocations, syscall ABI, dynamic process creation, arbitrary native code, or multiple segments.

Because MicroFS currently stores at most 255 bytes per text file and is accessed through a textual protocol, ELF bytes are stored **base64-encoded** (172 ASCII characters) in MicroFS. The terminal decodes the ELF and verifies the header and instructions, then copies it to a page mapped writable in the terminal and read+executable (not writable) in the isolated runner. The application manager dispatches one task to that preconfigured runner and prints the return value.

The code uses EL0 cache maintenance (`dc cvau` in terminal / `ic ivau` in runner) for QEMU's Cortex-A53. This depends on seL4 allowing EL0 cache operations, which is normally enabled in the AArch64 kernel configuration. Hardware cache line assumptions must be revisited for other platforms. Filesystem changes persist only after `sync`. **Do not run this prototype on untrusted hardware or treat the loader as a secure sandbox for arbitrary ELF.**

## Install without overwriting v0.6

From Ubuntu terminal, assuming you downloaded `micro-os-phase7.zip`:

```bash
cd ~/andros
unzip ~/Downloads/micro-os-phase7.zip
cd ~/andros/micro-os-phase7
make check
make test
make clean
make
```

**Bring forward your existing v0.6 disk once, before running v0.7:**

```bash
cp ../micro-os-phase6/disk.img ../micro-os-phase6/disk.img.backup
cp ../micro-os-phase6/disk.img ./disk.img
```

Only copy the disk once! `make clean` does not delete `disk.img`. Subsequent `make run` invocations will reuse it. Keep the existing `-global virtio-mmio.force-legacy=false` setting.

Boot:

```bash
make run
```

## Native code milestone

1. In Micro-OS, run `mkdir /apps` if it doesn't already exist.
2. On the **Ubuntu** terminal, generate the demo program's commands:

```bash
python3 tools/make_return_elf.py 42
```

3. The script prints two lines. Paste the **first** into the **Micro-OS terminal** (the `write ...` command). It stores 172 base64 characters representing a valid 128-byte ELF:

```text
write /apps/return42.elf f0VMRgIBAQAAAAAAAAAAAAMAtwABAAAAeAAAAAAAAABAAAAAAAAAAAAAAAAAAAAAAAAAAEAAOAABAAAAAAAAAAEAAAAFAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgAAAAAAAAACAAAAAAAAAABAAAAAAAAAAQAWAUsADX9Y=
```

4. Then run:

```text
elfrun /apps/return42.elf
```

Expected if the full seL4/QEMU execution works:

```text
native ELF returned 42 (AArch64 machine code)
```

5. Run `ps` to see the ninth, precreated `native_runner` domain. Run `sync` to save the program. Exit QEMU with Ctrl+A, X, restart with `make run` and try `elfrun /apps/return42.elf` again.

You can create different return-value ELF programs from Ubuntu with, for example:

```bash
python3 tools/make_return_elf.py 1234
```

Paste the generated `write` and `elfrun` commands into Micro-OS.

Previously working commands remain available: `help`, `apps`, `run calc 7 + 5`, `run counter 5`, `exec /apps/demo.mos`, `ls`, `cat`, `mkdir`, `write`, `sync`, `diskinfo`.

## Design / file layout

- `src/elf_loader.c` + `.h` strict base64 decoder and bounded ELF64 structural and instruction validator.
- `src/native_runner.c` dedicated isolated AArch64 execution domain.
- `src/terminal.c` reads file from MicroFS, validates and fills code page and calls manager.
- `src/app_manager.c` dispatches `elfrun` to ninth PD and maintains completion counts.
- `system.xml` adds `native_code` and `native_mailbox` regions, runner PD and channel #4.
- `tools/make_return_elf.py` emits a sample genuine ELF64/AArch64 binary encoded for MicroFS.
- `tests/test_elf.c` checks parser success and malformed ELF rejection.
- Existing filesystem, block-driver, VM and application host integration tests remain.

## Expected limitations / troubleshooting

- If Microkit refuses `perms="rx"` or cannot patch an address, attach the full build log. This full SDK build could not be executed in the development environment.
- If you get a seL4 fault when running `elfrun`, capture all console lines immediately before/after. Verify instruction-cache operation permissions and mapping permissions.
- If `elfrun` rejects your program, regenerate with `tools/make_return_elf.py` instead of trying a normal `gcc`-compiled executable.
- If UART input truncates the long base64 paste, run `cat /apps/return42.elf` and check it's complete; split paste or enter it more slowly.
- If `diskinfo` reports unavailable, preserve the existing QEMU option `-global virtio-mmio.force-legacy=false`.
- The full seL4/QEMU image has **not** been boot-tested here. Host tests and AArch64 syntax checks are not a substitute for verifying a real boot.

## Later: genuinely general-purpose ELF processes

Next milestones: binary-safe block-based filesystem, read streams >255 bytes, ELF PT_LOAD page mappings, zeroing BSS, stack and TLS, process ABI/syscalls, relocations/PIE and independent VSpaces, CSpaces and TCBs. The official seL4 libraries/process tutorial is a useful starting point for the direct seL4 API path.
