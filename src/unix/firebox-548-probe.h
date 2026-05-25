/* firebox#548 — libuv mutex / cv-lock state probe.
 *
 * Inherited mechanism foundation: firebox#526 (wasi-libc abort breadcrumb)
 * + firebox#527 (libuv-side stamp sites). This probe extends the
 * breadcrumb model with EAGER per-event emission so the cascade-9 wedge
 * surface (D1' mutex_held / D2' mutex_contention / D3' cv_wait_retval /
 * D4' uv_thread_state) can be classified from log dumps without losing
 * the all-but-last events the single-TLS stamp model elides.
 *
 * --- Why a libuv-layer probe (not wasi-libc) ---
 *
 * Per #545 P2 (cycle 10) finding C:
 *
 *   cascade-9's worker abort propagates via napi callback trampoline
 *   WasiError::Exit(127), BYPASSING the wasi signal path AND proc_exit
 *   syscall entirely.
 *
 * The wedge actor is AT or BELOW the uv_mutex_lock / uv_cond_wait
 * POSIX-portability layer. Per cycle-6 (#527) precedent libuv breadcrumbs
 * are byte-stable — they don't trigger the layout-sensitivity wedge that
 * guest-side wasi-libc mutex probes did in #533. So instrumenting libuv
 * is the lane-safe probe surface.
 *
 * --- Emission discipline (mirrors firebox_526_abort_trace) ---
 *
 *   - Stack-allocated buffer (no malloc — heap may be wedged)
 *   - Raw __wasi_fd_write directly to fd=2 (no stdio — stdio acquires a
 *     mutex via the same pthread_mutex_lock that IS the probe target;
 *     stdio re-entry through the probe would corrupt state)
 *   - Hand-rolled itoa (no snprintf — locale lock + reentrancy)
 *   - One syscall per emit (atomicity wrt other threads' stderr writes)
 *   - No pthread_self() — wasi-libc's pthread_self IS in the wedge actor
 *     surface; we use __wasi_thread_spawn-time tid recorded by libuv's
 *     own wrappers where available, else the cooperative TLS tid stamp.
 *
 * --- Gate ---
 *
 * Gated by env var FIREBOX_548_PROBE=1 (read once and cached at first
 * call). Production builds with the probe header linked but gate-off
 * pay one TLS load + one branch per stamp site. The gate is read via
 * getenv() ONLY ONCE (at the first stamp call), then cached in an
 * atomic flag — subsequent calls bypass libc.
 *
 * --- Schema ---
 *
 * Event lines have a stable schema for grep / awk classification:
 *
 *   FIREBOX_548_PROBE event=<verb> tid=<tid> mutex=<hex> cv=<hex> rc=<int> aux=<int>
 *
 * verbs:
 *   mutex_lock_enter   — uv_mutex_lock about to call pthread_mutex_lock
 *   mutex_lock_exit    — pthread_mutex_lock returned (rc=ret)
 *   mutex_unlock_enter — uv_mutex_unlock about to call
 *   mutex_unlock_exit  — pthread_mutex_unlock returned (rc=ret)
 *   cond_wait_enter    — uv_cond_wait about to call pthread_cond_wait
 *   cond_wait_exit     — pthread_cond_wait returned (rc=ret)
 *   cond_signal_enter  — uv_cond_signal about to call
 *   cond_broadcast_enter — uv_cond_broadcast about to call
 *   thread_create      — uv_thread_create called (mutex=parent_tid,
 *                                                 cv=child_tid)
 *   thread_join        — uv_thread_join entered
 *   worker_init        — threadpool worker entered its loop (aux=worker_id)
 *
 * Stamping a literal in the firebox_526_breadcrumb mechanism is STILL
 * done at the same sites (the abort path emits crumb=<literal> as before);
 * the new probe ADDS per-event emit lines so multi-event classification
 * is possible from log dumps.
 *
 * --- Retirement ---
 *
 * This probe retires when #548's RCA pins cascade-9 at the actual layer
 * (tracked in docs/reference/forks.md §5 retirement registry). The
 * stamping sites and header are removed in lockstep with the fix.
 *
 * See work/tasks/548-edgejs-cascade-9-cycle-11-libuv-mutex-cv-lock-probe/
 */

#ifndef FIREBOX_548_PROBE_H
#define FIREBOX_548_PROBE_H

#include <stddef.h>
#include <stdint.h>

#if defined(__wasi__) || defined(__wasix__)

/* Single emission function. All call sites pass a literal verb pointer
 * (compile-time string), a 64-bit tid (or 0 if unknown), 64-bit mutex
 * pointer (or 0), 64-bit cv pointer (or 0), int rc, int aux.
 *
 * The function is async-signal-safe (no stdio, no malloc, no locks). */
void firebox_548_probe_emit(const char *verb,
                            uint64_t tid,
                            uint64_t mutex_addr,
                            uint64_t cv_addr,
                            int rc,
                            int aux);

/* Cooperative TLS tid stamp — libuv's worker entry stores its tid into
 * this TLS slot so the probe can attribute events to a tid without
 * calling pthread_self() (which is itself in the wedge actor surface).
 * Main / non-worker tids leave this 0 and the probe emits tid=0. */
extern __thread uint64_t firebox_548_self_tid;

/* Convenience macros. All evaluate `mu`/`cv` arguments exactly once. */
#define FIREBOX_548_PROBE_M(verb, mu, rc) \
    firebox_548_probe_emit((verb), firebox_548_self_tid, (uint64_t)(uintptr_t)(mu), 0, (rc), 0)
#define FIREBOX_548_PROBE_C(verb, cv, mu, rc) \
    firebox_548_probe_emit((verb), firebox_548_self_tid, (uint64_t)(uintptr_t)(mu), (uint64_t)(uintptr_t)(cv), (rc), 0)
#define FIREBOX_548_PROBE_T(verb, parent, child, aux) \
    firebox_548_probe_emit((verb), (uint64_t)(parent), (uint64_t)(child), 0, 0, (aux))
#define FIREBOX_548_PROBE_W(verb, aux) \
    firebox_548_probe_emit((verb), firebox_548_self_tid, 0, 0, 0, (aux))

#else  /* non-wasi: compile-out */

static inline void firebox_548_probe_emit(const char *verb,
                                          uint64_t tid,
                                          uint64_t mutex_addr,
                                          uint64_t cv_addr,
                                          int rc,
                                          int aux) {
    (void)verb; (void)tid; (void)mutex_addr; (void)cv_addr; (void)rc; (void)aux;
}

#define FIREBOX_548_PROBE_M(verb, mu, rc)            ((void)0)
#define FIREBOX_548_PROBE_C(verb, cv, mu, rc)        ((void)0)
#define FIREBOX_548_PROBE_T(verb, parent, child, aux) ((void)0)
#define FIREBOX_548_PROBE_W(verb, aux)               ((void)0)

#endif

#endif /* FIREBOX_548_PROBE_H */
