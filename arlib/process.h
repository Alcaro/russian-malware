#pragma once
#include "global.h"
#include "string.h"
#include "thread.h"
#include "runloop.h"
#include "bytepipe.h"

#ifdef __unix__
#include <signal.h>
#endif

//On Linux, you must be careful about creating child processes through other functions. Make sure
// they don't fight over any process-global resources.
//More specifically, all SIGCHLD handlers must chain to the previous one, using the three-argument sigaction function;
//  they may not attempt concurrent installation with Arlib (sequential is fine); and they may not attempt to uninstall them.
//Additionally, nothing may use waitpid(-1).
//From what I can gather, g_spawn_*(), popen() and system() are safe.
//  However, g_child_watch_*() and GSubprocess are not; they install a SIGCHLD handler and discard the old one.
//I have not analyzed Qt.
class process : public nocopy {
public:
	class input;
	class output;
protected:
	runloop* loop;
	
private:
	input* ch_stdin = NULL;
	output* ch_stdout = NULL;
	output* ch_stderr = NULL;
	
	struct onexit_t;
	onexit_t* onexit_cb = NULL;
#ifdef __linux__
	void on_sigchld();
	
public:
	//used internally only, don't touch it
	bool _on_sigchld_offloop();
protected:
	//THREAD WARNING: these two may be written from wrong runloop
	//seems impossible to fix, the correct runloop may be stuck in terminate()
	pid_t pid = -1;
	int exitcode = -1;
	
	
	//Closes all open file descriptors in the process, except those which are numerically strictly less than lowfd.
	//For example, closefrom(3) would close everything except stdin/stdout/stderr.
	static bool closefrom(int lowfd);
	//Sets the file descriptor table to fds, closing all existing fds.
	//If an entry is -1, the corresponding fd is closed. Duplicates in the input are allowed.
	//Returns false on failure, but keeps doing its best anyways.
	//Will mangle the input array. While suboptimal, it's the only way to avoid a post-fork malloc.
	//The CLOEXEC flag is set to 'cloexec' on all fds.
	static bool set_fds(arrayvieww<int> fds, bool cloexec = false);
	
	//Like execlp, this searches PATH for the given program.
	static string find_prog(cstring prog);
	
	//stdio_fd is an array of { stdin, stdout, stderr } and should be sent to set_fds (possibly with a few additions) post-fork.
	//May modify its arguments arbitrarily.
	//Must return the child's pid, or -1 on failure.
#ifdef ARLIB_SANDBOX
	virtual
#endif
	pid_t launch_impl(const char * program, array<const char*> argv, array<int> stdio_fd);
	
public:
	
	//unfortunately, nothing else supports doing multiple handlers like this
	typedef void (*sigaction_t)(int signo, siginfo_t* info, void* context);
	//Call this to make Arlib not install its SIGCHLD handler. Instead, the caller must install a SIGCHLD handler,
	//  and must call the returned function pointer from said handler.
	//Calling that function for non-Arlib processes is fine, it does nothing.
	//Must be done before the first call to process::launch(). May only be done once.
	static sigaction_t claim_sigchld();
	//Makes Arlib's SIGCHLD handler call the given function. Works even if the above is done.
	//Must be done before the first call to process::launch(), or to claim_sigchld's return value. May only be done once.
	static void next_sigchld(sigaction_t handler);
private:
#endif
	
#ifdef _WIN32_disabled
#error outdated
	HANDLE proc = NULL;
	int exitcode = -1;
	
	HANDLE stdin_h = NULL;
	HANDLE stdout_h = NULL;
	HANDLE stderr_h = NULL;
#endif
	
public:
	process(runloop* loop) : loop(loop) {}
	//process(cstring prog, arrayview<string> args, runloop* loop) : loop(loop) { launch(prog, args); }
	
	void onexit(function<void(int)> cb); // Can only be called before launch().
	
	//Argument quoting is fairly screwy on Windows. Command line arguments at all are fairly screwy on Windows.
	//You may get weird results if you use too many backslashes, quotes and spaces.
	bool launch(cstring prog, arrayview<string> args, bool override_argv0 = false);
	bool launch(cstring prog, array<string> args) { return launch(prog, (arrayview<string>)args); }
	bool launch(cstring prog, arrayvieww<string> args) { return launch(prog, (arrayview<string>)args); }
	
	template<typename... Args>
	bool launch(cstring prog, Args... args)
	{
		string argv[sizeof...(Args)] = { args... };
		return launch(prog, arrayview<string>(argv));
	}
	
	bool launch(cstring prog)
	{
		return launch(prog, arrayview<string>(NULL));
	}
	
	
	class input : nocopy {
#ifdef __linux__
		input() {}
		input(int fd) { pipe[0] = -1; pipe[1] = fd; }
		
		int pipe[2];
		runloop* loop;
#endif
		friend class process;
		
		bytepipe buf;
		bool started = false;
		bool do_close = false;
		bool monitoring = false;
		
		void init(runloop* loop);
		void update(uintptr_t = 0);
		void terminate();
		
	public:
		void write(arrayview<byte> data) { buf.push(data); update(); }
		void write(cstring data) { write(data.bytes()); }
		//Sends EOF to the child, after all bytes have been written. Call only after the last write().
		void close() { do_close = true; update(); }
		
		static input& create_pipe(arrayview<byte> data = NULL);
		static input& create_pipe(cstring data) { return create_pipe(data.bytes()); }
		// Like create_pipe, but auto closes the pipe once everything has been written.
		static input& create_buffer(arrayview<byte> data = NULL);
		static input& create_buffer(cstring data) { return create_buffer(data.bytes()); }
		
		//Can't write/close these two. Just don't store them anywhere.
		static input& create_file(cstring path);
		//Uses caller's stdin. Make sure no two processes are trying to use stdin simultaneously, it acts badly.
		static input& create_stdin();
		
		~input();
	};
	//The process object takes ownership of the given object.
	//Can only be called before launch(), and only once. If omitted, process is given /dev/null.
	//It is undefined behavior to create an input/output object and not immediately attach it to a process.
	input* set_stdin(input& inp) { ch_stdin = &inp; return &inp; }
	
	class output : nocopy {
#ifdef __linux__
		output() {}
		output(int fd) { pipe[0] = -1; pipe[1] = fd; }
		
		int pipe[2];
#endif
		friend class process;
		
		array<byte> buf;
		size_t maxbytes = SIZE_MAX;
		
		void init(runloop* loop);
		void update();
		void update_with_cb(uintptr_t = 0);
		void terminate();
		
		runloop* loop;
		function<void()> on_readable;
		
	public:
		//Stops the process from writing too much data and wasting RAM.
		//If there, at any point, is more than 'max' bytes of unread data in the buffer, the pipe is closed.
		//Slightly more may be readable in practice, due to various buffers.
		void limit(size_t lim) { maxbytes = lim; }
		
		void callback(function<void()> cb);
		
		array<byte> readb()
		{
			update();
			return std::move(buf);
		}
		//Can return invalid UTF-8. Even if the program only processes UTF-8, it's possible
		// to read half a character, if the process is still running or the outlimit was hit.
		string read() { return string(readb()); }
		
		static output& create_buffer(size_t limit = SIZE_MAX);
		//Can't read these three. Just don't store them anywhere.
		static output& create_file(cstring path, bool append = false);
		static output& create_stdout();
		static output& create_stderr();
		
		~output() { terminate(); }
	};
	output* set_stdout(output& outp) { ch_stdout = &outp; return &outp; }
	output* set_stderr(output& outp) { ch_stderr = &outp; return &outp; }
	
	
#ifdef __linux__
	bool running() { return (lock_read_loose(&pid) != -1); }
	//Returns exit code, or -1 if it's still running. Can be called multiple times.
	int status() { return lock_read_loose(&exitcode); }
#endif
	//Doesn't return until the process is gone and onexit() is called.
	//The process is automatically terminated when the object is destroyed.
	void terminate();
	
	//Detaches the child process, allowing it to outlive this object, or the entire process.
	//Input/output objects may not be used after this.
	//TODO: implement
	void detach();
	
#ifdef ARLIB_SANDBOX
	virtual
#endif
	~process();
	
#ifdef ARLIB_TESTRUNNER
	//This fd is allowed to remain in runloops even when they destruct.
	static int sigchld_fd_test_runner_only();
#endif
};
