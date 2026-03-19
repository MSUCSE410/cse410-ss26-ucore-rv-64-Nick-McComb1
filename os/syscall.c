#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "proc.h"
#include "vm.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
    struct proc *p = curr_proc();

    TimeVal tv;
    uint64 cycle = get_cycle();
    tv.sec = cycle / CPU_FREQ;
    tv.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;

    if(copyout(p->pagetable, (uint64)val, (char*)&tv, sizeof(TimeVal)) < 0)
        return -1;

    return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)

uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd)
{
    struct proc *p = curr_proc();

    // 1. len == 0 → success
    if (len == 0)
        return 0;

    // 2. max size (1 GiB)
    if (len > (1ULL << 30))
        return -1;

    // 3. port validation
    if ((port & ~0x7) != 0)   // only lower 3 bits allowed
        return -1;

    if ((port & 0x7) == 0)    // must have at least one permission
        return -1;

    // 4. alignment (VERY IMPORTANT for your tests)
    if ((start % PGSIZE) != 0)
        return -1;

    if ((len % PGSIZE) != 0)
        return -1;

    uint64 va = start;
    uint64 end = start + len;

    // 5. check for already mapped pages
    for (uint64 a = va; a < end; a += PGSIZE) {
        if (walkaddr(p->pagetable, a) != 0) {
            return -1;
        }
    }

    // 6. convert port → PTE flags
    int perm = PTE_U;
    if (port & 0x1) perm |= PTE_R;
    if (port & 0x2) perm |= PTE_W;
    if (port & 0x4) perm |= PTE_X;

    // 7. allocate + map pages
    for (uint64 a = va; a < end; a += PGSIZE) {
        char *mem = kalloc();
        if (mem == 0) {
            // rollback previously allocated pages
            uvmunmap(p->pagetable, va, (a - va) / PGSIZE, 1);
            return -1;
        }

        memset(mem, 0, PGSIZE);

        if (mappages(p->pagetable, a, PGSIZE, (uint64)mem, perm) != 0) {
            kfree(mem);
            uvmunmap(p->pagetable, va, (a - va) / PGSIZE, 1);
            return -1;
        }
    }

    return 0;
}

uint64 sys_munmap(uint64 start, uint64 len)
{
    struct proc *p = curr_proc();

    if (len == 0)
        return 0;

    // ❗ IMPORTANT: enforce alignment
    if ((start % PGSIZE) != 0)
        return -1;

    if ((len % PGSIZE) != 0)
        return -1;

    uint64 va = start;
    uint64 npages = len / PGSIZE;

    // check all pages are mapped
    for (uint64 i = 0; i < npages; i++) {
        pte_t *pte = walk(p->pagetable, va + i * PGSIZE, 0);
        if (!pte || (*pte & PTE_V) == 0)
            return -1;
    }

    uvmunmap(p->pagetable, va, npages, 1);

    return 0;
}

/*
* LAB1: you may need to define sys_task_info here
*/
uint64 sys_task_info(TaskInfo *ti){
	struct proc *p = curr_proc();

    TaskInfo info = p->ti;

    uint64 now = (get_cycle() * 1000) / CPU_FREQ;
    info.time = now - info.start_time;

    if(copyout(p->pagetable, (uint64)ti, (char*)&info, sizeof(TaskInfo)) < 0)
        return -1;

    return 0;     
}

// uint64 sys_pid(int *pid){
// 	if (!pid) return -1;
// 	*pid = threadid();
// 	return 0;
// }

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	curr_proc()->ti.syscall_times[id]++;

	
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	// case SYS_getpid:
	// 	ret = sys_pid((int *)args[0]);
	// 	break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_taskinfo:
		ret = sys_task_info((TaskInfo *) args[0]);
		break;
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
