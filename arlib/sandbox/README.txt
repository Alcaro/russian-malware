Gravelcube sandbox
==================

(this was originally a school project, this is basically the report I wrote minus a bunch of fluff
 plus a bunch of updates)


Background
----------

Software is increasingly complex these days, and more complexity gives more bugs. Some of those bugs
 can lead to executing arbitrary code on the host system, often leading to the installation of
 ransomware.

Given a memory-unsafe language like C or C++, it's infeasible to fix all bugs.

But what if the program couldn't do anything harmful, even if it's taken over? This is called
 sandboxing. The more well known ones are Google Chrome's builtin one and Firejail. Containers and
 virtual machines can also be used as sandboxes, though they have other uses.

However, all have various disadvantages. Many sandboxes require root access to function (sometimes
 using setuid to allow less privileged users to launch them, but root is still nearby), meaning a
 sandbox breach can give root access, which is not only obviously undesirable, but also quite
 ironical; additionally, setuid binaries can only be installed by privileged users. They're also
 often complex - less complex than their contents, of course, but still - and many can only run
 sandbox-aware child programs.

I want to write a sandbox not suffering from these issues. It will be simple enough that one person
 can reasonably understand and verify its security, will not touch root, and will run unmodified
 programs in the sandbox.


Goals
-----

The goal statement is:

Under a suitable sandbox policy, typical runs of distro-provided command line programs shall yield
 identical results to an unsandboxed run, except slightly (but not noticably) higher resource use
 (both CPU and RAM).

Under the same sandbox policy, if a malicious program is substituted, it cannot in any way:
(1) access private data
(2) contact or disrupt any unrelated process
(3) keep running beyond termination of the sandbox

"Private data" includes (unless permitted by sandbox policy):
- Everything in /home
- The user's name, ID, and home directory location
- The list of installed packages
- All configuration files (for example /etc, including whether /etc/apache2 exists)
but does not include:
- That it's sandboxed
    impossible to hide, just try to do something disallowed
- The sandbox configuration
    sandbox can't use its permissions without knowing them
- Anything symlinked from an authorized path
    symlinks from authorized paths are considered to grant access to the target too
    (though obviously the sandbox isn't allowed to do anything funny with said symlinks)
- Anything preinstalled in /bin, /lib or /usr/{bin,lib}
    distros and their packages are public already
    for Gentoo, build flags are considered public as well
- Which distro is running
    can't hide that when all the system libraries vary between distros
- Installed base hardware (CPU model, amount of RAM and disk; GPU and peripherials are private)
    hard to hide, especially the CPU model (cpuid)
- Kernel version and config, unless likely to be edited by the user
    distro default config is as public as their packages
- Volatile information about the sandboxed process itself, such as its PID
    even if I try hiding it, it probably shows up in timing leaks from the scheduler
    the UID is considered secret
      perhaps the scheduler could leak something if the attacker can run sandboxes under multiple UIDs
        but probably not more than which of the two is greater, and I'll accept that.
- The current time
    I'd prefer to block it, but it's exported in vdso, blocking it is impossible
    and even if I could delete vdso, time also shows up on every fstat() of a newly created file, as well as the stdio pipes
    blocking it is more effort than it's worth.
- The current timezone
    like the above, it's exported in vdso
    hard-but-not-impossible is a waste of time, it will become a library routine and then it's easy
- inode numbers
    fstat() leaks them, and I'm not aware of anything useful that can be done with an inode, other than equality comparisons
though obviously, even acceptable data will not be leaked frivolously.

"Contact" includes:
- Sending signals, opening a Unix socket, or other obvious methods
- Writing to files
- The network
- Denying service to non-sandboxed software
but does not include:
- Resource hogging information-transfer side channels
    no non-hostile recipient would understand them
- Resource hogging information-transfer side channels, even if measurable across the network
    whoever launches a sandbox is assumed to have access to its output, so this gains the attacker nothing
- Two sandboxes communicating, if hostile programs are running in both
    they can do it already, using aforementioned side channels
- Cache timing side channels to leak data from non-sandboxed processes
    again, impossible to block, other than by using constant-time algorithms in the victim
    this includes Meltdown/Spectre; only the victim can defend against that, the sandbox can't do anything
      about it (though Spectre may possibly be usable against the sandbox itself, just like against any other program)
though again, such permissions will not be granted frivolously.

The above applies even if the attacker is able to cause arbitrary syscalls to fail during sandbox
 setup, and/or launch arbitrarily many arbitrary executables, including ones built using information
 gathered by previous ones. Each sandbox run is assumed able to create a few files in an
 initially-empty directory.

I choose to discount the possibility of kernel/glibc/compiler/hardware bugs; I know there are some,
 but smarter people than me have already looked for them. (And by restricting many syscalls,
 especially the rare and complex ones, there's a good chance said bugs are inaccessible.)


Prior work
----------

There are lots of sandboxes and similar devices. I was able to find the following (in arbitrary
 order, I suspect more exist), and various flaws (there are probably more in all of them):

Google Chrome https://chromium.googlesource.com/chromium/src/+/master/docs/linux_sandboxing.md
while it can run non-Chromes <https://bugzilla.mozilla.org/show_bug.cgi?id=986397#c6>, I suspect it won't work on anything complex
likely allows reading various world-readable system config, such as /etc/passwd
setuid
(actually three sandboxes, one each for Windows/Linux/OSX)

Firejail https://firejail.wordpress.com/
setuid
quite complex

Cybergenic Shade, Sandboxie http://www.shadesandbox.com/ http://www.sandboxie.com/
Windows only
both require root (or equivalent)

Mozilla Firefox https://wiki.mozilla.org/Security/Sandbox
doesn't want root, judging by https://bugzilla.mozilla.org/show_bug.cgi?id=1151607#c0 #3
                   as well as https://wiki.mozilla.org/Security/Sandbox/Specifics#Linux
but can only run Firefox
(like Chrome, there are three, plus various special cases <https://wiki.mozilla.org/Security/Sandbox#Current_Status>)

Dave Peterson http://sandbox.sourceforge.net/
made in 2003, never updated
requires not only root, but a kernel patch (adds a sbxwait system call, among others)

seccomp https://en.wikipedia.org/wiki/Seccomp
only allows a few syscalls, can only execute the most primitive programs
requires modification

EasySandbox https://github.com/daveho/EasySandbox
based on seccomp, so it only allows the most primitive syscalls
uses LD_PRELOAD, can be escaped by statically linking the binary, messing with INTERP, or (somewhat paradoxally) turning on setuid

seccomp-bpf https://www.kernel.org/doc/Documentation/prctl/seccomp_filter.txt
requires modification
quite limited; can either grant or deny all filesystem access, but can't allow access to /tmp only
docs say it's not a sandbox, it's only for minimizing kernel attack surface - but it certainly looks like one, or a large part of one

bubblewrap https://github.com/projectatomic/bubblewrap
setuid
not sure how much of a sandbox this is

Sandstorm https://sandstorm.io/
very complex
intended for server-side software like apache2, not utilities like gcc
requires modification (though wrappers are available for some common software)

Systrace http://www.citi.umich.edu/u/provos/systrace/
very complex
rewrites syscall arguments, can elevate child's permissions -> race conditions everywhere!
 unless it suspends all threads on every sensitive syscall, which I assume is many, yielding bad performance
 (how would it even do that? how does it elevate permissions in the first place, kernel patch?)
no updates since 2009

SELinux, AppArmor, TOMOYO, Smack
the four most well-known Linux Security Modules
there can only be one; if your machine runs another, not even root will save you
(RedHat uses SELinux, Ubuntu and SuSE prefer AppArmor)
as far as I can gather, they all require root to configure, even if installed - and are often disabled

grsecurity https://grsecurity.net/
it's a kernel patch, likely to conflict with your distro
seems to be more about protecting against kernel vulnerabilities than protecting processes from each other
seems to not be freely available anymore

chroot https://en.wikipedia.org/wiki/Chroot
not a complete sandbox, doesn't protect network
docs say it's not a sandbox, it's only for isolating non-hostile software - but it certainly looks like one, or a large part of one
only available to non-root via unshare(CLONE_NEWUSER), which is poorly documented
  (and CLONE_NEWUSER is restricted on some distros, such as Debian)

Deep Freeze http://www.faronics.com/products/deep-freeze/
requires root
not a complete sandbox, doesn't protect network
doesn't block reading anything
seems to not allow running unsandboxed programs simultaneously
Windows only

runc https://github.com/opencontainers/runc
requires modification, runs containers only
not sure if this is a proper sandbox

Power off the machine
more restrictive than even seccomp
(requires root)


Program architecture
--------------------

The first step is finding a suitable program architecture. Sandboxing isn't easy, it requires lots
 of what-ifs and low-level knowledge.

A good sandbox must first block exactly everything, then include a list of permitted actions. This
 is exactly what seccomp-bpf does, so that's the most important component.

The sandbox also needs to be able to open files. seccomp can't allow opening only some files, so we
 need a less restricted broker process that will receive file open requests, validate them, and
 return the new file descriptor via sendmsg/recvmsg.

Like Chrome, I will use seccomp-bpf, and an unrestricted broker process that opens files. Unlike
 Chrome, I will require seccomp, and I will also require user namespaces; I need either that,
 setuid, or a complex ptrace dance to block the filesystem while allowing the operations I need.

The sandbox will contain four main components: Broker, locker, filter, and emulator. Each has
 different safety requirements.

The broker is the main process. It will start the child process, then process requests for file
 access until the child exits. It is the most safety-critical component; it must assume all input is
 hostile, and anything unexpected must be safely rejected (possibly by terminating the child, or
 itself).

The locker is responsible for dropping privileges, blocking every unnecessary syscall, and otherwise
 ensuring it can't do anything it shouldn't. While also important, the sandbox configuration is
 considered trusted, so this component doesn't need to worry about malicious inputs. However,
 correctness is critical; if it fails to set up its expected restrictions, it must immediately
 terminate.

The filter is installed by the locker. It's the seccomp-bpf code that rejects unauthorized syscalls.
 While it's as safety-critical as the broker, its inputs are small and simple, so there are few ways
 to exploit implementation errors. However, design faults (permitting something too powerful) are
 possible. There may also be bugs in the BPF assembler, but as its input is non-hostile, such bugs
 are equally likely to block allowed operations as allowing something forbidden, and then I'll find
 them.

The emulator runs in the child once it's locked down, and emulates an unconstrained environment. It
 intercepts various syscalls and passes them to the broker, or otherwise emulates them. It is a very
 complex component; however, as it's fully inside the sandbox, anything malicious could easily issue
 its own syscalls (including broker requests), so it doesn't need to protect against hostile inputs.


Broker
------

While the most delicate, it's also the most boring. A quite good combination, boring code is easy
 to audit.

After forking the locker, it will keep a list of child-accessible file/directory paths, where they
 really are (so /@CWD/ can redirect to /home/username/whatever/; they're stored as file descriptors,
 to avoid leaking the real lengths), and how much (if anything) may be written there.


Locker
------

The locker's job is long, but fairly straightforward. After broker's fork() returns, it will (not necessarily in this order)
- unshare(CLONE_NEWUSER | CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWCGROUP | CLONE_NEWIPC | CLONE_NEWNS | CLONE_NEWUTS)
    NEWUSER is because NEWPID and chroot needs it
      and because the real UID is private
        blocking getuid() isn't enough, the kernel passes UIDs to every fresh program via auxv
    NEWPID is to enforce sandbox rule 3: No outliving the sandbox
      Since this includes the child's own forks, they too must die with the sandbox. This is the
        most straightforward way; NEWPID makes all grandchildren die along with the new process,
        so the broker only needs to kill one process to terminate them all.
    NEWNET is to ensure the process can't access the network
      The child needs access to recvmsg(), which is a network operation. I don't want it to access
        anything else. The child can't use bind() or sendto(), so this is probably redundant, but
        defense in depth.
    NEWCGROUP, to limit various resources
    NEWIPC, NEWNS and NEWUTC are mostly paranoia. I doubt they make any difference, but why not?
    (this is actually part of the fork(), and the fork is actually clone(), because unshare(CLONE_NEWPID) is weird)
- dup2 the broker socket to fd 3
    it was created before clone()
    it's half of socketpair(AF_UNIX, SOCK_SEQPACKET, 0), used for passing additional file descriptors around
      why not DGRAM? because it allows sendmsg to contact others' sockets <https://bugzilla.mozilla.org/show_bug.cgi?id=1066750>
      and I'm not sure if STREAM passes the ancillary data (file descriptors) when I want it
      DGRAM is probably safe, there are two locks each of which should be safe, but three is better
        chroot/CLONE_NEWNET ensures no sockaddr_un is valid
        and seccomp rejects sendto/sendmsg so no sockaddr_un can be used, we only need recvmsg
          still, I'd be happier if sendmsg took a flag saying 'ignore address'
- chroot("/proc/sys/debug/"), chdir("/")
    execveat() accesses the filesystem, which must be neutralized. See the execve section for details.
    While a freshly created and empty directory would work, I'd rather not write to the filesystem at all.
    /proc/sys/debug/ is chosen as root directory since that part of the kernel config is unlikely to be changed,
      and the defaults are public. Still, it is suboptimal.
- setrlimit(RLIMIT_FSIZE)
    to ensure the child doesn't waste too much disk space
    number of files is enforced by the broker
- (Ideally) set cgroup memory.memsw.limit_in_bytes to 100*1024*1024
    to ensure the child doesn't waste too much RAM
    https://www.kernel.org/doc/Documentation/cgroup-v1/memory.txt
    http://man7.org/linux/man-pages/man7/cgroups.7.html
    unfortunately, unsharing UIDs doesn't grant the ability to set up cgroups
    instead, RLIMIT_AS (address space) is set to one gigabyte
      this does nothing against forkbombs, but helps against non-malicious but misbehaving software
- Don't set cgroup cpu.cfs_period_us = 100*1000, cpu.cfs_quota_us = 50*1000
    again, to avoid resource waste
      RLIMIT_CPU doesn't help, I believe it's reset by fork()
    https://www.kernel.org/doc/Documentation/scheduler/sched-bwc.txt
    the broker will enforce that the child doesn't run longer than expected
    but like memsw.limit_in_bytes, cgroups are restricted
- Don't set cgroup blkio.throttle.{read,write}_bps_device
    they require setting for each partition, complex to implement and breaks if a USB drive is added
    slamming the disk serves only to annoy the user and attract attention, the attacker gains nothing
    https://www.kernel.org/doc/Documentation/cgroup-v1/blkio-controller.txt
- Set the lowest possible nice and ionice values
    this yields the same benefits (sandboxes can't exhaust resources for non-sandboxed processes) as the above two, but is available
    and unlike the above, it doesn't prevent them from running if the system is otherwise idle
- (Ideally) set cgroup pids.max to 10
    while the memory limit blocks some things, even a low memory limit can fit a lot of zombie processes; PIDs are a finite resource
    https://www.kernel.org/doc/Documentation/cgroup-v1/pids.txt
    but since cgroups are unavailable, only RLIMIT_NPROC is set
      unfortunately, that one includes root-namespace user's processes too, so the limit is set to 500
- Close fd 5 and everything above
    there should be none, and they should be CLOEXEC even if they exist, but no point taking silly risks
    3 and 4 are the broker socket and emulator fd, opened before clone() (if they were originally something else, dup2 will be used)
    0, 1 and 2 are kept if sandbox policy permits
- prctl(PR_SET_PDEATHSIG, SIGKILL)
    to ensure this process dies if the broker does, taking the children with it
    we must also ensure the sandbox terminates if the parent dies prior to this, otherwise it'd outlive the sandbox
- Send a ping message to the parent
    if parent dies before PR_SET_PDEATHSIG, the child may outlive the sandbox
    to avoid this, the sandbox must check if it has a parent, at some point after setting PDEATHSIG
    the most obvious one would be checking if getppid() is 1, but we're in a pid namespace, so that won't work
    however, we can ask the sandbox monitor if we have a parent
    since the sandbox monitor is the parent, it will reply only if it exists; therefore, any message at all works
      however, we must await the response, to avoid things happening in the order
        'parent dies / PDEATHSIG set / send() returns / parent's fds are closed'
    this is technically unnecessary, since the first post-exec syscall will be to ask the parent to open(/lib64/ld-linux-x86-64.so.2),
      but relying on that makes the security boundaries harder to define, so explicit is better than implicit
- prctl(PR_SET_NO_NEW_PRIVS)
    seccomp requires it (maybe CLONE_NEWUSER is sufficient authorization, but why not, it does no harm)
- prctl(PR_SET_SECCOMP)
    disable a lot of syscalls, for example unshare, chroot and prctl
- Create a new, clean environment block
    the old one contains various items considered private or otherwise unnecessary, such as $SSH_AUTH_SOCK, $LANG and $HOME
- execveat(emulator)
    everything before this (including after PR_SET_SECCOMP) is considered trusted code;
      we know it will execute correctly, or notice the failure and terminate
    everything afterwards is considered untrusted; it may misbehave arbitrarily


Emulator
--------

The untrusted component is also the most complex and interesting one. A good combination, untrusted
 code doesn't need to be audited. It starts as soon as execveat() returns.

Its job is to establish a SIGSYS handler, then hand over control to /lib64/ld-linux-x86-64.so.2. (Or
 another dynamic linker on other architectures, but I've only implemented x86_64.)

As soon as the SIGSYS handler is entered, it must emulate the intended syscall. A few are described
 below; most others are either always allowed, always rejected, or handled similarly to stat().

A minor issue is that it must be fully self-contained, no libc. But the useful parts of libc are
 easily recreated (syscall wrappers - easy to clone; printf - make my own on top of write(), using
 some C++ template tricks; malloc - global variables, or implement my own with mmap).

Another minor issue is that we're namespace init, so we can get grandchildren reparented to us,
 yielding zombies. This can be solved by creating an intermediate process that spawns the real one,
 then reaps children forever; or it can be considered too unlikely and inconsequential to worry
 about. Since zombies only show up if anything daemonizes or otherwise does stupid stuff, this
 sandbox is intended for short-lived utilities like gcc, and zombies aren't very expensive, we
 choose to accept a few of them (but there's a RLIMIT_NPROC in place to block too rampant abuse).


open
----

The easy case: Create a request packet (a constant-size struct), send() it to the broker, recvmsg()
 broker's reply (recv() isn't enough, only recvmsg supports ancillary data like SCM_RIGHTS), and
 return that.

Except it's not that easy; open() can take a relative path. To handle this, the emulator keeps track
 of the current working directory (by intercepting chdir, and getting the initial value from an
 environment variable) and turns relative paths into absolute ones. For simplicity, the sandbox also
 removes all . and .. components.

This means the sandbox may behave funnily if its current working directory is moved, or if you
 follow a symlink then ask for the parent directory, but I can't think of any usecases where this
 would matter.

There are, of course, other file-handling functions, such as stat(). The obvious implementation
 would be teaching the broker about them too, but I want to minimize the attack surface, so it's
 better to implement only open() in the broker; the emulator can implement stat() as open()+fstat().
 It's slower, but safer. (unlink() and access() are also implemented in the broker, they can't be
 properly emulated with open().)


openat, *at
-----------

aka per-thread working directory

This would be solvable with the same mechanism as open()'s cwd, if there is a way to ask the kernel
 where a fd points (this reveals the username, but perhaps that's acceptable). Similarly, fchdir
 requires the same mechanism.

There are many, but they all have drawbacks:
- readlink(/proc/self/fd/123)
    /proc/ isn't available from /proc/sys/debug/
- Mount procfs in our mount namespace, readlink()
    can't prove that's not a security hole
- readlinkat(123, "") or readlinkat(123, ".")
    ENOENT or EINVAL, respectively; the fd itself is a plain directory, . is a hardlink
- get fd to /proc/self, readlinkat(/proc/self, "123")
    how would broker find the right /proc entry? how would seccomp ensure it doesn't end up as readlink(/proc/self/../../etc/passwd)?
- fchdir, getcwd
    allowing fchdir reopens a pile of security holes in execveat
- mmap, read /proc/self/maps
    can't do that on a directory
- Other ways to export this information from the kernel
    the root source is dentry->d_name aka dentry->d_iname
    the only things using this are fs/dcache.c __dentry_path and prepend_path
      these are used mostly by the LSMs, debugging, and some filesystems and other specialized stuff
      the only relevant syscalls I can find are getcwd, readlink and lookup_dcookie
        lookup_dcookie doesn't take fds, but only strange oprofile-related cookies we don't have access to
- Ask broker to readlink it for us
    if broker checks /proc/child/fd/123, how does broker get child PID? how does broker verify child PID? is it a TOCTTOU?
    if broker gets fd, child needs sendmsg, which may be a security hole <https://bugzilla.mozilla.org/show_bug.cgi?id=1066750>
    in both cases, risks leaking the real path of something redirected, potentially leaking the username or other secret data
- A second broker, less privileged than the real one but still able to readlink()
    either it's hackable, in which case its sandbox better be fully secure too, so why not just put that in the child
    or it's not, in which case putting it in main is easier
- Keep track of all fds and where they came from
    not foolproof: breaks if I miss a syscall, across exec(), or if my allocated data structures are too small
    also way too complex

Chosen solution: Ignore it, leave it unimplemented (except openat(AT_FDCWD), which is just plain
  open()). Only a few programs need this function family.
(Programs using openat admittedly includes this one, but nested sandboxes ... let's not.)

There's one tiny additional issue: glibc implements open() as openat(AT_FDCWD) since 2015, so that
 case has to be handled. (This appears to be because newer Linux architectures only support openat
  <https://github.com/bminor/glibc/blob/ee19f1de0d0da24114be/sysdeps/unix/sysv/linux/generic/README>
 and they want to use the same code for everything.)
Easy fix (redirect openat(AT_FDCWD) to open(), reject every other openat()), but it means the
 sandbox has broken due to userspace upgrade, and will likely do so again. I don't think there's
 anything I can do about that, other than keep maintaining the sandbox. Which I have to do anyways,
 unmaintained software is generally not considered secure.


fork
----

fork, isn't this one easy? It requires no filesystem access, and the child already has access to all
 resources created by this one. And it's impossible to emulate without calling that exact syscall.

Answer: fork() itself is fine, but another resource needs cloning: The broker communicator socket.
 Each process needs its own socket, or the broker's replies are sent to wrong child. Therefore, the
 emulator must intercept fork() and create a new communicator socket.

To reissue the syscall without seccomp intercepting it, the emulator passes a nonsensical flag
 combination: CLONE_PARENT_SETTID && ptid==NULL, requesting that the child's TID is stored at
 address NULL in the parent. NULL isn't mapped, so how could the kernel do that? Answer: It doesn't,
 NULL internally means 'PARENT_SETTID wasn't set'. I'm surprised, but happy, it's not rejected with
 EINVAL. (Though I don't know if it'd break in the future.)

There are other possible implementations, such as an always-allowed syscall gate (hard to do with
 ASLR - can't have the sandbox reduce security, can we?) or !CLONE_PARENT_SETTID && ptid==<unlikely
 value> (unlikely means possible, I don't like uncertainity).

The child program could, of course, pass this nonsense flag itself, but that's not a security
 violation; it will only make the program mess up itself.

Threads are currently rejected. It's theoretically possible, just wrap open()/etc in a lock, I don't
 even need to intercept thread creation. But it's currently unimplemented.


execve
------

Leaving execve (initializing the new process) is covered above. However, entering it isn't obvious
 either.

To start with, execve() takes a filename, which must fail, or it'd leak which files exist. Even if
 we can ensure a known-good filename is passed, ELF files can contain a .interp section (usually
 pointing to /lib64/ld-linux-x86-64.so.2), which causes the kernel to access that filename, leaking
 whether it exists. Solution: chroot() to an empty directory.

But if we're in an empty chroot, we don't have access to the emulator either. Solution: fexecve().

Except that's not a syscall; glibc implements it as execve(/proc/self/fd/123), which won't work
 either. I could change the chroot to /proc/self/fd, but that'd probably screw up on fork(). And I'm
 not chrooting to /proc, too high risk of granting access to something unauthorized (kernel command
 line, for example, or the symlink /proc/<broker's PID>/root/ -> /).

Instead, execveat() can be employed. Like other *at() functions, it takes a fd and a path, using the
 fd instead of the current directory if the path is relative. It also supports a flag to execute the
 fd directly, if the path is empty. (I have no idea why nonempty paths are allowed with that flag.)

To ensure the path is empty, the path address must be the last mappable byte in the address space,
 0x00007FFF'FFFFEFFF; either it's "", or it's not a NUL-terminated string and the kernel returns
 -EFAULT. This check isn't necessary security-wise, but violating it shows that the child isn't
 sandbox aware, so like fork(), we're intercepting it for its own sake. (The child can't call
 execveat itself, for the same reasons as openat, but it's a rare syscall.)

Either way, when execve or execveat is intercepted, the emulator asks the broker for an fd
 containing said emulator and executes that, passing the intended program as argv[0]. After that,
 the emulator runs again, installs a new SIGSYS handler, and control is again passed to ld-linux.

Shebangs (and 32/64bit mismatch) make things trickier, as ld-linux can't handle that. It could be
 emulated in userspace, or put on the TODO list and forgotten. I chose the latter.


Symlinks
--------

can grant access to files outside the directly authorized paths. These are considered indirectly
 authorized, and no attempt is made to stop that (it'd be either openat() for each path component,
 or a TOCTTOU).

However, the existence of symlinks blocks another operation: rename(). Moving a relative symlink is,
 of course, not allowed, but checking whether something is a symlink is a TOCTTOU.

Renaming something within the same directory can't grant access to anything new, and cross-directory
 rename could be implemented via open()+linkat() to a temporary target. But, as usual, most programs
 don't need this, so I choose not to.

O_BENEATH would solve most symlink-related issues (other than restricted directories inside public
 ones, which can be "solved" by adding "don't do that" to the docs), but it was never merged.

AT_BENEATH/etc <https://lwn.net/Articles/723057/> seem similar; they claim to not be intended for
 sandboxes, but so does chroot. If it acts the way I need, it's a usable sandbox component. I'll
 take a look once (if) it's merged and my distro updates. The broker is still needed (to count the
 number of writes), but keeping reads in the same process would be a fair bit faster.


inode numbers
-------------

should probably not be exposed via stat() (and I'll admit I forgot them when designing the BPF
 rules), but I believe they're harmless.

Additionally, blocking it would be fairly tricky. First off, teaching the broker about stat() would
 add a bit of complexity - nothing terrible, but good to avoid if possible.

But fstat() is a lot worse. I'd have to either block it (which would break a lot of stuff - for
 example, glibc printf() does fstat(STDOUT_FILENO)), or have the child send the fd to the broker
 somehow (which would require opening sendmsg() to the children, and possibly a few others, with all
 potential security risks that yields).

As such, I choose to accept this leak, much like the vDSO leaking the current time.


Results
-------

This sandbox can compile itself in 1.16 seconds on my computer; on bare metal, it takes 0.97.

While 20% overhead is more than I expected, it is in fact not noticable; all programmers know
 compilers take a while <https://xkcd.com/303>.

And compilers are a quite nasty case; in these 1.16 seconds, it spawns 140 child processes, each of
 which has to load dynamic libraries and #include headers - the broker gets about 37000 requests. If
 anything, 20% is surprisingly little.

I intend to use this sandbox to allow random people to compile and run arbitrary C++ code, like
 <https://godbolt.org/> and <https://wandbox.org/>. However, unlike most public compilers I've seen,
 I intend to put other services on the same server; there will be no 'security by boring'
 <https://github.com/mattgodbolt/compiler-explorer/issues/419>.


Yet one problem remains:

Inventing your own crypto, or inventing your own security in general, is generally not recommended.
 So who am I, and how dare I try to make a security solution? Why would anyone trust this thing?


Vulnerabilities
---------------

Everything contains bugs. The following is a list of possible vulnerabilities that have been fixed
 since I presented it at school.
- filesystem: nonzero max_write was treated as infinite
    exploitability: trivial
    maximum impact: disk space exhaustion - though attacker is assumed able to launch arbitrarily
      many sandboxes, each of which can waste some disk space, so none in practice.
    severity: low, due to the trivial maximum impact
- launch_impl: failure in process::set_fds was ignored
    exploitability: extremely hard, requires resource exhaustion and then some luck
    maximum impact: child may be able to access (including write) some or all fds open in parent
      with the default parent, this does nothing whatsoever, unless called in strange ways where it
        inherits some fds from the shell (extremely rare, and most shell-inherited fds are lock
        files anyways)
      with other parents, it can be data leak, data loss, or even a full jailbreak
    severity: low, due to the infeasible exploitability
- filesystem::grant_native_redir, filesystem::child_file: open() of use-after-free garbage
    exploitability: in theory, takes a while, but perfectly possible
      in practice, innocent programs are very likely to hit these and fail to launch,
        prompting administrator attention
    maximum impact: full sandbox jailbreak
    severity: low, due to the low probability any buggy version was ever successfully deployed
- launch_impl: if parent process dies before its communicator fd, child may survive sandbox termination
    exploitability: extremely hard in theory, requires a race condition during sandbox launch
      and it's 100% impossible to do anything malicious even if you hit this race; the first code
        executed in the child is the emulator, which opens /lib64/ld-linux-x86-64.so.2, fails, and terminates
      and I don't know if the linux kernel allows this race condition in the first place;
        if it tears down fds before processing PR_SET_PDEATHSIG, it doesn't even enter the emulator
    maximum impact: in theory, outliving sandbox termination; in practice, zero
    severity: low, due to being impossible to exploit
- launch_impl: allocation failure could confuse sandbox about whether it's correctly launched
    exploitability: extremely hard, requires resource exhaustion and then some luck
    maximum impact: none; it can make parent wait forever for a nonexistent child process to exit, but child can already sleep forever
    severity: low, due to the trivial maximum impact
- seccomp filter: stat() or fstat() reveals inode numbers
    exploitability: trivial
    maximum impact: per the inode section above, most likely none
    severity: low, due to the trivial maximum impact
    notes: not fixed, security model was changed; however, it did break the stated security model, so still a vulnerability
- seccomp filter: ioctl(TCGETS) and ioctl(TIOCGWINSZ) were allowed
    exploitability: trivial
    maximum impact: reveals some properties of stdout, including whether it's a terminal or redirected
    severity: low, due to the trivial maximum impact
    notes: trivial leaks of non-authorized data is still a sandbox flaw; no clue why I ever allowed them
- Spectre type 2 (indirect jumps)
    exploitability: if I understand it correctly, fully possible for a sufficiently determined attacker
    maximum impact: revealing the full contents of the broker's address space, including the full
      environment (containing, for example, the user ID and username), and (under some parents)
      other private data (possibly including freed data)
    severity: medium; leaking the username is a medium-severity vulnerability
    notes: not fixed yet, I believe this one can only be fixed with a compiler patch
- Other Spectre and Meltdown variants
    not gonna list them separately, they're too many and they affect exactly everything
    exploitability: likely fully possible for a determined attacker
    maximum impact: see documentation for each Spectre variant
    severity: likely no more than medium, but depends on what exactly shows up
    notes: not fixed yet - in fact, it'd surprise me if all variants are even discovered yet

This is a total of 6 low-severity vulnerabilities, and an unknown but nonzero number of hardware
  bugs (Spectre variants) that yield low- or medium-severity vulnerabilities.


Missing kernel features
-----------------------

Each of these would either enable additional functionality, or simplify something existing.
- Clearly document why seccomp-bpf is not a sandbox, and whether it can be used as a component of one
- Clearly document why chroot is not a sandbox, and whether it can be used as a component of one
- Self-restricting resource access to the process tree, somewhere between cgroup and setrlimit
    in particular, I want to restrict the number of processes in the PID or user namespace, everything else can be done with setrlimit
    setrlimit(RLIMIT_NPROC) would be the obvious choice, but it's affected by processes outside the namespace
      http://elixir.free-electrons.com/linux/latest/source/kernel/fork.c#L1564
    setting /proc/sys/kernel/pid_max to 10 would also work, but that too is a global parameter, not per-pid-namespace
- A way to disable the filesystem completely, rather than just chroot to an empty (except . and ..) directory, it feels wrong
    and I can't find an always-existing guaranteed-empty directory (preferably one in a non-weird filesystem, /proc creeps me out)
- Allow execveat(pathname=NULL) and consider it equivalent to pathname="", that final-byte trick is kinda creepy
    the original execveat proposal <https://lkml.org/lkml/2012/9/11/528> used NULL rather than "", it's unclear why it changed
    alternatively, consider execveat(pathname!={NULL,""}, flags=AT_EMPTY_PATH|...) a nonsensical combination and make it fail
      though that is a strange request when I rely on the nonsensical
        combination clone(flags=CLONE_PARENT_SETTID|..., ptid=NULL) working and being a nop
- Slightly less convoluted naming policy in the kernel, these foobar/__foobar pairs confuse me (there's even ___sys_sendmsg)
- MSG_NOADDR in sendmsg(), to block non-NULL msg_name (for use with seccomp-bpf)
- A control mechanism for global disk bandwidth usage - probably available with cgroups, but CLONE_NEWUSER doesn't give me that
- fcntl(F_GETPATH) (like OSX), or similar
- A way to list existing fds in the process without /proc/self/fd
    my favorite interface would be fcntl(F_NEXTFD), which returns the lowest fd >= this (or error if there is none)
- A way to list threads in the process without /proc/self/tasks/, and a way to terminate them
- O_BENEATH or similar; it keeps getting proposed, but never gets merged
    <https://lwn.net/Articles/723057/> <https://lwn.net/Articles/767547/> <https://lwn.net/Articles/788914/>
- A way to read pointed-to arguments from seccomp (would require a massive restructuring of the kernel, but likely worthwhile long-term)


Future - Networking
-------------------

There are, of course, more features that could make sense. For example, perhaps some sandboxed
 children should access the network. This isn't part of this project, but like everything else I've
 chosen to exclude, it's architecturally possible.

Let's list some things to consider if adding this feature.

Obviously, the child can't be allowed access to the entire internet, only a whitelisted set of
 hostnames (and ports on said hostnames). Therefore, the broker must perform both connect() and
 getaddrinfo() - but they're blocking! We can't allow the child to freeze the broker like that, it
 may contain a limit for how long the child is allowed to execute.

Solutions: connect() can be made nonblocking with a setsockopt, and getaddrinfo() is the sequence 
 'open UDP socket, send DNS query, wait for reply, parse reply, return', each of which is (or can be
 made) nonblocking. It requires using a non-libc DNS client, but that's not too terrible.

We also need a way to ensure the child can't saturate the pipe and disrupt everything else trying to
 use the network. I believe this is best solved by wrapping the connection in a AF_LOCAL socketpair
 and letting the broker add a rate limit. This also stops the child from accessing anything
 unauthorized via getpeername. (Not that getpeername would get past seccomp, but defense in depth.)

Additionally, there may be complications in reproducing the high-level 'give me a TCP connection to
 example.com port 80' operation from the child's syscall sequence. I don't expect it to be a too big
 deal, but I haven't analyzed it.


Future - execve
---------------

is still a pretty nasty syscall. I believe it can't access anything harmful, but it tries to,
 necessitating various shenanigans to make sure said attempts fail. It also grants access to vdso,
 four syscalls (clock_gettime, getcpu, gettimeofday, time) the sandbox doesn't really need.

We already emulate parts of the syscall in userspace (preload.cpp). It would be better to emulate
 the rest too and disable the syscall.

Checking the manpage reveals a long list of things execve does. Most are easy to emulate (remove
 signal handlers and sigaltstack), irrelevant (detach SysV shared memory - blocked by seccomp), or
 both (replace /proc/self/cmdline - PR_SET_MM_ARG_START/etc, and not relevant outside debugging).
 Unfortunately, a few are tricky but required: Killing all other threads in the process,
 close-on-exec, and resuming the vfork parent.

Releasing vfork is impossible without execve or _exit. But that syscall is already unsafe; we have
 to return all the way through a signal handler, which won't work the second time vfork returns.
 Instead, vfork is intercepted and redirected to normal fork, which is slower but safe, so this one
 can be ignored.

cloexec is, in theory, easy; just iterate over the file descriptors, fcntl(F_GETFD), close
 everything with cloexec. In practice, which file descriptors are that? The only way to list open
 file descriptors on Linux is /proc/self/fd/, which isn't available. We can't ask the broker for a
 list either; while we can tell it our PID with SOCK_PASSCRED, using it would be a TOCTTOU. Another
 option would be reducing RLIMIT_NOFILE and iterating over them, but that's ugly, I'd rather not.

However, there is one possibility: Place a process in the sandbox's PID namespace, but otherwise
 unrestricted (no seccomp, no chroot), and ask it for /proc/getpid()/fd/. Since it's in the sandbox
 namespace, inappropriate PIDs would only leak information about other sandboxed processes. (Making
 this the namespace init, PID 1, would be the easiest solution; as mentioned above, this would also
 allow it to reap zombies.)

Killing the other threads is also tricky. (Threads aren't implemented yet, but they still need to be
 considered.) There's no syscall to list them, nor is there any obvious way to terminate them - even
 with tgkill(), sending SIGKILL terminates the entire process.

But, again, it's possible: A list of threads can be found in /proc/getpid()/task/ and fetched by the
 namespace init, and we could send a custom signal that causes any recipient thread to terminate
 itself. SIGSYS, for example, we already have a SIGSYS handler. Signals can be blocked, but only if
 sigprocmask() can get past seccomp, which it can't. And exec in multithreaded programs at all is
 rare - even if a multithreaded program wants to exec something, it usually does fork() first, which
 only clones the calling thread.

(A complication would be avoiding races between cloexec and open() in another thread; easy to fix,
 just kill the threads first. Another issue is races between clone() and thread killing; to fix that
 one, repeatedly kill all threads, until /proc/getpid()/task/ contains only one entry.)

However, it's a lot of effort, and it wouldn't accomplish much. It makes the chroot unnecessary (ELF
 .interp can leak file existence, disabling that would be an improvement), but no paths exist in the
 chroot, so it wouldn't accomplish much. (And I'd include the chroot anyways, defense in depth.)


Future - Sandbox-aware children
-------------------------------

As much fun as it is to sandbox gcc, sandbox-aware children can also do some nifty stuff, like
 sharing memory with an unconstrained process (which could be the broker).

For example, there is a system called Libretro <https://www.libretro.com/>; a large number of
 emulators and other games have been rewritten to be shared libraries ("cores"), under a consistent
 API that can be used by various programs ("frontends"). Obviously, there's a lot of attack surface
 in the cores (and the core itself could be malicious), but if the cores are sandboxed, said attack
 surface becomes irrelevant.

It's not finished, but the sandbox architecture allows it.


Future - Sandboxing of OpenGL
-----------------------------

Some Libretro cores want to use OpenGL. This requires access to X11 (or Wayland, but I'll ignore
 that, at least for now). Obviously, unrestricted X11 access is unacceptable - for example, the
 child could use XTEST to send ctrl-alt-T to open a terminal, then type whatever it wants and have
 it executed outside the sandbox.

Therefore, the broker must monitor all communication. Which means it must avoid the host's installed
 GPU drivers, since the ABIs are very complex, poorly documented, likely a race condition to try to
 audit, and possibly not stable.

Luckily, X11 (including GLX) supports running across the network. If the child thinks this is done,
 all communication will be done over a single TCP socket, which the broker can easily audit. (Yes,
 this is slower, I don't think safe and fast is possible. Maybe with Wayland, but who knows. Or
 maybe I should check what Flatpak does.)

To enable networking mode, I can set the env DISPLAY=127.0.0.1:123. But on the other hand, since
 this won't go to the connect() syscall, there's no reason to use localhost, and it wouldn't
 surprise me if localhost is special cased somehow. Instead, I'd rather use some other IP address;
 for example, 203.0.113.1 (from TEST-NET-3) would be usable. (While it would be tempting to skip
 DISPLAY and the X protocol completely, glFlush() ends up sending GLX opcodes, so I need Xlib to
 think it's talking to an X server.)

To reduce the amount of information the child can access, the child won't create a toplevel window;
 it will only create a child window using the XEmbed protocol, and the parent will aggressively
 censor irrelevant data. This also allows the parent to draw its own stuff (for example a 'disable
 sandbox' button), which the child can't interfere with, or even know about.

Another solution would be to implement a custom GL wrapper protocol, but the GL API is HUGE so let's
 not.

Vulkan would also be desirable, but I don't know how the Vulkan-kernel interfaces look.


Future - Removing the unprivileged namespaces
---------------------------------------------

Unprivileged user namespaces are enabled by default in some distros, like Ubuntu - but disabled by
 default in others, like Debian. It would be better if the sandbox could be used on all distros.

The namespaces are used to restrict user ID in auxv, to enforce the termination of all sandboxed
 processes upon sandbox exit, to grant access to chroot(), and possibly a few other operations. (I
 also want to add cgroup support, but that seems to require real root.)

To remove namespaces without compromising security, the above must be restricted in another way.
auxv can only be cleaned post-execveat(), at which point code execution cannot be trusted, so that
 syscall must be banned or emulated. The latter is (as discussed above) impossible without
 namespaces, so a namespace-less sandbox would be unable to run a significant fraction of programs.
 (However, the ones it can run may still be interesting.)
Terminating grandchildren is also necessary, and must be emulated somehow. Not only is CLONE_NEWPID
 the easiest way, but I couldn't find any other way at all. CLONE_THREAD confuses waitpid(), and
 blocks execveat() by indirectly requiring CLONE_VM; PR_SET_CHILD_SUBREAPER looks somewhat
 promising, but judging by the readme of <https://github.com/rootmos/dont-fear-the-reaper>, the
 desired result (terminating the subreaper causes termination of its children) will not happen. It
 is possible to loop over all children and terminate them prior to the broker terminating, but only
 if said code always runs - and since the broker cannot be assumed crash-free, that's an
 unacceptably big 'if'. This leaves only one option - disable fork(), further restricting the set of
 runnable children.
chroot is only used to disable the filesystem so execveat .INTERP doesn't do anything strange, and
 since execveat() must be banned for other reasons, no further consideration is needed. It is one
 less layer of defense-in-depth, but one solid layer is enough anyways.

Therefore, a namespace-less sandbox would be able to run a few programs, but significantly fewer
 than a namespace-enabled one. It would also be a fair bit of code, for a fairly small usecase:
 according to <https://wiki.mozilla.org/Security/Sandbox/Specifics>, 88% of Firefox users have
 access to unprivileged user namespaces; this is biased towards desktop Linux, which may or may not
 reflect the usecases for my sandbox, but I believe it's safe to assume a large majority of users
 support unprivileged namespaces.

However, removing namespaces isn't the only way to remove unprivileged namespaces. The alternative
 is adding privilege, i.e. setuid. This would require
- splitting the launch to a separate process and making it setuid
- changing effective user ID to caller's UID, to avoid pointless risks (for example, I believe the
    OOM killer deprioritizes root)
- fiddling with capset() to gain CAP_SYS_ADMIN, CAP_SETUID and CAP_SETGID (alternatively setting
    SECBIT_KEEP_CAPS before setting EUID)
- clone(CLONE_NEWPID|CLONE_NEWUSER|etc) (which clears capabilities in the calling user namespace,
    leaving the new child fully unprivileged)
- parent: send child's PID to broker
- broker: accept PID; reject any further attempts to set PID, only the first one is trustworthy
- child: await confirmation from broker that child's PID has been recorded, to avoid race conditions
    (CLONE_STOPPED would work just as well, but it was deleted in Linux 2.6.38 for some unclear
    reason)
- child: apply other launcher restrictions, then execve the emulator (otherwise, the setuid binary
    could be abused to exploit the exact vulnerabilities Debian's restrictions are trying to protect
    from - with the sandbox's restrictive seccomp rules, I believe no vulnerabilities are available)
which is easier to implement than removing namespaces, and can run as much as the normal one.
 However, it would complicate installation, and, like removing namespaces, would only benefit a
 small fraction of users.

Conclusion: The best course of action is most likely to keep the hard dependency on user namespaces,
 and (together with the web browsers) keep asking distros to unrestrict namespaces. Perhaps they
 would be receptive to a patch blocking unprivileged user namespaces from creating other namespaces;
 from what I can gather, most of the vulnerabilities are in the network namespace, which I only use
 as a dispensable defense-in-depth mechanism (though Firefox seems to actually need it).


Future - Cooperating with ld-linux
----------------------------------

The emulator would work better if it could live in the same world as the other libraries, rather
 than sitting below it. For example, if it could LD_PRELOAD the syscall wrappers rather than
 catching SIGSYS, it'd be a little faster (not much, but not zero), and having a proper malloc would
 be useful.

Unfortunately, ld-linux itself needs the emulator, so I'd still need an emulator that supports
 glibc-less operation. And since I can't force my LD_PRELOAD to run before other libraries'
 constructors, this one would have to support pretty much every syscall, and I'd rather not
 implement that twice. LD_AUDIT could help, but that would load two copies of glibc in the same
 process, and I don't know how well that works. There is code for it in glibc, but it's likely badly
 tested, and I don't know how much I can trust it.

It would be nice, but again, not worth it.


Appendix - Trivia
-----------------

Unrelated but potentially interesting stuff I found while creating this program.

Windows policy on per-process hibernation: https://blogs.msdn.microsoft.com/oldnewthing/20040420-00/?p=39723
Linux policy on per-process hibernation: https://criu.org/

Windows policy on per-process ASLR: https://blogs.msdn.microsoft.com/oldnewthing/20160413-00/?p=93301
Linux policy on per-process ASLR: https://security.stackexchange.com/a/58669
