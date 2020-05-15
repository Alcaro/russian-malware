#object filename structure:
#obj/RULE___OBJNAME___PATH.o
#RULE is the set of build flags for this file, probably DEFAULT or ARLIB
#OBJNAME is the OBJNAME= make parameter, or a default value
#PATH is the source filename, including extension, relative to project root, with slashes replaced with double underscore
#example: obj/ARLIB___linux___arlib__file-posix.cpp.o

#to create a profiled build:
#make clean
#make OPT=1 PROFILE=gen
#./program --do-expensive-operations=true
#make clean-prof
#make OPT=1 PROFILE=use

#TODO: if a config script is needed,
#obj/config-$(OBJNAME).mk:
#	truncate -s0 obj/config-$(OBJNAME).mk
#	
#	pkg-config --exists gtk+-3.0 && \
#		echo ARGUI := gtk3 >> obj/config-$(OBJNAME).mk && \
#		echo CFLAGS += `pkg-config --cflags gtk+-3.0` >> obj/config-$(OBJNAME).mk || \
#		true
#
#and find some way to auto include the correct one
#(config depends on platform, so it must have OBJNAME)

all: arlib-default

MAKEFLAGS := Rr
.SUFFIXES:
.SECONDEXPANSION:
.DELETE_ON_ERROR:

ifeq (,$(wildcard arlib/string.cpp))
  $(error wrong build directory, go up one level)
endif

CC = gcc
CFLAGS =
CXX = g++
CXXFLAGS = $(CFLAGS)
LD = g++
LFLAGS =
AS = as
ASFLAGS =
AR = ar

SPACE :=
SPACE +=

define LF


endef

ifeq ($(OS),Windows_NT)
  OS = windows
endif
ifeq ($(OS),)
  dumpmachine := $(shell $(CXX) -dumpmachine)
  ifneq ($(findstring mingw,$(dumpmachine)),)
    OS := windows
  else ifneq ($(findstring linux,$(dumpmachine)),)
    OS := linux
  else
    $(error unrecognized OS $(dumpmachine))
  endif
endif

#ifeq ($(OS),Windows_NT)
#  OS = windows
#else
#  UNAME_S := $(shell uname -s)
#  ifeq ($(UNAME_S),Linux)
#    OS = linux
#  else ifeq ($(UNAME_S),Darwin)
#    OS = osx
#  else ifeq ($(UNAME_S),)
#    $(error couldn't execute uname -s)
#  else
#    $(error unrecognized OS $(UNAME_S))
#  endif
#endif

ifneq ($(filter $(MAKECMDGOALS),test test-all test-all-twice test-cg check),)
  SELFTEST = 1
  EXCEPTIONS = 1
endif

ARTYPE ?= exe
ARGUI ?= 0
AROPENGL ?= 0
AROPENGL_D3DSYNC ?= 1
ARTHREAD ?= 0
ARSANDBOX ?= 0
ARWUTF ?= 0
ARSOCKET ?= 0
ARSOCKET_SSL ?= openssl

OPT ?= 0
ifeq ($(OPT),0)
  DEBUG ?= 1
else
  DEBUG ?= 0
endif

CCXXFLAGS = -fvisibility=hidden -Wall -Wmissing-declarations -Wimplicit-fallthrough -Wvla -Walloca -pipe
#to silence for (size_t unused : range(42)) - delete this once gcc starts making more sense
CCXXFLAGS += -Wno-unused-variable -Wno-unused-but-set-variable
#doesn't seem to do anything
#CFLAGS += -Wmissing-prototypes
ifeq ($(EXCEPTIONS),1)
  OPTFLAGS_CXX += -fno-enforce-eh-specs
  ifeq ($(OS),windows)
    # these eat a few hundred kilobytes, but better embedded than DLL
    CONF_LFLAGS += -static-libgcc -static-libstdc++
  endif
else
  CCXXFLAGS += -fno-exceptions
  OPTFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
endif

HOST_OS := unix

CC_FAMILY := gcc
ifneq ($(findstring clang,$(CC) $(CXX) $(LD)),)
  CC_FAMILY := clang
  CC = clang
  CXX = clang++
  LD = clang++
endif

#double gcc bug combo:
#(1) GCC hates this pattern:
#//#define foo(a,b,c) \
#//  bar(a) \
#//  bar(b) \
#//  bar(c)
# to my knowledge unreported
#(2) '#pragma GCC diagnostic ignored "-Wcomment"' does nothing
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53431
CCXXFLAGS += -Wno-comment
#every Arlib class is memmovable, I'm not interested in 99999 gcc warnings
ifeq ($(CC_FAMILY),gcc)
  CONF_CXXFLAGS += -Wno-class-memaccess
endif

EXESUFFIX =
CONF_CXXFLAGS += $(CONF_CFLAGS)

ifeq ($(OS),linux)
  CONF_LFLAGS += -ldl -Wl,-z,relro,-z,now,--as-needed,--no-undefined
  ifeq ($(ARTYPE),dll)
    CONF_CFLAGS += -fPIC
    CONF_LFLAGS += -shared
    EXESUFFIX = .so
  endif
  ifeq ($(ARTYPE),hybrid)
    CONF_CFLAGS += -fpie
    CONF_LFLAGS += -pie -rdynamic
  endif
  ifeq ($(ARTHREADS),1)
    CONF_CFLAGS += -pthread
    CONF_LFLAGS += -pthread
  endif
  OBJNAME := $(OBJNAME)linux
  TR = valgrind --num-callers=100 --leak-check=full
  ifeq ($(ARGUI),1)
    # Gtk leaves over 9000 'possibly lost' objects around. not interested
    TR += --show-possibly-lost=no
    #envs G_SLICE=always-malloc G_DEBUG=gc-friendly look useful, but don't make any difference in practice
  endif
endif
ifeq ($(TR),gdb)
  override TR = gdb --args
endif

ifeq ($(OS),windows)
  EXESUFFIX = .exe
  ifeq ($(ARTYPE),dll)
    EXESUFFIX = .dll
    CONF_LFLAGS += -shared
  endif
  ifeq ($(ARTYPE),hybrid)
    # an exe can export symbols, but its imports are ignored, so trying to call malloc() just segfaults
    # DllMain and TLS callbacks are ignored too, so imports cannot be resolved that way
    # marking i as native subsystem (not win32) may work, but that has 99999 side effects
    # tested on both XP and Wine
    # https://github.com/wine-mirror/wine/blob/ba1495f7c2c3cff3db9ce99600bfc8ddfa365387/dlls/ntdll/loader.c#L2563
    # todo list to lift this restriction:
    # - make my own process startup code
    #    probably fairly straightforward, just tedious
    #    if process startup is a lot trickier than dll startup, I could use mingw for the exe path and homemade for the dll path
    # - find the address of LoadLibrary and GetProcAddress from what's effectively shellcode
    #    also seems tedious but not hard
    #    grab PEB via TEB
    #    inspect LdrData to find kernel32 or ntdll (ntdll if imports are utf16, kernel32 if something else)
    #    parse its export table
    # - need to figure out destructors; both global variables, and dlls loaded by this one
    #    ntdll!LdrRegisterDllNotification probably
    # - each valid first-call entry point must call the process startup code
    $(error unsupportable configuration)
  endif
#  OBJ += obj/resource$(OBJNAME).o
#  RC = windres
#  RCFLAGS =
#obj/resource$(OBJNAME).o: ico/*
#	$(RC) $(RCFLAGS) ico/minir.rc obj/resource$(OBJNAME).o
  OBJNAME := $(OBJNAME)windows
#surprisingly, this is still not default as of mingw 8.1.0 (to check, objdump -x | grep DllCharacteristics, 0x0100 bit is nxcompat)
  CONF_LFLAGS += -Wl,--nxcompat
  ifeq ($(ARXPSUPPORT),1)
    DEFINES += _WIN32_WINNT=_WIN32_WINNT_WINXP
  else
    # for random.cpp
    CONF_LFLAGS += -lbcrypt
  endif

  ifeq ($(shell echo ^^),^)
    HOST_OS := windows
  else
    # building non-windows binaries on a windows host is not supported (Cygwin or WSL compiling for themselves will probbaly work)
    # why would you do such a thing, anyways?
    $(warning non-cmd windows detected; if SHELL $(SHELL) is something other than sh.exe, makefile should check for that instead)
  endif
endif

ifeq ($(V),)
  Q = @
  ECHOQ = @echo
else
  Q =
  ifeq ($(HOST_OS),windows)
    ECHOQ = @ver>nul
  else
    ECHOQ = @true
  endif
endif

OPTFLAGS += -Os -fomit-frame-pointer -fmerge-all-constants
OPTFLAGS += -ffunction-sections
#inhibits some optimizations, and more importantly, turns .bss into .data on Windows
#OPTFLAGS += -fdata-sections
OPTFLAGS += -fno-ident
OPTFLAGS += -DNDEBUG -DARLIB_OPT
OPTFLAGS_CXX += -fno-threadsafe-statics
OPTLFLAGS := -Wl,--gc-sections,--build-id=none,--relax,--sort-common
ifneq ($(OS),windows)
OPTLFLAGS += -Wl,--hash-style=gnu
endif
#OPTLFLAGS += -Wl,--print-map
ifneq ($(DEBUG),1)
  #DEBUG means still working on it, so -Werror isn't appropriate yet
  #unused variables, bad indent, etc is super common in WIP code
  OPTFLAGS += -Werror
  ifneq ($(STRIP),0)
    ifeq ($(SELFTEST),)
      OPTLFLAGS += -s
    endif
  endif
endif

CFLAGS += -g
ifneq ($(OPT),0)
  CONF_CFLAGS += $(OPTFLAGS)
  CONF_CXXFLAGS += $(OPTFLAGS_CXX)
  CONF_LFLAGS += $(OPTLFLAGS)
  ifeq ($(OPT),spd)
    CONF_CFLAGS += -O3
  endif
  OBJNAME := $(OBJNAME)-opt
else
  #OPT=1 DEBUG=1 should prioritize optimization over debugging; OPT=1 DEBUG=1 then 'strip' should be identical to OPT=1 DEBUG=0
  #so this one is gated on OPT=0, not DEBUG=1
  CFLAGS += -DDEBUG
endif

ifeq ($(PROFILE),gen)
  CONF_CFLAGS += -fprofile-generate
  CONF_LFLAGS += -lgcov
endif
ifeq ($(PROFILE),use)
  CONF_CFLAGS += -fprofile-use -fprofile-correction
endif

CONF_CFLAGS += -DARLIB_OBJNAME=$(OBJNAME)

OUTNAME = $(PROGRAM)$(EXESUFFIX)

#OBJMANGLE(rule,sources) - takes C/C++ source files and returns the mangled name under the specified rule
OBJMANGLE = $(patsubst %,obj/$1___$(OBJNAME)___%.o,$(subst /,__,$2))
#SOURCENAME(obj) - takes a .o file, returned from OBJMANGLE, and returns the corresponding source file
#does not handle multi-file inputs, use $(foreach)
SOURCENAME = $(strip $(subst __,/,$(lastword $(subst ___,$(SPACE),$(patsubst obj/%.o,%,$1)))))
#DOMAINNAME(obj) - takes a .o file, returned from OBJMANGLE, and returns the corresponding domain
DOMAINNAME = $(firstword $(subst ___,$(SPACE),$(patsubst obj/%.o,%,$1)))

CFLAGS_DEFAULT ?=
CFLAGS_NOWARN += -w

SOURCES += *.cpp
SOURCES_ARLIB := arlib/*.cpp

ifneq ($(SELFTEST),)
  CONF_CFLAGS += -DARLIB_TEST
  CFLAGS_ARLIB += -UARLIB_TEST -DARLIB_TESTRUNNER
  ifneq ($(ARLIB_MAIN),)
    CFLAGS_ARLIB += -DARLIB_TEST -DARLIB_TEST_ARLIB
  endif
  OBJNAME := $(OBJNAME)-test
endif

ifeq ($(ARGUI),1)
  SOURCES_ARLIB += arlib/gui/*.cpp
  ifeq ($(OS),windows)
    DEFINES += ARGUI_WINDOWS
    CONF_LFLAGS += -lgdi32 -lcomctl32 -lcomdlg32
  endif
  ifeq ($(OS),linux)
    DEFINES += ARGUI_GTK3 ARGUIPROT_X11
    CONF_CFLAGS += $(shell pkg-config --cflags gtk+-3.0)
    CONF_LFLAGS += -ldl -lX11 $(shell pkg-config --libs gtk+-3.0)
    ifneq ($(OPT),0)
      # don't know if this has ever caught anything except NULLs, but they're harmless on debug
      # but they do waste space on release builds.
      CONF_CFLAGS += -DG_DISABLE_CAST_CHECKS
    endif
  endif
else
  DEFINES += ARGUI_NONE
endif

ifeq ($(AROPENGL),1)
  ifeq ($(ARGUI),0)
    $(error can't use OpenGL without the GUI)
  endif
  SOURCES_ARLIB += arlib/opengl/*.cpp
  DEFINES += ARLIB_OPENGL
  ifeq ($(OS),linux)
    CONF_LFLAGS += -ldl
  endif
  ifeq ($(AROPENGL_D3DSYNC),1)
    DEFINES += AROPENGL_D3DSYNC
  endif
obj/glsym-slim.h:
	python3 arlib/opengl/generate.py used aropengl obj/glsym-slim.h
ifneq ($(OPT),0)
arlib.h: obj/glsym-slim.h
endif
endif

ifeq ($(ARTHREAD),1)
  SOURCES_ARLIB += arlib/thread/*.cpp
  DEFINES += ARLIB_THREAD
  ifeq ($(OS),linux)
    CONF_CFLAGS += -pthread
    CONF_LFLAGS += -pthread
  endif
endif

ifeq ($(ARSANDBOX),1)
  include arlib/sandbox/Makefile
endif

ifeq ($(ARWUTF),1)
  SOURCES_ARLIB += arlib/wutf/*.cpp
  DEFINES += ARLIB_WUTF
endif

ifeq ($(ARSOCKET),1)
  SOURCES_ARLIB += arlib/socket/*.cpp
  DEFINES += ARLIB_SOCKET
  
  ifeq ($(OS),windows)
    CONF_LFLAGS += -lws2_32 -liphlpapi
    ifdef ARSOCKET_SSL_WINDOWS
      ARSOCKET_SSL := $(ARSOCKET_SSL_WINDOWS)
    endif
  endif
  
  #TODO: allow using multiple at once, the tests could use that
  ifeq ($(ARSOCKET_SSL),no)
    #no SSL
    #socketssl will still be available at compile time, but fails linking
  else ifeq ($(ARSOCKET_SSL),schannel)
    CFLAGS_ARLIB += -DARLIB_SSL_SCHANNEL
    #TODO: check if this is still needed
    CONF_LFLAGS += -lsecur32
  else ifeq ($(ARSOCKET_SSL),openssl)
    CFLAGS_ARLIB += -DARLIB_SSL_OPENSSL
    CONF_LFLAGS += -lssl -lcrypto
  else ifeq ($(ARSOCKET_SSL),gnutls)
    CFLAGS_ARLIB += -DARLIB_SSL_GNUTLS
    CONF_LFLAGS += -lgnutls
  else ifeq ($(ARSOCKET_SSL),bearssl)
    CFLAGS_ARLIB += -DARLIB_SSL_BEARSSL
    SOURCES_ARLIB += arlib/socket/bear-ser.c
    #update make superclean if changing this
    OBJS_LATE += arlib/deps/bearssl-0.6/build/libbearssl.a
arlib/deps/bearssl-0.6/build/libbearssl.a:
	$(MAKE) -C arlib/deps/bearssl-0.6/ CFLAGS='$(OPTFLAGS) -fpic' lib
  else
    $(error unknown SSL library)
  endif
endif

ifneq ($(wildcard resources/*),)
  SOURCES_ARLIB += obj/resources.cpp
obj/resources.cpp: arlib/rescompile.py resources/ resources/*
	python3 arlib/rescompile.py
arlib.h: obj/resources.cpp
endif

SOURCES_NOWARN += arlib/deps/miniz.c

CONF_CFLAGS += $(patsubst %,-D%,$(DEFINES))

TRUE_CFLAGS := -std=c99 $(CCXXFLAGS) $(CONF_CFLAGS) $(CFLAGS)
# C++ version policy: I will switch to a newer version of C++ only if it adds something that
# - considerably simplifies existing code (everything is doable without that, but some things are painful)
# - works on the default compiler on current Debian stable, latest Ubuntu LTS, whatever compiler I use for Windows
#    (currently mingw-w64 8.1.0), and whatever other computers I have at the moment
#    working on additional compilers (for example Clang) is recommended, but not mandatory
# - is functional and non-experimental in a finalized -std= version (c++11 is final, c++0x isn't)
#    it's fine if not every part of that language version is supported in that compiler version
# I will not switch C++ version because a dependency needs that. Instead, I'll fork or reinvent the dependency.
# The following features would be useful; if enough of them show up, I will switch:
#   C++2a template lambdas (for the PNG decoder)
#   C++2a designated initializers (foo_t foo = { .a = 42, .b = 123 })
#   C++2a template string literals (for the regex parser, and so I can create a string formatting function)
#   C++2a concepts (also for the regex parser)
#   C++2a consteval (still for the regex parser - that template mess is terrible, I want it as normal code)
#   C++2a down with typename http://wg21.link/p0634r3
#   C++2a [[likely]]
#   C++2a [[no_unique_address]]
#   C++2a ban non-twos-complement signed
#   C++2a void foo(auto arg) (shorthand for template<typename T> void foo(T arg) - simplifies function::bind_member)
#   C++2a coroutines, for runloop-related stuff
#   C++17 <charconv> in libc (not libstdc++) - will speed up tostring(float), and probably simplify it too
#   [[trivially_relocatable]] http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1144r3.html
#   Lambdas without braces/return; function<int(int)> mul2 = [](int i) => i*2; (useful for LINQ)
#   Zero-overhead deterministic exceptions http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0709r0.pdf
#   Compile-time reflection (that SERIALIZE macro is a little too limited, and the error messages are worse than from the regexes)
#   Single-implementation interfaces (public functions and nothing else in header, member variables hidden; useful for the GUI classes)
#     the $class proposal P0707 would most likely permit this, as well as most or all of the reflection I want
#   Demote or, and, bitand, etc from keyword to predefined macro (or even better, banish them to the <ciso646> they should've never left)
#   std::embed http://open-std.org/JTC1/SC22/WG21/docs/papers/2018/p1040r0.html, to get rid of rescompile.py's assembly shenanigans
#     may allow some other optimizations/simplifications too
#   Allow int x(int y=0); int x(int y=0) { return y*2; } (only if default value is identical on both ends, of course)
#   Ranges TS - not because I want it as is, but because it'll get rid of the unused variable warning from for (size_t n : range(42))
# The following may or may not be usable, depending on the exact details:
#   C++2a modules (it's very easy to produce a bad module system, I don't know if this one avoids the pitfalls)
# Other stuff I've seen proposed is uninteresting, though I haven't seen all proposals.
# I'm not interested in library features. Most of them lack important functionality, have bad constant factors, and bloat the binary.
# The compiler-magic ones, like std::move and type traits, do one thing and do it well, and optimize to nothing. I use them.
TRUE_CXXFLAGS := -std=c++17 -fno-rtti $(CCXXFLAGS) $(CONF_CXXFLAGS) $(CXXFLAGS)
TRUE_LFLAGS := $(LFLAGS) -fvisibility=hidden $(CONF_LFLAGS)

obj:
	mkdir obj

ifeq ($(HOST_OS),windows)
clean:
	del /q obj\*

clean-prof:
	del /q obj\*.o
else
clean:
	rm -f obj/*

clean-prof:
	rm -f obj/*.o

superclean: clean
	rm -f arlib/deps/bearssl-0.6/build/obj/*
	rm -f arlib/deps/bearssl-0.6/build/libbearssl.a
endif

#ARLIB_H := $(patsubst %,\#define %$(LF),$(DEFINES))
#arlib.h: Makefile
#	$(eval export ARLIB_H)
#	echo "$$ARLIB_H" > arlib.h
#	echo '#include "arlib/arlib.h"' >> arlib.h


DOMAINS += DEFAULT NOWARN
#anything in the ARLIB domain goes in an ar archive, to be unpacked immediately
#this discards unneeded object files, including global constructors and unneeded -l dependencies, which --gc-sections doesn't touch
#therefore, global constructors in Arlib are prohibited from having any effect outside their own .cpp file
#(does not apply to the main program, nor to tests)
#--start-lib could replace this .a on gold and lld, but ld.bfd doesn't support that option
USE_AR_OPT := 0
ifeq ($(SELFTEST),)
 ifneq ($(OPT),0)
  USE_AR_OPT := 1
 endif
endif
ifeq ($(USE_AR_OPT),0)
 DOMAINS += ARLIB
endif

SOURCES_DEFAULT := $(SOURCES)
wild2 = $(or $(wildcard $(word)),$(word))
OBJS += $(foreach domain,$(sort $(DOMAINS)),$(call OBJMANGLE,$(domain),$(foreach word,$(SOURCES_$(domain)),$(wild2))))
OBJS := $(sort $(OBJS))
ifeq ($(USE_AR_OPT),1)
 # .a are order sensitive, do not rearrange
 OBJS += obj/$(OBJNAME)-arlib.a
endif
OBJS += $(sort $(OBJS_LATE))

arlib-default: $(OUTNAME)


obj/%.c.o: $$(call SOURCENAME,$$@) arlib.h | obj
	$(ECHOQ) CC $<
	$(Q)$(CC) $(TRUE_CFLAGS) $(CFLAGS_$(call DOMAINNAME,$@)) -c $< -o $@
obj/%.cpp.o: $$(call SOURCENAME,$$@) arlib.h | obj
	$(ECHOQ) CXX $<
	$(Q)$(CXX) $(TRUE_CXXFLAGS) $(CFLAGS_$(call DOMAINNAME,$@)) -c $< -o $@
obj/%.S.o: $$(call SOURCENAME,$$@) arlib.h | obj
	$(ECHOQ) AS $<
	$(Q)$(AS) $(TRUE_ASFLAGS) $(ASFLAGS_$(call DOMAINNAME,$@)) $< -o $@
#do not make this rule generic (obj/%.a.o), it hits some ridiculous unkillable 'chain of implicit rules'
# clause that deletes and refuses to rebuild object files
#https://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
#if genericity is needed, stamp out the rules with eval
obj/$(OBJNAME)-arlib.a: $(foreach src,$(foreach word,$(SOURCES_ARLIB),$(wild2)),$(call OBJMANGLE,ARLIB,$(src)))
	$(ECHOQ) AR $@
	$(Q)$(AR) rcs $@ $+

$(OUTNAME): $(OBJS)
	$(ECHOQ) LD $@
	$(Q)$(LD) $+ $(TRUE_LFLAGS) -o $@ -lm



ifneq ($(SELFTEST),)
obj/arlibtest$(EXESUFFIX): $(OBJS)
	$(ECHOQ) LD $@
	$(Q)$(LD) $+ $(TRUE_LFLAGS) -o $@ -lm

ifneq ($(TF),)
  TESTFILTER := --filter="$(TF)"
endif
test: obj/arlibtest$(EXESUFFIX)
	$(TR) obj/arlibtest$(EXESUFFIX) $(TESTFILTER)
check: test
test-all: obj/arlibtest$(EXESUFFIX)
	$(TR) obj/arlibtest$(EXESUFFIX) --all $(TESTFILTER)
test-all-twice: obj/arlibtest$(EXESUFFIX)
	$(TR) obj/arlibtest$(EXESUFFIX) --all --twice $(TESTFILTER)
test-cg: obj/arlibtest$(EXESUFFIX)
	-rm obj/callgrind.log
	valgrind --tool=callgrind --dump-instr=yes --callgrind-out-file=obj/callgrind.log obj/arlibtest$(EXESUFFIX) $(TESTFILTER)
	kcachegrind obj/callgrind.log
endif



ifeq ($(MAKECMDGOALS),me a sandwich)
.PHONY: me a sandwich
me a:
	@:
sandwich:
	@[ `id -u` -ne 0 ] && echo "What? Make it yourself." || echo Okay.
endif
