# Micro-OS phase 2: C + seL4 Microkit 2.4.0 for QEMU virt AArch64.
# Place SDK and this directory side-by-side, or override MICROKIT_SDK.
MICROKIT_SDK ?= ../microkit-sdk-2.4.0
BOARD         := qemu_virt_aarch64
CONFIG        := debug
BUILD         := build
BOARD_DIR     := $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)
MICROKIT_TOOL := $(MICROKIT_SDK)/bin/microkit

CC := aarch64-linux-gnu-gcc
LD := aarch64-linux-gnu-ld

CFLAGS  := -std=gnu11 -O2 -mcpu=cortex-a53 -mstrict-align \
           -nostdlib -ffreestanding -fno-builtin -fno-stack-protector \
           -fno-pie -Wall -Wextra -Isrc -I$(BOARD_DIR)/include
LDFLAGS := -L$(BOARD_DIR)/lib

.PHONY: all check run clean
all: $(BUILD)/loader.img

$(BUILD):
	mkdir -p $@

$(BUILD)/uart_driver.o: src/uart_driver.c src/ring.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/terminal.o: src/terminal.c src/ring.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/uart_driver.elf: $(BUILD)/uart_driver.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@

$(BUILD)/terminal.elf: $(BUILD)/terminal.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@

$(BUILD)/loader.img: $(BUILD)/uart_driver.elf $(BUILD)/terminal.elf system.xml
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
