# Offline host tests

Use `make test` on a Linux host with GCC, even without Microkit or QEMU installed.
The test harness stubs Microkit's notification/debug functions and exercises
requests to the filesystem component, not the complete seL4 system image.
