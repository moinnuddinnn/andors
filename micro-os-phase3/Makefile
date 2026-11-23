# Micro-OS phase 3: C + seL4 Microkit 2.4.0 for QEMU virt AArch64.
# Place SDK and this directory side-by-side, or override MICROKIT_SDK.
MICROKIT_SDK ?= ../microkit-sdk-2.4.0
BOARD         := qemu_virt_aarch64
CONFIG        := debug
BUILD         := build
BOARD_DIR     := $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)
MICROKIT_TOOL := $(MICROKIT_SDK)/bin/microkit

HOST_CC := gcc
CC := aarch64-linux-gnu-gcc
LD := aarch64-linux-gnu-ld

CFLAGS  := -std=gnu11 -O2 -mcpu=cortex-a53 -mstrict-align \
           -nostdlib -ffreestanding -fno-builtin -fno-stack-protector \
           -fno-pie -Wall -Wextra -Isrc -I$(BOARD_DIR)/include
LDFLAGS := -L$(BOARD_DIR)/lib

.PHONY: all check run clean test test-integration
all: $(BUILD)/loader.img

$(BUILD):
	mkdir -p $@

$(BUILD)/uart_driver.o: src/uart_driver.c src/ring.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/terminal.o: src/terminal.c src/ring.h src/fs_protocol.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/filesystem.o: src/filesystem.c src/fs_protocol.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/filesystem.elf: $(BUILD)/filesystem.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@

$(BUILD)/uart_driver.elf: $(BUILD)/uart_driver.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@

$(BUILD)/terminal.elf: $(BUILD)/terminal.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@

$(BUILD)/loader.img: $(BUILD)/uart_driver.elf $(BUILD)/terminal.elf $(BUILD)/filesystem.elf system.xml
	$(MICROKIT_TOOL) --board $(BOARD) --config $(CONFIG) \
		--search-path $(BUILD) -o $@ -r $(BUILD)/report.txt system.xml

check:
	@command -v $(CC) >/dev/null || { echo "Missing compiler $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null || { echo "Missing linker $(LD)"; exit 1; }
	@command -v qemu-system-aarch64 >/dev/null || { echo "Missing QEMU"; exit 1; }
	@test -f $(MICROKIT_TOOL) || { echo "Missing Microkit tool at $(MICROKIT_TOOL)"; exit 1; }
	@test -f $(BOARD_DIR)/include/microkit.h || { echo "Missing microkit.h at $(BOARD_DIR)"; exit 1; }
	@echo "Toolchain and Microkit SDK paths look good."

run: all
	qemu-system-aarch64 \
		-machine virt,virtualization=on \
		-cpu cortex-a53 \
		-nographic \
		-serial mon:stdio \
		-device loader,file=$(BUILD)/loader.img,addr=0x70000000,cpu-num=0 \
		-m size=2G

clean:
	rm -rf $(BUILD)

# Filesystem unit tests on the Ubuntu host; no seL4 SDK needed.
test:
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror \
		-Isrc -Itests/include tests/test_filesystem.c src/filesystem.c -o /tmp/micro-os-fs-test
	/tmp/micro-os-fs-test
	@rm -f /tmp/micro-os-fs-test

# Shell + RAMFS end-to-end simulation with mocked microkernel event delivery.
# This verifies our components' protocol but is NOT a real seL4/QEMU boot test.
test-integration:
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		-Dinit=fs_server_init -Dnotified=fs_server_notified \
		-Dfs_mailbox_vaddr=fs_server_mailbox_vaddr -c src/filesystem.c -o /tmp/micro-os-test-filesystem.o
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		-Dinit=terminal_init -Dnotified=terminal_notified \
		-c src/terminal.c -o /tmp/micro-os-test-terminal.o
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		tests/test_integration.c /tmp/micro-os-test-filesystem.o /tmp/micro-os-test-terminal.o \
		-o /tmp/micro-os-integration-test
	/tmp/micro-os-integration-test
	@rm -f /tmp/micro-os-test-filesystem.o /tmp/micro-os-test-terminal.o /tmp/micro-os-integration-test
