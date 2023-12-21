#include "os.h"
#include "test.h"
#include "file.h"

#ifdef __unix__
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <link.h>
#include <execinfo.h>
#include <dlfcn.h>

static int log_fd = -1;

void debug_log_exedir()
{
	log_fd = open(file::resolve(file::exedir(), "arlib-debug.log"), O_WRONLY|O_APPEND|O_CREAT|O_NOCTTY|O_CLOEXEC, 0644);
}

// Stack traces from arlib-debug.log can be decoded using addr2line.py
void debug_log(const char * text)
{
	size_t len = strlen(text);
	(void)! write(STDERR_FILENO, text, len);
	if (log_fd < 0)
	{
		// probably wrong directory, but calling malloc here can recurse
		log_fd = open("arlib-debug.log", O_WRONLY|O_APPEND|O_CREAT|O_NOCTTY|O_CLOEXEC, 0644);
	}
	if (log_fd >= 0)
		(void)! write(log_fd, text, len);
}

//method from https://src.chromium.org/viewvc/chrome/trunk/src/base/debug/debugger_posix.cc
static bool has_debugger()
{
	char buf[4096];
	int fd = open("/proc/self/status", O_RDONLY);
	if (fd < 0) return false;
	
	ssize_t bytes = read(fd, buf, sizeof(buf)-1);
	close(fd);
	
	if (bytes < 0) return false;
	buf[bytes] = '\0';
	
	const char * tracer = strstr(buf, "TracerPid:\t");
	if (!tracer) return false;
	tracer += strlen("TracerPid:\t");
	
	return (*tracer != '0');
}

bool debug_break(const char * text)
{
	if (RUNNING_ON_VALGRIND) VALGRIND_PRINTF_BACKTRACE("%s", text);
	else if (has_debugger()) raise(SIGTRAP);
	else return false;
	return true;
}

void debug_log_stack(const char * text)
{
	debug_log(text);
	
	void* addrs[80];
	int n_addrs = backtrace(addrs, ARRAY_SIZE(addrs));
	
	//*
	backtrace_symbols_fd(addrs, n_addrs, STDERR_FILENO);
	if (log_fd >= 0)
		backtrace_symbols_fd(addrs, n_addrs, log_fd);
	/*/
	// alternative variant that always prints offset from the start of the executable
	auto debug_log_sym = [](const char * fmt, void* ptr, const char * symname, void* sym_at)
	{
		size_t off = (char*)ptr - (char*)sym_at;
		fprintf(stderr, fmt, symname, off);
		if (log_file) fprintf(log_file, fmt, symname, off);
	};
	for (int i=0;i<n_addrs;i++)
	{
		Dl_info inf;
		dladdr(addrs[i], &inf);
		
		if (inf.dli_fname)
		{
			debug_log_sym("%s+0x%zx", addrs[i], inf.dli_fname, inf.dli_fbase);
			if (inf.dli_sname)
				debug_log_sym(" (%s+0x%zx)", addrs[i], inf.dli_sname, inf.dli_saddr);
		}
		else
		{
			fprintf(stderr, "%p", addrs[i]);
			if (log_file) fprintf(log_file, "%p", addrs[i]);
		}
		debug_log("\n");
	}
	// */
}

static void set_fatal_signals(struct sigaction * sa)
{
	sigaction(SIGABRT, sa, nullptr);
	sigaction(SIGILL, sa, nullptr);
	sigaction(SIGSEGV, sa, nullptr);
	sigaction(SIGFPE, sa, nullptr);
	sigaction(SIGBUS, sa, nullptr);
}
static void sig_handler(int sig, siginfo_t* info, void* ucontext)
{
	struct sigaction sa = {};
	sa.sa_handler = SIG_DFL;
	set_fatal_signals(&sa);
	
	debug_log_stack("fatal signal\n");
	
	while (true)
	{
		raise(sig);
		// this point should be unreachable, but...
		raise(SIGABRT);
	}
}
void debug_install_crash_handler()
{
	struct sigaction sa = {};
	sa.sa_sigaction = sig_handler;
	sa.sa_flags = SA_SIGINFO;
	set_fatal_signals(&sa);
}

extern const ElfW(Ehdr) __ehdr_start;
//extern const char __executable_start[]; // same as ehdr_start on normal Linux
template<typename T, typename T2>
static const T * ptr_offset(const T2* ptr, uintptr_t off)
{
	return (T*)((uint8_t*)ptr + off);
}
const uint8_t * debug_build_id()
{
	// ideally, the linker would offer a magic symbol pointing to the build id, but there's no such thing
	// there's some mention of a __build_id_start symbol, but it doesn't exist for me
	// there's also __start_ symbols, but only for section names that are valid C identifiers; __build_.note.gnu.build-id is not
	
	// the build id can be extracted with
	// readelf -n <exe> | grep Build
	const ElfW(Phdr)* phdr = ptr_offset<ElfW(Phdr)>(&__ehdr_start, __ehdr_start.e_phoff);
	for (size_t n=0;n<__ehdr_start.e_phnum;n++)
	{
		if (phdr[n].p_type == PT_NOTE)
		{
			const ElfW(Nhdr)* nhdr = ptr_offset<ElfW(Nhdr)>(&__ehdr_start, phdr[n].p_vaddr);
			uintptr_t len = phdr[n].p_filesz;
			while (len)
			{
				// documented as being 4-aligned, despite that possibly yielding misaligned int64s in Elf64_Nhdr
				// unclear which one's wrong, and most notes I've seen result in proper alignment
				size_t namesz_align = (nhdr->n_namesz+3) & ~3;
				size_t descsz_align = (nhdr->n_descsz+3) & ~3;
				const uint8_t * name = ptr_offset<uint8_t>(nhdr, sizeof(*nhdr));
				const uint8_t * desc = ptr_offset<uint8_t>(name, namesz_align);
				if (nhdr->n_type == NT_GNU_BUILD_ID && nhdr->n_namesz == 4 && memcmp(name, "GNU", 4) == 0)
					return desc;
				nhdr = ptr_offset<ElfW(Nhdr)>(desc, descsz_align);
			}
		}
	}
	__builtin_trap();
}
#endif

#ifdef _WIN32
#include <windows.h>

static HANDLE log_file = INVALID_HANDLE_VALUE;

static void open_debug_log(const char * fname)
{
	log_file = CreateFileA(fname, FILE_APPEND_DATA, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
	                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr );
}

void debug_log_exedir()
{
	open_debug_log(file::resolve(file::exedir(), "arlib-debug.log"));
}

// Stack traces from arlib-debug.log can be decoded using addr2line.py
void debug_log(const char * text)
{
	size_t len = strlen(text);
	DWORD written;
	WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, len, &written, nullptr);
	if (log_file == INVALID_HANDLE_VALUE)
		open_debug_log("arlib-debug.log");
	if (log_file != INVALID_HANDLE_VALUE)
		WriteFile(log_file, text, len, &written, nullptr);
}

bool debug_break(const char * text)
{
	if (IsDebuggerPresent()) DebugBreak();
	else return false;
	return true;
}

void debug_log_stack(const char * text)
{
	debug_log(text);
	
	void* addrs[80];
	int n_addrs = CaptureStackBackTrace(0, ARRAY_SIZE(addrs), addrs, nullptr);
	
	for (int i=0;i<n_addrs;i++)
	{
		HMODULE mod = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (char*)addrs[i], &mod);
		if (!addrs[i])
			mod = nullptr;
		if (mod)
		{
			char name[PATH_MAX];
			// 80% of stack frames will be same module as last one
			// but no point optimizing, this codepath is cold
			GetModuleFileNameA(mod, name, sizeof(name));
			char * nameend = name;
			for (char * iter = name; *iter; iter++)
			{
				if (*iter == '/' || *iter == '\\') nameend = iter+1;
			}
			
			size_t off = (char*)addrs[i] - (char*)mod;
			debug_log(nameend);
			char buf[32];
			char* iter = buf;
			*iter++ = '+';
			*iter++ = '0';
			*iter++ = 'x';
			iter += tostringhex_ptr(iter, off, 8);
			*iter++ = '\n';
			*iter++ = '\0';
			debug_log(buf);
		}
		else
		{
			char buf[32];
			char* iter = buf;
			*iter++ = '0';
			*iter++ = 'x';
			iter += tostringhex_ptr(iter, (uintptr_t)addrs[i], sizeof(void*)*2);
			*iter++ = '\n';
			*iter++ = '\0';
			debug_log(buf);
		}
	}
}

static WINAPI LONG exc_handler(EXCEPTION_POINTERS* ptrs)
{
	debug_log_stack("fatal exception received\n");
	return EXCEPTION_CONTINUE_SEARCH;
}
void debug_install_crash_handler()
{
	SetUnhandledExceptionFilter(exc_handler);
}


extern const IMAGE_DOS_HEADER __ImageBase;
const uint8_t * debug_build_id()
{
	// the build id can be extracted with
	// objdump -p <exe> | grep RSDS
	// note that objdump will treat the build-id as a GUID and permute it; this function will return the raw bytes
	// 00112233 4455 6677 8899aabbccddeeff ->
	// 33221100 5544 7766 8899aabbccddeeff
	
	uint8_t* image_base = (uint8_t*)&__ImageBase;
	IMAGE_NT_HEADERS* head_nt = (IMAGE_NT_HEADERS*)(image_base + __ImageBase.e_lfanew);
	IMAGE_DATA_DIRECTORY* dirs = head_nt->OptionalHeader.DataDirectory;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds" // this is a VLA; I know it's safe, but gcc doesn't
	uint32_t dbgdir_offset = dirs[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
#pragma GCC diagnostic pop
	IMAGE_DEBUG_DIRECTORY* dbgdir = (IMAGE_DEBUG_DIRECTORY*)(image_base + dbgdir_offset);
	struct CV_INFO_PDB70 {
		ULONG CvSignature;
		GUID Signature;
		ULONG Age;
		UCHAR PdbFileName[ANYSIZE_ARRAY];
	};
	CV_INFO_PDB70* codeview = (CV_INFO_PDB70*)((uint8_t*)&__ImageBase + dbgdir->AddressOfRawData);
	return (uint8_t*)&codeview->Signature;
}
#endif

void debug_warn(const char * text) { if (!debug_break(text)) debug_log(text); }
void debug_fatal(const char * text) { if (!debug_break(text)) { debug_log(text); } abort(); }
void debug_warn_stack(const char * text) { if (!debug_break(text)) debug_log_stack(text); }
void debug_fatal_stack(const char * text) { if (!debug_break(text)) debug_log_stack(text); abort(); }

void debug_log_exe_version(const char * friendly_name)
{
	debug_log(friendly_name);
	debug_log(" version ");
	char buf[34];
	tostringhex_ptr(buf, debug_build_id(), 32);
	buf[32] = '\n';
	buf[33] = '\0';
	debug_log(buf);
}
