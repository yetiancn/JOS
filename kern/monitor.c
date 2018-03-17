// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>

#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
    // 20180113
    { "backtrace", "Display backtrace stack from the current procedure", mon_backtrace },
    { "showmappings", "Display physical page mappings", mon_showmappings },
    { "setperm", "Set permission bit of pte", mon_setperm },
    { "clearperm", "Clear permission bit of pte", mon_setperm },
    { "changeperm", "Change permission bit of pte", mon_setperm },
    { "dump", "Dump virtual/physical memory", mon_dump },
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
    uint32_t ebp, eip, arg;
    int i;
    struct Eipdebuginfo info;

    cprintf("Stack backtrace:\n");

    ebp = read_ebp();
    while (ebp != 0) {
        cprintf("  ebp %x", ebp);
        eip = (uint32_t) (*((int *)ebp + 1));
        cprintf("  eip %x", eip);
        cprintf("  args");
        for (i = 0; i < 5; i++) {
            arg = (uint32_t) (*((int *)ebp + i + 2));
            cprintf(" %08x", arg);
        }
        cprintf("\n");

        debuginfo_eip((uintptr_t) eip, &info);
        cprintf("         %s:%d: ", info.eip_file, info.eip_line);
        cprintf("%.*s", info.eip_fn_namelen, info.eip_fn_name);
        cprintf("+%d\n", eip - (uint32_t) info.eip_fn_addr);

        ebp = (uint32_t) (*((uint32_t *)ebp));
    }

    return 0;
}

int showmappings_islegal(const char *s) {
    int i;
    if (strlen(s) <= 2 || strlen(s) >= 11)
        return -1;
    if (s[0] != '0' || s[1] != 'x')
        return -1;
    for (i = 2; i < strlen(s); i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')
               || (s[i] >= 'A' && s[i] <= 'F')))
               return -1;
    return 0;
}

void show_perm(pte_t *p) {
    pte_t pte = *p;

/*
    +---+---+---+---+---+---+---+---+---+
    | G | PS| D | A |PCD|PWT| U | W | P |
    +---+---+---+---+---+---+---+---+---+
    | %u | %u | %u | %u | %u | %u | %u | %u |
    +---+---+---+---+---+---+---+---+---+
*/
    cprintf("+---+---+---+---+---+---+---+---+---+\n");
    cprintf("| G | PS| D | A |PCD|PWT| U | W | P |\n");
    cprintf("+---+---+---+---+---+---+---+---+---+\n");
    cprintf("| %u | %u | %u | %u | %u | %u | %u | %u | %u |\n",    
        !!(pte & PTE_G), !!(pte & PTE_PS), !!(pte & PTE_D),
        !!(pte & PTE_A), !!(pte & PTE_PCD), !!(pte & PTE_PWT),
        !!(pte & PTE_U), !!(pte & PTE_W), !!(pte & PTE_P));
    cprintf("+---+---+---+---+---+---+---+---+---+\n");

/*
    cprintf("  PTE_P\t%u\n", !!(pte & PTE_P));
    cprintf("  PTE_W\t%u\n", !!(pte & PTE_W));
    cprintf("  PTE_U\t%u\n", !!(pte & PTE_U));
    cprintf("  PTE_PWT\t%u\n", !!(pte & PTE_PWT));
    cprintf("  PTE_PCD\t%u\n", !!(pte & PTE_PCD));
    cprintf("  PTE_A\t%u\n", !!(pte & PTE_A));
    cprintf("  PTE_D\t%u\n", !!(pte & PTE_D));
    cprintf("  PTE_PS\t%u\n", !!(pte & PTE_PS));
    cprintf("  PTE_G\t%u\n", !!(pte & PTE_G));
*/

}

// challenge!
int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
    uint32_t va_start, va_end, va;
    if (argc != 3) {
        cprintf("usage: showmappings [start] [end]\n");
        return 0;
    }
    
    if (showmappings_islegal(argv[1]) || showmappings_islegal(argv[2])) {
        cprintf("illegal address\n");
        return 0;
    }
    va_start = ROUNDDOWN(strtol(argv[1], 0, 16), PGSIZE);
    va_end = ROUNDUP(strtol(argv[2], 0, 16), PGSIZE);
    
    for (va = va_start; va <= va_end; va += PGSIZE) {
        uint32_t va_pde;
        pte_t *pde_p, *pte_p;

        cprintf("Virtual Address: %x\n", va);
        va_pde = (uint32_t) PGADDR(PDX(UVPT), PDX(va), PTX(va) << 2);
        pde_p = pgdir_walk(kern_pgdir, (const void *)va_pde, 0);
        if (pde_p == NULL || !((*pde_p) & PTE_P)) {
            cprintf("pde: none\t\n");
            continue;
        }
        else {
            cprintf("pde: %08x\t\n", *pde_p);
            show_perm(pde_p);
        }
        pte_p = pgdir_walk(kern_pgdir, (const void *)va, 0);
        if (pte_p == NULL || !((*pte_p) & PTE_P))
            cprintf("pte: none\n");
        else {
            cprintf("pte: %08x\n", *pte_p);
            show_perm(pte_p);
        }
        cprintf("\n");
    }
    return 0;
}

int getperm(const char* perm_s) {
    const char *perm_str[9] = {"PTE_P", "PTE_W", "PTE_U", "PTE_PWT",
                                  "PTE_PCD", "PTE_A", "PTE_D", "PTE_PS", 
                                  "PTE_G"};
    int i;
    for (i = 0; i < 9; i++)
        if (strcmp(perm_s, perm_str[i]) == 0)
            return i;
    return -1;
}

int
mon_setperm(int argc, char **argv, struct Trapframe *tf)
{
    int perm_shift;
    int mode;
    uint32_t va;
    pte_t* pte_p;
    if (argc != 3) {
        cprintf("usage: setperm/clrperm va PTE_X\n");
        return 0;
    }

    if (!strcmp(argv[0], "setperm"))
        mode = 1; // set
    else if (!strcmp(argv[0], "clearperm"))
        mode = 0; // clear
    else 
        mode = 2; // change

    perm_shift = getperm(argv[2]);
    if (perm_shift == -1) {
        cprintf("Illegal permission\n");
        return 0;
    }
    va = strtol(argv[1], 0, 16);
    pte_p = pgdir_walk(kern_pgdir, (const void *)va, 0);
    if (pte_p == NULL) {
        cprintf("Pte not exists\n");
        return 0;
    }
    // cprintf("Previous %s: %u\t", argv[2], !!((*pte_p) & (1 << perm_shift))); 
    cprintf("Previous permissions: \n");
    show_perm(pte_p);
    if (mode == 1)
        *pte_p |= (1 << perm_shift);
    else if (mode == 0)
        *pte_p &= (~(1 << perm_shift));
    else
        *pte_p = (*pte_p 
                    & (~(1 << perm_shift))) 
                    | ((!((*pte_p) & (1 << perm_shift))) << perm_shift);

    //cprintf("Current %s: %u\n", argv[2], !!((*pte_p) & (1 << perm_shift)));
    cprintf("Current permissions: \n");
    show_perm(pte_p);
    return 0;
}

void dump_phy(uint32_t start, uint32_t end) {
    uint32_t va;
    int cnt;
    if (start > end)
        return;
    cnt = 0;
    for (va = start; va <= end; va++) {
        if (cnt == 8) {
            cprintf("\n");
            cprintf("%08x:\t", va);
            cnt = 0;
        }
        if (va == start)
            cprintf("%08x:\t", start);
        cprintf("%02x ", *(uint8_t *)KADDR(va));
        cnt++;
    }
    cprintf("\n");
}

void dump_vir(uint32_t start, uint32_t end) {
    uint32_t va;
    int cnt;
    if (start > end)
        return;
    cnt = 0;
    for (va = start; va <= end; va++) {
        pte_t *pte_p;
        if (cnt == 8) {
            cprintf("\n");
            cprintf("%08x:\t", va);
            cnt = 0;
        }
        pte_p = pgdir_walk(kern_pgdir, (const void *)va, 0);
        if (pte_p == NULL || !((*pte_p) & PTE_P)) {
            cprintf("cannot access the address %08x\n", va);
            va += PGSIZE - 1;
            continue;
        }
        if (va == start)
            cprintf("%08x:\t", start);
        cprintf("%02x ", *(uint8_t *)va);
        cnt++;
    }
    cprintf("\n"); 
}

int
mon_dump(int argc, char **argv, struct Trapframe *tf)
{
    uint32_t start, end;
    if (argc != 4 || (argv[1][0] != 'p' && argv[1][0] != 'v')) {
        cprintf("usage: dump v/p start end\n");
        return 0;
    }
    start = strtol(argv[2], 0, 16);
    end = strtol(argv[3], 0, 16);
    if (argv[1][0] == 'p')
        dump_phy(start, end);
    else
        dump_vir(start, end);
    return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

    
	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
