// User-level page fault handler support.
// Rather than register the C page fault handler directly with the
// kernel as the page fault handler, we register the assembly language
// wrapper in pfentry.S, which in turns calls the registered C
// function.

#include <inc/lib.h>


// Assembly language pgfault entrypoint defined in lib/pfentry.S.
extern void _exception_upcall(void);

void
set_exception_handler(uint32_t trapno, void (*handler)(struct UTrapframe *utf))
{
	int r;

	if (thisenv->env_exception_upcall == 0) {
		// First time through!
		// LAB 4: Your code here.
        r = sys_page_alloc(0, (void *)(UXSTACKTOP - PGSIZE), 
                            PTE_W | PTE_U | PTE_P);
        if (r < 0)
            panic("set_exception_handler: %e\n", r);
        r = sys_env_set_exception_upcall(0, _exception_upcall);
        if (r < 0)
            panic("set_exception_handler: %e\n", r);
	}

	// Save handler pointer for assembly to call.
	r = sys_env_set_exception_handler(0, trapno, handler);
    if (r < 0)
        panic("set_exception_handler: %e\n", r);
}
