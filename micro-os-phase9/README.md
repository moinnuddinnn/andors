# Micro-OS v0.9: Private Data for Native C Programs

A nine-protection-domain OS prototype based on seL4 Microkit **2.4.0**,
running under QEMU AArch64, written mainly in freestanding C.

## New in v0.9

The native runner gets its own private **4 KiB RW data page** at virtual
address `0x040b0000`. The terminal does NOT map this page; only the native
runner can read/write it. The existing code page continues to be RW in the
terminal and RX in the runner. No RWX mapping is added.

The new native ABI has the C signature:

```c
uint32_t program(uint32_t x, uint32_t *output);
```

The native runner passes `x` in `w0` and `output` in `x1`, pointing to its
private data page. The small allowlisted C-compiled program computes
`y = x*x + 7`, writes `output[0]=y`, and returns `y`. The runner prints both
values to show an actual native store and read, in addition to native code
execution.

This is **not** a general C runtime, dynamic process loader or OS syscall
API. The ELF validator only allows three exact machine-code families:
- v0.7: `mov w0,#K; ret` (128-byte ELF, 172-character Base64)
- v0.8: `mov wN,#K; madd w0,w0,w0,wN; ret` (160 B / 216 chars)
- v0.9: `mov wN,#K; madd w0,w0,w0,wN; str w0,[x1]; ret` (176 B / 236 chars)

The memory-writing form cannot express an arbitrary address because the
only allowed store is `[x1]`, and the runner sets `x1` to its private data
page. This is a narrow instructional demonstration, not a security audit.

## Install

Unzip into `~/andros/micro-os-phase9` next to `microkit-sdk-2.4.0`.

```bash
cd ~/andros/micro-os-phase9
make check
make test
make test-compiler  # optional; requires clang
make clean && make
```

### Bring over previous disk safely

Before the first v0.9 boot only:

```bash
cp ../micro-os-phase8/disk.img ../micro-os-phase8/disk.img.backup
cp ../micro-os-phase8/disk.img ./disk.img
```

Do not repeat the copy after writing new files in v0.9. `make clean` does
NOT delete `disk.img`, and `make run` reuses an existing image.

```bash
make run
```

## Native C program with writable data

The sample source is `examples/store.c`. On Ubuntu:

```bash
sudo apt install clang
cd ~/andros/micro-os-phase9
python3 tools/make_data_elf.py examples/store.c --value 9
```

The tool prints a complete `write /apps/store.elf <base64>` command.
Paste the **entire** command into the **Micro-OS shell** (not Ubuntu).
`/apps` must exist: create it with `mkdir /apps` only if necessary.
The file is encoded as Base64 text in MicroFS, size **236 bytes**.

Then run inside Micro-OS:

```text
micro-os> elfrun /apps/store.elf 9
native C wrote 88 to private memory; returned 88
micro-os> elfrun /apps/store.elf 12
native C wrote 151 to private memory; returned 151
micro-os> sync
```

Reboot QEMU and repeat `elfrun /apps/store.elf 9` to test persistence.

## Existing features preserved

- UART PL011 serial driver and terminal shell.
- MicroFS directories/files, checkpoint persistence, VirtIO-MMIO block.
- Calculator and counter isolated applications.
- `.mos` script VM and its `exec` command.
- Restricted v0.7 and v0.8 native ELF formats and `elfrun` command.
- QEMU `-global virtio-mmio.force-legacy=false` fix from v0.4.

## Testing scope

`make test` runs host-simulated unit/integration tests using stubs; it
cannot prove that the executable page, private RAM page, AArch64 calling
convention or instruction/cache maintenance work in the actual QEMU system.
The full Microkit build and QEMU boot must be tested on the user's setup.

## Engineering limits

The runner is an existing statically defined protection domain, not a
newly created process. The code executes with the runner's privileges, not
an independent native user process privilege set. No arbitrary ELF loading,
relocations, dynamic allocation, stack guard pages, process termination or
recovery from an untrusted native function is supported yet.

The next architectural step is a richer process ABI with mapped code/data,
explicit stack, service-IPC stubs, fault handling and eventually dynamic
capability-managed address spaces (possibly moving to direct seL4 APIs).

Reference: <https://docs.sel4.systems/projects/microkit/manual/2.4.0/>.
