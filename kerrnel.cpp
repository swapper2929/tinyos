/*
 * Bare-metal x86 OS - Single file C++ kernel (64KB source)
 * Compile: i386-elf-g++ -m32 -ffreestanding -nostdlib -fno-exceptions -fno-rtti -c kernel.cpp -o kernel.o
 * Link:    i386-elf-ld -m elf_i386 -Ttext 0x100000 --nmagic -o kernel.elf kernel.o
 * Boot:    qemu-system-i386 -kernel kernel.elf
 */

// =========================== MULTIBOOT HEADER ===========================
__attribute__((section(".text")))
struct multiboot_header {
    unsigned int magic   = 0x1BADB002;
    unsigned int flags   = 0x00000003;
    unsigned int checksum = -(0x1BADB002 + 0x00000003);
} __attribute__((packed)) header;

// =========================== LIBRARY HELPERS ===========================
typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;
typedef int            int32_t;

#define outb(port, val) __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port))
#define inb(port) ({ unsigned char _v; __asm__ volatile ("inb %1, %0" : "=a"(_v) : "Nd"(port)); _v; })

void* memset(void* s, int c, unsigned int n) {
    unsigned char* p = (unsigned char*)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}
void* memcpy(void* dest, const void* src, unsigned int n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dest;
}
int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}
char* strcpy(char* d, const char* s) { char* r = d; while ((*d++ = *s++)); return r; }
unsigned int strlen(const char* s) { unsigned int i = 0; while (s[i]) i++; return i; }
char* strcat(char* d, const char* s) { char* r = d; while (*d) d++; while ((*d++ = *s++)); return r; }

// =========================== VGA TEXT DRIVER ===========================
static const uint32_t VGA_WIDTH = 80;
static const uint32_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEM = (uint16_t*)0xB8000;
static uint32_t vga_row = 0, vga_col = 0;
static uint8_t vga_color = 0x0F;

void vga_setcolor(uint8_t color) { vga_color = color; }
void vga_scroll() {
    for (uint32_t y = 1; y < VGA_HEIGHT; y++)
        for (uint32_t x = 0; x < VGA_WIDTH; x++)
            VGA_MEM[(y-1)*VGA_WIDTH + x] = VGA_MEM[y*VGA_WIDTH + x];
    for (uint32_t x = 0; x < VGA_WIDTH; x++)
        VGA_MEM[(VGA_HEIGHT-1)*VGA_WIDTH + x] = (vga_color << 8) | ' ';
}
void vga_putchar(char c) {
    if (c == '\n') { vga_col = 0; vga_row++; }
    else if (c == '\r') { vga_col = 0; }
    else if (c == '\b') { if (vga_col > 0) vga_col--; VGA_MEM[vga_row*VGA_WIDTH + vga_col] = (vga_color << 8) | ' '; }
    else {
        VGA_MEM[vga_row*VGA_WIDTH + vga_col] = (vga_color << 8) | (uint16_t)c;
        vga_col++;
    }
    if (vga_col >= VGA_WIDTH) { vga_col = 0; vga_row++; }
    if (vga_row >= VGA_HEIGHT) { vga_scroll(); vga_row = VGA_HEIGHT - 1; }
}
void vga_puts(const char* s) { while (*s) vga_putchar(*s++); }

typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_arg(v,l)   __builtin_va_arg(v,l)
#define va_end(v)     __builtin_va_end(v)

static void print_dec(uint32_t n) {
    char buf[12]; int i = 0;
    if (n == 0) { vga_putchar('0'); return; }
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    while (i--) vga_putchar(buf[i]);
}
static void print_hex(uint32_t n) {
    const char hex[] = "0123456789ABCDEF";
    vga_putchar('0'); vga_putchar('x');
    int leading = 1;
    for (int i = 28; i >= 0; i -= 4) {
        int d = (n >> i) & 0xF;
        if (d || !leading || i == 0) { vga_putchar(hex[d]); leading = 0; }
    }
}
void kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    for (const char* p = fmt; *p; ++p) {
        if (*p == '%') {
            ++p;
            if (*p == 's') { vga_puts(va_arg(args, const char*)); }
            else if (*p == 'd') { print_dec((uint32_t)va_arg(args, int)); }
            else if (*p == 'u') { print_dec(va_arg(args, uint32_t)); }
            else if (*p == 'x') { print_hex(va_arg(args, uint32_t)); }
            else if (*p == 'p') { print_hex((uint32_t)va_arg(args, void*)); }
            else { vga_putchar('%'); if (*p) vga_putchar(*p); }
        } else { vga_putchar(*p); }
    }
    va_end(args);
}

// =========================== GLOBAL DESCRIPTOR TABLE ===========================
struct gdt_entry { uint16_t limit_low; uint16_t base_low; uint8_t base_mid; uint8_t access; uint8_t granularity; uint8_t base_high; } __attribute__((packed));
struct gdt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));
static gdt_entry gdt[3];
static gdt_ptr gp;

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_mid = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}
void gdt_install() {
    gp.limit = sizeof(gdt) - 1;
    gp.base = (uint32_t)&gdt;
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    __asm__ volatile ("lgdt (%0)" : : "r"(&gp));
    __asm__ volatile (
        "mov $0x10, %ax \n"
        "mov %ax, %ds \n"
        "mov %ax, %es \n"
        "mov %ax, %fs \n"
        "mov %ax, %gs \n"
        "mov %ax, %ss \n"
        "ljmp $0x08, $1f \n"
        "1:"
    );
}

// =========================== INTERRUPT DESCRIPTOR TABLE ===========================
struct idt_entry { uint16_t base_low; uint16_t sel; uint8_t always0; uint8_t flags; uint16_t base_high; } __attribute__((packed));
struct idt_ptr { uint16_t limit; uint32_t base; } __attribute__((packed));
static idt_entry idt[256];
static idt_ptr idtp;

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}
void idt_install() {
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt;
    __asm__ volatile ("lidt (%0)" : : "r"(&idtp));
}

// =========================== PIC ===========================
#define PIC1 0x20
#define PIC2 0xA0
#define PIC1_CMD PIC1
#define PIC1_DATA (PIC1+1)
#define PIC2_CMD PIC2
#define PIC2_DATA (PIC2+1)
#define ICW1_ICW4 0x01
#define ICW1_INIT 0x10
#define ICW4_8086 0x01

void pic_remap() {
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);
    outb(PIC1_DATA, 0xFB);
    outb(PIC2_DATA, 0xFF);
    __asm__ volatile ("sti");
}

// =========================== ISR / IRQ HANDLERS ===========================
extern "C" void isr_handler(uint32_t int_no, uint32_t err_code) {
    kprintf("\n[!] Interrupt: %d, Error: %x\n", int_no, err_code);
    if (int_no == 14) {
        uint32_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        kprintf("Page Fault at addr: %x\n", cr2);
    }
    while(1) __asm__ volatile ("hlt");
}

extern "C" void irq_handler(uint32_t irq) {
    if (irq == 1) { /* keyboard */ }
    else if (irq == 0) { /* timer */ }
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}

#define ISR_STUB(i) \
    __asm__(".global isr_" #i "\n" \
            "isr_" #i ": \n" \
            "   pushl $0 \n" \
            "   pushl $" #i " \n" \
            "   jmp isr_common_stub \n");
#define IRQ_STUB(i) \
    __asm__(".global irq_" #i "\n" \
            "irq_" #i ": \n" \
            "   pushl $0 \n" \
            "   pushl $" #i " \n" \
            "   jmp irq_common_stub \n");

#define GEN_STUBS \
    ISR_STUB(0)  ISR_STUB(1)  ISR_STUB(2)  ISR_STUB(3) \
    ISR_STUB(4)  ISR_STUB(5)  ISR_STUB(6)  ISR_STUB(7) \
    ISR_STUB(8)  ISR_STUB(9)  ISR_STUB(10) ISR_STUB(11) \
    ISR_STUB(12) ISR_STUB(13) ISR_STUB(14) ISR_STUB(15) \
    ISR_STUB(16) ISR_STUB(17) ISR_STUB(18) ISR_STUB(19) \
    ISR_STUB(20) ISR_STUB(21) ISR_STUB(22) ISR_STUB(23) \
    ISR_STUB(24) ISR_STUB(25) ISR_STUB(26) ISR_STUB(27) \
    ISR_STUB(28) ISR_STUB(29) ISR_STUB(30) ISR_STUB(31) \
    IRQ_STUB(0)  IRQ_STUB(1)  IRQ_STUB(2)  IRQ_STUB(3) \
    IRQ_STUB(4)  IRQ_STUB(5)  IRQ_STUB(6)  IRQ_STUB(7) \
    IRQ_STUB(8)  IRQ_STUB(9)  IRQ_STUB(10) IRQ_STUB(11) \
    IRQ_STUB(12) IRQ_STUB(13) IRQ_STUB(14) IRQ_STUB(15)

GEN_STUBS

__asm__(
".macro ISR_NOERRCODE num \n"
"   .global isr_\\num \n"
"isr_\\num: \n"
"   pushl $0 \n"
"   pushl $\\num \n"
"   jmp isr_common_stub \n"
".endm \n"

"isr_common_stub: \n"
"   pusha \n"
"   push %ds \n"
"   push %es \n"
"   push %fs \n"
"   push %gs \n"
"   mov $0x10, %ax \n"
"   mov %ax, %ds \n"
"   mov %ax, %es \n"
"   push %esp \n"
"   call isr_handler \n"
"   pop %esp \n"
"   pop %gs \n"
"   pop %fs \n"
"   pop %es \n"
"   pop %ds \n"
"   popa \n"
"   add $8, %esp \n"
"   iret \n"

"irq_common_stub: \n"
"   pusha \n"
"   push %ds \n"
"   push %es \n"
"   push %fs \n"
"   push %gs \n"
"   mov $0x10, %ax \n"
"   mov %ax, %ds \n"
"   mov %ax, %es \n"
"   push %esp \n"
"   call irq_handler \n"
"   pop %esp \n"
"   pop %gs \n"
"   pop %fs \n"
"   pop %es \n"
"   pop %ds \n"
"   popa \n"
"   add $8, %esp \n"
"   iret \n"
);

void isr_install() {
    for (int i = 0; i < 32; i++) {
        uint32_t addr;
        __asm__ volatile ("lea isr_%c1, %0" : "=r"(addr) : "i"(i));
        idt_set_gate(i, addr, 0x08, 0x8E);
    }
    for (int i = 0; i < 16; i++) {
        uint32_t addr;
        __asm__ volatile ("lea irq_%c1, %0" : "=r"(addr) : "i"(i));
        idt_set_gate(i + 32, addr, 0x08, 0x8E);
    }
    idt_install();
    pic_remap();
}

// =========================== KEYBOARD DRIVER ===========================
static volatile uint8_t keyboard_buffer[256];
static volatile uint32_t keyboard_head = 0, keyboard_tail = 0;
static uint8_t shift_state = 0;

static const char keymap_normal[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', 8,  0,
    'q','w','e','r','t','y','u','i','o','p','[',']', 0,  0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,
    '\\','z','x','c','v','b','n','m',',','.','/', 0,  '*', 0,  ' ', 0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};
static const char keymap_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+', 8,  0,
    'Q','W','E','R','T','Y','U','I','O','P','{','}', 0,  0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0,
    '|','Z','X','C','V','B','N','M','<','>','?', 0,  '*', 0,  ' ', 0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

extern "C" void keyboard_handler(uint32_t irq) {
    (void)irq;
    uint8_t scancode = inb(0x60);
    if (scancode == 0x2A || scancode == 0x36) { shift_state = 1; return; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_state = 0; return; }
    if (scancode & 0x80) return;
    char c = shift_state ? keymap_shift[scancode] : keymap_normal[scancode];
    if (c) {
        keyboard_buffer[keyboard_head] = c;
        keyboard_head = (keyboard_head + 1) % 256;
    }
}
int keyb_haschar() { return keyboard_head != keyboard_tail; }
char keyb_getchar() {
    while (!keyb_haschar()) __asm__ volatile ("hlt");
    char c = keyboard_buffer[keyboard_tail];
    keyboard_tail = (keyboard_tail + 1) % 256;
    return c;
}
void keyb_readline(char* buf, int maxlen) {
    int i = 0;
    while (1) {
        char c = keyb_getchar();
        if (c == '\n' || c == '\r') { buf[i] = '\0'; vga_putchar('\n'); return; }
        else if (c == 8) { if (i > 0) { i--; vga_putchar(8); } }
        else if (i < maxlen - 1 && c >= 32 && c <= 126) { buf[i++] = c; vga_putchar(c); }
    }
}

// =========================== TIMER ===========================
static volatile uint32_t tick_count = 0;
extern "C" void timer_handler(uint32_t irq) {
    (void)irq;
    tick_count++;
}
void timer_init() {
    outb(0x43, 0x36);
    uint32_t freq = 1193180 / 100;
    outb(0x40, (uint8_t)(freq & 0xFF));
    outb(0x40, (uint8_t)((freq >> 8) & 0xFF));
}

// =========================== PHYSICAL MEMORY MANAGER ===========================
#define PMM_MAX_PAGES (1024 * 1024)
static uint32_t pmm_bitmap[PMM_MAX_PAGES / 32];
static uint32_t pmm_total_pages = 0;
static uint32_t pmm_used_pages = 0; // Thêm để đếm số trang đã dùng

void pmm_init(uint32_t mem_size_kb) {
    uint32_t mem_size = mem_size_kb * 1024;
    pmm_total_pages = mem_size / 4096;
    if (pmm_total_pages > PMM_MAX_PAGES) pmm_total_pages = PMM_MAX_PAGES;
    memset(pmm_bitmap, 0, sizeof(pmm_bitmap));
    // Mark first 1MB as used (kernel)
    for (uint32_t i = 0; i < 256; i++) {
        pmm_bitmap[i/32] |= (1 << (i % 32));
        pmm_used_pages++;
    }
}
uint32_t pmm_alloc() {
    for (uint32_t i = 0; i < pmm_total_pages / 32; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t bit = 0; bit < 32; bit++) {
                if (!(pmm_bitmap[i] & (1 << bit))) {
                    pmm_bitmap[i] |= (1 << bit);
                    pmm_used_pages++;
                    return i * 32 + bit;
                }
            }
        }
    }
    return 0xFFFFFFFF;
}
void pmm_free(uint32_t page) {
    if (page < pmm_total_pages && (pmm_bitmap[page/32] & (1 << (page % 32)))) {
        pmm_bitmap[page/32] &= ~(1 << (page % 32));
        pmm_used_pages--;
    }
}

// =========================== VIRTUAL MEMORY ===========================
#define PAGE_DIR_ENTRIES 1024
#define PAGE_TABLE_ENTRIES 1024
#define PAGE_SIZE 4096
typedef struct { uint32_t entries[PAGE_DIR_ENTRIES]; } page_directory_t;
typedef struct { uint32_t entries[PAGE_TABLE_ENTRIES]; } page_table_t;

static page_directory_t* kernel_page_dir = 0;
static page_table_t* kernel_page_tables[1024];

void vmm_init() {
    uint32_t pd_page = pmm_alloc();
    if (pd_page == 0xFFFFFFFF) return;
    kernel_page_dir = (page_directory_t*)(pd_page * PAGE_SIZE);
    memset(kernel_page_dir, 0, PAGE_SIZE);
    for (uint32_t i = 0; i < 1; i++) {
        uint32_t pt_page = pmm_alloc();
        if (pt_page == 0xFFFFFFFF) return;
        page_table_t* pt = (page_table_t*)(pt_page * PAGE_SIZE);
        memset(pt, 0, PAGE_SIZE);
        for (uint32_t j = 0; j < 1024; j++) {
            pt->entries[j] = (i * 1024 + j) * PAGE_SIZE | 3;
        }
        kernel_page_dir->entries[i] = (uint32_t)pt | 3;
        kernel_page_tables[i] = pt;
    }
    __asm__ volatile ("mov %0, %%cr3" : : "r"(kernel_page_dir));
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0));
}

// =========================== HEAP ALLOCATOR ===========================
#define HEAP_START 0x400000
#define HEAP_INIT_SIZE (4 * 1024 * 1024)

struct block_header {
    uint32_t size;
    uint8_t free;
    struct block_header* next;
} __attribute__((packed));
static block_header* free_list = 0;

void kmalloc_init() {
    free_list = (block_header*)HEAP_START;
    free_list->size = HEAP_INIT_SIZE - sizeof(block_header);
    free_list->free = 1;
    free_list->next = 0;
}
void* kmalloc(uint32_t size) {
    size += sizeof(block_header);
    block_header* curr = free_list;
    block_header* prev = 0;
    while (curr) {
        if (curr->free && curr->size >= size) {
            if (curr->size >= size + sizeof(block_header) + 16) {
                block_header* new = (block_header*)((uint32_t)curr + size);
                new->size = curr->size - size;
                new->free = 1;
                new->next = curr->next;
                curr->size = size;
                curr->next = new;
            }
            curr->free = 0;
            return (void*)((uint32_t)curr + sizeof(block_header));
        }
        prev = curr;
        curr = curr->next;
    }
    return 0;
}
void kfree(void* ptr) {
    if (!ptr) return;
    block_header* block = (block_header*)((uint32_t)ptr - sizeof(block_header));
    block->free = 1;
    if (block->next && block->next->free) {
        block->size += block->next->size + sizeof(block_header);
        block->next = block->next->next;
    }
    block_header* curr = free_list;
    while (curr && curr->next != block) curr = curr->next;
    if (curr && curr->free) {
        curr->size += block->size + sizeof(block_header);
        curr->next = block->next;
    }
}

// =========================== SHELL COMMANDS ===========================
static char command_history[10][64];
static int hist_idx = 0, hist_count = 0;

void cmd_help() {
    kprintf("Available commands:\n");
    kprintf("  help       - Show this help\n");
    kprintf("  clear      - Clear screen\n");
    kprintf("  echo [txt] - Print text\n");
    kprintf("  status     - Show system info\n");
    kprintf("  screenfetch- Show OS logo and system info\n");
    kprintf("  time       - Show uptime ticks\n");
    kprintf("  hexdump    - Dump memory\n");
    kprintf("  calc a+b   - Simple calculator (+, -, *, /)\n");
    kprintf("  snake      - Play snake game\n");
    kprintf("  mandel     - Draw Mandelbrot\n");
    kprintf("  reboot     - Reboot system\n");
}
void cmd_clear() {
    vga_row = vga_col = 0;
    for (uint32_t i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) VGA_MEM[i] = (vga_color << 8) | ' ';
}
void cmd_status() {
    kprintf("Kernel: Single-file C++ x86 OS\n");
    kprintf("Total RAM pages: %d\n", pmm_total_pages);
    kprintf("Used RAM pages: %d\n", pmm_used_pages);
    kprintf("Free list base: %x\n", (uint32_t)free_list);
    kprintf("Ticks: %d\n", tick_count);
}
void cmd_time() { kprintf("Uptime: %d ticks (10ms each)\n", tick_count); }
void cmd_hexdump(uint32_t addr, uint32_t len) {
    uint8_t* p = (uint8_t*)addr;
    for (uint32_t i = 0; i < len; i += 16) {
        kprintf("%x: ", (uint32_t)(p+i));
        for (uint32_t j = 0; j < 16 && i+j < len; j++) {
            kprintf("%02x ", p[i+j]);
            if (j == 7) vga_putchar(' ');
        }
        vga_putchar(' ');
        for (uint32_t j = 0; j < 16 && i+j < len; j++) {
            char c = p[i+j];
            if (c < 32 || c > 126) c = '.';
            vga_putchar(c);
        }
        vga_putchar('\n');
    }
}
void cmd_calc(char* expr) {
    int a, b; char op;
    if (sscanf(expr, "%d%c%d", &a, &op, &b) != 3) { kprintf("Invalid format. Use: calc 5+3\n"); return; }
    switch(op) {
        case '+': kprintf("%d\n", a+b); break;
        case '-': kprintf("%d\n", a-b); break;
        case '*': kprintf("%d\n", a*b); break;
        case '/': if (b) kprintf("%d\n", a/b); else kprintf("Division by zero!\n"); break;
        default: kprintf("Unknown operator\n");
    }
}

// =========================== SCREENFETCH ===========================
const char* os_logo =
"   █████╗ ██╗   ██╗ ██████╗ ███████╗\n"
"  ██╔══██╗╚██╗ ██╔╝██╔═══██╗██╔════╝\n"
"  ███████║ ╚████╔╝ ██║   ██║███████╗\n"
"  ██╔══██║  ╚██╔╝  ██║   ██║╚════██║\n"
"  ██║  ██║   ██║   ╚██████╔╝███████║\n"
"  ╚═╝  ╚═╝   ╚═╝    ╚═════╝ ╚══════╝\n";

void cmd_screenfetch() {
    // In logo màu xanh (mã 0x0A là xanh sáng)
    uint8_t old_color = vga_color;
    vga_setcolor(0x0A);
    vga_puts(os_logo);
    vga_setcolor(0x0F);

    // Thông tin hệ thống
    kprintf("OS:        MyOS 1.0 (Single-file C++ x86)\n");
    kprintf("Kernel:    Bare-metal 32-bit\n");
    kprintf("CPU:       x86 (i386 compatible)\n");
    kprintf("RAM:       %d KB total, %d KB used, %d KB free\n",
            pmm_total_pages * 4, pmm_used_pages * 4, (pmm_total_pages - pmm_used_pages) * 4);
    kprintf("Uptime:    %d ticks (%.1f seconds)\n", tick_count, (float)tick_count / 100.0f);
    kprintf("Shell:     Built-in command line\n");
    kprintf("Features:  VGA, Keyboard, Paging, Heap, Snake, Mandelbrot\n");
}

// =========================== SNAKE GAME ===========================
#define SNAKE_W 40
#define SNAKE_H 20
static int snake_x[100], snake_y[100], snake_len = 3;
static int snake_dx = 1, snake_dy = 0;
static int food_x, food_y;
static uint8_t snake_game_over = 0;

void snake_reset() {
    snake_len = 3;
    snake_dx = 1; snake_dy = 0;
    for (int i = 0; i < snake_len; i++) { snake_x[i] = SNAKE_W/2 - i; snake_y[i] = SNAKE_H/2; }
    food_x = 5; food_y = 5;
    snake_game_over = 0;
}
void snake_place_food() {
    int found = 0;
    for (int tries = 0; tries < 100; tries++) {
        int fx = rand() % SNAKE_W, fy = rand() % SNAKE_H;
        int ok = 1;
        for (int i = 0; i < snake_len; i++) if (snake_x[i] == fx && snake_y[i] == fy) { ok = 0; break; }
        if (ok) { food_x = fx; food_y = fy; found = 1; break; }
    }
    if (!found) { food_x = -1; food_y = -1; }
}
void snake_render() {
    for (int y = 0; y < SNAKE_H; y++) {
        for (int x = 0; x < SNAKE_W; x++) {
            char c = ' ';
            if (x == food_x && y == food_y) c = '@';
            else {
                for (int i = 0; i < snake_len; i++) {
                    if (snake_x[i] == x && snake_y[i] == y) { c = '#'; break; }
                }
            }
            vga_putchar(c);
        }
        vga_putchar('\n');
    }
}
void snake_update() {
    if (snake_game_over) return;
    int nx = snake_x[0] + snake_dx;
    int ny = snake_y[0] + snake_dy;
    if (nx < 0 || nx >= SNAKE_W || ny < 0 || ny >= SNAKE_H) { snake_game_over = 1; return; }
    for (int i = 0; i < snake_len - 1; i++) if (snake_x[i] == nx && snake_y[i] == ny) { snake_game_over = 1; return; }
    for (int i = snake_len - 1; i > 0; i--) { snake_x[i] = snake_x[i-1]; snake_y[i] = snake_y[i-1]; }
    snake_x[0] = nx; snake_y[0] = ny;
    if (nx == food_x && ny == food_y) {
        snake_len++;
        snake_x[snake_len-1] = snake_x[snake_len-2] - snake_dx;
        snake_y[snake_len-1] = snake_y[snake_len-2] - snake_dy;
        snake_place_food();
        if (food_x == -1) { snake_game_over = 1; }
    }
}
void cmd_snake() {
    cmd_clear();
    snake_reset(); snake_place_food();
    uint32_t last_tick = tick_count;
    while (!snake_game_over) {
        if (tick_count - last_tick >= 5) {
            last_tick = tick_count;
            while (keyb_haschar()) {
                char c = keyb_getchar();
                if (c == 'w' && snake_dy != 1) { snake_dx = 0; snake_dy = -1; }
                else if (c == 's' && snake_dy != -1) { snake_dx = 0; snake_dy = 1; }
                else if (c == 'a' && snake_dx != 1) { snake_dx = -1; snake_dy = 0; }
                else if (c == 'd' && snake_dx != -1) { snake_dx = 1; snake_dy = 0; }
                else if (c == 'q') { snake_game_over = 2; }
            }
            snake_update();
            cmd_clear();
            snake_render();
            kprintf("Score: %d | Use WASD, Q to quit\n", snake_len - 3);
        }
        __asm__ volatile ("hlt");
    }
    if (snake_game_over == 1) kprintf("\nGame Over! Press any key...\n");
    else kprintf("\nQuit. Press any key...\n");
    while (!keyb_haschar()) __asm__ volatile ("hlt");
    while (keyb_haschar()) keyb_getchar();
    cmd_clear();
}

// =========================== MANDELBROT ===========================
void cmd_mandel() {
    cmd_clear();
    int w = 60, h = 20;
    float xmin = -2.0, xmax = 1.0, ymin = -1.0, ymax = 1.0;
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            float x0 = xmin + (xmax - xmin) * px / w;
            float y0 = ymin + (ymax - ymin) * py / h;
            float x = 0, y = 0;
            int iter = 0, max_iter = 30;
            while (x*x + y*y < 4 && iter < max_iter) {
                float xt = x*x - y*y + x0;
                y = 2*x*y + y0;
                x = xt;
                iter++;
            }
            if (iter == max_iter) vga_putchar(' ');
            else if (iter > 25) vga_putchar('.');
            else if (iter > 20) vga_putchar('*');
            else if (iter > 15) vga_putchar('+');
            else if (iter > 10) vga_putchar('#');
            else vga_putchar('@');
        }
        vga_putchar('\n');
    }
    kprintf("\nPress any key to continue...");
    while (!keyb_haschar()) __asm__ volatile ("hlt");
    while (keyb_haschar()) keyb_getchar();
    cmd_clear();
}

// =========================== REBOOT ===========================
void cmd_reboot() {
    kprintf("Rebooting...\n");
    outb(0x64, 0xFE);
    while(1) __asm__ volatile ("hlt");
}

// =========================== COMMAND PARSER ===========================
void shell_loop() {
    char buf[128];
    char cmd[64], args[64];
    while (1) {
        kprintf("\nOS> ");
        keyb_readline(buf, 128);
        if (strlen(buf) == 0) continue;
        int i = 0; while (buf[i] && buf[i] != ' ') { cmd[i] = buf[i]; i++; } cmd[i] = 0;
        int j = 0; while (buf[i] == ' ') i++; while (buf[i]) args[j++] = buf[i++]; args[j] = 0;
        if (strcmp(cmd, "help") == 0) cmd_help();
        else if (strcmp(cmd, "clear") == 0) cmd_clear();
        else if (strcmp(cmd, "echo") == 0) kprintf("%s\n", args);
        else if (strcmp(cmd, "status") == 0) cmd_status();
        else if (strcmp(cmd, "screenfetch") == 0) cmd_screenfetch();
        else if (strcmp(cmd, "time") == 0) cmd_time();
        else if (strcmp(cmd, "hexdump") == 0) { uint32_t addr = (uint32_t)0x100000; if (args[0]) { int a; if (sscanf(args, "%x", &a) == 1) addr = a; } cmd_hexdump(addr, 128); }
        else if (strcmp(cmd, "calc") == 0) cmd_calc(args);
        else if (strcmp(cmd, "snake") == 0) cmd_snake();
        else if (strcmp(cmd, "mandel") == 0) cmd_mandel();
        else if (strcmp(cmd, "reboot") == 0) cmd_reboot();
        else kprintf("Unknown command. Type 'help'.\n");
    }
}

// =========================== KERNEL ENTRY ===========================
extern "C" void _start() {
    gdt_install();
    isr_install();
    timer_init();

    uint32_t mem_size = 64 * 1024;
    pmm_init(mem_size);
    vmm_init();
    kmalloc_init();

    idt_set_gate(33, (uint32_t)irq_1, 0x08, 0x8E);
    idt_install();

    kprintf("\n========================================\n");
    kprintf("  Single-file C++ x86 Bare-metal OS\n");
    kprintf("  (c) 2026 - 64KB Source Edition\n");
    kprintf("========================================\n");
    kprintf("[+] GDT, IDT, PIC initialized.\n");
    kprintf("[+] PMM: %d KB RAM managed.\n", mem_size);
    kprintf("[+] Paging: Identity mapped 4MB.\n");
    kprintf("[+] Heap: 4MB at 0x400000.\n");
    kprintf("[+] Timer: 100Hz.\n");
    kprintf("[+] Keyboard ready.\n");
    kprintf("Type 'help' to get started.\n");

    shell_loop();
}

extern "C" void __cxa_pure_virtual() { while(1) __asm__ volatile ("hlt"); }
void* operator new(unsigned int, void* p) { return p; }
void* operator new[](unsigned int, void* p) { return p; }
void operator delete(void*) {}
void operator delete[](void*) {}
