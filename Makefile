BUILD ?= linux
BUILD_DIR := build/$(BUILD)
OBJDIR := $(BUILD_DIR)/obj
INCDIR := $(BUILD_DIR)/include

OUTDIR := .

CC_linux   := gcc
CC_windows := x86_64-w64-mingw32-gcc
STRIP_windows := x86_64-w64-mingw32-strip

TARGET_TRIPLE_windows := x86_64-w64-mingw32
SYSROOT_windows := /usr/$(TARGET_TRIPLE_windows)
OBJDUMP_windows := $(TARGET_TRIPLE_windows)-objdump

CFLAGS_common   := -Wall -O3 -g
CPPFLAGS_common :=
LDFLAGS_common  :=

EXE :=
LIBS :=
PREP := prep_none

ifeq ($(BUILD),linux)
  CC := $(CC_linux)
  EXE :=
  CPPFLAGS := $(CPPFLAGS_common)
  CFLAGS   := $(CFLAGS_common)
  LDFLAGS  := $(LDFLAGS_common)
  LIBS     := -lpthread -ldl -lgcrypt -lOpenCL -lunrar
  GPU_BACKEND_OBJ := $(OBJDIR)/opencl_setup.o
endif

ifeq ($(BUILD),macos)
  # Metal backend on macOS (Apple Silicon).  Shaders compile at runtime via
  # metal_setup.m's newLibraryWithSource:, so no standalone metal toolchain is
  # required.  gcrypt comes from Homebrew (brew install libgcrypt).
  CC := clang
  EXE :=
  CPPFLAGS := $(CPPFLAGS_common) -DUSE_METAL -I/opt/homebrew/include
  CFLAGS   := $(CFLAGS_common)
  LDFLAGS  := $(LDFLAGS_common) -L/opt/homebrew/lib
  LIBS     := -lpthread -lgcrypt -lm -framework Metal -framework Foundation
  GPU_BACKEND_OBJ := $(OBJDIR)/metal_setup.o
endif

ifeq ($(BUILD),windows)
  CC := $(CC_windows)
  EXE := .exe

  CPPFLAGS := $(CPPFLAGS_common) -I$(INCDIR)
  CFLAGS   := $(CFLAGS_common)
  LDFLAGS  := $(LDFLAGS_common)

  LIBS := -lwinpthread -lgcrypt -lgpg-error -lbcrypt -lws2_32

  PREP := prep_opencl_headers
  GPU_BACKEND_OBJ := $(OBJDIR)/opencl_setup.o
endif

GPU_BACKEND_OBJ ?= $(OBJDIR)/opencl_setup.o

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(SRCS))

GEN_PROG      := crackalack_gen$(EXE)
UNITTEST_PROG := crackalack_unit_tests$(EXE)
GETCHAIN_PROG := get_chain$(EXE)
VERIFY_PROG   := crackalack_verify$(EXE)
RTC2RT_PROG   := crackalack_rtc2rt$(EXE)
LOOKUP_PROG   := crackalack_lookup$(EXE)
PERFECTIFY    := perfectify$(EXE)
ENUMERATE     := enumerate_chain$(EXE)

BINARIES := \
	$(OUTDIR)/$(GEN_PROG) \
	$(OUTDIR)/$(UNITTEST_PROG) \
	$(OUTDIR)/$(GETCHAIN_PROG) \
	$(OUTDIR)/$(VERIFY_PROG) \
	$(OUTDIR)/$(RTC2RT_PROG) \
	$(OUTDIR)/$(LOOKUP_PROG) \
	$(OUTDIR)/$(PERFECTIFY) \
	$(OUTDIR)/$(ENUMERATE)

.PHONY: all linux macos windows clean strip \
        prep_opencl_headers prep_none \
        bundle_windows cpu-tests

all: $(PREP) $(BINARIES)

linux:
	$(MAKE) BUILD=linux all

macos:
	$(MAKE) BUILD=macos all

windows:
	$(MAKE) BUILD=windows all bundle_windows

strip: windows
	$(STRIP_windows) $(OUTDIR)/*.exe || true

$(OBJDIR) $(INCDIR):
	mkdir -p $@

prep_none:
	@true

prep_opencl_headers: | $(INCDIR)
	@if [ ! -d /usr/include/CL ]; then \
		echo "ERROR: /usr/include/CL not found. Install OpenCL headers (e.g. opencl-headers)."; \
		exit 1; \
	fi
	@mkdir -p $(INCDIR)/CL
	@cp -a /usr/include/CL/* $(INCDIR)/CL/

DEPFLAGS = -MMD -MP
DEPS := $(OBJS:.o=.d)

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Objective-C rule for the Metal backend (macos build).  -fobjc-arc is required:
# metal_setup.m relies on ARC bridging (CFBridgingRetain, __bridge, etc.).
$(OBJDIR)/%.o: %.m | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fobjc-arc -c $< -o $@

-include $(DEPS)

# cpu-tests: build crackalack_cpu_tests with no GPU backend.
# Uses a dedicated build directory (build/cpu-tests/obj) so it never clobbers
# the normal platform build.  Object files are recompiled from scratch with
# CPU-tests-specific CPPFLAGS (no USE_CUDA, no USE_METAL on Linux; USE_METAL
# on macOS so gpu_backend.h resolves types as void* without needing Metal libs).
#
# Linux requirements: build-essential libgcrypt20-dev opencl-headers
# macOS requirements: brew install libgcrypt  (Xcode CLT already on PATH)
CPU_TESTS_PROG   := crackalack_cpu_tests$(EXE)
CPU_TESTS_OBJDIR := build/cpu-tests/obj
ifeq ($(shell uname -s),Darwin)
  CPU_TESTS_CC       := clang
  CPU_TESTS_CPPFLAGS := -DUSE_METAL -I. -Itests -I/opt/homebrew/include
  CPU_TESTS_CFLAGS   := -Wall -O3 -g
  CPU_TESTS_LDFLAGS  := -L/opt/homebrew/lib
  CPU_TESTS_LIBS     := -lpthread -lgcrypt -lm
else
  CPU_TESTS_CC       := gcc
  CPU_TESTS_CPPFLAGS := -I. -Itests -I/usr/include
  CPU_TESTS_CFLAGS   := -Wall -O3 -g
  CPU_TESTS_LDFLAGS  :=
  CPU_TESTS_LIBS     := -lpthread -lgcrypt -lm
endif

CPU_TESTS_OBJS := \
	$(CPU_TESTS_OBJDIR)/crackalack_cpu_tests.o \
	$(CPU_TESTS_OBJDIR)/cpu_tests_common.o \
	$(CPU_TESTS_OBJDIR)/test_golden.o \
	$(CPU_TESTS_OBJDIR)/test_shared.o \
	$(CPU_TESTS_OBJDIR)/cpu_rt_functions.o \
	$(CPU_TESTS_OBJDIR)/charset.o \
	$(CPU_TESTS_OBJDIR)/misc.o \
	$(CPU_TESTS_OBJDIR)/hash_validate.o \
	$(CPU_TESTS_OBJDIR)/file_lock.o

$(CPU_TESTS_OBJDIR):
	mkdir -p $@

# -MMD -MP emits per-object .d header-dependency files so that a change to a
# header (e.g. the rt_parameters struct layout in misc.h) forces a rebuild of
# every .o that includes it.  Without this, switching branches with a shared
# build/cpu-tests/obj mixes objects compiled against different struct layouts,
# producing an ABI mismatch and bogus test failures.
$(CPU_TESTS_OBJDIR)/%.o: %.c | $(CPU_TESTS_OBJDIR)
	$(CPU_TESTS_CC) $(CPU_TESTS_CPPFLAGS) $(CPU_TESTS_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(CPU_TESTS_OBJDIR)/%.o: tests/%.c | $(CPU_TESTS_OBJDIR)
	$(CPU_TESTS_CC) $(CPU_TESTS_CPPFLAGS) $(CPU_TESTS_CFLAGS) $(DEPFLAGS) -c $< -o $@

cpu-tests: $(CPU_TESTS_OBJS)
	$(CPU_TESTS_CC) $(CPU_TESTS_LDFLAGS) $^ -o $(OUTDIR)/$(CPU_TESTS_PROG) $(CPU_TESTS_LIBS)
	@echo "Built $(OUTDIR)/$(CPU_TESTS_PROG)"

CPU_TESTS_DEPS := $(CPU_TESTS_OBJS:.o=.d)
-include $(CPU_TESTS_DEPS)

$(OUTDIR)/$(GEN_PROG): \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/clock.o \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/crackalack_gen.o \
	$(OBJDIR)/file_lock.o \
	$(OBJDIR)/gws.o \
	$(OBJDIR)/hash_validate.o \
	$(OBJDIR)/misc.o \
	$(GPU_BACKEND_OBJ) \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/verify.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(UNITTEST_PROG): \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/crackalack_unit_tests.o \
	$(OBJDIR)/hash_validate.o \
	$(OBJDIR)/misc.o \
	$(GPU_BACKEND_OBJ) \
	$(OBJDIR)/test_chain.o \
	$(OBJDIR)/test_chain_ntlm9.o \
	$(OBJDIR)/test_hash.o \
	$(OBJDIR)/test_hash_ntlm9.o \
	$(OBJDIR)/test_hash_to_index.o \
	$(OBJDIR)/test_hash_to_index_ntlm9.o \
	$(OBJDIR)/test_index_to_plaintext.o \
	$(OBJDIR)/test_index_to_plaintext_ntlm9.o \
	$(OBJDIR)/test_shared.o \
	$(OBJDIR)/file_lock.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(GETCHAIN_PROG): $(OBJDIR)/get_chain.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(VERIFY_PROG): \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/crackalack_verify.o \
	$(OBJDIR)/file_lock.o \
	$(OBJDIR)/hash_validate.o \
	$(OBJDIR)/misc.o \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/verify.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(RTC2RT_PROG): \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/crackalack_rtc2rt.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(LOOKUP_PROG): \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/clock.o \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/crackalack_lookup.o \
	$(OBJDIR)/file_lock.o \
	$(OBJDIR)/hash_validate.o \
	$(OBJDIR)/misc.o \
	$(GPU_BACKEND_OBJ) \
	$(OBJDIR)/rar_decompress.o \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/test_shared.o \
	$(OBJDIR)/verify.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(PERFECTIFY): \
	$(OBJDIR)/clock.o \
	$(OBJDIR)/perfectify.o
	$(CC) $(LDFLAGS) $^ -o $@

$(OUTDIR)/$(ENUMERATE): \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/enumerate_chain.o \
	$(OBJDIR)/test_shared.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

bundle_windows:
	@echo "Bundling runtime DLLs into $(OUTDIR)..."
	@set -e; \
	cp -u "$(SYSROOT_windows)/bin/libgcrypt-20.dll" "$(OUTDIR)/" 2>/dev/null || true; \
	cp -u "$(SYSROOT_windows)/bin/libgpg-error-0.dll" "$(OUTDIR)/" 2>/dev/null || true; \
	cp -u "$(SYSROOT_windows)/lib/libwinpthread-1.dll" "$(OUTDIR)/" 2>/dev/null || true; \
	for exe in $(OUTDIR)/*.exe; do \
		[ -f "$$exe" ] || continue; \
		echo "  -> $$exe"; \
		"$(OBJDUMP_windows)" -p "$$exe" | awk '/DLL Name:/ {print $$3}' | while read dll; do \
			case "$$dll" in \
				KERNEL32.dll|USER32.dll|ADVAPI32.dll|WS2_32.dll|bcrypt.dll|GDI32.dll|SHELL32.dll|OLE32.dll|OLEAUT32.dll|CRYPT32.dll|ntdll.dll) \
					;; \
				*) \
					found=""; \
					for cand in \
						"$(SYSROOT_windows)/bin/$$dll" \
						"$(SYSROOT_windows)/lib/$$dll"; \
					do \
						if [ -f "$$cand" ]; then cp -u "$$cand" "$(OUTDIR)/"; found=1; break; fi; \
					done; \
					if [ -z "$$found" ]; then \
						src="$$(find "$(SYSROOT_windows)" -type f -iname "$$dll" 2>/dev/null | head -n 1)"; \
						if [ -n "$$src" ]; then cp -u "$$src" "$(OUTDIR)/"; \
						else echo "WARNING: could not locate $$dll on build machine"; fi; \
					fi; \
					;; \
			esac; \
		done; \
	done

clean:
	rm -rf build
	rm -f *.exe \
	      crackalack_gen crackalack_unit_tests crackalack_cpu_tests get_chain crackalack_verify crackalack_rtc2rt crackalack_lookup perfectify enumerate_chain \
	      libgcrypt-20.dll libgpg-error-0.dll libwinpthread-1.dll
