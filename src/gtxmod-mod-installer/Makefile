TITLE       := GTXMOD Mod Installer
VERSION     := 01.00
TITLE_ID    := YAHI00003
CONTENT_ID  := IV0000-YAHI00003_00-GTXMODINSTAL0001
LIBS        := -lc -lkernel -lc++ -lSceVideoOut
TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
PROJDIR     := installer
INTDIR      := $(PROJDIR)/x64/Debug
ASSETS      := $(shell find payload assets -type f -print 2>/dev/null)
LIBMODULES  := $(wildcard sce_module/*)
CFILES      :=
CPPFILES    := $(PROJDIR)/main.cpp
COBJS       :=
CPPOBJS     := $(INTDIR)/cpp_main.o
OBJS        := $(COBJS) $(CPPOBJS) $(INTDIR)/graphics_common.o
CFLAGS      := --target=x86_64-pc-freebsd12-elf -D_GNU_SOURCE -fPIC -funwind-tables -c -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include
CXXFLAGS    := $(CFLAGS) -std=c++11 -isystem $(TOOLCHAIN)/include/c++/v1 -I$(TOOLCHAIN)/samples/_common
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o
TOOLS       := $(TOOLCHAIN)/bin/linux
all: $(CONTENT_ID).pkg
$(CONTENT_ID).pkg: pkg.gp4
	$(TOOLS)/PkgTool.Core pkg_build $< .
pkg.gp4: eboot.bin sce_sys/param.sfo sce_sys/icon0.png $(LIBMODULES) $(ASSETS)
	python3 ../../tools/create_gp4_from_tree.py $@ $(CONTENT_ID)
	python3 ../../tools/fix_gp4_dirs.py $@
sce_sys/param.sfo: Makefile
	$(TOOLS)/PkgTool.Core sfo_new $@
	$(TOOLS)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLS)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLS)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLS)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'
eboot.bin: $(INTDIR) $(OBJS)
	ld.lld $(INTDIR)/*.o -o $(INTDIR)/installer.elf $(LDFLAGS)
	$(TOOLS)/create-fself -in=$(INTDIR)/installer.elf -out=$(INTDIR)/installer.oelf --eboot "eboot.bin" --paid 0x3800000000000011
$(INTDIR)/c_installer_%.o: $(PROJDIR)/%.c | $(INTDIR)
	clang $(CFLAGS) -o $@ $<
$(INTDIR)/c_miniz_%.o: third_party/miniz/%.c | $(INTDIR)
	clang $(CFLAGS) -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -o $@ $<
$(INTDIR)/cpp_%.o: $(PROJDIR)/%.cpp | $(INTDIR)
	clang++ $(CXXFLAGS) -o $@ $<
$(INTDIR)/graphics_common.o: $(TOOLCHAIN)/samples/_common/graphics.cpp | $(INTDIR)
	clang++ $(CXXFLAGS) -o $@ $<
$(INTDIR):
	mkdir -p $(INTDIR)
clean:
	rm -f $(CONTENT_ID).pkg pkg.gp4 eboot.bin sce_sys/param.sfo $(INTDIR)/*.elf $(INTDIR)/*.o
.PHONY: all clean
