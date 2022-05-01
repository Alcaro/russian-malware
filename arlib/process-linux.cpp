#include "process.h"

#ifdef __linux__
#include <fcntl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

// too bad pidfd is linux only, sigchld is such an enormous pain

//atoi is locale-aware, not gonna trust that to not call malloc or otherwise be stupid
static int atoi_simple(const char * text)
{
	int ret = 0; // ignore overflows, fds don't go higher than a few thousand before the kernel gets angry at you
	while (*text)
	{
		if (*text < '0' || *text > '9') return -1;
		ret *= 10;
		ret += *text - '0';
		text++;
	}
	return ret;
}

//everything should set O_CLOEXEC, but let's be paranoid
bool process::closefrom(int lowfd)
{
#ifdef __NR_close_range
	// available on kernel >= 5.9, october 2020 (glibc wrapper in 2.34, august 2021)
	// 20.04 uses 5.4, so will keep around for another month
	if (syscall(__NR_close_range, lowfd, UINT_MAX, 0) == 0) return true;
#endif
	
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
			if (fd < 0 && strcmp(dent->d_name, ".")!=0 && strcmp(dent->d_name, "..")!=0)
				abort();
			if (fd>=0 && fd!=dfd && fd>=lowfd)
			{
#ifdef ARLIB_TEST
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
		if (access(pathprog, X_OK) == 0) return pathprog;
	}
	return "";
}


void process::launch_impl(const char * program, array<const char*> argv, array<int> stdio_fd)
{
#ifdef __x86_64__
	pid_t ret = syscall(__NR_clone, CLONE_PIDFD, NULL, &this->pidfd, NULL, NULL);
#endif
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
		execvp(program, (char**)argv.ptr());
		while (true)
			_exit(EXIT_FAILURE);
	}
}

bool process::launch(string prog, arrayview<string> args, bool override_argv0)
{
	string progpath = find_prog(prog);
	if (!progpath) return false;
	
	array<const char*> argv;
	if (!override_argv0)
		argv.append(prog);
	for (size_t i=0;i<args.size();i++)
	{
		argv.append((const char*)args[i]);
	}
	argv.append(NULL);
	
	if (ch_stdin ) ch_stdin ->init(loop);
	if (ch_stdout) ch_stdout->init(loop);
	if (ch_stderr && ch_stderr != ch_stdout) ch_stderr->init(loop);
	
	array<int> fds;
	fds.append(ch_stdin  ? ch_stdin ->pipe[0] : open("/dev/null", O_RDONLY));
	fds.append(ch_stdout ? ch_stdout->pipe[1] : open("/dev/null", O_WRONLY));
	fds.append(ch_stderr ? ch_stderr->pipe[1] : open("/dev/null", O_WRONLY));
	
	
	//can poke this->pid/exitcode freely before calling sigchld::listen
	launch_impl(progpath, std::move(argv), fds);
	
	if (ch_stdin)
	{
		ch_stdin->started = true;
		ch_stdin->update();
	}
	
	close(fds[0]);
	close(fds[1]);
	close(fds[2]);
	
	if (this->pidfd < 0)
	{
		this->exitcode = 127; // the usual linux 'couldn't exec' error
		return false;
	}
	
	loop->set_fd(this->pidfd, bind_this(&process::reap));
	
	return true;
}

void process::reap(uintptr_t pidfd)
{
	siginfo_t inf;
	waitid((idtype_t)P_PIDFD, pidfd, &inf, WEXITED); // extra cast because libc/kernel mismatch
	if (inf.si_code == CLD_EXITED)
		this->exitcode = inf.si_status;
	else
		this->exitcode = inf.si_status | 256;
	loop->set_fd(pidfd, nullptr);
	close(pidfd);
	this->pidfd = -1;
	this->onexit_cb(this->exitcode);
}

void process::terminate()
{
	if (running())
	{
		syscall(__NR_pidfd_send_signal, pidfd, SIGKILL, NULL, 0);
		reap(this->pidfd);
	}
}

process::~process()
{
	terminate();
	delete ch_stdin;
	delete ch_stdout;
	delete ch_stderr;
}



void process::input::init(runloop* loop)
{
	this->loop = loop;
	update();
}
void process::input::update(uintptr_t)
{
	if (pipe[1] == -1) return;
	
	if (buf.size())
	{
		arrayview<uint8_t> bytes = buf.pull_begin();
		ssize_t nbytes = ::write(pipe[1], bytes.ptr(), bytes.size());
		if (nbytes < 0 && errno == EAGAIN) goto do_monitor;
		if (nbytes <= 0) goto do_terminate;
		buf.pull_finish(nbytes);
	}
	if (!buf.size() && started && do_close) goto do_terminate;
	
do_monitor:
	bool should_monitor;
	should_monitor = (buf.size());
	if (should_monitor != monitoring)
	{
		loop->set_fd(pipe[1], NULL, (should_monitor ? bind_this(&input::update) : NULL));
		this->monitoring = should_monitor;
	}
	return;
	
do_terminate:
	terminate();
	return;
}

process::input& process::input::create_pipe(arrayview<uint8_t> data)
{
	input* ret = new input();
	if (pipe2(ret->pipe, O_CLOEXEC) < 0) abort();
	fcntl(ret->pipe[1], F_SETFL, fcntl(ret->pipe[1], F_GETFL, 0) | O_NONBLOCK);
	ret->buf.push(data);
	return *ret;
}
process::input& process::input::create_buffer(arrayview<uint8_t> data)
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
	if (pipe[1] != -1)
	{
		if (this->monitoring)
			loop->set_fd(pipe[1], nullptr);
		::close(pipe[1]);
		pipe[1] = -1;
	}
}
process::input::~input() { terminate(); }



void process::output::init(runloop* loop)
{
	if (pipe[0] != -1) loop->set_fd(pipe[0], bind_this(&output::update_with_cb), NULL);
	this->loop = loop;
}
void process::output::callback(function<void()> cb)
{
	this->on_readable = cb;
	update();
}
void process::output::update()
{
	if (pipe[0] != -1)
	{
		uint8_t tmp[65536];
		ssize_t bytes = ::read(pipe[0], tmp, sizeof(tmp));
		if (bytes < 0 && errno == EAGAIN) return;
		if (bytes <= 0) return terminate();
		buf += arrayview<uint8_t>(tmp, bytes);
		if (buf.size() >= maxbytes) return terminate();
	}
}
void process::output::update_with_cb(uintptr_t)
{
	update();
	while (on_readable && buf.size() != 0) on_readable();
}

void process::output::terminate()
{
	if (pipe[0] != -1)
	{
		loop->set_fd(pipe[0], NULL);
		close(pipe[0]);
		pipe[0] = -1;
	}
}

process::output& process::output::create_buffer(size_t limit)
{
	output* ret = new output();
	if (pipe2(ret->pipe, O_CLOEXEC) < 0) abort();
	fcntl(ret->pipe[0], F_SETFL, fcntl(ret->pipe[0], F_GETFL, 0) | O_NONBLOCK);
	ret->maxbytes = limit;
	return *ret;
}

//process::output& process::output::create_file(cstring path, bool append)

process::output& process::output::create_stdout() { return *new output(fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0)); }
process::output& process::output::create_stderr() { return *new output(fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 0)); }
#endif
