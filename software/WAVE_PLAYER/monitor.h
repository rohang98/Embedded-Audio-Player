#ifndef MONITOR_H_
#define MONITOR_H_

#include "uart.h"

#define xgetc()		(char)uart0_get()

int  xatoi (char**, long*);
void xputs (const char*);
void xputc (char c);
void xitoa (signed long, signed char, signed char);
void xprintf (const char*, ...);
void put_dump (const unsigned char*, unsigned long ofs, int cnt);
void get_line (char*, int len);

#endif
