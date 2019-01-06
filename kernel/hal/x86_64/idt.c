#include <stdint.h>

#include "gdt.h"
#include "idt.h"

void add_idt_entry(struct idt_entry *idt, void *_handler, unsigned int irq) {
    uintptr_t handler = (uintptr_t) _handler;
    idt[irq].addr_low = (uint16_t) handler;
    idt[irq].target = CS_SELECTOR;
    idt[irq].flags = 0x8E00; // Present, CPL 0, Trap Handler
    idt[irq].addr_mid = (uint16_t) (handler >> 16);
    idt[irq].addr_high = (uint32_t) (handler >> 32);
    idt[irq].reserved = 0;
}

void remove_idt_entry(struct idt_entry *idt, unsigned int irq) {
    idt[irq].flags = 0; // Clears Present
}