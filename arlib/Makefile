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

ifneq (,)
  $(error conditionals are required; only GNU Make is supported)
panic:
	echo "conditionals are required; only GNU Make is supported"
	false
endif

ifeq (,$(wildcard arlib/string.cpp))
  $(error wrong build directory, go up one level)
endif

ifneq ($(ARMAKEHEAD),1)
  include arlib/Makefile-head
endif

ARTYPE ?= exe
ARGUI ?= 0
AROPENGL ?= 0
AROPENGL_D3DSYNC ?= 1
ARTHREAD ?= 0
ARWUTF ?= 0
ARSOCKET ?= 0
#ARSOCKET_SSL ?= openssl # leave them unset, there are some ?= further down
#ARTERMINAL ?= auto
ARXPSUPPORT ?= 0

ifeq ($(OS)-$(WINBITS)-$(ARXPSUPPORT),windows-32-32)
  ARXPSUPPORT = 1
endif
ifeq ($(OS)-$(WINBITS)-$(ARXPSUPPORT),windows-64-32)
  ARXPSUPPORT = 0
endif

CCXXFLAGS = -Wall -Wmissing-declarations -Wimplicit-fallthrough -Wvla -pipe
CONF_LFLAGS += -pipe
#to silence for (size_t unused : range(42)) - delete this once gcc stops complaining
CCXXFLAGS += -Wno-unused-variable
ifeq ($(CC_FAMILY),gcc)
  # Clang doesn't support those
  CCXXFLAGS += -Walloca -Wno-unused-but-set-variable
endif
#doesn't seem to do anything
#CFLAGS += -Wmissing-prototypes
ifeq ($(EXCEPTIONS),1)
  DEFINES += ARLIB_EXCEPTIONS
  OPTFLAGS_CXX += -fno-enforce-eh-specs
  ifeq ($(OS),windows)
    # these eat a few hundred kilobytes, but better embedded than DLL
    CONF_LFLAGS += -static-libgcc -static-libstdc++
  endif
else
  CCXXFLAGS += -fno-exceptions
  OPTFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
endif

#double gcc bug combo:
#(1) GCC hates commented-out backslash line continuations, even if the next line is commented out too, for example
#//#define foo(a,b,c) \
#//  bar(a) \
#//  bar(b) \
#//  bar(c)
# to my knowledge unreported
#(2) '#pragma GCC diagnostic ignored "-Wcomment"' does nothing
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=53431
CCXXFLAGS += -Wno-comment

# don't call pkg-config every time CONF_xFLAGS is used, if parent project's makefile sets it
CONF_CFLAGS := $(CONF_CFLAGS)
CONF_LFLAGS := $(CONF_LFLAGS)
CONF_CXXFLAGS := $(CONF_CXXFLAGS)
EXESUFFIX =

ifeq ($(ARTYPE),exe)
  DEFINES += ARTYPE_EXE
endif
ifeq ($(ARTYPE),dll)
  DEFINES += ARTYPE_DLL
endif
ifeq ($(ARTYPE),hybrid)
  DEFINES += ARTYPE_EXE
  DEFINES += ARTYPE_DLL
endif

ifeq ($(SELFTEST),1)
  ARTERMINAL = 1
endif

ifeq ($(OS),linux)
  # -ldl is unnecessary on glibc >= 2.34 (aug 2021); musl has, as far as I can determine, never needed it
  # it can be removed once 2.34 is 5 years old, and it's confirmed to be unnecessary on musl
  # other libcs are rare, they're safe to ignore
  CONF_LFLAGS += -ldl -Wl,-z,relro,-z,now,--as-needed,--no-undefined
  ifeq ($(ARTYPE),dll)
    CONF_CFLAGS += -fpic
    CONF_LFLAGS += -shared
    EXESUFFIX = .so
  endif
  ifeq ($(ARTYPE),hybrid)
    CONF_CFLAGS += -fpic
    CONF_LFLAGS += -rdynamic
  endif
endif

ifeq ($(OS),windows)
  EXESUFFIX = .exe
  ifeq ($(ARTYPE),dll)
    EXESUFFIX = .dll
    CONF_LFLAGS += -shared
  endif
  ifeq ($(ARTYPE),hybrid)
    DEFINES += ARLIB_HYBRID_DLL
    CONF_CFLAGS += -fpic
    CONF_LFLAGS += -Wl,--dynamicbase
  endif
  #surprisingly, this is still not default as of mingw 8.1.0 (to check, objdump -x | grep DllCharacteristics, 0x0100 bit is nxcompat)
  CONF_LFLAGS += -Wl,--nxcompat -ladvapi32
  ifeq ($(ARXPSUPPORT),1)
    DEFINES += _WIN32_WINNT=_WIN32_WINNT_WINXP
  endif

  ifeq ($(shell echo ^^),^)
    HOST_SHELL := cmd
  else
    # building non-windows binaries on a windows host is not supported
    # why would you do such a thing, anyways?
    $(warning non-cmd windows detected; if SHELL $(SHELL) is something other than sh.exe, makefile should check for that instead)
    $(warning alternatively, if MSYSTEM $(MSYSTEM) is nonempty, use that)
  endif
  # wine 7.0~repack-4 (Debian) can print stack traces on crash, but only with dwarf-4, not 5
  CFLAGS += -gdwarf-4
endif

CFLAGS += -g
ifneq ($(OPT),0)
  CONF_CFLAGS += $(OPTFLAGS)
  CONF_CXXFLAGS += $(OPTFLAGS_CXX)
  CONF_LFLAGS += $(OPTLFLAGS)
  ifeq ($(OPT),spd)
    CONF_CFLAGS += -O3
  endif
else
  #OPT=1 DEBUG=1 should prioritize optimization over debugging; OPT=1 DEBUG=1 then 'strip' should be identical to OPT=1 DEBUG=0
  #so this one is gated on OPT=0, not DEBUG=1
  CONF_CFLAGS += -DDEBUG
  # this one is an imminent segfault 99% of the time
  CONF_CFLAGS += -Werror=return-type
endif

ifeq ($(PROFILE),gen)
  CONF_CFLAGS += -fprofile-generate
  CONF_LFLAGS += -lgcov
endif
ifeq ($(PROFILE),use)
  CONF_CFLAGS += -fprofile-use -fprofile-correction
endif

ifneq ($(ASAN),)
  CONF_CFLAGS += -fsanitize=address
  #ASan must be first
  CONF_LFLAGS := -lasan $(CONF_LFLAGS)
endif
ifneq ($(TSAN),)
  CONF_CFLAGS += -fsanitize=thread
  CONF_LFLAGS += -ltsan
endif

OUTNAME = $(PROGRAM)$(EXESUFFIX)

CFLAGS_DEFAULT ?=
CFLAGS_NOWARN += -w
DEPS_DEFAULT :=


ifeq ($(ARLIB_MAIN),)
  SOURCES += *.cpp
endif
SOURCES_ARLIB := arlib/*.cpp

ifneq ($(SELFTEST),)
  CONF_CFLAGS += -DARLIB_TEST -DARLIB_TESTRUNNER
  CFLAGS_ARLIB += -UARLIB_TEST
  ifneq ($(ARLIB_MAIN),)
    CFLAGS_ARLIB += -DARLIB_TEST -DARLIB_TEST_ARLIB
  endif
endif

DEFINES += ARLIB_OBJNAME=$(OBJNAME)

ifeq ($(ARGUI),1)
  #SOURCES_ARLIB += arlib/gui/*.cpp
  ifeq ($(OS),windows)
    DEFINES += ARLIB_GUI ARLIB_GUI_WINDOWS
    CONF_LFLAGS += -lgdi32 -lcomctl32 -lcomdlg32
    ARTERMINAL_HASGUI = 1
  endif
  ifeq ($(OS),linux)
    DEFINES += ARLIB_GUI ARLIB_GUI_GTK3 ARLIB_GUI_X11
    CONF_CFLAGS += $(shell pkg-config --cflags gtk+-3.0)
    CONF_LFLAGS += -lX11 $(shell pkg-config --libs gtk+-3.0)
    ifneq ($(OPT),0)
      # these are useful on debug builds, but on release builds, they just waste space
      DEFINES += G_DISABLE_CAST_CHECKS
    endif
  endif
else ifeq ($(ARGUI),gtk4)
  #SOURCES_ARLIB += arlib/gui/*.cpp
  DEFINES += ARLIB_GUI ARLIB_GUI_GTK4
  CONF_CFLAGS += $(shell pkg-config --cflags gtk4)
  CONF_LFLAGS += -lX11 $(shell pkg-config --libs gtk4)
  ifneq ($(OPT),0)
    # these are useful on debug builds, but on release builds, they just waste space
    DEFINES += G_DISABLE_CAST_CHECKS
  endif
endif

ifeq ($(ARGAME),1)
  DEFINES += ARLIB_GAME
  ifeq ($(ARGUI),1)
    $(error can't use ARGAME and ARGUI simultaneously)
  endif
  ifeq ($(OS),windows)
    CONF_LFLAGS += -lgdi32
    ARTERMINAL_HASGUI = 1
  endif
endif

ifeq ($(AROPENGL),1)
  SOURCES_ARLIB += arlib/opengl/*.cpp
  DEFINES += ARLIB_OPENGL
  ifeq ($(OS),linux)
    DEFINES += ARLIB_GUI_X11
    CONF_LFLAGS += -lX11
  endif
  ifeq ($(AROPENGL_D3DSYNC),1)
    DEFINES += AROPENGL_D3DSYNC
  endif
  # if glsym-slim.h doesn't change, things that depend on it don't rebuild
  # but that also means make thinks the header itself is outdated and will try to rebuild it next time,
  #  which is noisy and failure prone
  # instead, add another intermediate
obj/glsym-slim-marker: $(SOURCES) | obj
	python3 arlib/opengl/generate.py used aropengl obj/glsym-slim.h
	echo a > obj/glsym-slim-marker
obj/glsym-slim.h: obj/glsym-slim-marker | obj
  ifneq ($(OPT),0)
    DEPS_DEFAULT += obj/glsym-slim.h
  endif
endif

ifeq ($(OS),windows)
  ifeq ($(ARTERMINAL_HASGUI),1)
    ARTERMINAL ?= error
  else
    ARTERMINAL ?= 1
  endif
  
  ifeq ($(ARTERMINAL),1)
    # nothing, -mconsole is the default
  else ifeq ($(ARTERMINAL),0)
    CONF_LFLAGS += -mwindows
    DEFINES += STDOUT_DELETE
  else ifeq ($(ARTERMINAL),error)
    CONF_LFLAGS += -mwindows
    DEFINES += STDOUT_ERROR
    CFLAGS_ARLIB += -D STDOUT_DELETE -U STDOUT_ERROR
  else ifeq ($(ARTERMINAL),hybrid)
    CONF_LFLAGS += -mwindows
  else
    $(error invalid ARTERMINAL value $(ARTERMINAL))
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

ifeq ($(ARWUTF),1)
  SOURCES_ARLIB += arlib/wutf/*.cpp
  DEFINES += ARLIB_WUTF
endif

ifeq ($(ARSOCKET),1)
  #SOURCES_ARLIB += arlib/socket/*.cpp
  DEFINES += ARLIB_SOCKET
  
  # TODO: change this when/if I reenable openssl and schannel
  ARSOCKET_SSL ?= bearssl
  ifeq ($(OS),windows)
    CONF_LFLAGS += -lws2_32 -liphlpapi
    ifdef ARSOCKET_SSL_WINDOWS
      ARSOCKET_SSL := $(ARSOCKET_SSL_WINDOWS)
    endif
    ARSOCKET_SSL ?= schannel
  endif
  ARSOCKET_SSL ?= openssl
  
  ifeq ($(ARSOCKET_SSL),no)
    ARSOCKET_SSL :=
  else
    DEFINES += ARLIB_SSL
  endif
  ifneq ($(findstring schannel,$(ARSOCKET_SSL)),)
    DEFINES += ARLIB_SSL_SCHANNEL
    CONF_LFLAGS += -lsecur32
  endif
  ifneq ($(findstring openssl,$(ARSOCKET_SSL)),)
    DEFINES += ARLIB_SSL_OPENSSL
    CONF_LFLAGS += -lssl -lcrypto
  endif
  ifneq ($(findstring bearssl,$(ARSOCKET_SSL)),)
    DEFINES += ARLIB_SSL_BEARSSL
    #SOURCES_ARLIB += arlib/socket/bear-ser.c
    ifeq ($(OS),windows)
      CONF_LFLAGS += -lcrypt32
    endif
    # update make superclean if changing this
    # there's only one libbearssl.a per OS, not per OBJNAME; BearSSL rarely changes, there's no need to be more granular
    OBJS_LATE += arlib/deps/bearssl-0.6/build-$(OS)$(WINBITS)/libbearssl.a
    arlib/deps/bearssl-0.6/build-$(OS)$(WINBITS)/libbearssl.a:
      # BearSSL detects mingw32-make as Unix; override some variables into something shell-neutral
	  @echo Signature: 8a477f597d28d172789f06886806bc55 > arlib/deps/bearssl-0.6/build-$(OS)$(WINBITS)/CACHEDIR.TAG
	  $(MAKE) -C arlib/deps/bearssl-0.6/ lib CFLAGS='$(OPTFLAGS) -fpic' BUILD=build-$(OS)$(WINBITS) OBJDIR=build-$(OS)$(WINBITS) CC=$(CC) MKDIR=mkdir
  endif
  ifneq ($(filter-out schannel openssl bearssl,$(ARSOCKET_SSL)),)
    $(error unknown SSL backend)
  endif
endif

ifneq ($(wildcard resources/*),)
  ifneq ($(wildcard resources/*.icon*),)
    ifeq ($(OS),windows)
      OBJS += obj/icons-$(OBJNAME).o
obj/icons-$(OBJNAME).o: obj/resources.h
	windres obj/icons.rc obj/icons-$(OBJNAME).o
    endif
  endif
  SOURCES_ARLIB += obj/resources.cpp
obj/resources.h: arlib/rescompile.py resources resources/* | obj
	python3 arlib/rescompile.py
obj/resources.cpp: obj/resources.h
    # if this recipe is empty, Make doesn't rebuild things depending on resources.cpp
    # and if both obj/resources.* have the above recipe, Make runs it twice
	@true
  DEPS_DEFAULT += obj/resources.h
endif

SOURCES_NOWARN += arlib/deps/miniz.c

CCXXFLAGS += $(patsubst %,-D%,$(DEFINES))

CONF_CXXFLAGS := $(CONF_CXXFLAGS) $(CONF_CFLAGS)
TRUE_CFLAGS := -std=c99 $(CCXXFLAGS) $(CONF_CFLAGS) $(CFLAGS)
# C++20/etc wishlist:
# - Coroutines, for runloop-related stuff (nearly every single socket-related operation is ugly)
#    present in GCC 11
# - If a lambda is a coroutine, the captures should be in the coroutine frame, not in the likely-temporary lambda object
#    reportedly considered for c++23, but not approved yet
# - Template string literals (for the regex parser)
#    present in GCC 9
# - Concepts (also for the regex parser, the errors are absurd)
# - consteval (still for the regex parser, that template mess is terrible)
#    present in GCC 10
# - Down with typename http://wg21.link/p0634r3
#    present in GCC 9
# - [[likely]]
# - [[no_unique_address]] (ideally on by default)
# - Ban non-twos-complement signed - will probably enable various other language simplifications and improvements
# - void foo(auto arg) (shorthand for template<typename T> void foo(T arg) - simplifies function::bind_member)
# - Designated initializers (foo_t foo = { .a = 42, .b = 123 })
# - std::stacktrace (C++23), to delete the testcall macro (and probably simplify debug.cpp)
# - [[trivially_relocatable]] http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1144r3.html
# - Lambdas without braces/return, function<int(int)> mul2 = [](int i) => i*2; https://wg21.link/p0573 (useful for LINQ)
# - Zero-overhead deterministic exceptions http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0709r0.pdf
# - Compile-time reflection (that SERIALIZE macro is a little too limited, and the error messages are worse than from the regexes)
# - Single-implementation interfaces (public functions and nothing else in header, member variables hidden)
#     useful for anything with multiple implementations, but where the active one is a #ifdef, for example gameview and the gui widgets
#     the $class proposal https://wg21.link/P0707 would most likely permit this, as well as most or all of the reflection I want
# - Demote or, and, bitand, etc from keyword to predefined macro (or even better, banish them to <ciso646>)
# - std::embed http://open-std.org/JTC1/SC22/WG21/docs/papers/2018/p1040r0.html
# - Allow int x(int y=0); int x(int y=0) { return y*2; } (only if default value is identical on both ends, of course)
# - Something to get rid of the unused variable warning from for (size_t n : range(42)) - Ranges TS, maybe?
# - Overloading operator[] depending on whether it's a read or write access, int x = std::map[key_t] mutating is silly
#     not sure if map[key].foo.bar() should count as read or write; not all wishes are realistic
# - Modules, if they're good (I haven't looked into them, it's easy to make a bad module system)
# - Virtual constants, constants stored directly in the vtable (as opposed to a function that returns the constant)
# - Singleton tag on a class - there's exactly one instance of it, and it's a global. Compiler is allowed to optimize the this pointer into a constant.
#     More for bsnes than myself, but we both would benefit.
# - #elifdef, #elifndef
# Much of the above exists in C++20, I just haven't upgraded Arlib yet.
TRUE_CXXFLAGS := -std=c++20 -fno-rtti $(CCXXFLAGS) $(CONF_CXXFLAGS) $(CXXFLAGS)
TRUE_LFLAGS := $(CONF_LFLAGS) $(LFLAGS)

obj:
	mkdir obj
	echo Signature: 8a477f597d28d172789f06886806bc55 > obj/CACHEDIR.TAG

ifeq ($(HOST_SHELL),cmd)
miniclean:
	del /q obj\DEFAULT_*
# I'd prefer to say 'delete everything except this the cachedir tag', but that doesn't exist in common shells
# I could list every name except that, but the list is big and annoying to maintain, and omissions may result in absurd heisenbugs
clean:
	del /q obj\*
	@echo Signature: 8a477f597d28d172789f06886806bc55 > obj/CACHEDIR.TAG
clean-prof:
	del /q obj\*.o
#superclean not supported on cmd
else
miniclean:
	rm -f obj/DEFAULT_*
clean:
	rm -f obj/*
	@echo Signature: 8a477f597d28d172789f06886806bc55 > obj/CACHEDIR.TAG
clean-prof:
	rm -f obj/*.o
superclean: clean
	-rm arlib/deps/bearssl-0.6/build*/*
endif


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
OBJS += $(OBJS_LATE)

arlib-default: $(OUTNAME)


obj/%.c.o: $$(call SOURCENAME,$$@) $$(DEPS_$$(call DOMAINNAME,$$@)) | obj
	$(ECHOQ) CC $<
	$(Q)$(CC) $(TRUE_CFLAGS) $(CFLAGS_$(call DOMAINNAME,$@)) -c $< -o $@
obj/%.cpp.o: $$(call SOURCENAME,$$@) $$(DEPS_$$(call DOMAINNAME,$$@)) | obj
	$(ECHOQ) CXX $<
	$(Q)$(CXX) $(TRUE_CXXFLAGS) $(CFLAGS_$(call DOMAINNAME,$@)) -c $< -o $@
obj/%.S.o: $$(call SOURCENAME,$$@) $$(DEPS_$$(call DOMAINNAME,$$@)) | obj
	$(ECHOQ) AS $<
	$(Q)$(AS) $(TRUE_ASFLAGS) $(ASFLAGS_$(call DOMAINNAME,$@)) $< -o $@
#do not make this rule generic (obj/%.a.o), it hits some ridiculous 'chain of implicit rules'
# clause that deletes and refuses to rebuild object files
#https://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
#if genericity is needed, stamp out the rules with eval
obj/$(OBJNAME)-arlib.a: $(foreach src,$(foreach word,$(SOURCES_ARLIB),$(wild2)),$(call OBJMANGLE,ARLIB,$(src)))
#obj/$(OBJNAME)-arlib.a: $(call OBJMANGLE,$(domain),$(foreach word,$(SOURCES_$(domain)),$(wild2)))
	$(ECHOQ) AR $@
	$(Q)$(AR) rcs $@ $+

$(OUTNAME): $(OBJS)
	$(ECHOQ) LD $@
	$(Q)$(LD) $+ $(TRUE_LFLAGS) -o $@ -lm



ifneq ($(SELFTEST),)
obj/arlibtest$(EXESUFFIX): $(OBJS)
	$(ECHOQ) LD $@
	$(Q)$(LD) $+ $(TRUE_LFLAGS) -o $@ -lm

ifeq ($(OS),linux)
  #if make TR=, these assignments do nothing
  TR = valgrind --num-callers=100 --leak-check=full --keep-debuginfo=yes
  ifeq ($(ARGUI),1)
    # Gtk leaves over 9000 'possibly lost' objects around. not interested
    TR += --show-possibly-lost=no
    #envs G_SLICE=always-malloc G_DEBUG=gc-friendly look useful, but don't make any difference in practice
  endif
endif
ifeq ($(TR),gdb)
  override TR = gdb --args
endif

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
endif



ifeq ($(MAKECMDGOALS),me a sandwich)
.PHONY: me a sandwich
me a:
	@:
sandwich:
	@[ `id -u` -ne 0 ] && echo "What? Make it yourself." || echo Okay.
endif
