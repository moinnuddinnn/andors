# Micro-OS starter for seL4 Microkit SDK 2.4.0 / qemu_virt_aarch64.
# Extract SDK as a sibling of this directory or override MICROKIT_SDK.
MICROKIT_SDK ?= ../microkit-sdk-2.4.0
BOARD         := qemu_virt_aarch64
CONFIG        := debug
BUILD         := build
BOARD_DIR     := $(MICROKIT_SDK)/board/$(BOARD)/$(CONFIG)
MICROKIT_TOOL := $(MICROKIT_SDK)/bin/microkit

CC := aarch64-linux-gnu-gcc
LD := aarch64-linux-gnu-ld

CFLAGS  := -mcpu=cortex-a53 -mstrict-align -nostdlib -fno-stack-protector \
           -fno-pie -ffreestanding -g -Wall -Wextra -I$(BOARD_DIR)/include
LDFLAGS := -L$(BOARD_DIR)/lib

.PHONY: all run check clean
all: $(BUILD)/loader.img

$(BUILD):
	mkdir -p $@

$(BUILD)/main.o: src/main.c Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/main.elf: $(BUILD)/main.o
	$(LD) $(LDFLAGS) $^ -lmicrokit -Tmicrokit.ld -o $@

$(BUILD)/loader.img: $(BUILD)/main.elf system.xml
	$(MICROKIT_TOOL) --board $(BOARD) --config $(CONFIG) \
		--search-path $(BUILD) -o $@ -r $(BUILD)/report.txt system.xml

check:
	@command -v $(CC) >/dev/null || { echo "Missing: $(CC)"; exit 1; }
	@command -v $(LD) >/dev/null || { echo "Missing: $(LD)"; exit 1; }
	@command -v qemu-system-aarch64 >/dev/null || { echo "Missing: qemu-system-aarch64"; exit 1; }
	@test -f $(MICROKIT_TOOL) || { echo "Missing: $(MICROKIT_TOOL)"; exit 1; }
	@test -f $(BOARD_DIR)/include/microkit.h || { echo "Missing: $(BOARD_DIR)/include/microkit.h"; exit 1; }
	@echo "Cross compiler, QEMU, and Microkit SDK paths look good."

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
