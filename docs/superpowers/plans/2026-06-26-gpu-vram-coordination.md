# GPU-Memory-Aware Co-Running (CUDA) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let `crackalack_lookup` run on a GPU shared with hashcat without crashing — wait for free VRAM before heavy allocations, and survive an OOM by waiting indefinitely and retrying.

**Architecture:** Add a VRAM-query primitive to the GPU abstraction (`cuMemGetInfo` on CUDA; stub elsewhere). A backend-neutral `gpu_wait_for_free_vram` helper polls until enough VRAM is free. Wire it in proactively before the two bursty lookup allocators (false-alarm-check, precompute), and as an OOM safety net inside CUDA `gpu_create_buffer`. Also surface the real `CUresult` in `gpu_finish` so failures are diagnosable.

**Tech Stack:** C, CUDA Driver API (`cuMemGetInfo`, `cuMemAlloc`, `cuGetErrorString`), POSIX `usleep`.

---

## Platform / build notes (read first)

- The feature is **CUDA/Linux only** in behavior. Metal (macOS) and OpenCL (Windows) get stubs and are behavior-unchanged.
- `cuda_setup.c` only compiles on a CUDA host. **Tasks touching `cuda_setup.c` must be compile-verified on a CUDA box (dell3), not on this Mac.** Run there: `make clean && make`.
- Backend-neutral files (`gpu_backend.h`, `gws.c`, `crackalack_lookup.c`) compile on this Mac via `make` (Metal). Use that to catch syntax errors in the neutral code early.
- Runtime validation (the actual co-run-with-hashcat test) happens on dell3 — see Task 6.
- Per the repo convention, commit after each task. Push only when the user asks.

---

## File Structure

- `cuda_setup.c` — CUDA backend. Add `gpu_get_free_memory`; add OOM retry to `gpu_create_buffer`; improve `gpu_finish` error reporting.
- `metal_setup.m` — Metal backend. Add `gpu_get_free_memory` stub.
- `opencl_setup.c` — OpenCL backend. Add `gpu_get_free_memory` stub.
- `gpu_backend.h` — declare `gpu_get_free_memory` (all 3 backend blocks) and `gpu_wait_for_free_vram` (once, common).
- `gws.c` — implement backend-neutral `gpu_wait_for_free_vram`.
- `crackalack_lookup.c` — call `gpu_wait_for_free_vram` before the FA-check and precompute allocation blocks.

---

## Task 1: Surface the real CUDA error in `gpu_finish`

**Files:**
- Modify: `cuda_setup.c:601-604`

This is the diagnostic prerequisite. No unit test (GPU sync error can't be triggered in a unit test); verification is compilation + reading the diff.

- [ ] **Step 1: Replace `gpu_finish`**

In `cuda_setup.c`, replace the current body (lines 601-604):

```c
int gpu_finish(gpu_queue q) {
  CUresult res = cuStreamSynchronize(q);
  return (res == CUDA_SUCCESS) ? 0 : -1;
}
```

with:

```c
int gpu_finish(gpu_queue q) {
  CUresult res = cuStreamSynchronize(q);
  if (res != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuStreamSynchronize failed: %s (%d)\n", err ? err : "(unknown)", res);
    return -1;
  }
  return 0;
}
```

- [ ] **Step 2: Compile-verify on a CUDA box (dell3)**

Run: `make clean && make`
Expected: builds with no new warnings/errors. (`cuGetErrorString` is already used elsewhere in this file, so no new include is needed.)

- [ ] **Step 3: Commit**

```bash
git add cuda_setup.c
git commit -m "fix(cuda): surface real CUresult in gpu_finish instead of bare -1"
```

---

## Task 2: Add `gpu_get_free_memory` to the GPU abstraction

**Files:**
- Modify: `gpu_backend.h` (3 backend blocks: METAL ~185-201, CUDA ~203-220, OpenCL ~222+)
- Modify: `cuda_setup.c` (add impl, e.g. next to `gpu_finish`)
- Modify: `metal_setup.m` (add stub)
- Modify: `opencl_setup.c` (add stub)

Contract: returns `0` on success with `*free`/`*total` set (bytes); returns non-zero if VRAM can't be reported (callers then skip waiting).

- [ ] **Step 1: Declare in `gpu_backend.h` — Metal block**

In the `#ifdef USE_METAL` block, after the `gpu_finish` declaration (line ~195), add:

```c
int gpu_get_free_memory(gpu_device device, uint64_t *free_bytes, uint64_t *total_bytes);
```

- [ ] **Step 2: Declare in `gpu_backend.h` — CUDA block**

In the `#elif defined(USE_CUDA)` block, after its `gpu_finish` declaration (line ~214), add the identical line:

```c
int gpu_get_free_memory(gpu_device device, uint64_t *free_bytes, uint64_t *total_bytes);
```

- [ ] **Step 3: Declare in `gpu_backend.h` — OpenCL block**

In the `#else` (OpenCL) block, add the identical line among the other non-`rc_*` function declarations (e.g. near `void *rc_dlopen(...)` at line ~224):

```c
int gpu_get_free_memory(gpu_device device, uint64_t *free_bytes, uint64_t *total_bytes);
```

- [ ] **Step 4: Implement on CUDA**

In `cuda_setup.c`, immediately after `gpu_finish`, add:

```c
/* Reports free/total VRAM in bytes via the current CUDA context.  The device
 * parameter is unused (cuMemGetInfo operates on the current context).  Returns
 * 0 on success, non-zero on failure (caller then skips VRAM waiting). */
int gpu_get_free_memory(gpu_device device, uint64_t *free_bytes, uint64_t *total_bytes) {
  (void)device;
  size_t f = 0, t = 0;
  CUresult res = cuMemGetInfo(&f, &t);
  if (res != CUDA_SUCCESS)
    return -1;
  if (free_bytes)  *free_bytes  = (uint64_t)f;
  if (total_bytes) *total_bytes = (uint64_t)t;
  return 0;
}
```

- [ ] **Step 5: Stub on Metal**

In `metal_setup.m`, add (near `gpu_finish`, line ~672):

```c
/* VRAM-budget waiting is a CUDA-only feature; report "unsupported" so callers
 * skip waiting on Metal. */
int gpu_get_free_memory(gpu_device device, uint64_t *free_bytes, uint64_t *total_bytes) {
  (void)device; (void)free_bytes; (void)total_bytes;
  return -1;
}
```

- [ ] **Step 6: Stub on OpenCL**

In `opencl_setup.c`, add (alongside the other `gpu_*` function definitions):

```c
/* VRAM-budget waiting is a CUDA-only feature; report "unsupported" so callers
 * skip waiting on OpenCL. */
int gpu_get_free_memory(gpu_device device, uint64_t *free_bytes, uint64_t *total_bytes) {
  (void)device; (void)free_bytes; (void)total_bytes;
  return -1;
}
```

- [ ] **Step 7: Compile-verify**

On this Mac (exercises Metal stub + header): `make clean && make` — expected: builds clean.
On dell3 (exercises CUDA impl): `make clean && make` — expected: builds clean.

- [ ] **Step 8: Commit**

```bash
git add gpu_backend.h cuda_setup.c metal_setup.m opencl_setup.c
git commit -m "feat(gpu): add gpu_get_free_memory (cuMemGetInfo on CUDA, stub elsewhere)"
```

---

## Task 3: Add the backend-neutral `gpu_wait_for_free_vram` helper

**Files:**
- Modify: `gpu_backend.h` (common section, outside the per-backend `#ifdef` blocks)
- Modify: `gws.c` (implementation; add `#include <unistd.h>` and `#include <stdio.h>`/`<stdint.h>` as needed)

- [ ] **Step 1: Declare once in `gpu_backend.h` (common)**

Add this declaration in a backend-neutral location — immediately AFTER the three-way `#ifdef USE_METAL / #elif USE_CUDA / #else` block closes with its `#endif` (the one ending the low-level backend ops, around line 250+; place it after that `#endif`):

```c
/* Backend-neutral: blocks until at least needed_bytes (+ a safety margin) of GPU
 * memory is free, polling periodically.  No-op on backends where
 * gpu_get_free_memory reports "unsupported".  Logs once on entry and once on
 * resume.  Used to coexist with other GPU processes (e.g. hashcat). */
void gpu_wait_for_free_vram(gpu_device device, uint64_t needed_bytes);
```

- [ ] **Step 2: Implement in `gws.c`**

At the top of `gws.c`, ensure these includes are present (add any missing):

```c
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
```

Then add at the end of `gws.c`:

```c
/* Safety headroom required on top of the requested allocation. */
#define GPU_VRAM_SAFETY_MARGIN ((uint64_t)256 * 1024 * 1024)
/* How long to sleep between free-VRAM polls, in microseconds (2 s). */
#define GPU_VRAM_POLL_USEC (2 * 1000 * 1000)

void gpu_wait_for_free_vram(gpu_device device, uint64_t needed_bytes) {
  uint64_t required = needed_bytes + GPU_VRAM_SAFETY_MARGIN;
  int waited = 0;

  for (;;) {
    uint64_t free_bytes = 0, total_bytes = 0;

    /* If the backend can't report VRAM, don't wait at all. */
    if (gpu_get_free_memory(device, &free_bytes, &total_bytes) != 0)
      return;

    if (free_bytes >= required) {
      if (waited)
        printf("GPU memory available again (%"PRIu64" MB free); resuming.\n",
               free_bytes / (1024 * 1024));
      return;
    }

    if (!waited) {
      printf("Waiting for GPU memory: need %"PRIu64" MB, %"PRIu64" MB free. "
             "(Is another GPU process running?)\n",
             required / (1024 * 1024), free_bytes / (1024 * 1024));
      fflush(stdout);
      waited = 1;
    }

    usleep(GPU_VRAM_POLL_USEC);
  }
}
```

Note: `PRIu64` requires `<inttypes.h>`. If `gws.c` does not already include it, add `#include <inttypes.h>` with the other includes.

- [ ] **Step 3: Compile-verify**

On this Mac: `make clean && make` — expected: builds clean (this exercises the helper + the Metal stub path, where `gpu_get_free_memory` returns non-zero so the helper returns immediately).

- [ ] **Step 4: Commit**

```bash
git add gpu_backend.h gws.c
git commit -m "feat(gpu): add gpu_wait_for_free_vram poll-until-free helper"
```

---

## Task 4: OOM safety net in CUDA `gpu_create_buffer`

**Files:**
- Modify: `cuda_setup.c:489-501`

Retry `cuMemAlloc` on `CUDA_ERROR_OUT_OF_MEMORY` by waiting for VRAM; keep current behavior (print + return 0) for all other errors.

- [ ] **Step 1: Replace `gpu_create_buffer`**

Replace the current body (lines 489-501):

```c
gpu_buffer gpu_create_buffer(gpu_context context, int flags, size_t size) {
  (void)context;  /* not needed; context is implicit on current thread */
  (void)flags;    /* CUDA doesn't distinguish RO/WO/RW at allocation time */
  CUdeviceptr dptr = 0;
  CUresult res = cuMemAlloc(&dptr, size);
  if (res != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuMemAlloc(%zu) failed: %s\n", size, err ? err : "(unknown)");
    return 0;
  }
  return dptr;
}
```

with:

```c
gpu_buffer gpu_create_buffer(gpu_context context, int flags, size_t size) {
  (void)context;  /* not needed; context is implicit on current thread */
  (void)flags;    /* CUDA doesn't distinguish RO/WO/RW at allocation time */
  CUdeviceptr dptr = 0;

  for (;;) {
    CUresult res = cuMemAlloc(&dptr, size);
    if (res == CUDA_SUCCESS)
      return dptr;

    /* If we ran out of VRAM (likely a co-running GPU process such as hashcat),
     * wait for memory to free up and retry rather than failing the run. */
    if (res == CUDA_ERROR_OUT_OF_MEMORY) {
      gpu_wait_for_free_vram(0, (uint64_t)size);
      continue;
    }

    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuMemAlloc(%zu) failed: %s\n", size, err ? err : "(unknown)");
    return 0;
  }
}
```

Note: `gpu_wait_for_free_vram` is declared in `gpu_backend.h` (already included by `cuda_setup.c`). The device arg is `0` (ignored by the CUDA `gpu_get_free_memory`). The helper's own `usleep` prevents a tight busy-loop.

- [ ] **Step 2: Compile-verify on dell3**

Run: `make clean && make`
Expected: builds clean.

- [ ] **Step 3: Commit**

```bash
git add cuda_setup.c
git commit -m "feat(cuda): retry cuMemAlloc on OOM by waiting for free VRAM"
```

---

## Task 5: Proactive VRAM wait before lookup's heavy allocators

**Files:**
- Modify: `crackalack_lookup.c` (FA-check block ~before line 1246; precompute block ~before line 1535)

Insert a proactive wait sized to the dominant GPU buffers each phase is about to allocate. Line numbers are approximate — anchor on the listed `CLCREATEARG` lines.

- [ ] **Step 1: FA-check phase — insert before the first `CLCREATEARG`**

Find this line in the false-alarm-check kernel function (currently line 1246):

```c
  CLCREATEARG(0, hash_type_buffer, CL_RO, args->hash_type, sizeof(gpu_uint));
```

Immediately BEFORE it, insert:

```c
  /* Coexist with other GPU processes: wait for enough VRAM for the dominant
   * device buffers (start indices + outputs + hash base indices) before
   * allocating them. */
  gpu_wait_for_free_vram(gpu->device,
    ((uint64_t)num_start_indices + (uint64_t)output_block_len +
     (uint64_t)num_hash_base_indices) * sizeof(gpu_ulong));
```

(`num_start_indices`, `output_block_len`, and `num_hash_base_indices` are all in scope at this point — `output_block_len` is set at line ~1238, `num_start_indices` and `num_hash_base_indices` are parameters/locals used by the `CLCREATEARG_ARRAY` calls just below.)

- [ ] **Step 2: Precompute phase — insert before the first `CLCREATEARG`**

Find this line in the precompute host function (currently line 1535):

```c
  CLCREATEARG(0, hash_type_buffer, CL_RO, args->hash_type, sizeof(gpu_uint));
```

Immediately BEFORE it, insert:

```c
  /* Coexist with other GPU processes: wait for enough VRAM for the precompute
   * output buffer before allocating it. */
  gpu_wait_for_free_vram(gpu->device, (uint64_t)output_block_len * sizeof(gpu_ulong));
```

(`output_block_len` is set at line ~1514 and `gpu->device` is in scope.)

- [ ] **Step 3: Compile-verify**

On this Mac: `make clean && make` — expected: builds clean (verifies the new calls reference in-scope variables and the declaration resolves).
On dell3: `make clean && make` — expected: builds clean.

- [ ] **Step 4: Commit**

```bash
git add crackalack_lookup.c
git commit -m "feat(lookup): wait for free VRAM before FA-check and precompute allocations"
```

---

## Task 6: End-to-end validation on dell3 (manual)

GPU OOM behavior cannot be unit-tested; this is the acceptance test. No code changes.

- [ ] **Step 1: Build on dell3**

Run: `make clean && make` (with the correct `CUDA_PATH` per the dell2/dell3 build notes).
Expected: clean build of `crackalack_lookup`.

- [ ] **Step 2: Reproduce the co-run scenario**

Start a VRAM-heavy hashcat job on the same GPU, then launch a `crackalack_lookup` run against a table set (the same NetNTLMv1 7-char set that crashed, or any set large enough to reach the FA-check phase).

Expected observations:
- crackalack prints `Waiting for GPU memory: need N MB, M MB free...` when VRAM is tight, and `GPU memory available again ...; resuming.` when hashcat releases memory — instead of dying with `gpu_finish failed: -1`.
- The lookup proceeds through the false-alarm-check phase that previously crashed (table ~46) and continues.

- [ ] **Step 3: Confirm the diagnostic (optional but recommended)**

If a failure does occur, confirm the message now names the real `CUresult` (e.g. `cuStreamSynchronize failed: out of memory (2)`) rather than a bare `-1`, validating Task 1.

- [ ] **Step 4: Record the result**

Note in the run log / memory whether the co-run completed without crashing. If it surfaced a non-OOM `CUresult`, capture it — the design assumed OOM, and a different error would mean revisiting scope.

---

## Self-review

- **Spec coverage:** Phase 0 → Task 1. Component 1 (`gpu_get_free_memory`) → Task 2. Component 2 (`gpu_wait_for_free_vram`) → Task 3. Component 4 (OOM safety net in `gpu_create_buffer`) → Task 4. Component 3 (proactive checks at FA-check + precompute) → Task 5. Testing section → Task 6. All covered.
- **Defaults:** `SAFETY_MARGIN = 256 MB`, `POLL_INTERVAL = 2 s` — match the spec (Task 3).
- **No new CLI flags** — confirmed, none added.
- **Metal/OpenCL unchanged** — stubs return non-zero so the helper no-ops (Task 2 steps 5-6).
- **Type/name consistency:** `gpu_get_free_memory(gpu_device, uint64_t*, uint64_t*)` and `gpu_wait_for_free_vram(gpu_device, uint64_t)` used identically in declarations, impl, and all call sites.
