ifneq ($(OS),linux)
  $(error sandbox is only supported on Linux)
endif

SOURCES_ARLIB += arlib/sandbox/*.cpp
DEFINES += ARLIB_SANDBOX

#ensure we're usable as linker, with ASLR enabled
PRELOADER_CFLAGS += -fpic
PRELOADER_LFLAGS += -static -shared -Wl,-Bsymbolic -Wl,--no-dynamic-linker

#runtime libraries aren't available, don't use them and don't use anything using them
PRELOADER_CFLAGS += -ffreestanding
PRELOADER_LFLAGS += -nostdlib -nostartfiles -nodefaultlibs
PRELOADER_CFLAGS += -fno-builtin-memcpy -fno-builtin-memset -fno-builtin-memmove
#nothing should nor can override our symbols, and allowing it causes relocation issues
PRELOADER_CFLAGS += -fvisibility=hidden
#gcc sure loves inserting calls to random stuff, force it not to
#I'd prefer if I could enable -fstack-protector, but the stack cookie is at %fs:40, which is a segfault
#I could set it up, but that'd require a bunch of assembly, leak memory, and confuse the real libc when it tries to set up %fs
#glibc seems built without stack protector anyways, and the low amount of stack used by this thing is easy to audit by hand
PRELOADER_CFLAGS += -fno-stack-protector -fno-exceptions -fno-rtti -fno-jump-tables
ifeq ($(CC_FAMILY),gcc)
  PRELOADER_CFLAGS += -fno-sync-libcalls
endif

#AT&T assembly is ugly
#yes, there's only ~7 lines of asm in this thing, AT&T is still ugly
PRELOADER_CFLAGS += -masm=intel
ifeq ($(CC_FAMILY),clang)
#TODO: reenable integrated-as once Clang's integrated assembler supports mov %eax,42 (rather than just mov eax,42)
#as of Clang 6.0.0, it does not
#(Clang will also need a __asm__(".intel_syntax") early in the C++ - under an ifdef, gcc doesn't like that)
PRELOADER_CFLAGS += -fno-integrated-as
endif

#I could include TRUE_{C,L}FLAGS, but that includes silly stuff like -pthread and -lgtk-3, so just hardcode them
#CFLAGS/LFLAGS are set by command line only, empty by default, let's include them
PRELOADER_CFLAGS += -DSANDBOX_INTERNAL -std=c++11 $(CFLAGS)
PRELOADER_CFLAGS += -Wall -Wmissing-declarations -Wno-comment
PRELOADER_LFLAGS += -Wl,-z,now $(LFLAGS)

ifneq ($(OPT),0)
  PRELOADER_CFLAGS += $(OPTFLAGS)
  PRELOADER_LFLAGS += $(OPTLFLAGS)
endif
ifeq ($(DEBUG),1)
  #not sure if gdb can find the debug symbols of the sandbox child, or do anything useful with the child at all
  #(strace works fine, but doesn't care about symbols)
  #but gdb can run the launcher without the sandbox, so keep debug by default anyways
  PRELOADER_CFLAGS += -g
endif

#TODO: move to obj/, so programs can have custom sandbox rules
arlib/sandbox/bpf.inc: arlib/sandbox/bpf.S arlib/sandbox/bpfasm.py
	python3 arlib/sandbox/bpfasm.py $< $@
$(call OBJMANGLE,ARLIB,arlib/sandbox/launch-linux-sand.cpp): arlib/sandbox/launch-linux-sand.cpp arlib/sandbox/bpf.inc | obj

PRELOADER_SOURCES := arlib/sandbox/preload-linux-sand.cpp arlib/sandbox/sysemu-linux-sand.cpp
PRELOADER_OBJS := $(call OBJMANGLE,arlibsand,$(wildcard $(PRELOADER_SOURCES)))
obj/arlibsand___%.cpp.o: $$(call SOURCENAME,$$@) arlib/sandbox/internal-linux-sand.h | obj
	$(ECHOQ) CXX $<
	$(Q)$(CXX) $(PRELOADER_CFLAGS) -c $< -o $@

obj/sand-preload-$(OBJNAME).elf: $(PRELOADER_OBJS) | obj
	$(ECHOQ) LD $@
	$(Q)$(CXX) -D SANDBOX_INTERNAL $(PRELOADER_LFLAGS) $(PRELOADER_OBJS) -o $@
#ensure failure if this thing isn't 100% static
#requires .DELETE_ON_ERROR, which is set in Arlib's main makefile
	$(Q)objdump -R $@ 2>&1 | grep -A500 -E 'DYNAMIC RELOCATION RECORDS$$' || true
	$(Q)objdump -R $@ 2>&1 | grep -qF 'DYNAMIC RELOCATION RECORDS (none)' || \
	    objdump -R $@ 2>&1 | grep -qF 'not a dynamic object'
$(call OBJMANGLE,ARLIB,arlib/sandbox/preload-linux-sand.cpp): arlib/sandbox/preload-linux-sand.cpp obj/sand-preload-$(OBJNAME).elf | obj
