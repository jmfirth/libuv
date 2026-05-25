/* firebox#527 — breadcrumb stamp for cascade-9 wedge isolation.
 *
 * Inherited mechanism from firebox#526: wasi-libc's patched abort()
 * reads a TLS-backed `firebox_526_breadcrumb` pointer and emits
 *   FIREBOX_526_ABORT_FROM crumb=<literal>
 * via raw __wasi_fd_write before raising SIGABRT. Each suspected
 * abort() call site is annotated with a literal string identifying
 * the source location; the literal is recorded in TLS by a single-
 * instruction store via firebox_526_stamp().
 *
 * Why this exists at the libuv layer (firebox#527): #526 phase 4
 * empirically observed every wedge-class abort emitting
 * `crumb=(unset)` — i.e., the wedge actor bypasses every wasi-libc-
 * internal stamp site (raise, __assert_fail, __pthread_exit, signal
 * handlers, abort-internal raise). The candidate space narrows to
 * "caller code outside wasi-libc that calls abort() directly."
 * libuv is the highest-priority surface per #511 Phase 3 evidence
 * (abort fires on PID=1 main thread immediately after uv__work_submit
 * returns).
 *
 * Implementation notes:
 *
 *  - The stamp is a TLS pointer assignment, not a function side-
 *    effect on the stamp argument. The argument MUST be a string
 *    literal (or otherwise outlive the abort path); we never copy
 *    or free it. Cost per stamp: one TLS store (~1 instruction).
 *
 *  - clang on non-Emscripten wasm32 REJECTS __builtin_return_address;
 *    breadcrumbs are the wasm-correct substitute for backtrace-style
 *    "who called abort()" probing. See
 *    [[class_lesson_wasm_no_builtin_return_address]] for the family
 *    of constraints this works around.
 *
 *  - We gate on __wasi__ / __wasix__ so non-wasi builds compile
 *    (the symbol is only defined in the wasi-libc fork). On other
 *    platforms the stamp becomes a no-op static-inline that the
 *    optimizer drops.
 *
 *  - Stamp literals follow the convention:
 *        "libuv:<file>:<line>:<symptom>"
 *    so the post-mortem grep is exact (one literal -> one source
 *    line). Don't compose at runtime; the literal pointer IS the
 *    breadcrumb.
 *
 * See work/tasks/527-edgejs-cascade-9-libuv-abort-instrumentation/
 * and work/tasks/526-edgejs-cascade-9-wasilibc-fork-abort-wrapper/
 * for the full investigation arc.
 */

#ifndef FIREBOX_526_BREADCRUMB_H
#define FIREBOX_526_BREADCRUMB_H

#if defined(__wasi__) || defined(__wasix__)
extern void firebox_526_stamp(const char *crumb);
#else
static inline void firebox_526_stamp(const char *crumb) { (void)crumb; }
#endif

#endif /* FIREBOX_526_BREADCRUMB_H */
