# Micro-OS v0.6 — Disk-backed program loading (interpreted)

**Target:** Ubuntu x86-64 development host, seL4 Microkit SDK 2.4.0, QEMU `virt` AArch64, C.

This phase adds a persistent program-file loading workflow. A command such as `exec /apps/demo.mos` reads a text program from the existing MicroFS service, sends it to the app manager over IPC, and executes it in **another preconfigured protection domain** (`script_vm`). This is a deliberately bounded text interpreter, **NOT** an ELF loader or dynamic process creation mechanism. The calculator and counter remain separate preconfigured native-C protection domains.

## Install and preserve your existing disk

```bash
cd ~/andros
unzip ~/Downloads/micro-os-phase6.zip
cd micro-os-phase6
make check
make test
make
```

Copy your disk from v0.5 **once**, only if you want to retain your existing files:

```bash
cp ../micro-os-phase5/disk.img ../micro-os-phase5/disk.img.backup
cp ../micro-os-phase5/disk.img ./disk.img
```

> IMPORTANT: Don't overwrite `micro-os-phase6/disk.img` on subsequent runs: it would erase your new changes by replacing them with the old v0.5 image! `make clean` is safe; `make reset-disk` is destructive.

Boot:

```bash
make run
```

Our previous VirtIO-MMIO fix (`-global virtio-mmio.force-legacy=false`) is included in the Makefile. QEMU still uses a 32-slot virt machine with VirtIO-MMIO at transport base `0x0a003e00` (IRQ 79) under this single-device configuration.

## First program saved *on disk* and then executed

Enter these commands **inside the Micro-OS terminal** (not the Ubuntu shell):

```text
micro-os> mkdir /apps
micro-os> write /apps/demo.mos print Hello from a saved program!; add 7 5; count 3; end
micro-os> cat /apps/demo.mos
micro-os> exec /apps/demo.mos
Hello from a saved program!
7 + 5 = 12
count: 1 2 3
micro-os> sync
```

Exit QEMU with Ctrl+A, X, then run `make run` again. `exec /apps/demo.mos` should still work because the program itself is stored in persistent MicroFS data.

Use `apps` and `ps` to inspect the three app domains (calculator, counter, and script_vm). The two native apps continue to work as in v0.5 (`run calc 7 + 5`, `run counter 5`). Try `exec /apps/does-not-exist.mos` to test the loader's error handling.

## Tiny `.mos` language

Programs are a single line of **semicolon-separated statements**. Commands are lowercase, and whitespace between operands is space-separated:

| Syntax | Meaning |
| --- | --- |
| `print Hello from disk` | Print text to the terminal |
| `add 7 5` | Add two bounded signed integers |
| `count 3` | Print a sequence from 1 to 3 |
| `end` | Stop executing the file |

Limits: 255 bytes per file, at most 12 statements, count 1–20, integer operands within +/-1,000,000. The shell `write` command can write up to 191 text bytes at a time; use `append` to add more up to the FS file maximum. No imports, loops, system calls, arbitrary native instructions or filesystem access from the VM. The VM can only return output through its worker mailbox.

## Architecture / security boundary

```text
QEMU / seL4 Microkit
  ├── uart_driver      (hardware MMIO and UART IRQ)
  ├── block_driver     (hardware MMIO, DMA and disk IRQ)
  ├── filesystem       (private data + persistent snapshots)
  ├── terminal         (CLI, single FS mailbox client)
  ├── app_manager      (dispatches worker requests)
  ├── calculator       (native C, preconfigured)
  ├── counter          (native C, preconfigured)
  └── script_vm        (new: interprets saved .mos program files)

  'exec /apps/demo.mos'
    terminal --'load'--> filesystem
    terminal --'exec'--> app_manager
    app_manager -------> script_vm
    script_vm ----------> app_manager --> terminal
```

MicroFS adds only a **new read-only mailbox operation** (`load`) returning `OK:<program>` or `ERR:<message>`. The underlying disk snapshot layout is **unchanged** from v0.4/v0.5. The app protocol argument field grows from 192 to 384 bytes; this is internal to the statically compiled PDs and does not affect on-disk data.

## Test coverage

`make test` runs four suites:

1. Existing filesystem + terminal + simulated disk persistence and crash recovery. Also verifies the new machine-readable `load` filesystem API.
2. Existing VirtIO-MMIO driver and simulated DMA/interrupt tests.
3. Shell + simulated filesystem reply + app manager + calculator + counter + script VM end-to-end notification flow.
4. Script VM unit tests for parsing, arithmetic, bounds and reply sequencing.

The eight production C source files were compiled to AArch64 object files using Clang with a Microkit test header. This validates syntax/target code generation, **not** full seL4 SDK linking, deployment or a real QEMU boot. You will perform the hardware integration test by running `make && make run` on your Ubuntu machine.

## What's next: actual AArch64 ELF programs

Microkit preconfigures PDs, memory maps, image paths, channels and most caps at system build time. An actual run-time AArch64 ELF process loader requires designing capability allocation, address-space creation, page allocation/map permissions, ELF validation and relocation strategy, a user stack, execution context and IPC, etc. The seL4 tutorial `Libraries: Processes & ELF loading` is a good reference: https://docs.sel4.systems/Tutorials/libraries-3 . We should investigate using direct seL4 APIs for that step rather than claiming the script VM already provides it.
