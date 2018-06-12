// hello, world
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	cprintf("hello, world\n");
    cprintf("argc: %d\n", argc);
    cprintf("argv[0]: %s\n", argv[0]);
    cprintf("argv[1]: %s\n", argv[1]);
	cprintf("i am environment %08x\n", thisenv->env_id);
}
