# Micro-OS v0.6: Microkit symbol patch fix

The original v0.6 `system.xml` erroneously mapped `script_mailbox` inside the `counter` protection domain with `setvar_vaddr="script_mailbox_vaddr"`. `app_counter.c` only has the `counter_mailbox_vaddr` symbol.

This package removes that extra `<map>` from the `counter` protection domain. All other protection-domain mappings, C sources and QEMU launch options are unchanged.

To replace only the configuration in an existing project, copy this package's `system.xml` to your `~/andros/micro-os-phase6/system.xml`, then run:

```sh
make
make run
```

`disk.img` is NOT included; do not delete or overwrite your existing disk.

Validation: XML parsed; all `setvar_vaddr` entries checked against corresponding C source variable definitions; `make test` passed. Full Microkit/QEMU integration requires your SDK and Ubuntu environment.
