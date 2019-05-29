#object filename structure:
#obj/RULE___OBJNAME___PATH.o
#RULE is the set of build flags for this file, probably DEFAULT
#OBJNAME is the OBJNAME= make parameter, or a default value
#PATH is the source filename, including extension, relative to project root, with slashes replaced with double underscore
#example: obj/DEFAULT___linux___arlib__file-posix.cpp.o

#to create a profiled build:
#make clean
#make OPT=1 PROFILE=gen
#./program --do-expensive-operations=true
#make clean-prof
#make OPT=1 PROFILE=use

arlib-default: arlib-default2

.SUFFIXES:

ifeq (,$(wildcard arlib/string.cpp))
  $(error wrong build directory, go up one level)
endif

SPACE :=
SPACE +=

define LF


endef

ifeq ($(OS),Windows_NT)
  OS = windows
else
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Linux)
    OS = linux
  else ifeq ($(UNAME_S),Darwin)
    OS = osx
  else ifeq ($(UNAME_S),)
    $(error couldn't execute uname -s)
  else
    $(error what weird OS is $(UNAME_S)?)
  endif
endif

ifeq ($(V),)
Q = @
ECHOQ = @echo
else
Q =
#TODO: windows version - echo works on both, but true doesn't
ECHOQ = @true
endif

ifneq (,$(findstring test,$(MAKECMDGOALS)))
  SELFTEST = 1
endif
ifneq (,$(findstring check,$(MAKECMDGOALS)))
  SELFTEST = 1
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

CC = gcc
CFLAGS =
CXX = g++
CXXFLAGS = $(CFLAGS)
LD = g++
LFLAGS =
AS = as
ASFLAGS =
AR = ar
CCXXFLAGS = -fvisibility=hidden -Wall -Wmissing-declarations -pipe
ifeq ($(CC_FAMILY),gcc)
CCXXFLAGS += -fno-semantic-interposition
endif
#doesn't seem to do anything, and CFLAGS is used for c++ too which throws warnings all over
#CFLAGS += -Wmissing-prototypes
ifneq ($(EXCEPTIONS),1)
  ifneq ($(SELFTEST),1)
    CCXXFLAGS += -fno-exceptions
  endif
else
  OPTFLAGS += -fno-enforce-eh-specs
endif

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
  OBJNAME = linux
  TR = valgrind --num-callers=100
endif
ifeq ($(TR),gdb)
  override TR = gdb --args
endif

ifeq ($(OS),windows)
  EXESUFFIX = .exe
#  OBJ += obj/resource$(OBJNAME).o
#  RC = windres
#  RCFLAGS =
#obj/resource$(OBJNAME).o: ico/*
#	$(RC) $(RCFLAGS) ico/minir.rc obj/resource$(OBJNAME).o
  OBJNAME = windows
  $(error check if -Wl,--nxcompat does anything or if it defaults to true)
endif

OPTFLAGS += -Os -fomit-frame-pointer -fmerge-all-constants -fvisibility=hidden
OPTFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables
OPTFLAGS += -ffunction-sections -fdata-sections
OPTFLAGS += -fno-ident
OPTFLAGS += -DNDEBUG -DARLIB_OPT
OPTFLAGS_CXX += -fno-threadsafe-statics
OPTLFLAGS := -Wl,--gc-sections,--build-id=none,--hash-style=gnu,--relax,--sort-common
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

.SECONDEXPANSION:
.DELETE_ON_ERROR:

SOURCES += *.cpp
SOURCES_ARLIB := arlib/*.cpp

ifneq ($(SELFTEST),)
  ifeq ($(ARLIB_MAIN),)
    CONF_CFLAGS += -DARLIB_TEST
    CFLAGS_ARLIB += -UARLIB_TEST -DARLIB_TESTRUNNER
  else
    # with SOURCES blanked, the real main isn't compiled
    SOURCES :=
    CFLAGS_ARLIB += -DARLIB_TEST -DARLIB_TESTRUNNER -DARLIB_TEST_ARLIB
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
	python arlib/opengl/generate.py used aropengl obj/glsym-slim.h
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
    CONF_LFLAGS += -lws2_32
    ifdef ARSOCKET_SSL_WINDOWS
      ARSOCKET_SSL := $(ARSOCKET_SSL_WINDOWS)
    else
      ARSOCKET_SSL := schannel
    endif
  endif
  
  #TODO: allow using multiple at once, the tests could use that
  ifeq ($(ARSOCKET_SSL),no)
    #no SSL
    #socketssl will still be available at compile time, but fails linking
  else ifeq ($(ARSOCKET_SSL),schannel)
    CFLAGS_ARLIB += -DARLIB_SSL_SCHANNEL
    #TODO: check if this is still needed
    #CONF_LFLAGS += -lcrypt32 -lsecur32
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
  else ifeq ($(ARSOCKET_SSL),tlse)
    CFLAGS_ARLIB += -DARLIB_SSL_TLSE
    SOURCES_NOWARN += arlib/deps/tlse.c
  else
    $(error unknown SSL library)
  endif
endif

ifneq ($(wildcard resources/*),)
  SOURCES_ARLIB += obj/resources.cpp
obj/resources.cpp: arlib/rescompile.py resources/ resources/*
	$(ECHOQ) rescompile.py
	$(Q)python3 arlib/rescompile.py || python2 arlib/rescompile.py
arlib.h: obj/resources.cpp
endif

SOURCES_NOWARN += arlib/deps/miniz.c

CONF_CFLAGS += $(patsubst %,-D%,$(DEFINES))

TRUE_CFLAGS := -std=c99 $(CCXXFLAGS) $(CONF_CFLAGS) $(CFLAGS)
# C++ version policy: I will switch to a newer version of C++ only if it adds something that
# - considerably simplifies existing code (everything is doable without that, some things are less painful than others)
# - works on the default compiler on current Debian stable, latest Ubuntu LTS, and whatever other computers I have at the moment
# - is functional and non-experimental on a finalized -std= version (c++11 is final, c++0x isn't)
#    it's fine if not every part of that language version is supported in that compiler version
# I will not switch C++ version because a dependency needs that. Instead, I'll reinvent or clone the dependency.
# C++14 offers one useful feature, which is not big enough to switch for:
#   std::<trait>_v (rather than std::<trait>::value)
# C++17 offers plenty of useful features, and I will switch once Debian 10 releases with a GCC newer than 6.3:
#   template<auto val> (for the function<> member binder, that macro sucks. Though bind_this will likely always be a macro)
#   Guaranteed copy elision
#   constexpr if (for the min function, and probably a few others)
#   Structured binding (auto [ x, y ] = foo())
#   __has_include (for valgrind.h)
# The following features would be useful; if enough of them show up, I will switch:
#   C++2a template lambdas (for the PNG decoder)
#   C++2a designated initializers (foo_t foo { .a = 42, .b = 123 })
#   C++2a down with typename
#   C++2a [[likely]]
#   C++2a [[no_unique_address]]
#   C++2a ban non-power-of-two integers
#   C++2a consteval
#   C++2a void foo(auto arg) (shorthand for template<typename T> void foo(T arg))
#   [[trivially_relocatable]] http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1144r3.html
#   Lambdas without braces; function<int(int)> foo = [](int i) => i*2;
#   Zero-overhead deterministic exceptions http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0709r0.pdf
#   String literals as template parameters http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0424r2.pdf
#   Compile-time reflection (that SERIALIZE macro is a little too limited)
#   Single-implementation interfaces (public functions and nothing else in header, member variables hidden; useful for the GUI classes)
#     the $class proposal would most likely permit this, as well as most or all of the reflection I want
#   Demote or, and, bitand, etc from keyword to predefined macro (or even better, banish them to the <ciso646> they should've never left)
# The following may or may not be usable, depending on the exact details:
#   C++2a coroutines (more useful for bsnes than for me; some of my callbacks could become coroutines, but I don't yet know how many)
#   C++2a modules (it's very easy to produce a bad module system, I don't know if this one avoids the pitfalls)
# Most other stuff I've seen proposed is uninteresting.
# I'm not interested in library features. Most of them lack important functionality, have bad constant factors, and bloat the binary.
# The compiler-magic ones, like std::move and type traits, do one thing and do it well, and optimize to nothing. I use them.
TRUE_CXXFLAGS := -std=c++11 -fno-rtti $(CCXXFLAGS) $(CONF_CXXFLAGS) $(CXXFLAGS)
TRUE_LFLAGS := $(LFLAGS) -fvisibility=hidden $(CONF_LFLAGS)

ifeq ($(OS),windows)
  TRUE_LFLAGS += -static-libgcc 
endif

#On Windows, cleaning up the object directory is expected to be done with 'del /q obj\*' in a batch script.
clean:
	rm -f obj/*

clean-prof:
	rm -f obj/*.o

superclean: clean
	rm -f arlib/deps/bearssl-0.6/build/obj/*
	rm -f arlib/deps/bearssl-0.6/build/libbearssl.a

obj:
	mkdir obj

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
#.a are order sensitive, do not sort or rearrange these
OBJS += obj/$(OBJNAME)-arlib.a
endif
OBJS += $(sort $(OBJS_LATE))

all: $(OUTNAME)
arlib-default2: $(OUTNAME)


obj/%.c.o: $$(call SOURCENAME,$$@) arlib.h | obj
	$(ECHOQ) CC $<
	$(Q)$(CC) $(TRUE_CFLAGS) $(CFLAGS_$(call DOMAINNAME,$@)) -c $< -o $@
obj/%.cpp.o: $$(call SOURCENAME,$$@) arlib.h | obj
	$(ECHOQ) CXX $<
	$(Q)$(CXX) $(TRUE_CXXFLAGS) $(CFLAGS_$(call DOMAINNAME,$@)) -c $< -o $@
obj/%.S.o: $$(call SOURCENAME,$$@) arlib.h | obj
	$(ECHOQ) AS $<
	$(Q)$(AS) $(TRUE_ASFLAGS) $(ASFLAGS_$(call DOMAINNAME,$@)) $< -o $@
#do not make this rule generic (obj/%.a.o), that hits some ridiculous unkillable 'chain of implicit rules'
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
endif



ifeq ($(MAKECMDGOALS),me a sandwich)
.PHONY: me a sandwich
me a:
	@:
sandwich:
	@[ `id -u` -ne 0 ] && echo "What? Make it yourself." || echo Okay.
endif
