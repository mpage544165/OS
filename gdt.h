#ifndef INCLUDE_GDT_H_
#define INCLUDE_GDT_H_

void gdt_set_gate(int num, unsigned long base, unsigned long limit, unsigned char access, unsigned char gran);

void gdt_install();

#endif

