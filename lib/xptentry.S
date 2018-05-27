#include <inc/mmu.h>
#include <inc/memlayout.h>


.text
.globl _exception_upcall
_exception_upcall:
	// Call the C page fault handler.
    movl (%esp), %eax
    
	pushl %esp			// function argument: pointer to UTF
	call *%eax
	addl $8, %esp			// pop function argument
	
	// Now the C page fault handler has returned and you must return
	// to the trap time state.
	// Push trap-time %eip onto the trap-time stack.
	//
	// Explanation:
	//   We must prepare the trap-time stack for our eventual return to
	//   re-execute the instruction that faulted.
	//   Unfortunately, we can't return directly from the exception stack:
	//   We can't call 'jmp', since that requires that we load the address
	//   into a register, and all registers must have their trap-time
	//   values after the return.
	//   We can't call 'ret' from the exception stack either, since if we
	//   did, %esp would have the wrong value.
	//   So instead, we push the trap-time %eip onto the *trap-time* stack!
	//   Below we'll switch to that stack and call 'ret', which will
	//   restore %eip to its pre-fault value.
	//
	//   In the case of a recursive fault on the exception stack,
	//   note that the word we're pushing now will fit in the
	//   blank word that the kernel reserved for us.
	//
	// Throughout the remaining code, think carefully about what
	// registers are available for intermediate calculations.  You
	// may find that you have to rearrange your code in non-obvious
	// ways as registers become unavailable as scratch space.
	//
	// LAB 4: Your code here.
    movl 0x30(%esp), %eax
    subl $4, %eax
    movl 0x28(%esp), %ebx
    movl %ebx,(%eax)
    movl %eax, 0x30(%esp)

	// Restore the trap-time registers.  After you do this, you
	// can no longer modify any general-purpose registers.
	// LAB 4: Your code here.
    addl $8, %esp
    popal

	// Restore eflags from the stack.  After you do this, you can
	// no longer use arithmetic operations or anything else that
	// modifies eflags.
	// LAB 4: Your code here.
    addl $4, %esp
    popfl

	// Switch back to the adjusted trap-time stack.
	// LAB 4: Your code here.
    popl %esp

	// Return to re-execute the instruction that faulted.
	// LAB 4: Your code here.
    ret

