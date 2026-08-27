# Makefile for MyOS Kernel
CXX = i386-elf-g++
LD = i386-elf-ld
OBJCOPY = i386-elf-objcopy
QEMU = qemu-system-i386

CXXFLAGS = -m32 -ffreestanding -nostdlib -fno-exceptions -fno-rtti -fno-stack-protector -O2 -Wall -Wextra
LDFLAGS = -m elf_i386 -T linker.ld --nmagic
LIBS = -lgcc

SOURCES = kernel.cpp
OBJECTS = $(SOURCES:.cpp=.o)
TARGET = kernel.elf
ISO = myos.iso
FLOPPY = myos.flp

.PHONY: all clean run iso floppy debug release

all: $(TARGET)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS) $(LIBS)
	@echo "Kernel built: $(TARGET)"

# Create bootable ISO using GRUB
iso: $(TARGET)
	mkdir -p iso/boot/grub
	cp $(TARGET) iso/boot/
	cp grub.cfg iso/boot/grub/
	grub-mkrescue -o $(ISO) iso
	@echo "ISO created: $(ISO)"

# Create floppy image (1.44MB)
floppy: $(TARGET)
	dd if=/dev/zero of=$(FLOPPY) bs=1024 count=1440
	mkdir -p floppy/boot/grub
	cp $(TARGET) floppy/boot/
	cp grub.cfg floppy/boot/grub/
	grub-mkrescue -o $(FLOPPY) floppy/
	@echo "Floppy image created: $(FLOPPY)"

# Run in QEMU
run: $(TARGET)
	$(QEMU) -kernel $(TARGET)

# Run with ISO
run-iso: iso
	$(QEMU) -cdrom $(ISO)

# Run with floppy
run-floppy: floppy
	$(QEMU) -fda $(FLOPPY)

# Debug with GDB
debug: $(TARGET)
	$(QEMU) -kernel $(TARGET) -s -S &
	gdb -ex "target remote localhost:1234" -ex "symbol-file $(TARGET)"

# Clean build artifacts
clean:
	rm -f $(OBJECTS) $(TARGET) $(ISO) $(FLOPPY)
	rm -rf iso floppy

# Release build (optimized)
release: CXXFLAGS += -DNDEBUG -Os
release: clean all iso floppy
	@echo "Release build complete!"
