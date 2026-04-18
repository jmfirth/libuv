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
#include <string.h> /* memcmp, memset */
#include <signal.h>
#include <sys/socket.h> /* socketpair */

/* ========================================================================
 * Process title / threadpool cleanup
 *
 * Both are provided by the existing portable files already in the WASI
 * source list:
 *   - uv__process_title_cleanup — src/unix/no-proctitle.c
 *   - uv__threadpool_cleanup    — src/threadpool.c
 * We don't re-stub them here to avoid duplicate-symbol errors at link.
 * ======================================================================== */


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
 * Provided by src/fs-poll.c (portable, already in the source list); do
 * not re-stub here or we hit a duplicate-symbol link error.
 * ======================================================================== */


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
 * Provided by src/threadpool.c (portable, already in the source list).
 * We don't re-stub uv__work_submit / uv__work_done here — doing so
 * would produce duplicate-symbol link errors. If the threadpool code
 * path is exercised at runtime it'll try to spawn worker threads; our
 * v1 scope (CMake bootstrap subset) does not exercise this path.
 * ======================================================================== */


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


/* ========================================================================
 * Condvars / thread join — referenced by src/threadpool.c
 *
 * The threadpool file is in libuv's portable source list, so it ends
 * up in our archive even though we don't exercise the threadpool in
 * bootstrap-scope consumers. Link-time references from dead paths have
 * to resolve; single-threaded no-ops are sufficient.
 * ======================================================================== */

int uv_cond_init(uv_cond_t* cond) {
  return 0;
}


void uv_cond_destroy(uv_cond_t* cond) {
}


void uv_cond_signal(uv_cond_t* cond) {
}


void uv_cond_broadcast(uv_cond_t* cond) {
}


void uv_cond_wait(uv_cond_t* cond, uv_mutex_t* mutex) {
}


int uv_cond_timedwait(uv_cond_t* cond, uv_mutex_t* mutex, uint64_t timeout) {
  return UV_ENOSYS;
}


int uv_thread_join(uv_thread_t* tid) {
  return 0;
}


int uv_thread_create(uv_thread_t* tid, void (*entry)(void* arg), void* arg) {
  (void) tid;
  (void) entry;
  (void) arg;
  return UV_ENOSYS;
}


int uv_thread_create_ex(uv_thread_t* tid,
                        const uv_thread_options_t* params,
                        void (*entry)(void* arg),
                        void* arg) {
  (void) tid;
  (void) params;
  (void) entry;
  (void) arg;
  return UV_ENOSYS;
}


int uv_thread_detach(uv_thread_t* tid) {
  (void) tid;
  return 0;
}


uv_thread_t uv_thread_self(void) {
  uv_thread_t t;
  /* zero-initialize; comparison via uv_thread_equal returns true only
   * against another zero value, which on WASI is fine because we never
   * have more than one thread. */
#ifdef __wasi__
  /* uv_thread_t is pthread_t on Unix; wasix-libc uses a struct/pointer.
   * Just return a zero-initialized value. */
  memset(&t, 0, sizeof(t));
#else
  t = (uv_thread_t){0};
#endif
  return t;
}


int uv_thread_equal(const uv_thread_t* t1, const uv_thread_t* t2) {
  return memcmp(t1, t2, sizeof(*t1)) == 0;
}


/* ========================================================================
 * TCP / poll / socketpair — referenced by core.c / stream.c / process.c
 *
 * These are defined in tcp.c / poll.c which we deliberately exclude
 * from the WASI source set. The references are in dead code paths
 * (no TCP handles get initialized), but wasm-ld still needs
 * resolution. Stub with UV_ENOSYS.
 * ======================================================================== */

void uv__tcp_close(uv_tcp_t* handle) {
  (void) handle;
}


int uv__tcp_nodelay(int fd, int on) {
  (void) fd;
  (void) on;
  return UV_ENOSYS;
}


int uv__tcp_keepalive(int fd, int on, unsigned int delay) {
  (void) fd;
  (void) on;
  (void) delay;
  return UV_ENOSYS;
}


void uv__poll_close(uv_poll_t* handle) {
  (void) handle;
}


/* uv_socketpair wraps socketpair(2). wasix-libc has socketpair; this
 * real implementation is useful because libuv's uv_pipe(fds, flags)
 * on some platforms delegates to uv_socketpair for SOCK_STREAM pipes.
 * For WASI v1 we gate it behind a trivial wrapper that forwards.
 */
int uv_socketpair(int type, int protocol, uv_os_sock_t fds[2], int flags0, int flags1) {
  int sv[2];
  (void) flags0;
  (void) flags1;
  if (socketpair(AF_UNIX, type, protocol, sv) != 0)
    return UV__ERR(errno);
  fds[0] = sv[0];
  fds[1] = sv[1];
  return 0;
}


/* uv__fs_copy_file_range: Linux-only fast-path used by uv_fs_copyfile.
 * wasix-libc does not expose copy_file_range. Return ENOSYS so libuv's
 * fallback (read+write loop) kicks in. Only referenced under
 * __linux__ / __FreeBSD__ guards in libuv, but let's provide a safe
 * fallback just in case.
 */
#if defined(__linux__) || defined(__FreeBSD__)
ssize_t uv__fs_copy_file_range(int fd_in, off_t* off_in,
                               int fd_out, off_t* off_out,
                               size_t len, unsigned int flags) {
  (void) fd_in; (void) off_in; (void) fd_out; (void) off_out;
  (void) len; (void) flags;
  errno = ENOSYS;
  return -1;
}
#endif
