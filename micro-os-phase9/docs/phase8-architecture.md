# v0.8 native C compilation experiment

Host Clang -> ARM64 .o -> extract .text (12 bytes) -> allowlist -> wrap ELF64 ET_DYN with one RX PT_LOAD segment (160 bytes) -> Base64 (216 bytes) -> `write` into MicroFS -> `elfrun /apps/square.elf 9` -> filesystem IPC -> terminal ELF validator -> shared page RW in terminal / RX in native_runner -> notify app manager -> runner executes uint32_t (*)(uint32_t) -> IPC result -> shell.

This is not general process creation. The C function must compile to `movz wN,#K; madd w0,w0,w0,wN; ret`, and accepts x in w0, returns in w0. The parser rejects other instructions. Code instructions live at image offset 128. The legacy v0.7 two-instruction image remains supported at offset 120. Program bytes are copied from the ELF file, but no arbitrary file is allowed to execute.

Next engineering milestones: safe separated code/data segments, multi-page ELF loading, proper runtime ABI, application memory protections and rights; dynamic process creation will likely require direct seL4 APIs and capability management rather than Microkit static PDs.
