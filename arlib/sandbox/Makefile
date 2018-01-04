ifneq ($(OS),linux)
  $(error sandbox is only supported on Linux)
endif

SOURCES_ARLIB += arlib/sandbox/*.cpp
DEFINES += ARLIB_SANDBOX

#enable ASLR and ensure we're usable as linker
PRELOADER_FLAGS := -fPIC -static -shared
#runtime libraries aren't available, don't use them and don't use anything using them
PRELOADER_FLAGS += -nostdlib -nostartfiles -nodefaultlibs -ffreestanding
#gcc sure loves inserting calls to random stuff, force it not to
#I'd prefer if I could enable -fstack-protector, but the stack cookie is at %fs:40, which is a segfault
#I could set it up, but that'd require a lot of assembly, leak memory, and confuse the real libc when it tries to set up %fs
#glibc seems built without stack protector, anyways
PRELOADER_FLAGS += -fno-stack-protector -fno-exceptions -fno-rtti -fno-jump-tables -fno-sync-libcalls
#nothing should nor can override our symbols, and allowing it causes relocation issues
PRELOADER_FLAGS += -fvisibility=hidden
#AT&T assembly is ugly
#yes, there's only ~7 lines of asm in this thing, AT&T is still ugly
PRELOADER_FLAGS += -masm=intel

#this could be in obj/, but if I put it there, Python is mandatory for building Arlib. I'd rather not.
arlib/sandbox/bpf.inc: arlib/sandbox/bpf.S arlib/sandbox/bpfasm.py
	python3 arlib/sandbox/bpfasm.py $< $@
$(call OBJMANGLE,ARLIB,arlib/sandbox/launch-linux-sand.cpp): arlib/sandbox/launch-linux-sand.cpp arlib/sandbox/bpf.inc | obj

PRELOADER_SOURCES = arlib/sandbox/preload-linux-sand.cpp arlib/sandbox/sysemu-linux-sand.cpp
obj/sand-preload-$(OBJNAME).elf: $(PRELOADER_SOURCES) arlib/sandbox/internal-linux-sand.h | obj
	$(CXX) -D SANDBOX_INTERNAL $(TRUE_CXXFLAGS) $(PRELOADER_FLAGS) $(PRELOADER_SOURCES) $(TRUE_LFLAGS) -o $@ -Wl,-z,now -s
#ensure failure if this thing isn't 100% static
#requires .DELETE_ON_ERROR, which is set in Arlib's main makefile
	objdump -R $@ | grep -A500 -E 'DYNAMIC RELOCATION RECORDS$$' || true
	objdump -R $@ | grep -qF 'DYNAMIC RELOCATION RECORDS (none)'
$(call OBJMANGLE,ARLIB,arlib/sandbox/preload-linux-sand.cpp): arlib/sandbox/preload-linux-sand.cpp obj/sand-preload-$(OBJNAME).elf | obj
