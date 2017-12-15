#include "process.h"

#ifdef __linux__
#ifdef ARLIB_THREAD
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include "set.h"
#include "file.h"

//will set 'status' and futex(FUTEX_WAKE, status, INT_MAX) once child exits
//'status' must be initialized to -1
static void set_sigchld(pid_t pid, int* status);
static void sigchld_handler(int fd);

//atoi is locale-aware, not gonna trust that to not call malloc or otherwise be stupid
static int atoi_simple(const char * text)
{
	int ret = 0; // ignore overflows, fds don't go higher than a couple thousand before the kernel gets angry at you
	while (*text)
	{
		if (*text<'0' || *text>'9') return -1;
		ret *= 10;
		ret += *text-'0';
		text++;
	}
	return ret;
}

//trusting everything to set O_CLOEXEC isn't enough, this is a sandbox
bool process::closefrom(int lowfd)
{
	//getdents[64] is documented do-not-use and opendir should be used instead.
	//However, we're in (the equivalent of) a signal handler, and opendir is not signal safe.
	//Therefore, raw kernel interface it is.
	struct linux_dirent64 {
		ino64_t        d_ino;    /* 64-bit inode number */
		off64_t        d_off;    /* 64-bit offset to next structure */
		unsigned short d_reclen; /* Size of this dirent */
		unsigned char  d_type;   /* File type */
		char           d_name[]; /* Filename (null-terminated) */
	};
	
	int dfd = open("/proc/self/fd/", O_RDONLY|O_DIRECTORY);
	if (dfd < 0) return false;
	
	while (true)
	{
		char bytes[1024];
		// this interface always returns full structs, no need to paste things together
		int nbytes = syscall(SYS_getdents64, dfd, &bytes, sizeof(bytes));
		if (nbytes < 0) { close(dfd); return false; }
		if (nbytes == 0) break;
		
		int off = 0;
		while (off < nbytes)
		{
			linux_dirent64* dent = (linux_dirent64*)(bytes+off);
			off += dent->d_reclen;
			
			int fd = atoi_simple(dent->d_name);
			if (fd>=0 && fd!=dfd && fd>=lowfd)
			{
#ifdef ARLIB_TEST_ARLIB
				if (fd >= 1024) continue; // shut up, Valgrind
#endif
				close(fd); // seems like close() can't fail, per https://lwn.net/Articles/576478/
			}
		}
	}
	close(dfd);
	return true;
}

bool process::set_fds(arrayvieww<int> fds, bool cloexec)
{
	if (fds.size() > INT_MAX) return false;
	
	bool ok = true;
	
	//probably doable with fewer dups, but yawn, don't care.
	for (size_t i=0;i<fds.size();i++)
	{
		while (fds[i] < (int)i && fds[i] >= 0)
		{
			fds[i] = fcntl(fds[i], F_DUPFD_CLOEXEC);
			if (fds[i] < 0) ok = false;
		}
	}
	
	for (size_t i=0;i<fds.size();i++)
	{
		if (fds[i] >= 0)
		{
			if (fds[i] != (int)i)
			{
				if (dup3(fds[i], i, O_CLOEXEC) < 0)
				{
					ok = false;
					close(i);
				}
			}
			fcntl(i, F_SETFD, cloexec ? FD_CLOEXEC : 0);
		}
		if (fds[i] < 0) close(i);
	}
	
	if (!closefrom(fds.size())) ok = false;
	
	return ok;
}

string process::find_prog(cstring prog)
{
	if (prog.contains("/")) return prog;
	array<string> paths = string(getenv("PATH")).split(":");
	for (string& path : paths)
	{
		string pathprog = path+"/"+prog;
		if (access(pathprog, X_OK)==0) return pathprog;
	}
	return "";
}


pid_t process::launch_impl(array<const char*> argv, array<int> stdio_fd)
{
	pid_t ret = fork();
	if (ret == 0)
	{
		//WARNING:
		//fork(), POSIX.1-2008, http://pubs.opengroup.org/onlinepubs/9699919799/functions/fork.html
		//  If a multi-threaded process calls fork(), the new process shall contain a replica of the
		//  calling thread and its entire address space, possibly including the states of mutexes and
		//  other resources. Consequently, to avoid errors, the child process may only execute
		//  async-signal-safe operations until such time as one of the exec functions is called.
		//In particular, malloc must be avoided.
		
		set_fds(stdio_fd);
		execvp(argv[0], (char**)argv.ptr());
		while (true) _exit(EXIT_FAILURE);
	}
	
	return ret;
}

bool process::launch(cstring prog, arrayview<string> args)
{
	array<const char*> argv;
	string progpath = find_prog(prog); // don't inline, it must be kept alive
	if (!progpath) return false;
	argv.append((const char*)progpath);
	for (size_t i=0;i<args.size();i++)
	{
		argv.append((const char*)args[i]);
	}
	argv.append(NULL);
	
	array<int> fds;
	if (ch_stdin) fds[0] = ch_stdin->fd_read;
	else fds[0] = open("/dev/null", O_RDONLY);
	
	if (ch_stdout) fds[1] = ch_stdout->fd_write;
	else fds[1] = open("/dev/null", O_WRONLY);
	
	if (ch_stderr) fds[2] = ch_stderr->fd_write;
	else fds[2] = open("/dev/null", O_WRONLY);
	
	this->pid = launch_impl(std::move(argv), fds);
	
	if (ch_stdin)
	{
		ch_stdin->started = true;
		ch_stdin->update(0);
	}
	
	close(fds[0]);
	close(fds[1]);
	close(fds[2]);
	
	if (this->pid < 0)
	{
		this->exitcode = 1;
		return false;
	}
	
	this->exitcode = -1;
	set_sigchld(this->pid, &this->exitcode);
	
	return true;
}

bool process::running()
{
	int status = lock_read(&this->exitcode);
	return (status==-1);
}

int process::wait()
{
	futex_wait_while_eq(&this->exitcode, -1);
	if (ch_stdout) ch_stdout->update(0);
	if (ch_stderr) ch_stderr->update(0);
	return this->exitcode;
}

void process::terminate()
{
	if (lock_read(&this->exitcode) == -1)
	{
		kill(this->pid, SIGKILL);
		//can't just wait(), if we're in the fd monitor thread then that deadlocks
		while (lock_read_loose(&this->exitcode)==-1)
		{
			sigchld_handler(0);
		}
		wait();
	}
}

process::~process()
{
	terminate();
	delete ch_stdin;
	delete ch_stdout;
	delete ch_stderr;
}



void process::input::update(int fd)
{
	synchronized(mut)
	{
		if (fd_write == (uintptr_t)-1) return;
		
		if (buf.size() == 0)
		{
			if (started && do_close) goto do_terminate;
			else goto do_unmonitor;
		}
		
		ssize_t bytes = ::write(fd_write, buf.ptr(), buf.size());
		if (bytes < 0 && errno == EAGAIN) goto do_monitor;
		if (bytes <= 0) goto do_terminate;
		buf = buf.skip(bytes);
		
		goto do_monitor;
	}
	
do_monitor:
	fd_mon_thread(fd_write, NULL, bind_this(&input::update));
	return;
	
do_unmonitor:
	fd_mon_thread(fd_write, NULL, NULL);
	return;
	
do_terminate:
	terminate();
	return;
}

process::input& process::input::create_pipe(arrayview<byte> data)
{
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) < 0) abort();
	input* ret = new input();
	ret->fd_read = fds[0];
	ret->fd_write = fds[1];
	fcntl(ret->fd_write, F_SETFL, fcntl(ret->fd_write, F_GETFL, 0) | O_NONBLOCK);
	ret->buf = data;
	ret->update(0);
	return *ret;
}
process::input& process::input::create_buffer(arrayview<byte> data)
{
	input& ret = create_pipe(data);
	ret.do_close = true;
	return ret;
}

//process::input& process::input::create_file(cstring path)

//dup because process::launch closes it
process::input& process::input::create_stdin() { return *new input(fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 0)); }

void process::input::terminate()
{
	//ignore fd_read, closed by process::launch
	int fd = lock_xchg_loose(&fd_write, -1);
	if (fd != -1)
	{
		fd_mon_thread(fd, NULL, NULL);
		::close(fd);
	}
}
process::input::~input() { terminate(); }



void process::output::update(int fd)
{
	synchronized(mut)
	{
	again:
		if (fd_read == (uintptr_t)-1) return;
		
		uint8_t tmp[4096];
		ssize_t bytes = ::read(fd_read, tmp, sizeof(tmp));
		if (bytes < 0 && errno == EAGAIN) return;
		if (bytes <= 0) goto do_terminate;
		buf += arrayview<byte>(tmp, bytes);
		if (buf.size() >= maxbytes) goto do_terminate;
		goto again;
	}
	
do_terminate:
	terminate();
	return;
}

void process::output::terminate()
{
	//ignore fd_write, closed by process::launch
	int fd = lock_xchg_loose(&fd_read, -1);
	if (fd != -1)
	{
		fd_mon_thread(fd, NULL, NULL);
		::close(fd);
	}
}

process::output& process::output::create_buffer(size_t limit)
{
	int fds[2];
	if (pipe2(fds, O_CLOEXEC) < 0) abort();
	output* ret = new output();
	ret->fd_write = fds[1];
	ret->fd_read = fds[0];
	fcntl(ret->fd_read, F_SETFL, fcntl(ret->fd_read, F_GETFL, 0) | O_NONBLOCK);
	ret->maxbytes = limit;
	fd_mon_thread(ret->fd_read, bind_ptr(&output::update, ret), NULL);
	return *ret;
}

//process::output& process::output::create_file(cstring path, bool append)

process::output& process::output::create_stdout() { return *new output(fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0)); }
process::output& process::output::create_stderr() { return *new output(fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 0)); }

process::output::~output()
{
	terminate();
}



static bool sigchld_installed = false;
static int sigchld_pipe[2];
static mutex sigchld_mut;
static map<pid_t, int*> sigchld_handlers;

static void sigchld_handler_w(int signo, siginfo_t* info, void* context)
{
	int bytes = write(sigchld_pipe[1], &info->si_pid, sizeof(pid_t));
	if (bytes != sizeof(pid_t)) abort();
}

static void sigchld_handler(int fd)
{
	synchronized(sigchld_mut)
	{
		pid_t pid;
		int bytes = read(sigchld_pipe[0], &pid, sizeof(pid_t));
		if (bytes < 0) return;
		if (bytes != sizeof(pid_t)) abort();
		int* futex = sigchld_handlers.get_or(pid, NULL);
		if (!futex) return; // not our child (anymore)? ignore
		
		int status;
		pid_t exited = waitpid(pid, &status, WNOHANG);
		if (exited != pid) abort(); // shouldn't happen
		
		futex_set_and_wake(futex, status);
		sigchld_handlers.remove(pid);
	}
}

static void set_sigchld(pid_t pid, int* status)
{
	synchronized(sigchld_mut)
	{
		if (!sigchld_installed)
		{
			int piperes = pipe2(sigchld_pipe, O_CLOEXEC|O_NONBLOCK);
			if (piperes < 0) abort();
			fd_mon_thread(sigchld_pipe[0], sigchld_handler, NULL);
			
			struct sigaction act;
			act.sa_sigaction = sigchld_handler_w;
			sigemptyset(&act.sa_mask);
			//recursion is bad, but the alternative is trying to waitpid every single child as soon as one dies
			act.sa_flags = SA_SIGINFO|SA_NODEFER|SA_NOCLDSTOP;
			sigaction(SIGCHLD, &act, NULL);
			
			sigchld_installed = true;
		}
		
		sigchld_handlers.insert(pid, status);
		
		//in case of race conditions
		int status;
		pid_t exited = waitpid(pid, &status, WNOHANG);
		if (exited == pid)
		{
			futex_set_and_wake(sigchld_handlers.get(pid), status);
			sigchld_handlers.remove(pid);
		}
	}
}
#endif
#endif
