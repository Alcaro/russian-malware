ifneq ($(ARTHREAD),1)
  $(error the sandbox requires threads support)
endif

ifneq ($(OS),linux)
  $(error sandbox is only supported on Linux)
endif

SOURCES_ARLIB += arlib/sandbox/*.cpp
DEFINES += ARLIB_SANDBOX

#enable ASLR and ensure we're usable as linker
PRELOADER_FLAGS := -fPIC -static -shared
#runtime libraries aren't available, don't use them and don't use anything using them
PRELOADER_FLAGS += -nostdlib -nostartfiles -ffreestanding
#pretty sure most of these are implied by -ffreestanding, but better safe than sorry
PRELOADER_FLAGS += -fno-stack-protector -fno-exceptions -fno-rtti -fno-jump-tables -fno-sync-libcalls
#nothing should nor can override our symbols, and allowing it causes relocation issues
PRELOADER_FLAGS += -fvisibility=hidden
#AT&T assembly is ugly
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
