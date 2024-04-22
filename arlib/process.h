#pragma once
#include "file.h"
#include "runloop2.h"

class process {
	fd_t fd;
public:
	
	struct raw_params {
		arrayview<const char *> progs; // All will be tried, in order.
		const char * const * argv = NULL;
		const char * const * envp = NULL;
		arrayvieww<fd_raw_t> fds; // Will be mutated.
		bool detach = false;
	};
	struct params {
		string prog; // If this doesn't contain a slash, getenv(PATH) will be split by colon, and all will be tried.
		array<string> argv;
		array<string> envp;
		array<fd_raw_t> fds; // Will be mutated. If shorter than three elements, will be extended with 0 1 2.
		bool detach = false;
	};
	// Returns the child process' ID, or zero for failure.
	// If detached, will always return zero, and the process object will contain nothing.
	int create(raw_params& param);
	int create(params& param);
	int create(raw_params&& param) { return create((raw_params&)param); }
	int create(params&& param) { return create((params&)param); }
	
	async<int> wait(); // Only the first await works; subsequent ones will just return -1.
	void kill();
	
	~process() { kill(); }
	
	class pipe {
		pipe() = delete;
		pipe(fd_t rd, fd_t wr) : rd(std::move(rd)), wr(std::move(wr)) {}
		pipe(const pipe&) = delete;
		
	public:
		static pipe create();
		pipe(pipe&& other) = default;
		
		fd_t rd;
		fd_t wr;
	};
};
