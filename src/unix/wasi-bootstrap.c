/* Copyright libuv contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * WASI bootstrap stubs. Modeled on CMake's well-tested
 * Utilities/cmlibuv/src/unix/cmake-bootstrap.c, which CMake uses to
 * build a minimal libuv for its own bootstrap compile. CMake's shim
 * zeroes out the pieces of libuv its bootstrap doesn't need — the
 * exact same subset our WASI port skips in v1.
 *
 * What we stub in this file:
 *
 *   - Threadpool (uv__work_submit / uv__work_done / uv__threadpool_cleanup).
 *     Our v1 port is single-threaded; libuv's async DNS and async fs
 *     paths are off.
 *
 *   - Async handles (uv_async_init / send / close / fork / stop). Used
 *     for cross-thread wakeup, which the bootstrap path does not need.
 *
 *   - UDP (uv_udp_open / close). Out-of-scope for v1.
 *
 *   - Dynamic linking (uv_dlopen / dlerror / dlsym / dlclose). Out-of-
 *     scope for v1; our WASM binaries are statically linked.
 *
 *   - Mutex / rwlock / once / pthread_atfork / pthread_sigmask. These
 *     are supplied as trivial single-threaded no-ops. wasix-libc does
 *     provide real pthread primitives; we could wire them up in a
 *     later revision. For v1 the stubs suffice.
 *
 *   - uv__fs_poll_close (so stat-polling fs watchers don't undef).
 *
 *   - Process title. WASI has no argv modification ABI; title ops
 *     are tracked in the common uv-common.c layer but need the
 *     cleanup entry point.
 *
 *   - Linux-specific internal wrappers (accept4/dup3/pipe2/preadv/
 *     pwritev/utimesat/statx/copy_file_range). libuv's core.c probes
 *     them under __linux__ via weak refs; on WASI they're ENOSYS.
 *     We do NOT define them unconditionally — only inside
 *     `#if defined(__linux__)` mirrors, matching CMake's approach —
 *     to avoid stepping on any path that does get compiled on WASI
 *     that uses the real primitive (wasix-libc has `accept4`).
 */

#include "uv.h"
#include "internal.h"

#include <errno.h>
#include <stdlib.h> /* abort */
#include <signal.h>

/* ========================================================================
 * Process title / threadpool cleanup
 * ======================================================================== */

void uv__process_title_cleanup(void) {
}


void uv__threadpool_cleanup(void) {
}


/* ========================================================================
 * UDP
 * ======================================================================== */

int uv_udp_open(uv_udp_t* handle, uv_os_sock_t sock) {
  return UV_ENOSYS;
}


void uv__udp_close(uv_udp_t* handle) {
}


void uv__udp_finish_close(uv_udp_t* handle) {
}


/* ========================================================================
 * fs-poll (stat-polling fallback)
 *
 * Provide the close hook so uv_close() on a uv_fs_poll_t compiles. v1
 * callers should not instantiate fs-poll handles; if they do, the
 * cleanup path is a no-op and the handle is merely freed by the
 * generic uv_close machinery.
 * ======================================================================== */

void uv__fs_poll_close(uv_fs_poll_t* handle) {
}


/* ========================================================================
 * Async
 * ======================================================================== */

int uv_async_init(uv_loop_t* loop, uv_async_t* handle, uv_async_cb async_cb) {
  /* Match cmake-bootstrap.c: return success without wiring anything.
   * Single-threaded WASI v1 has no foreign thread to wake up.
   */
  return 0;
}


int uv_async_send(uv_async_t* handle) {
  return 0;
}


void uv__async_close(uv_async_t* handle) {
}


int uv__async_fork(uv_loop_t* loop) {
  return 0;
}


void uv__async_stop(uv_loop_t* loop) {
}


/* ========================================================================
 * Threadpool / work queue
 *
 * CMake-bootstrap aborts inside uv__work_submit because its bootstrap
 * build never invokes the threadpool. Our WASI v1 is the same. If a
 * call sneaks through, we prefer a visible trap to silent hangs.
 * ======================================================================== */

void uv__work_submit(uv_loop_t* loop, struct uv__work* w,
                     enum uv__work_kind kind,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w, int status)) {
  abort();
}


void uv__work_done(uv_async_t* handle) {
}


/* ========================================================================
 * pthread trampolines (single-threaded bootstrap — no-ops)
 * ======================================================================== */

int uv__pthread_atfork(void (*prepare)(void), void (*parent)(void),
                       void (*child)(void)) {
  return 0;
}


int uv__pthread_sigmask(int how, const sigset_t* set, sigset_t* oset) {
  return 0;
}


/* ========================================================================
 * Mutex / rwlock / once — trivial single-threaded implementations
 * ======================================================================== */

int uv_mutex_init(uv_mutex_t* mutex) {
  return 0;
}


int uv_mutex_init_recursive(uv_mutex_t* mutex) {
  return 0;
}


void uv_mutex_destroy(uv_mutex_t* mutex) {
}


void uv_mutex_lock(uv_mutex_t* mutex) {
}


int uv_mutex_trylock(uv_mutex_t* mutex) {
  return 0;
}


void uv_mutex_unlock(uv_mutex_t* mutex) {
}


int uv_rwlock_init(uv_rwlock_t* rwlock) {
  return 0;
}


void uv_rwlock_destroy(uv_rwlock_t* rwlock) {
}


void uv_rwlock_wrlock(uv_rwlock_t* rwlock) {
}


int uv_rwlock_trywrlock(uv_rwlock_t* rwlock) {
  return 0;
}


void uv_rwlock_wrunlock(uv_rwlock_t* rwlock) {
}


void uv_rwlock_rdlock(uv_rwlock_t* rwlock) {
}


int uv_rwlock_tryrdlock(uv_rwlock_t* rwlock) {
  return 0;
}


void uv_rwlock_rdunlock(uv_rwlock_t* rwlock) {
}


void uv_once(uv_once_t* guard, void (*callback)(void)) {
  if (*guard) {
    return;
  }
  *guard = 1;
  callback();
}
