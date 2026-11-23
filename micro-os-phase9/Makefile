# Micro-OS v0.9: seL4 Microkit 2.4.0 / QEMU AArch64 / VirtIO disk.
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

.PHONY: all check run clean test test-integration test-driver test-apps test-vm test-elf test-compiler disk reset-disk
all: $(BUILD)/loader.img

$(BUILD):
	mkdir -p $@

$(BUILD)/uart_driver.o: src/uart_driver.c src/ring.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/terminal.o: src/terminal.c src/ring.h src/fs_protocol.h src/app_protocol.h src/elf_loader.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/filesystem.o: src/filesystem.c src/fs_protocol.h src/block_protocol.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/virtio_blk.o: src/virtio_blk.c src/block_protocol.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/app_manager.o: src/app_manager.c src/app_protocol.h src/app_utils.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/app_calc.o: src/app_calc.c src/app_protocol.h src/app_utils.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/app_counter.o: src/app_counter.c src/app_protocol.h src/app_utils.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/elf_loader.o: src/elf_loader.c src/elf_loader.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/native_runner.o: src/native_runner.c src/app_protocol.h src/elf_loader.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/script_vm.o: src/script_vm.c src/app_protocol.h src/app_utils.h Makefile | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/uart_driver.elf: $(BUILD)/uart_driver.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@
$(BUILD)/terminal.elf: $(BUILD)/terminal.o $(BUILD)/elf_loader.o
	$(LD) $(LDFLAGS) $^ -lmicrokit -Tmicrokit.ld -o $@
$(BUILD)/filesystem.elf: $(BUILD)/filesystem.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@
$(BUILD)/virtio_blk.elf: $(BUILD)/virtio_blk.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@
$(BUILD)/app_manager.elf: $(BUILD)/app_manager.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@
$(BUILD)/app_calc.elf: $(BUILD)/app_calc.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@
$(BUILD)/app_counter.elf: $(BUILD)/app_counter.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@
$(BUILD)/script_vm.elf: $(BUILD)/script_vm.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@
$(BUILD)/native_runner.elf: $(BUILD)/native_runner.o
	$(LD) $(LDFLAGS) $< -lmicrokit -Tmicrokit.ld -o $@

$(BUILD)/loader.img: $(BUILD)/uart_driver.elf $(BUILD)/terminal.elf \
                     $(BUILD)/filesystem.elf $(BUILD)/virtio_blk.elf \
                     $(BUILD)/app_manager.elf $(BUILD)/app_calc.elf \
                     $(BUILD)/app_counter.elf $(BUILD)/script_vm.elf $(BUILD)/native_runner.elf system.xml
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
		-global virtio-mmio.force-legacy=false \
		-cpu cortex-a53 \
		-nographic \
		-serial mon:stdio \
		-device loader,file=$(BUILD)/loader.img,addr=0x70000000,cpu-num=0 \
		-drive if=none,file=$(DISK_IMAGE),format=raw,id=disk0,cache=writethrough \
		-device virtio-blk-device,drive=disk0 \
		-m size=2G

clean:
	rm -rf $(BUILD)

# Host tests use a simulated disk + simulated notifications, not real MMIO.
test: test-integration test-driver test-apps test-vm test-elf

# Optional: verify actual Clang output is inside the allowlist.
test-compiler:
	python3 tools/make_data_elf.py examples/store.c --value 9 > /dev/null
	@echo "PASS: Clang generated accepted v0.9 AArch64 code"

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
		tests/test_persistence.c src/elf_loader.c /tmp/micro-os-phase4-fs.o /tmp/micro-os-phase4-terminal.o \
		-o /tmp/micro-os-phase4-test
	/tmp/micro-os-phase4-test
	@rm -f /tmp/micro-os-phase4-fs.o /tmp/micro-os-phase4-terminal.o /tmp/micro-os-phase4-test

# Host fake-MMIO unit test of the real VirtIO block driver.
test-driver:
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		tests/test_virtio.c src/virtio_blk.c -o /tmp/micro-os-phase4-driver-test
	/tmp/micro-os-phase4-driver-test
	@rm -f /tmp/micro-os-phase4-driver-test

# Five real production C files linked together with renamed PD entry points.
# The host harness simulates notification delivery and verifies shell output.
test-apps:
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		-Dinit=app_terminal_init -Dnotified=app_terminal_notified \
		-Dfs_mailbox_vaddr=apps_test_fs_mailbox_vaddr \
		-c src/terminal.c -o /tmp/micro-os5-term.o
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		-Dinit=manager_init -Dnotified=manager_notified \
		-Dapp_mailbox_vaddr=manager_app_mailbox_vaddr \
		-Dcalc_mailbox_vaddr=manager_calc_mailbox_vaddr \
		-Dcounter_mailbox_vaddr=manager_counter_mailbox_vaddr \
		-Dscript_mailbox_vaddr=manager_script_mailbox_vaddr \
		-Dnative_mailbox_vaddr=manager_native_mailbox_vaddr \
		-c src/app_manager.c -o /tmp/micro-os5-manager.o
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		-Dinit=calc_init -Dnotified=calc_notified \
		-Dcalc_mailbox_vaddr=calc_worker_mailbox_vaddr \
		-c src/app_calc.c -o /tmp/micro-os5-calc.o
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		-Dinit=counter_init -Dnotified=counter_notified \
		-Dcounter_mailbox_vaddr=counter_worker_mailbox_vaddr \
		-c src/app_counter.c -o /tmp/micro-os5-counter.o
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		-Dinit=native_init -Dnotified=native_notified \
		-Dnative_mailbox_vaddr=native_worker_mailbox_vaddr \
		-c src/native_runner.c -o /tmp/micro-os7-native.o
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		-Dinit=vm_init -Dnotified=vm_notified \
		-Dscript_mailbox_vaddr=vm_worker_mailbox_vaddr \
		-c src/script_vm.c -o /tmp/micro-os6-vm.o
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		-Dterminal_init=app_terminal_init -Dterminal_notified=app_terminal_notified \
		tests/test_apps.c src/elf_loader.c /tmp/micro-os5-term.o /tmp/micro-os5-manager.o \
		/tmp/micro-os5-calc.o /tmp/micro-os5-counter.o /tmp/micro-os6-vm.o \
		/tmp/micro-os7-native.o -o /tmp/micro-os5-apps-test
	/tmp/micro-os5-apps-test
	@rm -f /tmp/micro-os5-term.o /tmp/micro-os5-manager.o \
		/tmp/micro-os5-calc.o /tmp/micro-os5-counter.o /tmp/micro-os5-apps-test /tmp/micro-os6-vm.o /tmp/micro-os7-native.o

# Standalone VM test using the actual production VM implementation.
test-vm:
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc -Itests/include \
		tests/test_vm.c src/script_vm.c -o /tmp/micro-os6-vm-test
	/tmp/micro-os6-vm-test
	@rm -f /tmp/micro-os6-vm-test

# Strict v0.7/v0.8 ELF validation with the real production parser.
test-elf:
	$(HOST_CC) -std=gnu11 -O1 -g -Wall -Wextra -Werror -Isrc \
		tests/test_elf.c src/elf_loader.c -o /tmp/micro-os7-elf-test
	/tmp/micro-os7-elf-test
	@rm -f /tmp/micro-os7-elf-test
