# GPU-Memory-Aware Co-Running (CUDA) — Design

**Date:** 2026-06-26
**Status:** Approved (design)
**Component:** `crackalack_lookup` / CUDA backend

## Problem

Running `crackalack_lookup` and hashcat on the same NVIDIA GPU causes crackalack to
die mid-run with `gpu_finish failed: -1`. Observed during a ~1.5-day, 4096-table
NetNTLMv1 lookup: the process crashed at the false-alarm-check phase (table 46),
right after `Checking 16569 potential matches (across 23 tables)`.

Root cause is GPU resource contention — almost certainly VRAM exhaustion, since the
two processes share one VRAM pool and the FA-check phase is a bursty allocator. The
`-1` is uninformative because `gpu_finish` (`cuda_setup.c:601`) collapses every
`CUresult` from `cuStreamSynchronize` into `-1`, discarding the real error.

`nvidia-smi` is explicitly *not* the mechanism: it is an external binary requiring
screen-scraping. The in-process equivalent is the CUDA Driver API call
`cuMemGetInfo()`. Two mutually-unaware processes cannot truly coordinate; the
achievable goal is for crackalack to be **polite and self-defensive** — wait for
VRAM before heavy allocations, and survive an OOM by waiting and retrying.

## Goal

Let `crackalack_lookup` run alongside hashcat on the same GPU without crashing:

1. **Proactively** wait for sufficient free VRAM before heavy GPU allocations.
2. **Survive OOM** as a safety net if one slips through — wait indefinitely and
   self-heal when memory is released.

## Non-Goals

- No two-way protocol with hashcat (impossible — it doesn't know crackalack exists).
- No global refactor of the `exit(-1)` GPU macros (`gpu_backend.h:312`, `:372`).
  Only the heavy allocation points are guarded.
- No new CLI flags. Sensible hardcoded defaults only.
- No behavior change on Metal (macOS) or OpenCL (Windows).

## Design

### Phase 0 — Surface the real CUDA error (prerequisite + diagnostic)

Patch `gpu_finish` (`cuda_setup.c:601`) to print the real `CUresult` via
`cuGetErrorString` before returning `-1`. Needed to *confirm* failures are OOM
(currently inferred) and useful as a standalone diagnostic. Low risk.

```c
int gpu_finish(gpu_queue q) {
  CUresult res = cuStreamSynchronize(q);
  if (res != CUDA_SUCCESS) {
    const char *err = NULL;
    cuGetErrorString(res, &err);
    fprintf(stderr, "cuStreamSynchronize failed: %s (%d)\n", err ? err : "?", res);
    return -1;
  }
  return 0;
}
```

### Component 1 — VRAM query in the GPU abstraction

Add to `gpu_backend.h`:

```c
/* Returns 0 on success and fills *free/*total (bytes). Returns non-zero if the
 * backend cannot report VRAM (callers then skip all waiting). */
int gpu_get_free_memory(gpu_device device, uint64_t *free, uint64_t *total);
```

- **CUDA** (`cuda_setup.c`): `cuMemGetInfo(&free, &total)`. Operates on the current
  thread context; the `device` parameter is ignored. No device plumbing needed.
- **Metal** (`metal_setup.m`) and **OpenCL** (`opencl_setup.c`): stub returning
  non-zero (unsupported) → callers skip waiting. No behavior change on these
  platforms.

### Component 2 — Wait-for-VRAM helper (backend-neutral)

```c
void gpu_wait_for_free_vram(gpu_device device, uint64_t needed_bytes);
```

Behavior:
- Query free VRAM via `gpu_get_free_memory`. If unsupported → return immediately.
- If `free >= needed_bytes + SAFETY_MARGIN` → return.
- Else log once on entry (`Waiting for GPU memory: need X MB, Y MB free...`),
  sleep `POLL_INTERVAL`, re-check. Loops forever. Log again when it resumes.

Defaults (hardcoded constants):
- `SAFETY_MARGIN = 256 MB`
- `POLL_INTERVAL = 2 s`

Lives in a backend-neutral C file (uses only `gpu_get_free_memory` + sleep).

### Component 3 — Proactive check at heavy lookup phases

Call `gpu_wait_for_free_vram(device, needed_bytes)` immediately before the two
bursty GPU allocators in `crackalack_lookup.c`:

1. The **false-alarm-check batch** (the phase that crashed).
2. The **precompute** buffers.

`needed_bytes` is the size that phase is about to allocate.

### Component 4 — OOM safety net at the allocation chokepoint

In CUDA `gpu_create_buffer` (`cuda_setup.c:489`): if `cuMemAlloc` returns
`CUDA_ERROR_OUT_OF_MEMORY`, call `gpu_wait_for_free_vram` and retry `cuMemAlloc`
in a loop rather than returning 0. Every allocation gains OOM-survival from one
place. Other errors behave as today (print + return 0). Catches the sync-time
kernel-launch case the proactive check cannot perfectly predict.

## Testing

GPU OOM cannot be unit-tested cleanly. Validation is **manual on dell3**:

1. Co-run `crackalack_lookup` with hashcat (or a VRAM hog) on the same GPU.
2. Confirm crackalack logs `Waiting for GPU memory...` and resumes rather than
   crashing.
3. Force a `gpu_finish` failure and confirm Phase 0 surfaces the real `CUresult`
   instead of a bare `-1`.

## Risks / Open Details

- `gpu_create_buffer` retry must avoid a tight busy-loop — it relies on the
  helper's `POLL_INTERVAL` sleep.
- Computing accurate `needed_bytes` for Component 3 requires reading each phase's
  buffer sizing at the call sites; conservative over-estimation is acceptable.
- Indefinite waiting is intentional (per requirement) — a wedged/zombie hashcat
  would block crackalack forever, but that is the chosen trade-off for unattended
  long runs.
