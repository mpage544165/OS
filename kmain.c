#include "io.h"
#include "gdt.h"
#include "idt.h"
#include "isrs.h"
#include "irq.h"
#include "kb.h"
#include "pt.h"

#define BG_BLACK 0
#define FB_GREEN     2
#define FB_DARK_GREY 8 
#define FB_WHITE 15

static unsigned char *fb = (unsigned char *) 0xC00B8000;
const unsigned int TERMINAL_BUF_LEN = 2000; // 80 * 25

unsigned int terminal_col = 0;
unsigned int terminal_row = 0;
unsigned char terminal_color;

const unsigned int VGA_WIDTH = 80;
const unsigned int VGA_HEIGHT = 25;

enum vga_color {
	VGA_COLOR_BLACK = 0,
	VGA_COLOR_BLUE = 1,
	VGA_COLOR_GREEN = 2,
	VGA_COLOR_CYAN = 3,
	VGA_COLOR_RED = 4,
	VGA_COLOR_MAGENTA = 5,
	VGA_COLOR_BROWN = 6,
	VGA_COLOR_LIGHT_GREY = 7,
	VGA_COLOR_DARK_GREY = 8,
	VGA_COLOR_LIGHT_BLUE = 9,
	VGA_COLOR_LIGHT_GREEN = 10,
	VGA_COLOR_LIGHT_CYAN = 11,
	VGA_COLOR_LIGHT_RED = 12,
	VGA_COLOR_LIGHT_MAGENTA = 13,
	VGA_COLOR_LIGHT_BROWN = 14,
	VGA_COLOR_WHITE = 15,
};
 
static inline unsigned char vga_entry_color(enum vga_color fg, enum vga_color bg) 
{
	return fg | bg << 4;
}
 
static inline unsigned int vga_entry(unsigned char uc, unsigned char color) 
{
	return (unsigned int) uc | (unsigned int) color << 8;
}

/* Finds length of a string */
unsigned int strlen(const char* str) 
{
	unsigned int len = 0;
	while (str[len])
		len++;
	return len;
}

/* The I/O ports */

/* All the I/O ports are calculated relative to the data port. This is because
 * all serial ports (COM1, COM2, COM3, COM4) have their ports in the same
 * order, but they start at different values.
 */

#define SERIAL_COM1_BASE                0x3F8      /* COM1 base port */

#define SERIAL_DATA_PORT(base)          (base)
#define SERIAL_FIFO_COMMAND_PORT(base)  (base + 2)
#define SERIAL_LINE_COMMAND_PORT(base)  (base + 3)
#define SERIAL_MODEM_COMMAND_PORT(base) (base + 4)
#define SERIAL_LINE_STATUS_PORT(base)   (base + 5)

/* The I/O port commands */

/* SERIAL_LINE_ENABLE_DLAB:
 * Tells the serial port to expect first the highest 8 bits on the data port,
 * then the lowest 8 bits will follow
 */
#define SERIAL_LINE_ENABLE_DLAB         0x80

/** serial_configure_baud_rate:
 *  Sets the speed of the data being sent. The default speed of a serial
 *  port is 115200 bits/s. The argument is a divisor of that number, hence
 *  the resulting speed becomes (115200 / divisor) bits/s.
 *
 *  @param com      The COM port to configure
 *  @param divisor  The divisor
 */
void serial_configure_baud_rate(unsigned short com, unsigned short divisor)
{
    outb(SERIAL_LINE_COMMAND_PORT(com),
         SERIAL_LINE_ENABLE_DLAB);
    outb(SERIAL_DATA_PORT(com),
         (divisor >> 8) & 0x00FF);
    outb(SERIAL_DATA_PORT(com),
         divisor & 0x00FF);
}

/** serial_configure_line:
 *  Configures the line of the given serial port. The port is set to have a
 *  data length of 8 bits, no parity bits, one stop bit and break control
 *  disabled.
 *
 *  @param com  The serial port to configure
 */
void serial_configure_line(unsigned short com)
{
    /* Bit:     | 7 | 6 | 5 4 3 | 2 | 1 0 |
     * Content: | d | b | prty  | s | dl  |
     * Value:   | 0 | 0 | 0 0 0 | 0 | 1 1 | = 0x03
     */
    outb(SERIAL_LINE_COMMAND_PORT(com), 0x03);
}

void serial_configure_buffers(unsigned short com) 
{
	outb(SERIAL_FIFO_COMMAND_PORT(com), 0xC7);
}

void serial_configure_modem(unsigned short com)
{
	outb(SERIAL_MODEM_COMMAND_PORT(com), 0x03);
}

/** serial_is_transmit_fifo_empty:
 *  Checks whether the transmit FIFO queue is empty or not for the given COM
 *  port.
 *
 *  @param  com The COM port
 *  @return 0 if the transmit FIFO queue is not empty
 *          1 if the transmit FIFO queue is empty
 */
int serial_is_transmit_fifo_empty(unsigned int com)
{
    /* 0x20 = 0010 0000 */
    return inb(SERIAL_LINE_STATUS_PORT(com)) & 0x20;
}

void init_serial() {
	serial_configure_baud_rate(SERIAL_COM1_BASE, 0x08);
	serial_configure_line(SERIAL_COM1_BASE);
	serial_configure_buffers(SERIAL_COM1_BASE);
	serial_configure_modem(SERIAL_COM1_BASE);

}

void serial_putchar(unsigned short com, char c) {
	while (serial_is_transmit_fifo_empty(com) == 0);
 
	outb(SERIAL_DATA_PORT(com), c);
}

void write_serial(unsigned short com, char* buf)
{
	unsigned int len = strlen(buf);

	for (unsigned int i = 0; i < len; i++) {
		serial_putchar(com, buf[i]);
	}
}

/* The I/O ports */
#define FB_COMMAND_PORT         0x3D4
#define FB_DATA_PORT            0x3D5

/* The I/O port commands */
#define FB_HIGH_BYTE_COMMAND    14
#define FB_LOW_BYTE_COMMAND     15

/** fb_move_cursor:
 *  Moves the cursor of the framebuffer to the given position
 *
 *  @param pos The new position of the cursor
 */
void fb_move_cursor(unsigned short pos)
{
    outb(FB_COMMAND_PORT, FB_HIGH_BYTE_COMMAND);
    outb(FB_DATA_PORT,    ((pos >> 8) & 0x00FF));
    outb(FB_COMMAND_PORT, FB_LOW_BYTE_COMMAND);
    outb(FB_DATA_PORT,    pos & 0x00FF);
}


/** fb_write_cell:
 *  Writes a character with the given foreground and background to position i
 *  in the framebuffer.
 *
 *  @param i  The location in the framebuffer
 *  @param c  The character
 *  @param fg The foreground color
 *  @param bg The background color
 */
void fb_write_cell(unsigned int i, char c, unsigned char fg, unsigned char bg)
{
    fb[i] = c;
    fb[i + 1] = ((fg & 0x0F) << 4) | (bg & 0x0F);
}

void init_terminal() 
{
	terminal_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

	for (unsigned int i = 160; i < 800; i += 2)
	{
		fb[i] = ' ';
    	fb[i + 1] = ((BG_BLACK & 0x0F) << 4) | (BG_BLACK & 0x0F);
	}
}

void terminal_scroll() {
	for (unsigned int i = 0; i < TERMINAL_BUF_LEN * 2 - 160; i += 2) {
		fb[i] = fb[i + 160];
		fb[i + 1] = fb[i + 160 + 1];
	}

	for (unsigned int i = 0; i < VGA_WIDTH; i += 2) {
		fb_write_cell((VGA_HEIGHT - 2) * 2 * VGA_WIDTH + i, '\0', BG_BLACK, BG_BLACK);
	}
}

void putchar(char c) 
{
	if (c == '\n')
	{
		terminal_col = 0;
		terminal_row++;
		c = '\0';
	}

	unsigned int index = terminal_row * VGA_WIDTH * 2 + terminal_col;
	//fb_write_cell(terminal_col, terminal_row, c, BG_BLACK, FB_WHITE);
	fb_write_cell(index, c, BG_BLACK, FB_WHITE);
	//fb_write_cell(terminal_pos, 'A', FB_GREEN, FB_DARK_GREY);

	terminal_col += 2;
	if (terminal_col == VGA_WIDTH) {
		terminal_col = 0;
		terminal_row++;
	}

	if (terminal_row == VGA_HEIGHT - 1) {
		terminal_scroll();
		terminal_row--;
	}

	unsigned short cursor_index = (terminal_row + 1) * VGA_WIDTH + terminal_col;
	fb_move_cursor(cursor_index);
}

void write(char* buf)
{
	unsigned int len = strlen(buf);

	for(unsigned int i = 0; i < len; i++) {

		putchar(buf[i]);
	}
}

char* itoa(int val, int base){
	
	static char buf[32] = {0};
	
	int i = 30;
	
	for(; val && i ; --i, val /= base)
	
		buf[i] = "0123456789abcdef"[val % base];
	
	return &buf[i+1];
	
}

extern kernel_physical_end;
extern kernel_physical_start;
extern kernel_virtual_end;
extern kernel_virtual_start;

void kmain( unsigned int kernel_virtual_start, 
			unsigned int kernel_virtual_end, 
			unsigned int kernel_physical_start, 
			unsigned int kernel_physical_end )
{
	gdt_install();
	idt_install();
	install_page_directory();
	isrs_install();
	irq_install();
	keyboard_install();

	__asm__ __volatile__ ("sti"); 

	init_terminal();
	init_serial();

	write("Hello Kernel World\n");

	write("Hello Kernel World\n");

	for (unsigned int i = 0; i < VGA_HEIGHT; i++)
	{
		write("Hello Kernel World\n");
	}

	write("New Line!\n");
	write("New Line 2!\n");

	write_serial(SERIAL_COM1_BASE, itoa(kernel_physical_start, 16));
	write(itoa(kernel_physical_start, 16));
	putchar('\n');
	write(itoa(kernel_physical_end, 16));
	putchar('\n');
	write(itoa(kernel_virtual_start, 16));
	putchar('\n');
	write(itoa(kernel_virtual_end, 16));
	putchar('\n');
}