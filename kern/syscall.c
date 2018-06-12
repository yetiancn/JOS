/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

//#include <inc/types.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>

#include <inc/fs.h>
#include <inc/elf.h>

#define debug 0

 //void*   diskaddr(uint32_t blockno);
 //void region_alloc(struct Env *, void *, size_t);

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
    user_mem_assert(curenv, s, len, 0);
	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
    struct Env *newenv;
    int ret;
    ret = env_alloc(&newenv, curenv->env_id);
    if (ret < 0)
        return ret;
    newenv->env_status = ENV_NOT_RUNNABLE;
    newenv->env_tf = curenv->env_tf;
    newenv->env_tf.tf_regs.reg_eax = 0;

    return newenv->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
    struct Env *e;
    if (envid2env(envid, &e, 1) < 0) 
        return -E_BAD_ENV;
    if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
        return -E_INVAL;
    e->env_status = status;
    return 0; 
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3), interrupts enabled, and IOPL of 0.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
    struct Env *e;
    if (envid2env(envid, &e, 1) < 0)
        return -E_BAD_ENV;
    user_mem_assert(e, tf, sizeof(struct Trapframe), PTE_U);
    e->env_tf = *tf;
    e->env_tf.tf_cs = GD_UT | 3;
    e->env_tf.tf_eflags |= FL_IF;
    e->env_tf.tf_eflags &= ~FL_IOPL_MASK;
    return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.
    struct Env *e;
    if (envid2env(envid, &e, 1) < 0)
        return -E_BAD_ENV;
    e->env_pgfault_upcall = func;
    return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
    struct PageInfo *pp;
    struct Env *e;

    if (envid2env(envid, &e, 1) < 0)
        return -E_BAD_ENV;
    if ((uint32_t) va >= UTOP || va != ROUNDDOWN(va, PGSIZE))
        return -E_INVAL;
    if (perm & ~PTE_SYSCALL)
        return -E_INVAL;

    pp = page_alloc(1);
    if (pp == NULL)
        return -E_NO_MEM;
    if (page_insert(e->env_pgdir, pp, va, perm | PTE_U | PTE_P) < 0) {
        page_free(pp);
        return -E_NO_MEM;
    }
    return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva,
	     envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
    struct Env *srcenv, *dstenv;
    struct PageInfo *pp;
    pte_t *pte;

    if (envid2env(srcenvid, &srcenv, 1) < 0 || envid2env(dstenvid, &dstenv, 1) < 0)
        return -E_BAD_ENV;
    if ((uint32_t) srcva >= UTOP || srcva != ROUNDDOWN(srcva, PGSIZE))
        return -E_INVAL;
    if ((uint32_t) dstva >= UTOP || dstva != ROUNDDOWN(dstva, PGSIZE))
        return -E_INVAL;
    
    pp = page_lookup(srcenv->env_pgdir, srcva, &pte);
    if (pp == NULL)
        return -E_INVAL;
    if (perm & ~PTE_SYSCALL)
        return -E_INVAL;
    if ((perm & PTE_W) && !((*pte) & PTE_W))
        return -E_INVAL;
    
    if (page_insert(dstenv->env_pgdir, pp, dstva, perm | PTE_U | PTE_P) < 0)
        return -E_NO_MEM;
    
    return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
    struct Env *e;

    if (envid2env(envid, &e, 1) < 0)
        return -E_BAD_ENV;
    if ((uint32_t) va >= UTOP || va != ROUNDDOWN(va, PGSIZE))
        return -E_INVAL;
    page_remove(e->env_pgdir, va);
    return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
    int r;
    struct Env *e;
    pte_t *pte_p;
    struct PageInfo *pp;
    
    r = envid2env(envid, &e, 0);
    if (r < 0)
        return -E_BAD_ENV;

    if (e->env_ipc_recving == 0 || e->env_ipc_from != 0)
        return -E_IPC_NOT_RECV;

    if ((uint32_t)srcva < UTOP && e->env_ipc_dstva != 0) {
        if (srcva != ROUNDDOWN(srcva, PGSIZE))
            return -E_INVAL;
        if (perm & ~(PTE_SYSCALL | PTE_P | PTE_U))
            return -E_INVAL;
        pp = page_lookup(curenv->env_pgdir, srcva, &pte_p);
        if (pp == NULL) 
            return -E_INVAL;
        if (perm & PTE_W && !(*pte_p & PTE_W)) 
            return -E_INVAL;
        r = page_insert(e->env_pgdir, pp, e->env_ipc_dstva, perm);
        if (r < 0)
            return r;
        e->env_ipc_perm = perm;
    } 
    else 
        e->env_ipc_perm = 0;
    
    e->env_ipc_recving = 0;
    e->env_ipc_from = curenv->env_id;
    e->env_ipc_value = value;
    e->env_tf.tf_regs.reg_eax = 0;
    e->env_status = ENV_RUNNABLE;

    return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.
    if ((uint32_t)dstva < UTOP) {
        if (dstva != ROUNDDOWN(dstva, PGSIZE))
            return -E_INVAL;
        curenv->env_ipc_dstva = dstva;
    }
    else
        curenv->env_ipc_dstva = NULL;

    curenv->env_ipc_recving = 1;
    curenv->env_status = ENV_NOT_RUNNABLE;
    curenv->env_ipc_from = 0;
    curenv->env_ipc_perm = 0;

    sched_yield();
}

// lab5 challenge!
static void
region_alloc(struct Env *e, void *va, size_t len)
{
    int ret;
    struct PageInfo *pp;
    void *eva = (void *)ROUNDUP(va + len, PGSIZE);
    va = (void *)ROUNDDOWN(va, PGSIZE);
    for (; va < eva; va += PGSIZE) {
        pp = page_alloc(0);
        if (!pp)
            panic("region_alloc: page_alloc failed!\n");                       
        ret = page_insert(e->env_pgdir, pp, va, PTE_W | PTE_U);
        if (ret)
            panic("region_alloc: %e\n", ret);
    }
}

void*
diskaddr(uint32_t blockno)
{
    const uint32_t DISKMAP = 0x10000000;
    return (char*) (DISKMAP + blockno * BLKSIZE);
}


uint32_t
getblockno(struct File *f, uint32_t offset)
{
    int filebno = offset/BLKSIZE; // i-th block
    // cprintf("[getblockno] filebno: %u\n", filebno);
    if (filebno < NDIRECT)
        return (f->f_direct)[filebno];
    uint32_t *indirect = (uint32_t *)diskaddr(f->f_indirect);
    return indirect[filebno - NDIRECT];
}

void *
offset2addr(struct File *f, uint32_t offset)
{
    int blockno = getblockno(f, offset);
    // cprintf("[offset2addr] blockno: %u\n", blockno);
    void *bsva = (void *)diskaddr(blockno); // bsva: block starting va
    uint32_t bsoffset = ROUNDDOWN(offset, BLKSIZE); // bsoffset: block starting offset
    return bsva + offset - bsoffset; 
}

static int
init_stack(const char **argv, uintptr_t *init_esp)
{
    size_t string_size;
    int argc, i, r;
    char *string_store;
    uintptr_t *argv_store;
    
    page_remove(curenv->env_pgdir, (void *)(USTACKTOP - 2*PGSIZE));
    region_alloc(curenv, (void *)(USTACKTOP - 2*PGSIZE), PGSIZE);
    memset((void *)(USTACKTOP - 2*PGSIZE), 0, PGSIZE);

    string_size = 0;
    for (argc = 0; argv[argc] != 0; argc++)
        string_size += strlen(argv[argc]) + 1;

    string_store = (char *) USTACKTOP - PGSIZE - string_size;
    // cprintf("[init_stack] argc: %u\n", argc);
    for (i = 0; i < argc; i++) {
        // argv_store[i] = (uintptr_t)string_store;
        strcpy(string_store, argv[i]);
        string_store += strlen(argv[i]) + 1;
    }
    assert(string_store == (char *) USTACKTOP - PGSIZE);



    page_remove(curenv->env_pgdir, (void *)(USTACKTOP - PGSIZE));
    region_alloc(curenv, (void *)(USTACKTOP - PGSIZE), PGSIZE);
    memset((void *)(USTACKTOP - PGSIZE), 0, PGSIZE);

    string_store = (char *) USTACKTOP - PGSIZE - string_size;
    argv_store = (uintptr_t *) USTACKTOP - 4 * (argc + 1);
    for (i = 0; i < argc; i++) {
        argv_store[i] = (uintptr_t)string_store;
        // strcpy(string_store, argv[i]);
        string_store += strlen(string_store) + 1;
    }

    argv_store[argc] = 0;
    assert(string_store == (char *) USTACKTOP - PGSIZE);
    
    argv_store[-1] = (uintptr_t)argv_store;
    argv_store[-2] = (uintptr_t)argc;

    *init_esp = (uintptr_t)(argv_store - 2);
    return 0;
}



// argv[0] == filename
static int
sys_exec(uint32_t fileaddr, char **argv)
{
    struct File *f;
    int i, fs_env_index;
    char temp[BLKSIZE];
    struct Elf *elf;
    uint32_t va, ph_offset, eph_offset;
    uint32_t st_offset, ed_offset, nextblk_offset, finished;
    uint32_t phpva, phpfilesz, phpmemsz;
    uint32_t fileaddr_k;
    struct Proghdr *ph;

    fileaddr_k = fileaddr;

    int argc;
    for (argc = 0; argv[argc] != 0; argc++);
    
    uintptr_t init_esp;
    int r;
   
    if ((r = init_stack((const char **)argv, &init_esp)) < 0)
        panic("sys_exec: init_stack error\n");

    curenv->env_tf.tf_esp = init_esp;


    // clear mapping
    for (va = 0; va < UTOP; va += PGSIZE)
        if (va != USTACKTOP - 2 * PGSIZE && va != USTACKTOP - PGSIZE)
            page_remove(curenv->env_pgdir, (void *)va);

    for (i = 0; i < NENV; i++)
        if (envs[i].env_type == ENV_TYPE_FS)
            break;
    assert(i < NENV);

    fs_env_index = i;
    lcr3(PADDR(envs[fs_env_index].env_pgdir));
    
    f = (struct File *)fileaddr_k;
   
    // *elf* should point to the first address of the first block
    elf = (struct Elf *)diskaddr(getblockno(f, 0));
    if (elf->e_magic != ELF_MAGIC)
        panic("sys_exec: not a valid ELF!\n");

    curenv->env_tf.tf_eip = elf->e_entry;

    // ph_offset: offset of *ph* in ELF
    ph_offset = elf->e_phoff;
    eph_offset = elf->e_phoff + elf->e_phnum * sizeof(struct Proghdr);

    for (; ph_offset < eph_offset; ph_offset += sizeof(struct Proghdr)) {
        ph = (struct Proghdr *)offset2addr(f, ph_offset);

        if (ph->p_type != ELF_PROG_LOAD)
            continue;
        if (ph->p_filesz > ph->p_memsz)
            panic("sys_exec: p_filesz > p_memsz!\n");
        region_alloc(curenv, (void *)ph->p_va, ph->p_memsz);
        
        st_offset = ph->p_offset;
        ed_offset = ph->p_offset + ph->p_filesz;
        
        nextblk_offset = ROUNDUP(st_offset, BLKSIZE);
        finished = 0;
        for (; st_offset < ed_offset; ) {
            // move (nextblk_offset - st_offset) bytes 
            // from st
            // to   (ph->p_va + finished)
            memset((void *)temp, 0, sizeof(temp));
            memcpy((void *)temp, offset2addr(f, st_offset), 
                        nextblk_offset - st_offset);
            void *dst = (void *)ph->p_va + finished;
            lcr3(PADDR(curenv->env_pgdir));
            memmove(dst, temp, nextblk_offset - st_offset);
            lcr3(PADDR(envs[fs_env_index].env_pgdir));
           
            finished += nextblk_offset - st_offset;
            st_offset = nextblk_offset;
            nextblk_offset += BLKSIZE;
        }
        
        phpva = ph->p_va;
        phpfilesz = ph->p_filesz;
        phpmemsz = ph->p_memsz;
        lcr3(PADDR(curenv->env_pgdir));
        memset((void *)(phpva + phpfilesz), 0, phpmemsz - phpfilesz);
         
        lcr3(PADDR(envs[fs_env_index].env_pgdir));
    }

    lcr3(PADDR(curenv->env_pgdir));
    // region_alloc(curenv, (void *)(USTACKTOP - PGSIZE), PGSIZE);
    // *(uint32_t *)(USTACKTOP - 4) = (uint32_t)argc;
    // *(uint32_t *)(USTACKTOP - 8) = (uint32_t)argv;
    // curenv->env_tf.tf_esp = USTACKTOP - 8;
    
    

    return 0;
}


// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.

	// panic("syscall not implemented");
	switch (syscallno) {
    case SYS_cputs:
        sys_cputs((const char *)a1, a2);
        return 0; // I am not sure!
    case SYS_cgetc:
        return sys_cgetc();
    case SYS_getenvid:
        return sys_getenvid();
    case SYS_env_destroy:
        return sys_env_destroy(a1); 
    case SYS_page_alloc:
        return sys_page_alloc((envid_t)a1, (void *)a2, (int)a3);
    case SYS_page_map:
        return sys_page_map((envid_t)a1, (void *)a2, (envid_t)a3, (void *)a4, (int)a5);
    case SYS_page_unmap:
        return sys_page_unmap((envid_t)a1, (void *)a2);
    case SYS_exofork:
        return sys_exofork();
    case SYS_env_set_status:
        return sys_env_set_status((envid_t)a1, (int)a2);
    case SYS_env_set_pgfault_upcall:
        return sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);
    case SYS_env_set_trapframe:
        return sys_env_set_trapframe((envid_t)a1, (struct Trapframe *)a2);
    case SYS_yield:
        sys_yield();
        return 0;
    case SYS_ipc_try_send:
        return sys_ipc_try_send((envid_t)a1, a2, (void *)a3, (unsigned)a4);
    case SYS_ipc_recv:
        return sys_ipc_recv((void *)a1);
    case SYS_exec:
        return sys_exec((uint32_t)a1, (char **)a2);
    default:
		return -E_INVAL;
	}
}

