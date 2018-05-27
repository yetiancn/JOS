#include <inc/lib.h>

void handler(struct UTrapframe *utf)
{
    cprintf("General Protection Fault!\n");
    exit();
}
void umain(int argc, char **argv)
{
    set_exception_handler(T_GPFLT, handler);
    asm volatile("int $13");
}
