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

# Coverage instrumentation (opt-in: `make <target> COVERAGE=1`).  Strips LTO and
# -O3 (which break / distort gcov line mapping) and adds gcov-style coverage to
# both compile and link.  Produces .gcno/.gcda consumable by gcov / llvm-cov gcov.
ifdef COVERAGE
  CFLAGS  := $(filter-out -flto -flto=auto -O3 -O2 -O1,$(CFLAGS)) -O0 --coverage
  LDFLAGS := $(filter-out -flto -flto=auto,$(LDFLAGS)) --coverage
endif

# ThreadSanitizer (opt-in: `make <target> TSAN=1`).  Mutually exclusive with
# COVERAGE/ASan.  Strips LTO/-O3 (clean TSan reports) and adds -fsanitize=thread.
ifdef TSAN
  CFLAGS  := $(filter-out -flto -flto=auto -O3 -O2 -O1,$(CFLAGS)) -O1 -g -fsanitize=thread
  LDFLAGS := $(filter-out -flto -flto=auto,$(LDFLAGS)) -fsanitize=thread
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
RT2RTC_PROG   := crackalack_rt2rtc$(EXE)
GENKNOWN_PROG    := gen_known_hash$(EXE)
CPU_TESTS_PROG   := crackalack_cpu_tests$(EXE)

BINARIES := \
	$(OUTDIR)/$(GEN_PROG) \
	$(OUTDIR)/$(UNITTEST_PROG) \
	$(OUTDIR)/$(GETCHAIN_PROG) \
	$(OUTDIR)/$(VERIFY_PROG) \
	$(OUTDIR)/$(RTC2RT_PROG) \
	$(OUTDIR)/$(RT2RTC_PROG) \
	$(OUTDIR)/$(LOOKUP_PROG) \
	$(OUTDIR)/$(PERFECTIFY) \
	$(OUTDIR)/$(ENUMERATE) \
	$(OUTDIR)/$(SORT_PROG) \
	$(OUTDIR)/$(PLAN_PROG)

.PHONY: all linux linux-opencl macos windows clean strip \
        prep_opencl_headers prep_none \
        bundle_windows gen_known_hash cpu-tests tsan-sort

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

# cpu-tests: build crackalack_cpu_tests with no GPU backend.
# Uses a dedicated build directory (build/cpu-tests/obj) so it never clobbers
# the normal platform build.  Object files are recompiled from scratch with
# CPU-tests-specific CPPFLAGS (no USE_CUDA, no USE_METAL on Linux; USE_METAL
# on macOS so gpu_backend.h resolves types as void* without needing Metal libs).
#
# Linux requirements: build-essential libgcrypt20-dev opencl-headers
# macOS requirements: brew install libgcrypt  (Xcode CLT already on PATH)
CPU_TESTS_OBJDIR := build/cpu-tests/obj
ifeq ($(shell uname -s),Darwin)
  CPU_TESTS_CC       := clang
  CPU_TESTS_CPPFLAGS := -DUSE_METAL -I/opt/homebrew/include
  CPU_TESTS_CFLAGS   := -Wall -O3 -g
  CPU_TESTS_LDFLAGS  := -L/opt/homebrew/lib
  CPU_TESTS_LIBS     := -lpthread -lgcrypt -lm
else
  CPU_TESTS_CC       := gcc
  CPU_TESTS_CPPFLAGS := -I/usr/include
  CPU_TESTS_CFLAGS   := -Wall -O3 -g
  CPU_TESTS_LDFLAGS  :=
  CPU_TESTS_LIBS     := -lpthread -lgcrypt -lm
endif

CPU_TESTS_OBJS := \
	$(CPU_TESTS_OBJDIR)/crackalack_cpu_tests.o \
	$(CPU_TESTS_OBJDIR)/cpu_tests_common.o \
	$(CPU_TESTS_OBJDIR)/test_challenge_host.o \
	$(CPU_TESTS_OBJDIR)/test_misc.o \
	$(CPU_TESTS_OBJDIR)/test_bloom.o \
	$(CPU_TESTS_OBJDIR)/test_sort.o \
	$(CPU_TESTS_OBJDIR)/test_decompress.o \
	$(CPU_TESTS_OBJDIR)/test_precompute_collate.o \
	$(CPU_TESTS_OBJDIR)/test_markov.o \
	$(CPU_TESTS_OBJDIR)/test_mask_parse.o \
	$(CPU_TESTS_OBJDIR)/test_markov_mask.o \
	$(CPU_TESTS_OBJDIR)/markov_mask.o \
	$(CPU_TESTS_OBJDIR)/test_hcmask.o \
	$(CPU_TESTS_OBJDIR)/hcmask.o \
	$(CPU_TESTS_OBJDIR)/test_golden.o \
	$(CPU_TESTS_OBJDIR)/test_shared.o \
	$(CPU_TESTS_OBJDIR)/misc.o \
	$(CPU_TESTS_OBJDIR)/fa_batch.o \
	$(CPU_TESTS_OBJDIR)/bloom.o \
	$(CPU_TESTS_OBJDIR)/cpu_rt_functions.o \
	$(CPU_TESTS_OBJDIR)/charset.o \
	$(CPU_TESTS_OBJDIR)/markov.o \
	$(CPU_TESTS_OBJDIR)/mask_parse.o \
	$(CPU_TESTS_OBJDIR)/sort_utils.o \
	$(CPU_TESTS_OBJDIR)/parallel_sort.o \
	$(CPU_TESTS_OBJDIR)/precompute_collate.o \
	$(CPU_TESTS_OBJDIR)/rtc_decompress.o \
	$(CPU_TESTS_OBJDIR)/rti2_decompress.o \
	$(CPU_TESTS_OBJDIR)/file_lock.o \
	$(CPU_TESTS_OBJDIR)/hash_validate.o

$(CPU_TESTS_OBJDIR):
	mkdir -p $@

# -MMD -MP emits per-object .d header-dependency files so that a change to a
# header (e.g. the rt_parameters struct layout in misc.h) forces a rebuild of
# every .o that includes it.  Without this, switching branches with a shared
# build/cpu-tests/obj mixes objects compiled against different struct layouts,
# producing an ABI mismatch and bogus test failures.
$(CPU_TESTS_OBJDIR)/%.o: %.c | $(CPU_TESTS_OBJDIR)
	$(CPU_TESTS_CC) $(CPU_TESTS_CPPFLAGS) $(CPU_TESTS_CFLAGS) $(DEPFLAGS) -c $< -o $@

cpu-tests: $(CPU_TESTS_OBJS)
	$(CPU_TESTS_CC) $(CPU_TESTS_LDFLAGS) $^ -o $(OUTDIR)/$(CPU_TESTS_PROG) $(CPU_TESTS_LIBS)
	@echo "Built $(OUTDIR)/$(CPU_TESTS_PROG)"

CPU_TESTS_DEPS := $(CPU_TESTS_OBJS:.o=.d)
-include $(CPU_TESTS_DEPS)

# tsan-sort: build and run test_parallel_sort_tsan.c under ThreadSanitizer.
# Uses a dedicated build directory (build/tsan-sort/obj) so it never clobbers
# the normal platform build.
TSAN_SORT_OBJDIR := build/tsan-sort/obj
TSAN_SORT_BIN    := $(CURDIR)/build/tsan-sort/test_parallel_sort_tsan
ifeq ($(shell uname -s),Darwin)
  TSAN_SORT_CC    := clang
  TSAN_SORT_INC   := -I/opt/homebrew/include
  TSAN_RUN_PREFIX :=
else
  TSAN_SORT_CC    := gcc
  TSAN_SORT_INC   :=
  # On Linux, high-entropy ASLR collides with TSan's fixed shadow mapping
  # ("FATAL: ThreadSanitizer: unexpected memory mapping").  Run under
  # `setarch -R` to disable ASLR for the process.  (setarch is util-linux;
  # not present/needed on macOS.)
  TSAN_RUN_PREFIX := setarch $(shell uname -m) -R
endif
TSAN_SORT_CFLAGS  := -Wall -O1 -g -fsanitize=thread
TSAN_SORT_LDFLAGS := -fsanitize=thread

$(TSAN_SORT_OBJDIR):
	mkdir -p $@

$(TSAN_SORT_OBJDIR)/test_parallel_sort_tsan.o: test_parallel_sort_tsan.c | $(TSAN_SORT_OBJDIR)
	$(TSAN_SORT_CC) $(TSAN_SORT_INC) $(TSAN_SORT_CFLAGS) -c $< -o $@

$(TSAN_SORT_OBJDIR)/parallel_sort.o: parallel_sort.c | $(TSAN_SORT_OBJDIR)
	$(TSAN_SORT_CC) $(TSAN_SORT_INC) $(TSAN_SORT_CFLAGS) -c $< -o $@

tsan-sort: $(TSAN_SORT_OBJDIR)/test_parallel_sort_tsan.o $(TSAN_SORT_OBJDIR)/parallel_sort.o
	$(TSAN_SORT_CC) $(TSAN_SORT_LDFLAGS) $^ -o $(TSAN_SORT_BIN) -lpthread
	@echo "==> Running TSan sort test..."
	$(TSAN_RUN_PREFIX) $(TSAN_SORT_BIN)

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
	$(OBJDIR)/hcmask.o \
	$(OBJDIR)/markov.o \
	$(OBJDIR)/markov_mask.o \
	$(OBJDIR)/mask_parse.o \
	$(OBJDIR)/misc.o \
	$(GPU_BACKEND_OBJ) \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/verify.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(UNITTEST_PROG): \
	$(OBJDIR)/bloom.o \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/cpu_tests_common.o \
	$(OBJDIR)/crackalack_unit_tests.o \
	$(OBJDIR)/fa_batch.o \
	$(OBJDIR)/gws.o \
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
	$(OBJDIR)/test_index_to_plaintext_mask.o \
	$(OBJDIR)/markov.o \
	$(OBJDIR)/markov_mask.o \
	$(OBJDIR)/test_chain_markov.o \
	$(OBJDIR)/test_chain_markov_mask.o \
	$(OBJDIR)/test_chain_markov_ntlm8.o \
	$(OBJDIR)/test_chain_markov_ntlm9.o \
	$(OBJDIR)/test_chain_mask.o \
	$(OBJDIR)/test_markov.o \
	$(OBJDIR)/mask_parse.o \
	$(OBJDIR)/test_mask_parse.o \
	$(OBJDIR)/test_markov_mask.o \
	$(OBJDIR)/hcmask.o \
	$(OBJDIR)/test_hcmask.o \
	$(OBJDIR)/test_challenge_host.o \
	$(OBJDIR)/test_misc.o \
	$(OBJDIR)/test_golden.o \
	$(OBJDIR)/precompute_collate.o \
	$(OBJDIR)/test_precompute_collate.o \
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
	$(OBJDIR)/hcmask.o \
	$(OBJDIR)/markov.o \
	$(OBJDIR)/markov_mask.o \
	$(OBJDIR)/mask_parse.o \
	$(OBJDIR)/misc.o \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/verify.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(RTC2RT_PROG): \
	$(OBJDIR)/rtc_decompress.o \
	$(OBJDIR)/crackalack_rtc2rt.o
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OUTDIR)/$(RT2RTC_PROG): \
	$(OBJDIR)/rtc_compress.o \
	$(OBJDIR)/crackalack_rt2rtc.o
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
	$(OBJDIR)/markov_mask.o \
	$(OBJDIR)/mask_parse.o \
	$(OBJDIR)/misc.o \
	$(OBJDIR)/precompute_collate.o \
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
	$(OBJDIR)/gws.o \
	$(OBJDIR)/mask_parse.o \
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
	$(OBJDIR)/mask_parse.o \
	$(OBJDIR)/misc.o
	$(CC) $(LDFLAGS) $^ -o $@ -lgcrypt -lm

$(abspath $(OUTDIR)/$(GENKNOWN_PROG)): \
	$(OBJDIR)/gen_known_hash.o \
	$(OBJDIR)/cpu_rt_functions.o \
	$(OBJDIR)/charset.o \
	$(OBJDIR)/markov.o \
	$(OBJDIR)/markov_mask.o \
	$(OBJDIR)/mask_parse.o
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
	      crackalack_gen crackalack_unit_tests crackalack_cpu_tests get_chain crackalack_verify crackalack_rtc2rt crackalack_rt2rtc crackalack_lookup perfectify enumerate_chain crackalack_sort crackalack_plan \
	      libgcrypt-20.dll libgpg-error-0.dll libwinpthread-1.dll
