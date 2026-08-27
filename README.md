# tinyos# MyOS - Single-file C++ Bare-metal x86 Operating System

[![Build MyOS Kernel](https://github.com/swapper2929/tinyos/actions/workflows/build.yml/badge.svg)](https://github.com/swapper2929/tinyos/actions/workflows/build.yml)

## Overview

A minimal x86 operating system written entirely in **one C++ file** (~65KB). Features include:

- ✅ VGA Text Output (80x25)
- ✅ Keyboard Driver (with Shift support)
- ✅ Physical Memory Manager (bitmap)
- ✅ Virtual Memory (Paging, Identity Map 4MB)
- ✅ Heap Allocator (kmalloc/kfree)
- ✅ GDT, IDT, PIC, ISR/IRQ handlers
- ✅ Timer (PIT 100Hz)
- ✅ Shell with commands
- ✅ Screenfetch (OS logo)
- ✅ Snake game
- ✅ Mandelbrot fractal viewer
- ✅ Simple calculator

## Download

### Latest Release
- [Download ELF](https://github.com/your-username/your-repo/releases/latest/download/kernel.elf)
- [Download ISO](https://github.com/your-username/your-repo/releases/latest/download/myos.iso)
- [Download Floppy](https://github.com/your-username/your-repo/releases/latest/download/myos.flp)

### Build from Source

```bash
# Install dependencies
sudo apt-get install gcc-multilib xorriso grub-pc-bin qemu-system-x86
# Install i386-elf toolchain
wget https://github.com/lordmilko/i686-elf-tools/releases/download/7.1.0/i686-elf-tools-linux.zip
unzip i686-elf-tools-linux.zip
sudo cp i686-elf-tools/bin/* /usr/local/bin/

# Build
make
make iso
make floppy

# Run
make run          # QEMU with kernel ELF
make run-iso      # QEMU with ISO
make run-floppy   # QEMU with floppy image
