# Micro-OS starter (C + seL4 Microkit)

Minimal first-boot skeleton for Microkit **2.4.0** on the `qemu_virt_aarch64` board.
This is a first protection domain, **not yet a complete OS**. It prints using
Microkit's debug interface and then waits for events.

## Install prerequisites (Ubuntu x86-64 host)

```bash
sudo apt update
sudo apt install -y make curl gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu qemu-system-arm
```

## Get the SDK

Keep `micro-os-starter` and `microkit-sdk-2.4.0` alongside one another:

```bash
cd ..   # leave micro-os-starter and enter its parent
curl -fL https://github.com/seL4/microkit/releases/download/2.4.0/microkit-sdk-2.4.0-linux-x86-64.tar.gz -o sdk.tar.gz
tar xf sdk.tar.gz
cd micro-os-starter
make check
make
make run
```

If you already have the SDK elsewhere, specify its full path:

```bash
make check MICROKIT_SDK=/absolute/path/to/microkit-sdk-2.4.0
make run MICROKIT_SDK=/absolute/path/to/microkit-sdk-2.4.0
```

Exit QEMU with **Ctrl+A**, followed by **X**.

## Expected result

Once seL4 boots, a custom banner beginning `MICRO-OS: first boot on seL4!`
should be visible in the QEMU serial terminal.

## Next steps

1. Create a UART driver protection domain (real serial output and input).
2. Create another protection domain; add a Microkit channel between the two.
3. Build an interactive terminal/shell on top of the UART driver.
4. Add a service directory and a RAM-backed filesystem.
5. Investigate dynamic service creation, process loading, and user-level memory management.

## Sources

- Microkit 2.4.0 tutorial: https://docs.sel4.systems/projects/microkit/tutorial/part0.html
- Microkit 2.4.0 manual: https://docs.sel4.systems/projects/microkit/manual/2.4.0/
- seL4 microkernel: https://github.com/seL4/seL4

## Build caveat

The project files were validated for Makefile syntax and XML format, but the
binary was **not** built or executed here because the SDK and cross-toolchain
were not available in the execution environment.
