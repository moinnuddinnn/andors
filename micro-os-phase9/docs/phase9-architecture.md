# Micro-OS v0.9: Native C writable data in private runner memory

```
Host clang -> strict ELF packer -> base64 -> MicroFS (persistent disk)
     -> terminal loader (RW code page) -> notification/mailbox IPC
     -> application manager -> native runner (RX code page, RW private data)
     -> native program(uint32_t x, uint32_t *private_output)
     -> runner reads private_output[0] and reports value via IPC
```

Memory mappings:

| Region | Terminal PD | Native runner PD |
|---|---|---|
| `native_code` | RW @ 0x04090000 | RX @ 0x04090000 |
| `native_data` | not mapped | RW @ 0x040b0000 |
| `native_mailbox` | not mapped | RW @ 0x040a0000 (also manager) |

The added ELF type is a fixed 176-byte ELF64 ET_DYN with one PT_LOAD,
entry at 144. Its four strictly checked AArch64 instructions are:

```
movz wN,#K
madd w0,w0,w0,wN
str w0,[x1]
ret
```

The runner provides `x1` pointing into its private page and `w0` as the
integer parameter. No general machine-code acceptance is implemented.
