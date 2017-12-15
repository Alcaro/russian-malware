#pragma once
#include "global.h"
#include "string.h"
#include "thread.h"

#ifdef ARLIB_THREAD
//On Linux, you must be careful about creating child processes through other functions. Make sure
// they don't fight over any process-global resources.
//Said resources are waitpid(-1) and SIGCHLD. This one requires the latter, and requires that
// nothing uses the former.
//g_spawn_*(), popen() and system() are safe. However, g_child_watch_*() is not.
class process : nocopy {
public:
	class input;
	class output;
private:
	input* ch_stdin = NULL;
	output* ch_stdout = NULL;
	output* ch_stderr = NULL;
#ifdef __linux__
protected:
	pid_t pid = -1;
	int exitcode = -1;
	
	
	//Closes all open file descriptors in the process, except those which are numerically strictly less than lowfd.
	//For example, closefrom(3) would close everything except stdin/stdout/stderr.
	static bool closefrom(int lowfd);
	//Sets the file descriptor table to fds, closing all existing fds.
	//If an entry is -1, the corresponding fd is closed. Duplicates in the input are allowed.
	//Returns false on failure, but keeps doing its best anyways.
	//Will mangle the input array. While suboptimal, it's the only way to avoid a post-fork malloc.
	static bool set_fds(arrayvieww<int> fds, bool cloexec = false);
	
	//Like execlp, this searches PATH for the given program.
	static string find_prog(cstring prog);
	
	//stdio_fd is an array of { stdin, stdout, stderr } and should be sent to set_fds (possibly with a few additions) post-fork.
	//Must return the child's pid, or -1 on failure.
#ifdef ARLIB_SANDBOX
	virtual
#endif
	pid_t launch_impl(array<const char*> argv, array<int> stdio_fd);
#endif
	
#ifdef _WIN32
#error outdated
	HANDLE proc = NULL;
	int exitcode = -1;
	
	HANDLE stdin_h = NULL;
	HANDLE stdout_h = NULL;
	HANDLE stderr_h = NULL;
#endif
	
public:
	process() {}
	process(cstring prog, arrayview<string> args) { launch(prog, args); }
	
	//Argument quoting is fairly screwy on Windows. Command line arguments at all are fairly screwy on Windows.
	//You may get weird results if you use too many backslashes, quotes and spaces.
	bool launch(cstring prog, arrayview<string> args);
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
		input() {}
		input(uintptr_t fd) : fd_read(fd) {}
		
		uintptr_t fd_read = -1; // HANDLE on windows, int on linux
		uintptr_t fd_write = -1;
		friend class process;
		
		array<byte> buf;
		bool started = false;
		bool do_close = false;
		mutex mut;
		
		void update(int fd); // call without holding the mutex
		void terminate();
		
	public:
		void write(arrayview<byte> data) { synchronized(mut) { buf += data; } update(0); }
		void write(cstring data) { write(data.bytes()); }
		//Sends EOF to the child, after all bytes have been written. Call only after the last write().
		void close() { do_close = true; update(0); }
		
		static input& create_pipe(arrayview<byte> data = NULL);
		static input& create_pipe(cstring data) { return create_pipe(data.bytes()); }
		// Like create_pipe, but auto closes the pipe once everything has been written.
		static input& create_buffer(arrayview<byte> data = NULL);
		static input& create_buffer(cstring data) { return create_buffer(data.bytes()); }
		
		//Can't use write/close on these two. Just don't store them anywhere.
		static input& create_file(cstring path);
		//Uses caller's stdin. Make sure no two processes are trying to use stdin simultaneously, it acts badly.
		static input& create_stdin();
		
		~input();
	};
	//The process object takes ownership of the given object.
	//Can only be called before launch(), and only once.
	//Default is NULL, equivalent to /dev/null.
	//It is undefined behavior to create an input object and not immediately attach it to a process.
	input* set_stdin(input& inp) { ch_stdin = &inp; return &inp; }
	
	class output : nocopy {
		output() {}
		output(uintptr_t fd) : fd_write(fd) {}
		
		uintptr_t fd_write = -1;
		uintptr_t fd_read = -1;
		friend class process;
		
		array<byte> buf;
		mutex mut;
		size_t maxbytes = SIZE_MAX;
		
		void update(int fd);
		void terminate();
		
	public:
		//Stops the process from writing too much data and wasting RAM.
		//If there, at any point, is more than 'max' bytes of unread data in the buffer, the pipe is closed.
		//Slightly more may be readable in practice, due to kernel-level buffering.
		void limit(size_t lim) { maxbytes = lim; }
		
		array<byte> readb()
		{
			update(0);
			synchronized(mut) { return std::move(buf); }
			return NULL; //unreachable, gcc is just being stupid
		}
		//Can return invalid UTF-8. Even if the program only processes UTF-8, it's possible
		// to read half a character, if the process is still running or the outlimit was hit.
		string read() { return string(readb()); }
		
		static output& create_buffer(size_t limit = SIZE_MAX);
		static output& create_file(cstring path, bool append = false);
		static output& create_stdout();
		static output& create_stderr();
		
		~output();
	};
	output* set_stdout(output& outp) { ch_stdout = &outp; return &outp; }
	output* set_stderr(output& outp) { ch_stderr = &outp; return &outp; }
	
	
	bool running();
	//Waits until the process exits, then returns exit code. Can be called multiple times.
	//Remember to close stdin first, if it's from create_pipe.
	int wait();
	void terminate(); // The process is automatically terminated when the object is destroyed.
	
#ifdef ARLIB_SANDBOX
	virtual
#endif
	~process();
};
#endif
