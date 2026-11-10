PROJ=saveguard
include $(OO_PS4_TOOLCHAIN)/defs.mk
include $(OO_PS4_TOOLCHAIN)/common.mk
all:
$(CXX) $(CXXFLAGS) -I. -c main.cpp -o main.o
$(call create-fself,main.o,$(PROJ).elf,$(PROJ).self)
@mkdir -p assets/fonts assets/images sce_module
@touch assets/fonts/Gontserrat-Regular.ttf assets/images/icon0.png sce_module/libSceFios2.prx sce_module/libSceFontGsm.prx
python3 ../../tools/fix_gp4_dirs.py . $(PROJ).gp4
$(call create-gp4,$(PROJ).gp4,$(PROJ).pkg,SaveGuard PS4)
