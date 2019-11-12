#pragma once
//usage: int fd = syscall<__NR_open>("foo", O_RDONLY);
//WARNING: uses the raw kernel interface!
//If the manpage splits an argument in high/low, you'd better follow suit.
//If the argument order changes between platforms, you must follow that.
//If the syscall is completely different from the wrapper (hi clone()), you must use syscall semantics.
//In particular, there is no errno in this environment. Instead, that's handled by returning -ENOENT.

#ifdef __x86_64__
#define CLOBBER "memory", "cc", "rcx", "r11" // https://en.wikibooks.org/wiki/X86_Assembly/Interfacing_with_Linux#syscall
#define REG_SYSNO "rax" // Linux nolibc https://github.com/torvalds/linux/blob/1c4e395cf7ded47f/tools/include/nolibc/nolibc.h#L403
#define REG_ARG1 "rdi" // claims r10, r8 and r9 are clobbered too, but wikibooks doesn't
#define REG_ARG2 "rsi" // and nolibc syscall6 doesn't clobber r9, but syscall5 and below do
#define REG_ARG3 "rdx" // testing reveals no clobbering of those registers either
#define REG_ARG4 "r10" // I'll assume it's nolibc being overcautious, despite being in the kernel tree
#define REG_ARG5 "r8"
#define REG_ARG6 "r9"
#define SYSCALL_INSTR "syscall"
#define REG_RET "rax"
#endif
#ifdef __i386__
#define CLOBBER "memory", "cc" // https://en.wikibooks.org/wiki/X86_Assembly/Interfacing_with_Linux#int_0x80
#define REG_SYSNO "eax"
#define REG_ARG1 "ebx"
#define REG_ARG2 "ecx"
#define REG_ARG3 "edx"
#define REG_ARG4 "esi"
#define REG_ARG5 "edi"
#define REG_ARG6 "ebp"
#define SYSCALL_INSTR "int 0x80"
#define REG_RET "eax"
#endif

//ideally, these functions could be inlined into the templates, but that fails due to
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=33661 (and duplicates: 36080 64951 66393 80264)
//it's said that making the regs volatile fixes it, but that would likely demand the 'correct' initialization order and yield worse code
static inline long syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
	//register assignment per http://stackoverflow.com/a/2538212
	register long sysno asm(REG_SYSNO) = n;
	register long a1r asm(REG_ARG1) = a1;
	register long a2r asm(REG_ARG2) = a2;
	register long a3r asm(REG_ARG3) = a3;
	register long a4r asm(REG_ARG4) = a4;
	register long a5r asm(REG_ARG5) = a5;
	register long a6r asm(REG_ARG6) = a6;
	register long ret asm(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r), "r"(a4r), "r"(a5r), "r"(a6r) : CLOBBER);
	return ret;
}

static inline long syscall5(long n, long a1, long a2, long a3, long a4, long a5)
{
	register long sysno asm(REG_SYSNO) = n;
	register long a1r asm(REG_ARG1) = a1;
	register long a2r asm(REG_ARG2) = a2;
	register long a3r asm(REG_ARG3) = a3;
	register long a4r asm(REG_ARG4) = a4;
	register long a5r asm(REG_ARG5) = a5;
	register long ret asm(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r), "r"(a4r), "r"(a5r) : CLOBBER);
	return ret;
}

static inline long syscall4(long n, long a1, long a2, long a3, long a4)
{
	register long sysno asm(REG_SYSNO) = n;
	register long a1r asm(REG_ARG1) = a1;
	register long a2r asm(REG_ARG2) = a2;
	register long a3r asm(REG_ARG3) = a3;
	register long a4r asm(REG_ARG4) = a4;
	register long ret asm(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r), "r"(a4r) : CLOBBER);
	return ret;
}

static inline long syscall3(long n, long a1, long a2, long a3)
{
	register long sysno asm(REG_SYSNO) = n;
	register long a1r asm(REG_ARG1) = a1;
	register long a2r asm(REG_ARG2) = a2;
	register long a3r asm(REG_ARG3) = a3;
	register long ret asm(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r) : CLOBBER);
	return ret;
}

static inline long syscall2(long n, long a1, long a2)
{
	register long sysno asm(REG_SYSNO) = n;
	register long a1r asm(REG_ARG1) = a1;
	register long a2r asm(REG_ARG2) = a2;
	register long ret asm(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r) : CLOBBER);
	return ret;
}

static inline long syscall1(long n, long a1)
{
	register long sysno asm(REG_SYSNO) = n;
	register long a1r asm(REG_ARG1) = a1;
	register long ret asm(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r) : CLOBBER);
	return ret;
}

static inline long syscall0(long n)
{
	register long sysno asm(REG_SYSNO) = n;
	register long ret asm(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno) : CLOBBER);
	return ret;
}


template<int nr, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
long syscall(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6)
{
	return syscall6(nr, (long)a1, (long)a2, (long)a3, (long)a4, (long)a5, (long)a6);
}

template<int nr, typename T1, typename T2, typename T3, typename T4, typename T5>
long syscall(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5)
{
	return syscall5(nr, (long)a1, (long)a2, (long)a3, (long)a4, (long)a5);
}

template<int nr, typename T1, typename T2, typename T3, typename T4>
long syscall(T1 a1, T2 a2, T3 a3, T4 a4)
{
	return syscall4(nr, (long)a1, (long)a2, (long)a3, (long)a4);
}

template<int nr, typename T1, typename T2, typename T3>
long syscall(T1 a1, T2 a2, T3 a3)
{
	return syscall3(nr, (long)a1, (long)a2, (long)a3);
}

template<int nr, typename T1, typename T2>
long syscall(T1 a1, T2 a2)
{
	return syscall2(nr, (long)a1, (long)a2);
}

template<int nr, typename T1>
long syscall(T1 a1)
{
	return syscall1(nr, (long)a1);
}

template<int nr>
long syscall()
{
	return syscall0(nr);
}
