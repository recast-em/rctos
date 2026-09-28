# rctos - Kernel und Boot-Image bauen, in QEMU starten, Budgets prüfen.
#
#   make            Kernel bauen (build/x86_64/kernel.elf) und Budget prüfen
#   make iso        Boot-Image (build/rctos-x86_64.iso, UEFI und BIOS)
#   make run        in QEMU starten (Fenster, serielles Log im Terminal)
#   make test       ohne Fenster booten (UEFI und BIOS), Log prüfen, Bildschirmfotos
#   make font       Konsolenschrift aus raw_assets/charmap01.png erzeugen
#   make clean

ARCH    ?= x86_64
BUILD   := build/$(ARCH)
CC      := clang
LD      := ld.lld
HOSTCC  ?= cc
PYTHON  ?= python3

KERNEL  := $(BUILD)/kernel.elf
ISO     := build/rctos-$(ARCH).iso
LIMINE  := build/limine

CFLAGS  := --target=x86_64-unknown-none-elf -std=c11 -Os \
           -ffreestanding -fno-builtin -nostdlib -fno-stack-protector -fno-stack-check \
           -fno-pic -fno-pie -mcmodel=kernel -mno-red-zone \
           -mno-mmx -mno-sse -mno-sse2 -mno-80387 -msoft-float \
           -fno-asynchronous-unwind-tables -fno-unwind-tables \
           -ffunction-sections -fdata-sections \
           -Wall -Wextra -Werror -MMD -MP \
           -Ikernel/core -Ikernel/arch/$(ARCH) -Ikernel/boot -I$(BUILD)
LDFLAGS := -nostdlib -static --gc-sections --build-id=none -z max-page-size=0x1000 \
           -T kernel/arch/$(ARCH)/kernel.ld

SRCS    := $(sort $(wildcard kernel/core/*.c) $(wildcard kernel/arch/$(ARCH)/*.c) \
                  $(wildcard kernel/arch/$(ARCH)/*.S))
OBJS    := $(SRCS:%=$(BUILD)/%.o)

all: $(KERNEL) budget

$(BUILD)/buildinfo.h: $(SRCS) $(wildcard kernel/*/*.h kernel/arch/*/*.h) tools/buildinfo.sh FORCE
	@mkdir -p $(@D)
	@tools/buildinfo.sh > $@.tmp
	@cmp -s $@.tmp $@ && rm $@.tmp || mv $@.tmp $@

$(BUILD)/kernel/core/main.c.o: $(BUILD)/buildinfo.h

$(BUILD)/%.c.o: %.c
	@mkdir -p $(@D)
	@echo "  CC    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.S.o: %.S
	@mkdir -p $(@D)
	@echo "  AS    $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(OBJS) kernel/arch/$(ARCH)/kernel.ld
	@echo "  LD    $@"
	@$(LD) $(LDFLAGS) $(OBJS) -o $@

budget: $(KERNEL)
	@$(PYTHON) tools/budget.py $(KERNEL) kernel/core/budget.h $$(tools/loc.sh kernel)

$(LIMINE)/.ok: boot/limine.sha256
	tools/fetch-limine.sh $(LIMINE)
	@touch $@

$(LIMINE)/limine: $(LIMINE)/.ok
	$(HOSTCC) -std=c99 -O2 -o $@ $(LIMINE)/limine.c

$(ISO): $(KERNEL) boot/limine.conf $(LIMINE)/.ok $(LIMINE)/limine
	@rm -rf build/iso_root
	@mkdir -p build/iso_root/boot/limine build/iso_root/EFI/BOOT
	cp $(KERNEL) build/iso_root/boot/rctos-kernel
	cp boot/limine.conf $(LIMINE)/limine-bios.sys $(LIMINE)/limine-bios-cd.bin \
	   $(LIMINE)/limine-uefi-cd.bin build/iso_root/boot/limine/
	cp $(LIMINE)/BOOTX64.EFI build/iso_root/EFI/BOOT/
	xorriso -as mkisofs -quiet -R -r -J -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
	    -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    build/iso_root -o $@
	$(LIMINE)/limine bios-install $@ >/dev/null

iso: $(ISO)

run: $(ISO)
	$(PYTHON) tools/qemu.py --iso $(ISO)

test: $(ISO)
	$(PYTHON) tools/qemu.py --iso $(ISO) --headless --rtc family \
	    --expect "rctos: gate ok" --expect "rctos: idle" \
	    --log build/serial-uefi.log --shot build/screen-uefi.png
	$(PYTHON) tools/qemu.py --iso $(ISO) --headless --bios --rtc family \
	    --expect "rctos: gate ok" --expect "rctos: idle" \
	    --log build/serial-bios.log --shot build/screen-bios.png

font:
	$(PYTHON) tools/mkfont.py raw_assets/charmap01.png kernel/core/font8x8.c

clean:
	rm -rf build/$(ARCH) build/iso_root $(ISO) build/*.png build/*.log

FORCE:
.PHONY: all budget iso run test font clean FORCE
-include $(OBJS:.o=.d)
