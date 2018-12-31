#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kernel/display/vga.h>
#include <kernel/display/terminal.h>

static size_t row = 0;
static size_t col = 0;
static enum vga_color foreground = VGA_COLOR_WHITE;
static enum vga_color background = VGA_COLOR_BLACK;

void clear_terminal() {
    for (size_t row = 0; row < VGA_HEIGHT; row++) {
        for (size_t col = 0; col < VGA_WIDTH; col++) {
            VGA_BASE[VGA_INDEX(row, col)] = VGA_ENTRY(' ', foreground, background);
        }
    }
}

bool kprint(const char *str, size_t len) {
    if (row >= VGA_HEIGHT) {
        row = 0;
    }
    for (size_t i = 0; str[i] != '\0' && i < len; i++) {
        if (str[i] == '\n' || col >= VGA_WIDTH) {
            while (col < VGA_WIDTH) {
                VGA_BASE[VGA_INDEX(row, col++)] = VGA_ENTRY(' ', foreground, background);
            }
            row++;
            col = 0;
            if (row >= VGA_HEIGHT) {
                memmove(VGA_BASE, VGA_BASE + VGA_WIDTH, VGA_WIDTH * (VGA_HEIGHT - 1) * sizeof(uint16_t) / sizeof(unsigned char));
                for (int i = 0; i < VGA_WIDTH; i++) {
                    VGA_BASE[VGA_INDEX(VGA_HEIGHT - 1, i)] = VGA_ENTRY(' ', foreground, background);
                }
                row--;
            }
        } else {
            VGA_BASE[VGA_INDEX(row, col++)] = VGA_ENTRY(str[i], foreground, background);
        }
    }
    return true;
}

void set_foreground(enum vga_color _foreground) {
    foreground = _foreground;
}

void set_background(enum vga_color _background) {
    background = _background;
}