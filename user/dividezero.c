#include <inc/lib.h>

void handler(struct UTrapframe *utf)
{
    cprintf("Divide zero!\n");
    exit();
}
void umain(int argc, char **argv)
{
    int zero = 0;
    set_exception_handler(T_DIVIDE, handler);
    cprintf("%d\n", 1/zero);
}
