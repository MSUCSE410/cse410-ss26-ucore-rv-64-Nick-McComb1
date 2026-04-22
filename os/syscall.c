#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
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

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 va)
{
	// TODO: your job is to complete the sys call
	return -1;
}

uint64 sys_set_priority(long long prio)
{
	// TODO: your job is to complete the sys call
	return -1;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}
/*
 * sys_fstat - Syscall ID 80
 * Gets the status/metadata of an open file and writes it into a
 * user-space Stat struct.
 *
 * Parameters:
 *   fd  - file descriptor index into the current process's file table
 *   stat - user-space virtual address where the Stat struct will be written
 *
 * Returns 0 on success, -1 on error.
 */
int sys_fstat(int fd, uint64 stat) {
	/* 
     * Validate the file descriptor range.
     * fd must be between 0 and FD_BUFFER_SIZE-1 (e.g. 0-15).
     * Negative fds are obviously invalid. fds >= FD_BUFFER_SIZE would
     * be an out-of-bounds access on the files[] array.
     */
    if (fd < 0 || fd >= FD_BUFFER_SIZE)
        return -1;

    struct proc *p = curr_proc();
	/*
     * Look up the file struct for this fd in the process's file table.
     */
    struct file *f = p->files[fd];
    if (f == NULL) {
        errorf("invalid fd %d\n", fd);
        return -1;
    }
	/*
     * Only FD_INODE files (regular files on disk) have inode metadata.
     * if not they have no meaningful stat info, so we reject them here.
     */
    if (f->type != FD_INODE)
        return -1;
	 /*
     * Delegate to inodestat()
	 */
    return inodestat(f->ip, p->pagetable, stat);
}
/*
 * sys_linkat - Syscall ID 37
 * Creates a hard link: makes a new directory entry (newpath) that points
 * to the same inode as an existing file (oldpath). After this, both names
 * refer to the same file data and the inode's nlink count increases by 1.
 *
 * Parameters:
 *   olddirfd - ignored (always AT_FDCWD = -100 in this implementation)
 *   oldpath  - user-space virtual address of the source filename string
 *   newdirfd - ignored (always AT_FDCWD = -100 in this implementation)
 *   newpath  - user-space virtual address of the new link filename string
 *   flags    - ignored (always 0 in this implementation)
 *
 * Returns 0 on success, -1 on error (e.g. same name, source not found).
 */
int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath, uint64 flags) {
    struct proc *p = curr_proc();
    char old[200], new[200];
	/*
	 * Copy the old and new path strings from user space into kernel buffers.
     * This is necessary because user pointers cannot be dereferenced
     * directly in the kernel — the kernel and user have separate
     * virtual address spaces.
     */
    copyinstr(p->pagetable, old, oldpath, 200);
    copyinstr(p->pagetable, new, newpath, 200);
    // Error: linking to the same name
    if (strncmp(old, new, 200) == 0)
        return -1;
	/*
     * Delegate to filelink(old, new) which:
	 */
    return filelink(old, new);
}

/*
 * sys_unlinkat - Syscall ID 35
 * Removes a directory entry (filename) and decrements the inode's nlink count.
 *
 * Parameters:
 *   dirfd - ignored (always AT_FDCWD = -100 in this implementation)
 *   name  - user-space virtual address of the filename string to remove
 *   flags - ignored (always 0 in this implementation)
 *
 * Returns 0 on success, -1 on error (e.g. file not found, permission denied).
 */
int sys_unlinkat(int dirfd, uint64 name, uint64 flags) {
    struct proc *p = curr_proc();
    char path[200];
	//copy to kernel buffer
    copyinstr(p->pagetable, path, name, 200);
    return fileunlink(path);
}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_fstat:
	    ret = sys_fstat(args[0],args[1]);
		break;
	case SYS_linkat:
	    ret = sys_linkat(args[0],args[1],args[2],args[3],args[4]);
		break;
	case SYS_unlinkat:
	    ret = sys_unlinkat(args[0],args[1],args[2]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
