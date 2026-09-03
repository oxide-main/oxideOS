#include "vga.h"
#include "../common_headers/io.h"

volatile uint16_t* vga_buffer = (volatile uint16_t*) 0xB8000;

uint8_t vga_entry_color(uint8_t fg, uint8_t bg) 
{
    return fg | (bg << 4);
}

uint16_t vga_entry(unsigned char uc, uint8_t color) 
{
    return (uint16_t) uc | ((uint16_t) color << 8);
}

void vga_put_char(char c, uint8_t color, int x, int y) 
{
    update_cursor(x, y);
    vga_buffer[y * VGA_WIDTH + x] = vga_entry(c, color);
}

void vga_clean_screen()
{
    for (int y = 0; y < VGA_HEIGHT; y++)
    {
        for (int x = 0; x < VGA_WIDTH; x++)
        {
            vga_put_char(' ', vga_entry_color(WHITE, BLACK), x, y);
        }
    }
}

int vga_put_chars(char* c, uint8_t color, int line) 
{
    if (line < 0 || line >= VGA_HEIGHT) 
    {
        return -1;
    }

    int column = 0;

    for (int i = 0; c[i] != '\0'; i++) 
    {
        if (c[i] == '\n') 
        {
            line++;
            column = 0;
            if (line >= VGA_HEIGHT) 
            {
                vga_scroll();
                line = VGA_HEIGHT - 1;
            }
            continue;
        }
        
        if (c[i] == '\r')
        {
            column = 0;
            continue;
        }

        if (column >= VGA_WIDTH) 
        {
            column = 0;
            line++;
            if (line >= VGA_HEIGHT) 
            {
                vga_scroll();
                line = VGA_HEIGHT - 1;
            }
        }

        vga_put_char(c[i], color, column, line);
        column++;
    }

    return line + 1;
}

void vga_scroll() 
{
    for (int y = 0; y < VGA_HEIGHT - 1; y++) 
    {
        for (int x = 0; x < VGA_WIDTH; x++) 
        {
            vga_buffer[y * VGA_WIDTH + x] = vga_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }

    uint16_t blank = vga_entry(' ', vga_entry_color(WHITE, BLACK));
    for (int x = 0; x < VGA_WIDTH; x++) 
    {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }
}

void update_cursor(int x, int y)
{
	uint16_t pos = y * VGA_WIDTH + x;

	outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t) (pos & 0xFF));
	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}
