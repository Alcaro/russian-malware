#if defined(_WIN32) && defined(ARLIB_HYBRID_DLL)
// don't include the Arlib headers, we can't use OS facilities in this file

#include <windows.h>
#include <winternl.h>
#include <stdint.h>

// TODO: unloading the dll - dtors, and unloading dependencies

typedef void(*ctor_t)();
extern ctor_t __CTOR_LIST__[];
extern ctor_t __DTOR_LIST__[];

extern "C" void _pei386_runtime_relocator();


namespace {

bool streq(const char * a, const char * b)
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
#ifdef __i386__
	__asm__("{mov %%fs:(0x30), %0|mov %0, fs:[0x30]}" : "=r"(peb));
#else
	__asm__("{mov %%gs:(0x60), %0|mov %0, gs:[0x60]}" : "=r"(peb));
#endif
	
	// windows maintains a list of all DLLs in the process, available via PEB (available via TEB)
	// in three different orders - load order, memory order, and init order
	// ntdll is always #2 in load order, with the exe being #1 (http://www.nynaeve.net/?p=185)
	// (kernel32 is probably also always present, but I don't think that's guaranteed, and I don't need kernel32 anyways)
	// only parts of the relevant structs are documented officially, so this is based on
	// https://github.com/wine-mirror/wine/blob/master/include/winternl.h
	PEB_LDR_DATA* pld = peb->Ldr;
	LDR_DATA_TABLE_ENTRY* ldte_exe = (LDR_DATA_TABLE_ENTRY*)pld->Reserved2[1];
	LDR_DATA_TABLE_ENTRY* ldte_ntdll = (LDR_DATA_TABLE_ENTRY*)ldte_exe->Reserved1[0];
	return (HMODULE)ldte_ntdll->DllBase;
}

void* pe_get_section(HMODULE mod, int sec)
{
	uint8_t* base_addr = (uint8_t*)mod;
	
	IMAGE_DOS_HEADER* head_dos = (IMAGE_DOS_HEADER*)base_addr;
	IMAGE_NT_HEADERS* head_nt = (IMAGE_NT_HEADERS*)(base_addr + head_dos->e_lfanew);
	
	IMAGE_DATA_DIRECTORY* directory = &head_nt->OptionalHeader.DataDirectory[sec];
	return base_addr + directory->VirtualAddress;
}

void* pe_get_proc_address(HMODULE mod, const char * name)
{
	uint8_t* base_addr = (uint8_t*)mod;
	IMAGE_EXPORT_DIRECTORY* exports = (IMAGE_EXPORT_DIRECTORY*)pe_get_section(mod, IMAGE_DIRECTORY_ENTRY_EXPORT);
	
	DWORD * addr_off = (DWORD*)(base_addr + exports->AddressOfFunctions);
	DWORD * name_off = (DWORD*)(base_addr + exports->AddressOfNames);
	WORD * ordinal = (WORD*)(base_addr + exports->AddressOfNameOrdinals);
	
	if ((uintptr_t)name < 0x10000) // ordinal
	{
		size_t idx = (uintptr_t)name - exports->Base;
		if (idx > exports->NumberOfFunctions)
			return NULL;
		return base_addr + addr_off[idx];
	}
	
	for (size_t i=0;i<exports->NumberOfNames;i++)
	{
		const char * exp_name = (const char*)(base_addr + name_off[i]);
		if (streq(name, exp_name))
			return base_addr + addr_off[ordinal[i]];
	}
	return NULL;
}


typedef struct _LDR_DLL_LOADED_NOTIFICATION_DATA {
    ULONG Flags;                    //Reserved.
    const UNICODE_STRING * FullDllName;   //The full path name of the DLL module.
    const UNICODE_STRING * BaseDllName;   //The base file name of the DLL module.
    void* DllBase;                  //A pointer to the base address for the DLL in memory.
    ULONG SizeOfImage;              //The size of the DLL image, in bytes.
} LDR_DLL_LOADED_NOTIFICATION_DATA, *PLDR_DLL_LOADED_NOTIFICATION_DATA;

typedef struct _LDR_DLL_UNLOADED_NOTIFICATION_DATA {
    ULONG Flags;                    //Reserved.
    const UNICODE_STRING * FullDllName;   //The full path name of the DLL module.
    const UNICODE_STRING * BaseDllName;   //The base file name of the DLL module.
    void* DllBase;                  //A pointer to the base address for the DLL in memory.
    ULONG SizeOfImage;              //The size of the DLL image, in bytes.
} LDR_DLL_UNLOADED_NOTIFICATION_DATA, *PLDR_DLL_UNLOADED_NOTIFICATION_DATA;

typedef union _LDR_DLL_NOTIFICATION_DATA {
    LDR_DLL_LOADED_NOTIFICATION_DATA Loaded;
    LDR_DLL_UNLOADED_NOTIFICATION_DATA Unloaded;
} LDR_DLL_NOTIFICATION_DATA, *PLDR_DLL_NOTIFICATION_DATA;

typedef void (CALLBACK LDR_DLL_NOTIFICATION_FUNCTION)(ULONG NotificationReason,
                       const LDR_DLL_NOTIFICATION_DATA * NotificationData, void* Context);

#define LDR_DLL_NOTIFICATION_REASON_LOADED 1
#define LDR_DLL_NOTIFICATION_REASON_UNLOADED 2

struct ntdll_t {
NTSTATUS WINAPI (*LdrLoadDll)(const WCHAR * DirPath, DWORD Flags, const UNICODE_STRING * ModuleFileName, HMODULE* ModuleHandle);
NTSTATUS WINAPI (*LdrRegisterDllNotification)(ULONG Flags, LDR_DLL_NOTIFICATION_FUNCTION* NotificationFunction,
                                              void* Context, void** Cookie);
NTSTATUS WINAPI (*LdrUnloadDll)(HMODULE ModuleHandle);
NTSTATUS WINAPI (*LdrUnregisterDllNotification)(void* Cookie);
HMODULE WINAPI (*RtlPcToFileHeader)(void* PcValue, HMODULE* BaseOfImage);
};
#define ntdll_t_names \
	"LdrLoadDll\0" \
	"LdrRegisterDllNotification\0" \
	"LdrUnloadDll\0" \
	"LdrUnregisterDllNotification\0" \
	"RtlPcToFileHeader\0" \


void pe_get_ntdll_syms(ntdll_t* out)
{
	void* * fp = (void**)out;
	const char * names = ntdll_t_names;
	
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
	
	IMAGE_IMPORT_DESCRIPTOR* imports = (IMAGE_IMPORT_DESCRIPTOR*)pe_get_section(mod, IMAGE_DIRECTORY_ENTRY_IMPORT);
	while (imports->Name)
	{
		const char * libname = (char*)(base_addr + imports->Name);
		WCHAR libname16[64];
		WCHAR* libname16iter = libname16;
		while (*libname) *libname16iter++ = *libname++;
		*libname16iter = '\0';
		
		UNICODE_STRING libname_us = { (uint16_t)((libname16iter-libname16)*sizeof(WCHAR)), sizeof(libname16), libname16 };
		
		HMODULE mod;
		ntdll->LdrLoadDll(NULL, 0, &libname_us, &mod);
		
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


static bool ctors_called = false;
static bool imports_processed = false;

void arlib_hybrid_call_ctors()
{
	if (ctors_called) return;
	// don't set ctors_called, arlib_hybrid_exe_init() below does that
	
	ctor_t * iter = __CTOR_LIST__+1;
	while (*iter)
	{
		(*iter)();
		iter++;
	}
}

void arlib_hybrid_call_dtors()
{
	if (!ctors_called) return;
	ctors_called = false;
	
	ctor_t * iter = __DTOR_LIST__+1;
	while (*iter)
	{
		(*iter)();
		iter++;
	}
}

static ntdll_t ntdll;
static void* dll_notif_cookie;
static HMODULE this_hmod;

void CALLBACK ldr_dll_notif(ULONG reason, const LDR_DLL_NOTIFICATION_DATA * dat, void* context)
{
	if (reason != LDR_DLL_NOTIFICATION_REASON_UNLOADED) return;
	if ((HMODULE)dat->Unloaded.DllBase != this_hmod) return;
	
	// TODO: this seems to crash; do it on another thread, maybe?
	arlib_hybrid_call_dtors();
	
	ntdll.LdrUnregisterDllNotification(dll_notif_cookie);
	
	// TODO: unload dll dependencies
}

}

__attribute__((constructor))
static void arlib_hybrid_exe_init() // called if ran as exe, and from call_ctors above
{
	imports_processed = true;
	ctors_called = true;
}

void arlib_hybrid_dll_init();
void arlib_hybrid_dll_init()
{
	if (imports_processed) return;
	
	pe_get_ntdll_syms(&ntdll);
	pe_process_imports(&ntdll, ntdll.RtlPcToFileHeader((void*)arlib_hybrid_dll_init, &this_hmod));
	
	_pei386_runtime_relocator(); // TODO: does this work?
	
	if (ntdll.LdrRegisterDllNotification) // TODO: test, and determine which systems it works on
		ntdll.LdrRegisterDllNotification(0, ldr_dll_notif, NULL, &dll_notif_cookie);
	
	arlib_hybrid_call_ctors();
}
#endif
