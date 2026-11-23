# Micro-OS v0.4: seL4 Microkit 2.4.0 / QEMU AArch64 / VirtIO disk.
MICROKIT_SDK ?= ../microkit-sdk-2.4.0
BOARD := qemu_virt_aarch64
CONFIG := debug
BUILD := build
BOARD_DIR := $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)
MICROKIT_TOOL := $(MICROKIT_SDK)/bin/microkit
HOST_CC := gcc
CC := aarch64-linux-gnu-gcc
LD := aarch64-linux-gnu-ld

CFLAGS := -std=gnu11 -O2 -mcpu=cortex-a53 -mstrict-align \
          -nostdlib -ffreestanding -fno-builtin -fno-stack-protector \
          -fno-pie -Wall -Wextra -Isrc -I$(BOARD_DIR)/include
LDFLAGS := -L$(BOARD_DIR)/lib
DISK_IMAGE := disk.img
DISK_SIZE := 8M

.PHONY: all check run clean test test-integration test-driver disk reset-disk
all: $(BUILD)/loader.img

$(BUILD):
	mkdir -p $@

$(BUILD)/uart_driver.o: src/uart_driver.c src/ring.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/terminal.o: src/terminal.c src/ring.h src/fs_protocol.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/filesystem.o: src/filesystem.c src/fs_protocol.h src/block_protocol.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/virtio_blk.o: src/virtio_blk.c src/block_protocol.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/uart_driver.elf: $(BUILD)/uart_driver.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@
$(BUILD)/terminal.elf: $(BUILD)/terminal.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@
$(BUILD)/filesystem.elf: $(BUILD)/filesystem.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@
$(BUILD)/virtio_blk.elf: $(BUILD)/virtio_blk.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@

$(BUILD)/loader.img: $(BUILD)/uart_driver.elf $(BUILD)/terminal.elf \
                     $(BUILD)/filesystem.elf $(BUILD)/virtio_blk.elf system.xml
	$(MICROKIT_TOOL) --board $(BOARD) --config $(CONFIG) \
		--search-path $(BUILD) -o $@ -r $(BUILD)/report.txt system.xml

check:
	@command -v $(CC) >/dev/null || { echo "Missing compiler $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null || { echo "Missing linker $(LD)"; exit 1; }
	@command -v qemu-system-aarch64 >/dev/null || { echo "Missing qemu-system-aarch64"; exit 1; }
	@test -f $(MICROKIT_TOOL) || { echo "Missing Microkit tool at $(MICROKIT_TOOL)"; exit 1; }
	@test -f $(BOARD_DIR)/include/microkit.h || { echo "Missing microkit.h"; exit 1; }
	@echo "Toolchain and Microkit SDK paths look good."

# Preserve the same disk.img across make clean and rebuilds.
disk:
	@if [ ! -e $(DISK_IMAGE) ]; then truncate -s $(DISK_SIZE) $(DISK_IMAGE); echo "Created $(DISK_IMAGE) ($(DISK_SIZE))"; else echo "Keeping existing $(DISK_IMAGE)"; fi

# This intentionally destroys all persisted files and directories.
reset-disk:
	@echo "WARNING: reset-disk permanently deletes $(DISK_IMAGE)"
	@rm -f $(DISK_IMAGE)
	@$(MAKE) disk

run: all disk
	qemu-system-aarch64 \
		-machine virt,virtualization=on \
		-cpu cortex-a53 \
		-global virtio-mmio.force-legacy=false \
		-nographic \
		-serial mon:stdio \
		-device loader,file=$(BUILD)/loader.img,addr=0x70000000,cpu-num=0 \
		-drive if=none,file=$(DISK_IMAGE),format=raw,id=disk0,cache=writethrough \
		-device virtio-blk-device,drive=disk0 \
		-m size=2G

clean:
	rm -rf $(BUILD)

# Host tests use a simulated disk + simulated notifications, not real MMIO.
test: test-integration test-driver

test-integration:
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		-Dinit=fs_server_init -Dnotified=fs_server_notified \
		-Dfs_mailbox_vaddr=fs_server_mailbox_vaddr \
		-Dblock_mailbox_vaddr=fs_server_block_mailbox_vaddr \
		-c src/filesystem.c -o /tmp/micro-os-phase4-fs.o
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		-Dinit=terminal_init -Dnotified=terminal_notified \
		-c src/terminal.c -o /tmp/micro-os-phase4-terminal.o
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		tests/test_persistence.c /tmp/micro-os-phase4-fs.o /tmp/micro-os-phase4-terminal.o \
		-o /tmp/micro-os-phase4-test
	/tmp/micro-os-phase4-test
	@rm -f /tmp/micro-os-phase4-fs.o /tmp/micro-os-phase4-terminal.o /tmp/micro-os-phase4-test

# Host fake-MMIO unit test of the real VirtIO block driver.
test-driver:
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		tests/test_virtio.c src/virtio_blk.c -o /tmp/micro-os-phase4-driver-test
	/tmp/micro-os-phase4-driver-test
	@rm -f /tmp/micro-os-phase4-driver-test
