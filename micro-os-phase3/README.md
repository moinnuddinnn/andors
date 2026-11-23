# Micro-OS v0.3 — seL4 Microkit + RAM filesystem

**Status:** Phase 3 prototype; C on QEMU's AArch64 `virt` machine using the **seL4
Microkit 2.4.0** SDK. This project extends the **working Phase 2 UART + shell**.

New in v0.3: a **third, independent protection domain** implementing a small
hierarchical RAM filesystem. Only the filesystem PD owns the file contents;
the terminal sends operations through a **shared mailbox + Microkit
notifications**. No device MMIO is given to the filesystem.

## Layout

```text
micro-os-phase3/
├── Makefile
├── system.xml
├── src/
│   ├── terminal.c       # shell and command dispatch
│   ├── uart_driver.c    # working Phase 2 QEMU PL011 driver, IRQ 33
│   ├── filesystem.c     # new isolated RAM filesystem service
│   ├── fs_protocol.h    # shared request/reply mailbox
│   └── ring.h           # UART transport rings
├── tests/               # can be tested on the Ubuntu host
└── docs/architecture.md
```

## Install without overwriting the working Phase 2

Expected directory layout (your screenshot shows you are using `~/andros`):

```text
~/andros/
├── microkit-sdk-2.4.0/
├── micro-os-phase2/      # working old version, leave untouched
└── micro-os-phase3/      # extract this ZIP here
```

On Ubuntu:

```bash
cd ~/andros
unzip ~/Downloads/micro-os-phase3.zip
cd micro-os-phase3
make check
make clean && make
make run
```

If you installed the Microkit SDK elsewhere:

```bash
make run MICROKIT_SDK=/absolute/path/to/microkit-sdk-2.4.0
```

To exit QEMU: **Ctrl+A**, then **X**.

IMPORTANT: As with Phase 2, the Makefile uses `-std=gnu11` (not `-std=c11`)
because seL4's AArch64 headers use GNU inline assembly.

## Commands

| Command | Description |
|---|---|
| `help` | Show help |
| `pwd` | Print current directory |
| `ls [path]` | List a directory (defaults to current) |
| `cd [path]` | Change directory (defaults to `/`) |
| `mkdir <path>` | Create a directory |
| `touch <path>` | Create an empty file |
| `write <path> <text>` | Create/overwrite a file with text |
| `append <path> <text>` | Append text to a file |
| `cat <path>` | Print a file |
| `rm <path>` | Remove a file |
| `rmdir <path>` | Remove an **empty** directory |
| `fsinfo` | Print filesystem capacity and usage |
| `about`, `version`, `devices`, `ipc`, `clear`, `echo <text>` | Earlier shell features |

The filesystem initially contains `/home/readme.txt` and
`/system/version.txt`. Paths can be absolute (`/home/readme.txt`) or relative
(`notes.txt`, `../system/version.txt`, `./notes.txt`). File names cannot
contain spaces, and directory names are limited to **23 characters**. Text
passed to `write` and `append` can contain spaces. This is intentionally a
basic prototype, with no quoting/escaping, shell scripts or external programs.

### Try these after `make run`

```text
micro-os> ls
micro-os> cat /home/readme.txt
micro-os> mkdir projects
micro-os> cd projects
micro-os> touch hello.txt
micro-os> write hello.txt Hello from my new OS!
micro-os> cat hello.txt
micro-os> append hello.txt -- It works!
micro-os> cat hello.txt
micro-os> pwd
micro-os> fsinfo
micro-os> cd ..
micro-os> ls
```

`append` consumes the spaces between the filename and text. Use a delimiter
(e.g. `--`) when joining two fragments; this basic shell does not yet offer
quoted strings for leading spaces.

## Offline host tests (no Microkit SDK required)

```bash
make test
make test-integration
```

`make test` tests filesystem operations; `make test-integration` tests the
terminal <-> filesystem command protocol with mock notification delivery.
These **do not** boot or exercise the real seL4 kernel or the QEMU UART. The
full Microkit SDK, AArch64 cross compiler and QEMU are needed for `make run`.

## Architectural and security properties

- `uart_driver` (priority 200): only PD with MMIO `0x09000000` / IRQ 33.
- `filesystem` (priority 150): owns `node_t` file data in its private memory.
- `terminal` (priority 100): parses input and sends RPC-like async requests.
- UART messages travel over two SPSC shared-memory rings plus notifications.
- FS commands travel over one shared 4-KiB mailbox plus a channel; only one
  outstanding request is supported at a time.
- Shared memory is **mutually writable** by both filesystem and terminal;
  this demo does not enforce a secure adversarial protocol. The server's data
  itself is private and isolated from the shell PD.

## Prototype limitations (deliberate)

- **Volatile storage.** A reboot destroys all created files; no block device,
  flash persistence or on-disk filesystem yet.
- **Fixed limits.** 48 total filesystem entries (including root), maximum
  255 text bytes per file, 23 characters per file/directory component, single
  shell session, and a fixed mailbox/reply buffer.
- `write`/`append` cannot insert embedded newlines; `cat` prints one newline
  after a file's contents for readability.
- UART output can be lost when the 2-KiB ring fills. Shell typeahead may be
  lost while a filesystem request is pending. Neither path has production
  backpressure or error recovery yet.
- Terminal input is limited to 255 ASCII characters per command; UTF-8,
  quoting, interactive editing/history and tab completion are not supported.
- No process creation or executable loading; all three PDs are configured at
  image-build time. This is a small OS research prototype, not a POSIX system.

## References

- Microkit 2.4.0 manual: https://docs.sel4.systems/projects/microkit/manual/2.4.0/
- Official tutorial (notification channels and shared memory): https://docs.sel4.systems/projects/microkit/tutorial/part2.html
- Official tutorial (QEMU PL011 UART): https://docs.sel4.systems/projects/microkit/tutorial/part1.html

## Validation performed during packaging

- The XML was parsed and all three PDs and both channels were checked.
- The C files compiled with native GCC (stubbed Microkit API) and
  AArch64-targeted Clang (stubbed Microkit API).
- Filesystem unit tests and simulated shell/filesystem integration tests passed.
- The full seL4/Microkit SDK link and QEMU boot were **not available** in the
  packaging environment. Your `make run` is the real-system integration test.
