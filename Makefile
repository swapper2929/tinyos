# Makefile for MyOS Kernel
CXX = i386-elf-g++
LD = i386-elf-ld
OBJCOPY = i386-elf-objcopy
QEMU = qemu-system-i386

CXXFLAGS = -m32 -ffreestanding -nostdlib -fno-exceptions -fno-rtti \
           -fno-stack-protector -O2 -Wall -Wextra -fno-builtin
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
	@echo "✓ Kernel built: $(TARGET)"
	@ls -lh $(TARGET)

# Create bootable ISO using GRUB
iso: $(TARGET)
	@echo "Creating ISO image..."
	mkdir -p iso/boot/grub
	cp $(TARGET) iso/boot/
	cp grub.cfg iso/boot/grub/ || echo "grub.cfg not found, using default"
	# Create default grub.cfg if missing
	@if [ ! -f grub.cfg ]; then \
		echo 'set timeout=5' > iso/boot/grub/grub.cfg; \
		echo 'set default=0' >> iso/boot/grub/grub.cfg; \
		echo 'menuentry "MyOS" {' >> iso/boot/grub/grub.cfg; \
		echo '  multiboot /boot/kernel.elf' >> iso/boot/grub/grub.cfg; \
		echo '  boot' >> iso/boot/grub/grub.cfg; \
		echo '}' >> iso/boot/grub/grub.cfg; \
	fi
	grub-mkrescue -o $(ISO) iso 2>/dev/null || grub-mkrescue -o $(ISO) iso
	@echo "✓ ISO created: $(ISO)"
	@ls -lh $(ISO)

# Create floppy image
floppy: $(TARGET)
	@echo "Creating floppy image..."
	dd if=/dev/zero of=$(FLOPPY) bs=1024 count=1440 2>/dev/null
	mkdir -p floppy/boot/grub
	cp $(TARGET) floppy/boot/
	# Use same grub.cfg or create default
	@if [ -f grub.cfg ]; then \
		cp grub.cfg floppy/boot/grub/; \
	else \
		echo 'set timeout=5' > floppy/boot/grub/grub.cfg; \
		echo 'set default=0' >> floppy/boot/grub/grub.cfg; \
		echo 'menuentry "MyOS" {' >> floppy/boot/grub/grub.cfg; \
		echo '  multiboot /boot/kernel.elf' >> floppy/boot/grub/grub.cfg; \
		echo '  boot' >> floppy/boot/grub/grub.cfg; \
		echo '}' >> floppy/boot/grub/grub.cfg; \
	fi
	grub-mkrescue -o $(FLOPPY) floppy/ 2>/dev/null || grub-mkrescue -o $(FLOPPY) floppy/
	@echo "✓ Floppy image created: $(FLOPPY)"
	@ls -lh $(FLOPPY)

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
	@echo "✓ Release build complete!"

# Install required tools (Ubuntu/Debian)
setup:
	sudo apt-get update
	sudo apt-get install -y build-essential gcc-multilib g++-multilib xorriso grub-pc-bin grub-common qemu-system-x86 nasm mtools dosfstools
	@echo "Downloading i386-elf toolchain..."
	wget -q https://github.com/lordmilko/i686-elf-tools/releases/download/7.1.0/i686-elf-tools-linux.zip
	unzip -q i686-elf-tools-linux.zip
	sudo cp i686-elf-tools/bin/* /usr/local/bin/
	rm -rf i686-elf-tools i686-elf-tools-linux.zip
	@echo "✓ Setup complete! Run 'make' to build."
