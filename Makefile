# Default BUILD to the host platform so on-demand targets (e.g. `make
# gen_known_hash`) pick the right toolchain/dirs without an explicit BUILD=.
# The linux:/macos:/windows: aliases still override this via the command line.
ifeq ($(shell uname -s),Darwin)
  BUILD ?= macos
else
  BUILD ?= linux
endif
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
  CUDA_PATH ?= /usr/local/cuda
  CPPFLAGS := $(CPPFLAGS_common) -DUSE_CUDA -I$(CUDA_PATH)/include
  CFLAGS   := $(CFLAGS_common) -march=native -flto=auto
  LDFLAGS  := $(LDFLAGS_common) -flto=auto -L$(CUDA_PATH)/lib64 -Wl,-rpath,$(CUDA_PATH)/lib64
  LIBS     := -lpthread -ldl -lgcrypt -lcuda -lnvrtc -lm
  GPU_BACKEND_OBJ := $(OBJDIR)/cuda_setup.o
endif

ifeq ($(BUILD),macos)
  CC := clang
  EXE :=
  CPPFLAGS := $(CPPFLAGS_common) -DUSE_METAL -I/opt/homebrew/include
  CFLAGS   := $(CFLAGS_common) -march=native -flto
  LDFLAGS  := $(LDFLAGS_common) -L/opt/homebrew/lib -flto
  LIBS     := -lpthread -lgcrypt -lm -framework Metal -framework Foundation
  GPU_BACKEND_OBJ := $(OBJDIR)/metal_setup.o
endif

ifeq ($(BUILD),linux-opencl)
  # Native Linux build using the OpenCL backend (instead of CUDA).  Useful for
  # exercising the OpenCL (.cl) kernels on Linux hosts that have an OpenCL ICD
  # (e.g. NVIDIA's, or PoCL).  Binaries go under build/linux-opencl/ so they do
  # not clobber the CUDA binaries in the project root.  Run them from the repo
  # root so the CL/ kernel dir is found.  Requires OpenCL headers (/usr/include/CL)
  # -- opencl_setup.c dlopen()s libOpenCL at runtime, so no -lOpenCL is needed.
  CC := $(CC_linux)
  EXE :=
  OUTDIR := $(BUILD_DIR)
  CPPFLAGS := $(CPPFLAGS_common)
  CFLAGS   := $(CFLAGS_common) -march=native -flto=auto
  LDFLAGS  := $(LDFLAGS_common) -flto=auto
  LIBS     := -lpthread -ldl -lgcrypt -lm
  GPU_BACKEND_OBJ := $(OBJDIR)/opencl_setup.o
endif

ifeq ($(BUILD),windows)
  CC := $(CC_windows)
  EXE := .exe

  CPPFLAGS := $(CPPFLAGS_common) -I$(INCDIR)
  CFLAGS   := $(CFLAGS_common)
  LDFLAGS  := $(LDFLAGS_common)

  LIBS := -lwinpthread -lgcrypt -lgpg-error -lbcrypt -lws2_32

  PREP := prep_opencl_headers
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
SORT_PROG     := crackalack_sort$(EXE)
PLAN_PROG     := crackalack_plan$(EXE)
GENKNOWN_PROG := gen_known_hash$(EXE)

BINARIES := \
	$(OUTDIR)/$(GEN_PROG) \
	$(OUTDIR)/$(UNITTEST_PROG) \
	$(OUTDIR)/$(GETCHAIN_PROG) \
	$(OUTDIR)/$(VERIFY_PROG) \
	$(OUTDIR)/$(RTC2RT_PROG) \
	$(OUTDIR)/$(LOOKUP_PROG) \
	$(OUTDIR)/$(PERFECTIFY) \
	$(OUTDIR)/$(ENUMERATE) \
	$(OUTDIR)/$(SORT_PROG) \
	$(OUTDIR)/$(PLAN_PROG)

.PHONY: all linux linux-opencl macos windows clean strip \
        prep_opencl_headers prep_none \
        bundle_windows gen_known_hash

# On-demand convenience target.  NOT part of the default `all`/BINARIES list
# because gen_known_hash needs OpenSSL (-lssl -lcrypto), an extra dependency the
# other tools don't have.  Build it explicitly: `make macos && make gen_known_hash`.
#
# Re-invoke make with the platform's BUILD so the object/output dirs and toolchain
# match the rest of the build (mirrors the linux:/macos: aliases).  Detect macOS
# vs Linux unless the caller already set BUILD on the command line.
# Depend on the absolute binary path so the phony name (gen_known_hash) does not
# normalize to the same target as ./gen_known_hash (which would be circular).
gen_known_hash: $(abspath $(OUTDIR)/$(GENKNOWN_PROG))

all: $(PREP) $(BINARIES)

linux:
	$(MAKE) BUILD=linux all

linux-opencl:
	$(MAKE) BUILD=linux-opencl all

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

$(OBJDIR)/%.o: %.m | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fobjc-arc -c $< -o $@

-include $(DEPS)

$(OUTDIR)/$(GEN_PROG): \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/checkpoint.o \
	$(OBJDIR)/clock.o \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/crackalack_gen.o \
	$(OBJDIR)/file_lock.o \
	$(OBJDIR)/gws.o \
	$(OBJDIR)/hash_validate.o \
	$(OBJDIR)/markov.o \
	$(OBJDIR)/misc.o \
	$(GPU_BACKEND_OBJ) \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/verify.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(UNITTEST_PROG): \
	$(OBJDIR)/bloom.o \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/crackalack_unit_tests.o \
	$(OBJDIR)/fa_batch.o \
	$(OBJDIR)/hash_validate.o \
	$(OBJDIR)/misc.o \
	$(GPU_BACKEND_OBJ) \
	$(OBJDIR)/test_bloom.o \
	$(OBJDIR)/test_chain.o \
	$(OBJDIR)/test_chain_md5_8.o \
	$(OBJDIR)/test_chain_md5_9.o \
	$(OBJDIR)/test_chain_netntlmv1.o \
	$(OBJDIR)/test_chain_ntlm9.o \
	$(OBJDIR)/test_hash.o \
	$(OBJDIR)/test_hash_md5.o \
	$(OBJDIR)/test_hash_netntlmv1.o \
	$(OBJDIR)/test_hash_netntlmv1_7_fast.o \
	$(OBJDIR)/test_hash_ntlm9.o \
	$(OBJDIR)/test_hash_to_index.o \
	$(OBJDIR)/test_hash_to_index_netntlmv1.o \
	$(OBJDIR)/test_hash_to_index_ntlm9.o \
	$(OBJDIR)/test_index_to_plaintext.o \
	$(OBJDIR)/test_index_to_plaintext_ntlm9.o \
	$(OBJDIR)/test_index_to_plaintext_markov.o \
	$(OBJDIR)/markov.o \
	$(OBJDIR)/test_chain_markov.o \
	$(OBJDIR)/test_chain_markov_ntlm8.o \
	$(OBJDIR)/test_chain_markov_ntlm9.o \
	$(OBJDIR)/test_markov.o \
	$(OBJDIR)/test_misc.o \
	$(OBJDIR)/test_sort.o \
	$(OBJDIR)/test_decompress.o \
	$(OBJDIR)/test_shared.o \
	$(OBJDIR)/file_lock.o \
	$(OBJDIR)/parallel_sort.o \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/rti2_decompress.o \
	$(OBJDIR)/sort_utils.o \
	$(OBJDIR)/verify.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(GETCHAIN_PROG): $(OBJDIR)/get_chain.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(VERIFY_PROG): \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/crackalack_verify.o \
	$(OBJDIR)/file_lock.o \
	$(OBJDIR)/hash_validate.o \
	$(OBJDIR)/markov.o \
	$(OBJDIR)/misc.o \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/verify.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(RTC2RT_PROG): \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/crackalack_rtc2rt.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(LOOKUP_PROG): \
	$(OBJDIR)/bloom.o \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/clock.o \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/crackalack_lookup.o \
	$(OBJDIR)/fa_batch.o \
	$(OBJDIR)/file_lock.o \
	$(OBJDIR)/gws.o \
	$(OBJDIR)/hash_validate.o \
	$(OBJDIR)/markov.o \
	$(OBJDIR)/misc.o \
	$(GPU_BACKEND_OBJ) \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/rti2_decompress.o \
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
	$(OBJDIR)/markov.o \
	$(OBJDIR)/test_shared.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(SORT_PROG): \
	$(OBJDIR)/crackalack_sort.o \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/file_lock.o \
	$(OBJDIR)/hash_validate.o \
	$(OBJDIR)/misc.o \
	$(OBJDIR)/parallel_sort.o \
	$(OBJDIR)/sort_utils.o \
	$(GPU_BACKEND_OBJ)
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(PLAN_PROG): \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/crackalack_plan.o \
	$(OBJDIR)/file_lock.o \
	$(OBJDIR)/hash_validate.o \
	$(OBJDIR)/markov.o \
	$(OBJDIR)/misc.o
	$(CC) $(LDFLAGS) $^ -o $@ -lgcrypt -lm

$(abspath $(OUTDIR)/$(GENKNOWN_PROG)): \
	$(OBJDIR)/gen_known_hash.o \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/markov.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS) -lssl -lcrypto

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
	      crackalack_gen crackalack_unit_tests get_chain crackalack_verify crackalack_rtc2rt crackalack_lookup perfectify enumerate_chain crackalack_sort crackalack_plan \
	      libgcrypt-20.dll libgpg-error-0.dll libwinpthread-1.dll
