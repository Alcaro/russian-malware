#ifdef SANDBOX_INTERNAL

//These 200 lines of code implement one tiny thing: Call one function (sysemu.cpp), then pass control to ld-linux.so.

//The sandbox environment is quite strict. It can't run normal programs unchanged, they'd call open() which fails.
//LD_PRELOAD doesn't help either, the loader open()s all libraries before initializing that (and init order isn't guaranteed).
//Its lesser known cousin LD_AUDIT works better, but the library itself is open()ed.
//This can be worked around with ptrace to make that specific syscall return a pre-opened fd, but that's way too much effort.
//Instead, we will be the loader. Of course we don't want to actually load the program, that's even harder than ptrace,
// so we'll "just" load the real loader, after setting up a SIGSYS handler in sysemu.cpp to let open() act normally.
//We'll skip most error checking. Everything will succeed on a sane system, and if it doesn't, there's no way to recover.

//#define _GNU_SOURCE // default (mandatory) in c++
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <elf.h>

#include "syscall.h"

#define KNOWN_INIT(x) = x

//__asm__(".intel_syntax");

#ifdef __clang__
// Clang really insists on having a memset
// have to do weird stuff to ensure it doesn't try to pick memset from glibc, or get confused about which to use
__asm__(R"(
.hidden memset
.globl memset
memset:
  jmp my_memset
)");
#endif

//have to redefine the entire libc, fun
//gcc recognizes various function names and reads attributes (such as extern) from the headers, force it not to
#define memset my_memset
#define memcpy my_memcpy
#define memcmp my_memcmp
extern "C" { // extern "C" so I won't need to jmp to a mangled name
void memset(void* ptr, int value, size_t num);
void memset(void* ptr, int value, size_t num)
{
	//compiler probably optimizes this
	uint8_t* ptr_ = (uint8_t*)ptr;
	for (size_t i=0;i<num;i++) ptr_[i] = value;
}
void memcpy(void * dest, const void * src, size_t n);
void memcpy(void * dest, const void * src, size_t n)
{
	uint8_t* dest_ = (uint8_t*)dest;
	uint8_t* src_ = (uint8_t*)src;
	for (size_t i=0;i<n;i++) dest_[i] = src_[i];
}
int memcmp(const void * ptr1, const void * ptr2, size_t n);
int memcmp(const void * ptr1, const void * ptr2, size_t n)
{
	uint8_t* ptr1_ = (uint8_t*)ptr1;
	uint8_t* ptr2_ = (uint8_t*)ptr2;
	for (size_t i=0;i<n;i++)
	{
		if (ptr1_[i] != ptr2_[i]) return ptr1_[i]-ptr2_[i];
	}
	return 0;
}
}


//for functions other than memset/cpy/cmp, slapping it in a namespace is enough to get rid of gcc's attributes
namespace mysand { namespace {

static inline int open(const char * pathname, int flags, mode_t mode = 0)
{
	return syscall3(__NR_open, (long)pathname, flags, mode);
}

static inline ssize_t read(int fd, void * buf, size_t count)
{
	return syscall3(__NR_read, fd, (long)buf, count);
}

static inline int close(int fd)
{
	return syscall1(__NR_close, fd);
}

static inline void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	return (void*)syscall6(__NR_mmap, (long)addr, length, prot, flags, fd, offset);
}

static inline int munmap(void* addr, size_t length)
{
	return syscall2(__NR_munmap, (long)addr, length);
}

// Clang's __builtin_align_up would make more sense, but gcc doesn't have that
#define ALIGN_MULT 4096
#define ALIGN_MASK (ALIGN_MULT-1)
#define ALIGN_OFF(ptr) ((uintptr_t)(ptr) & ALIGN_MASK)
#define ALIGN_DOWN(ptr) ((ptr) - ALIGN_OFF(ptr))
#define ALIGN_UP(ptr) (ALIGN_DOWN(ptr) + (ALIGN_OFF(ptr) ? ALIGN_MULT : 0))

static inline Elf64_Ehdr* map_binary(int fd, uint8_t*& base, uint8_t* hbuf, size_t buflen)
{
	//uselib() would be the easy way out, but it doesn't tell where it's mapped, and it may be compiled out of the kernel
	//no clue how (or if) it ever worked
	read(fd, hbuf, buflen);
	
	Elf64_Ehdr* ehdr = (Elf64_Ehdr*)hbuf;
	static const unsigned char exp_hdr[16] = { '\x7F', 'E', 'L', 'F', ELFCLASS64, ELFDATA2LSB, EV_CURRENT, ELFOSABI_SYSV, 0 };
	if (memcmp(ehdr->e_ident, exp_hdr, 16) != 0) return NULL;
	if (ehdr->e_type != ET_DYN) return NULL;
	
	Elf64_Phdr* phdr = (Elf64_Phdr*)(hbuf + ehdr->e_phoff);
	
	//find how big it needs to be
	size_t memsize = 0;
	for (int i=0;i<ehdr->e_phnum;i++)
	{
		if (phdr[i].p_type != PT_LOAD) continue;
		
		size_t thisend = ALIGN_UP(phdr[i].p_vaddr + phdr[i].p_memsz);
		if (thisend > memsize) memsize = thisend;
	}
	
	//find somewhere it fits (this unfortunately under-aligns it - could fix it if needed, but it seems to work in practice)
	base = (uint8_t*)mmap(NULL, memsize, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
	if (base == MAP_FAILED) return NULL;
	munmap(base, memsize); // in a multithreaded process, this would be a race condition, but nothing else has spawned threads yet
	                       // we can map on top of the target, but that leaves pointless PROT_NONE holes
	                       // they can be removed manually, but this is easier
	
	for (int i=0;i<ehdr->e_phnum;i++)
	{
		//kernel ignores other segment types, so let's follow suit
		if (phdr[i].p_type != PT_LOAD) continue;
		
		int prot = 0;
		if (phdr[i].p_flags & PF_R) prot |= PROT_READ;
		if (phdr[i].p_flags & PF_W) prot |= PROT_WRITE;
		if (phdr[i].p_flags & PF_X) prot |= PROT_EXEC;
		
		uint8_t* map_start = ALIGN_DOWN(base + phdr[i].p_vaddr);
		uint8_t* map_end = ALIGN_UP(base + phdr[i].p_vaddr + phdr[i].p_memsz);
		
		if (mmap(map_start, map_end-map_start, prot, MAP_PRIVATE|MAP_FIXED, fd, ALIGN_DOWN(phdr[i].p_offset)) != map_start)
		{
			return NULL;
		}
		if (phdr[i].p_memsz != phdr[i].p_filesz)
		{
			//no clue why this ALIGN_UP is needed, but it segfaults without it. is p_memsz wrong and nobody noticed?
			uint8_t* clear_start = base + phdr[i].p_vaddr + phdr[i].p_filesz;
			uint8_t* clear_end = ALIGN_UP(base + phdr[i].p_vaddr + phdr[i].p_memsz);
			memset(clear_start, 0, clear_end-clear_start);
		}
	}
	
	return ehdr;
}

//ld-linux can be the main program, in which case it opens the main binary as a normal library and executes that.
//It checks this by checking if the main program's entry point is its own.
//So how does the loader find this entry point? As we all know, main() has three arguments: argc,
// argv, envp. But the loader also gets a fourth argument, auxv, containing some entropy (for
// stack cookies), user ID, page size, ELF header size, the main program's entry point, and some
// other stuff.
//Since we're the main program, we're the entry point, both in auxv and the actual entry, and
// ld-linux won't recognize us. But we know where ld-linux starts, so let's put it in auxv. It's on
// the stack, it's writable. (ld-linux replaces a few auxv entries too, it's only fair.)
typedef void(*funcptr)();
static inline void update_auxv(Elf64_auxv_t* auxv, uint8_t* ld_base, Elf64_Ehdr* ehdr)
{
	for (int i=0;auxv[i].a_type != AT_NULL;i++)
	{
		//I don't think ld-linux uses PHDR or PHNUM, but why not
		if (auxv[i].a_type == AT_PHDR)
		{
			auxv[i].a_un.a_val = (uintptr_t)ld_base + ehdr->e_phoff;
		}
		if (auxv[i].a_type == AT_PHNUM)
		{
			auxv[i].a_un.a_val = ehdr->e_phnum;
		}
		//AT_BASE looks like it should be relevant, but is actually NULL
		if (auxv[i].a_type == AT_ENTRY)
		{
			//a_un is a union, but it only has one member. Apparently the rest were removed to allow
			// a 64bit program to use the 32bit structs. Even though auxv isn't accessible on wrong
			// bitness. I guess someone removed all pointers, sensible or not.
			//Backwards compatibility, fun for everyone...
			auxv[i].a_un.a_val = (uintptr_t)(funcptr)(ld_base + ehdr->e_entry);
		}
	}
}


extern "C" void preload_action(char** argv, char** envp);
extern "C" void preload_error(const char * why); // doesn't return
extern "C" funcptr bootstrap_start(void** stack)
{
	int* argc = (int*)stack;
	char* * argv = (char**)(stack+1);
	char* * envp = argv+*argc+1;
	void* * tmp = (void**)envp;
	while (*tmp) tmp++;
	Elf64_auxv_t* auxv = (Elf64_auxv_t*)(tmp+1);
	
	preload_action(argv, envp);
	
	// this could call the syscall emulator directly, but if I don't, the preloader can run unsandboxed as well
	// it means a SIGSYS penalty, but there's dozens of those already, another one makes no difference
	int fd = open("/lib64/ld-linux-x86-64.so.2", O_RDONLY);
	if (fd < 0) preload_error("couldn't open dynamic linker");
	uint8_t hbuf[832]; // FILEBUF_SIZE from glibc elf/dl-load.c
	uint8_t* ld_base KNOWN_INIT(nullptr); // ld-linux has 7 segments, 832 bytes fits 13 (plus ELF header)
	Elf64_Ehdr* ld_ehdr = map_binary(fd, ld_base, hbuf, sizeof(hbuf));
	close(fd);
	if (!ld_ehdr) preload_error("couldn't initialize dynamic linker");
	
	update_auxv(auxv, ld_base, ld_ehdr);
	
	return (funcptr)(ld_base + ld_ehdr->e_entry);
}

}}

//we need the stack pointer
//I could take the address of an argument, but I'm pretty sure that's undefined behavior
//and our callee also needs the stack, and I don't trust gcc to enforce a tail call
//therefore, the bootloader's first step must be written in asssembly
__asm__(R"(
# asm works great with raw strings, surprised I haven't seen it elsewhere
# I guess c++ and asm is a rare combination
.globl _start
_start:
mov %rdi, %rsp
push %rdx             # preserve %rdx because https://www.uclibc.org/docs/psABI-x86_64.pdf page 30 says so
call bootstrap_start  # Linux doesn't use that feature (https://elixir.bootlin.com/linux/v5.9/source/arch/x86/include/asm/elf.h#L170),
pop %rdx              # but userspace may react weirdly if %rdx starts out nonzero
jmp %rax              # zeroing it by preserving via stack (rather than xor rdx,rdx) also ensures %rsp is aligned in bootstrap_start
)");


#else
#define STR_(x) #x
#define STR(x) STR_(x)

extern const char sandbox_preload_bin[];
extern const unsigned sandbox_preload_len;
__asm__(R"(
.pushsection .rodata
.globl sandbox_preload_bin
sandbox_preload_bin:
.incbin "obj/sand-preload-)" STR(ARLIB_OBJNAME) R"(.elf"
.equ my_len, .-sandbox_preload_bin
.align 4
.globl sandbox_preload_len
sandbox_preload_len:
.int my_len
.popsection
)");
#endif
