#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	int fd, n, r;
	char buf[512+1];

	binaryname = "icode";

	cprintf("icode: exec /init\n");
	if ((r = execl("/echo", "init", "initarg1", "initarg2", (char*)0)) < 0)
		panic("icode: exec /init: %e", r);

	cprintf("icode: exiting\n");
}
