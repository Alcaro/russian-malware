#if defined(_WIN32) && defined(ARLIB_HYBRID_DLL)
// we can't use OS facilities in this file, be careful with what functions are called

#include <windows.h>
#include <winternl.h>
#include <stdint.h>

#include "simd.h"
#include "thread/atomic.h"

#ifdef __GNUC__
#define LIKELY(expr)    __builtin_expect(!!(expr), true)
#define UNLIKELY(expr)  __builtin_expect(!!(expr), false)
#else
#define LIKELY(expr)    (expr)
#define UNLIKELY(expr)  (expr)
#endif

template<typename T> T launder(T v)
{
	__asm__("" : "+r"(v));
	return v;
}

typedef void(*funcptr)();

extern const IMAGE_DOS_HEADER __ImageBase;

// Global variables can be accessed before relocations are processed, by using RELOCATE(&g_integer).
// Requires a local variable HMODULE this_mod, which can be calculated using get_this_hmodule().
// Call this macro once per global and reuse the result, it optimizes poorly.
#ifdef __i386__
#define RELOCATE(x) launder((decltype(x+0))((uint8_t*)x - (uint8_t*)&__ImageBase + (uint8_t*)this_mod))
#else
#define RELOCATE(x) x
#endif

namespace {

bool streq(const char * a, const char * b) // no strcmp, it's an OS facility
{
	while (true)
	{
		if (*a != *b) return false;
		if (!*a) return true;
		a++;
		b++;
	}
}

#define strlen my_strlen
size_t strlen(const char * a)
{
	const char * b = a;
	while (*b) b++;
	return b-a;
}


HMODULE pe_get_ntdll()
{
	PEB* peb;
#if defined(__i386__)
	__asm__("mov {%%fs:(0x30),%0|%0,fs:[0x30]}" : "=r"(peb)); // *(PEB* __seg_fs*)0x30 would be a lot cleaner, but gcc rejects that in C++
#elif defined(__x86_64__)
	__asm__("mov {%%gs:(0x60),%0|%0,gs:[0x60]}" : "=r"(peb));
#elif defined(_M_IX86)
	peb = (PEB*)__readfsdword(0x30);
#elif defined(_M_AMD64)
	peb = (PEB*)__readgsqword(0x60);
#else
	#error "don't know what platform this is"
#endif
	
	// windows maintains a list of all DLLs in the process, available via PEB (available via TEB)
	// in three different orders - load order, memory order, and init order
	// ntdll is always #2 in load order, with the exe being #1 (http://www.nynaeve.net/?p=185)
	// (kernel32 is probably also always present, but I don't think that's guaranteed, and I don't need kernel32 anyways)
	// only parts of the relevant structs are documented, so this is based on
	// https://github.com/wine-mirror/wine/blob/master/include/winternl.h
	PEB_LDR_DATA* pld = peb->Ldr;
	LDR_DATA_TABLE_ENTRY* ldte_exe = (LDR_DATA_TABLE_ENTRY*)pld->Reserved2[1];
	LDR_DATA_TABLE_ENTRY* ldte_ntdll = (LDR_DATA_TABLE_ENTRY*)ldte_exe->Reserved1[0];
	return (HMODULE)ldte_ntdll->DllBase;
}

IMAGE_NT_HEADERS* pe_get_nt_headers(HMODULE mod)
{
	uint8_t* base_addr = (uint8_t*)mod;
	IMAGE_DOS_HEADER* head_dos = (IMAGE_DOS_HEADER*)base_addr;
	return (IMAGE_NT_HEADERS*)(base_addr + head_dos->e_lfanew);
}
IMAGE_DATA_DIRECTORY* pe_get_section_header(HMODULE mod, int sec)
{
	return &pe_get_nt_headers(mod)->OptionalHeader.DataDirectory[sec];
}
void* pe_get_section_body(HMODULE mod, int sec)
{
	return (uint8_t*)mod + pe_get_section_header(mod, sec)->VirtualAddress;
}

void* pe_get_proc_address(HMODULE mod, const char * name)
{
	if (!mod) return NULL;
	
	uint8_t* base_addr = (uint8_t*)mod;
	IMAGE_EXPORT_DIRECTORY* exports = (IMAGE_EXPORT_DIRECTORY*)pe_get_section_body(mod, IMAGE_DIRECTORY_ENTRY_EXPORT);
	
	DWORD * addr_off = (DWORD*)(base_addr + exports->AddressOfFunctions);
	DWORD * name_off = (DWORD*)(base_addr + exports->AddressOfNames);
	WORD * ordinal = (WORD*)(base_addr + exports->AddressOfNameOrdinals);
	
	// TODO: enable this (if I find one, mingw prefers names)
	//if ((uintptr_t)name < 0x10000) // ordinal
	//{
	//	size_t idx = (uintptr_t)name - exports->Base;
	//	if (idx > exports->NumberOfFunctions)
	//		return NULL;
	//	return base_addr + addr_off[idx];
	//}
	
	// TODO: forwarder RVAs (if I find one, mingw resolves them when creating the exe/dll)
	
	for (size_t i=0;i<exports->NumberOfNames;i++)
	{
		const char * exp_name = (const char*)(base_addr + name_off[i]);
		if (streq(name, exp_name))
			return base_addr + addr_off[ordinal[i]];
	}
	return NULL;
}



struct ntdll_t {
NTSTATUS WINAPI (*LdrLoadDll)(const WCHAR * DirPath, uint32_t Flags, const UNICODE_STRING * ModuleFileName, HMODULE* ModuleHandle);
NTSTATUS WINAPI (*NtProtectVirtualMemory)(HANDLE process, void** addr_ptr, size_t* size_ptr, uint32_t new_prot, uint32_t* old_prot);
IMAGE_BASE_RELOCATION* WINAPI (*LdrProcessRelocationBlock)(void* page, unsigned count, uint16_t* relocs, intptr_t delta);
};
static const char ntdll_t_names[] =
	"LdrLoadDll\0"
	"NtProtectVirtualMemory\0"
	"LdrProcessRelocationBlock\0"
	;

void pe_get_ntdll_syms(ntdll_t* out, HMODULE this_mod)
{
	void* * fp = (void**)out;
	const char * names = RELOCATE(ntdll_t_names);
	
	HMODULE mod = pe_get_ntdll();
	while (*names)
	{
		*fp = pe_get_proc_address(mod, names);
		fp++;
		names += strlen(names)+1;
	}
}

void pe_process_imports(ntdll_t* ntdll, HMODULE mod)
{
	uint8_t* base_addr = (uint8_t*)mod;
	
	IMAGE_IMPORT_DESCRIPTOR* imports = (IMAGE_IMPORT_DESCRIPTOR*)pe_get_section_body(mod, IMAGE_DIRECTORY_ENTRY_IMPORT);
	while (imports->Name)
	{
		const char * libname = (char*)(base_addr + imports->Name);
		WCHAR libname16[64]; // I hope nothing uses non-ascii in dll names... ...how would it even be represented?
		WCHAR* libname16iter = libname16;
		while (*libname) *libname16iter++ = *libname++;
		*libname16iter = '\0';
		
		UNICODE_STRING libname_us = { (uint16_t)((libname16iter-libname16)*sizeof(WCHAR)), sizeof(libname16), libname16 };
		
		HMODULE mod;
		if (FAILED(ntdll->LdrLoadDll(NULL, 0, &libname_us, &mod))) mod = NULL;
		
		void* * out = (void**)(base_addr + imports->FirstThunk);
		uintptr_t* thunks = (uintptr_t*)(base_addr + (imports->OriginalFirstThunk ? imports->OriginalFirstThunk : imports->FirstThunk));
		
		while (*thunks)
		{
			IMAGE_IMPORT_BY_NAME* imp = (IMAGE_IMPORT_BY_NAME*)(base_addr + *thunks);
			*out = pe_get_proc_address(mod, (char*)imp->Name);
			thunks++;
			out++;
		}
		
		imports++;
	}
}

void pe_do_relocs(ntdll_t* ntdll, HMODULE mod)
{
	uint8_t* base_addr = (uint8_t*)mod;
	
	IMAGE_NT_HEADERS* head_nt = pe_get_nt_headers(mod);
	uint8_t* orig_base_addr = (uint8_t*)head_nt->OptionalHeader.ImageBase;
	
	intptr_t delta = base_addr - orig_base_addr;
	if (!delta) return;
	
	uint32_t prot_prev[32]; // static allocation, malloc doesn't work yet... a normal exe has 19 sections, hope it won't grow too much
#ifndef ARLIB_OPT
	if (head_nt->FileHeader.NumberOfSections > sizeof(prot_prev)/sizeof(*prot_prev))
		__builtin_trap(); // not debug_fatal(), it's not relocated so it won't work yet
#endif
	IMAGE_SECTION_HEADER* sec = (IMAGE_SECTION_HEADER*)((uint8_t*)&head_nt->OptionalHeader + head_nt->FileHeader.SizeOfOptionalHeader);
	for (uint16_t i=0;i<head_nt->FileHeader.NumberOfSections;i++)
	{
		// ideally, there should be no relocations in .text, so we can just skip that section
		// in practice, __CTOR_LIST__ is there, and possibly some others (lots of others on i386, but i386 is currently unsupported anyways)
		// (no clue why, it makes more sense in .rdata, or as a sequence of call instructions rather than pointers)
		// the easiest solution is to just mark everything PAGE_EXECUTE_READWRITE instead of PAGE_READWRITE
		// we're already deep into shenanigans territory, a W^X violation is nothing to worry about
		// (if I want to fix the W^X, I'd map a copy of this function somewhere as r-x, and let that one make the real one rw-)
		// (alternatively, I could reimplement LdrProcessRelocationBlock and call WriteProcessMemory aka NtWriteVirtualMemory)
		// (on i386, I could write some asm that writes a ROP chain to the stack, but ms64 calling convention is less stack-focused)
		void* sec_addr = base_addr + sec[i].VirtualAddress;
		size_t sec_size = sec[i].SizeOfRawData;
		ntdll->NtProtectVirtualMemory((HANDLE)-1, &sec_addr, &sec_size, PAGE_EXECUTE_READWRITE, &prot_prev[i]);
	}
	
	IMAGE_DATA_DIRECTORY* relocs = pe_get_section_header(mod, IMAGE_DIRECTORY_ENTRY_BASERELOC);
	IMAGE_BASE_RELOCATION* reloc = (IMAGE_BASE_RELOCATION*)(base_addr + relocs->VirtualAddress);
	IMAGE_BASE_RELOCATION* reloc_end = (IMAGE_BASE_RELOCATION*)(base_addr + relocs->VirtualAddress + relocs->Size);
	while (reloc < reloc_end)
	{
		reloc = ntdll->LdrProcessRelocationBlock(base_addr + reloc->VirtualAddress,
		                                         (reloc->SizeOfBlock-sizeof(*reloc)) / sizeof(uint16_t),
		                                         (uint16_t*)(reloc+1), delta);
	}
	
	for (uint16_t i=0;i<head_nt->FileHeader.NumberOfSections;i++)
	{
		void* sec_addr = base_addr + sec[i].VirtualAddress;
		size_t sec_size = sec[i].SizeOfRawData;
		ntdll->NtProtectVirtualMemory((HANDLE)-1, &sec_addr, &sec_size, prot_prev[i], &prot_prev[0]);
	}
}



// many ctors call atexit to register destructors, and even if their dtors work differently, ctors often allocate memory (-> leak)
// therefore, don't call the entire ctor table; call only a whitelist of known good ctors
// to do this, group up the safe ones in the ctor table, with labels around the safe subsection
#ifdef __i386__
__asm__(R"(
.section .ctors.arlibstatic1,"dr"
_init_last:
.long _arlib_hybrid_exe_init  # this one must be "last" (.ctors is processed backwards)

.section .ctors.arlibstatic9,"dr"
_init_first:

.text
)");
#endif
#ifdef __x86_64__
__asm__(R"(
.section .ctors.arlibstatic1,"dr"
init_last:
.quad arlib_hybrid_exe_init  # this one must be "last" (.ctors is processed backwards)

.section .ctors.arlibstatic9,"dr"
init_first:

.text
)");
#endif

extern "C" const funcptr init_first[];
extern "C" const funcptr init_last[];

static void run_static_ctors()
{
	// comparing two "unrelated" pointers is undefined behavior, so let's force gcc to forget their relationship and lack thereof
	const funcptr * iter = launder(init_first);
	const funcptr * end = launder(init_last);
	// do-while optimizes better if at least one entry is guaranteed to exist (which is arlib_hybrid_exe_init)
	// I don't know why it's a list of pointers, rather than a list of call instructions,
	//  it'd be both smaller and nicer to the branch predictor
	// maybe less platform dependent, maybe they want same mechanism for ctors and dtors (which run in opposite order)
	do {
		iter--;
		(*iter)(); 
	} while (iter != end);
}



// can't use a normal runonce or mutex here, OS facilities aren't available yet
// I could switch from busyloop to OS facility once relocs are done and ctors are running, but no real point
enum { init_no, init_busy, init_done };
static uint8_t init_state = init_no;

extern "C" __attribute__((used))
void arlib_hybrid_exe_init()
{
	// if we're a dll, this is the last ctor to run; use an atomic write, so concurrent dll inits see other ctors
	// if we're an exe, there is no concurrency; atomic is unnecessary but harmless
	lock_write<lock_rel>(&init_state, init_done);
}

#ifdef __i386__
__asm__(R"(
# letting gcc choose a register seems impossible for code like this, so I'll hardcode it (ebx is caller preserve)
get_pc_ebx:
.byte 0x8B,0x1C,0x24 # movl %ebx, [%esp] (toplevel asm can't have dialectal variants)
ret
)");
#endif
// can be called both before and after reloc processing
HMODULE get_this_hmodule()
{
#ifdef __i386__
	HMODULE ret;
	__asm__(R"(
	call get_pc_ebx # also known as __x86.get_pc_thunk.bx, but that name is Linux only
	.L%=:
	.byte 0x81,0xC3 # add %%ebx, imm32 (local asm can have dialectal variants, but at&t is ugly)
	.long ___ImageBase-.L%=
	)" : "=b"(ret));
	return ret;
#else
	return (HMODULE)&__ImageBase;
#endif
}

}

void arlib_hybrid_dll_init();
void arlib_hybrid_dll_init()
{
	HMODULE this_mod = get_this_hmodule();
	
	uint8_t* init_state_p = RELOCATE(&init_state);
	int state = lock_read<lock_acq>(init_state_p);
	if (state == init_done) return;
	
#ifdef ARLIB_THREAD
	state = lock_cmpxchg<lock_acq, lock_acq>(init_state_p, init_no, init_busy);
	if (UNLIKELY(state != init_no))
	{
		while (state == init_busy)
		{
#ifdef runtime__SSE2__
			_mm_pause();
#endif
			state = lock_read<lock_acq>(init_state_p);
		}
		return;
	}
#endif
	
	ntdll_t ntdll;
	pe_get_ntdll_syms(&ntdll, this_mod);
	pe_process_imports(&ntdll, this_mod);
	pe_do_relocs(&ntdll, this_mod);
	
	// the normal EXE and DLL startup code calls _pei386_runtime_relocator(),
	//  but pseudo relocs are disabled in Arlib (and the entire function is stubbed out in misc.cpp), so no point
	
	run_static_ctors();
}
#endif
