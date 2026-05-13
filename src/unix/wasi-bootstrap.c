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


/* ========================================================================
 * Public uv_tcp_/uv_udp_/uv_poll_ surface
 *
 * Edge.js (`wasmerio/edgejs`, firebox#366 Mechanism α) compiles a wider
 * surface than our v1 CMake-bootstrap scope: edge_tcp_wrap.cc / node's
 * tcp_wrap.cc reference the FULL public `uv_tcp_*` API; Node's
 * dns_wrap.cc references `uv_getaddrinfo` / `uv_getnameinfo`; the
 * UDP wrap references `uv_udp_*` set-methods; threadpool worker code
 * references `uv_sem_*`; the dynamic-loader binding references
 * `uv_dl*`. Our CMake source list deliberately omits tcp.c / udp.c /
 * poll.c / dl.c / getaddrinfo.c — all of which expand to dead paths
 * since wasm32-wasi has no real socket-handle/posix-poll/dlopen
 * primitives behind them. But the symbol references in Edge.js's
 * static-link still need resolution.
 *
 * All stubs return UV_ENOSYS and set errno. JS callers see the
 * standard "ENOSYS"/"not implemented" error path Node already has for
 * other unsupported platforms. uv_get_*_memory and uv_loadavg/uv_uptime
 * are in src/unix/wasi.c (real per-platform-utility location); these
 * are the cross-link stubs only.
 * ======================================================================== */

/* -- uv_tcp_* (public) -- */

int uv_tcp_init(uv_loop_t* loop, uv_tcp_t* handle) {
  (void) loop; (void) handle;
  return UV_ENOSYS;
}


int uv_tcp_open(uv_tcp_t* handle, uv_os_sock_t sock) {
  (void) handle; (void) sock;
  return UV_ENOSYS;
}


int uv_tcp_getsockname(const uv_tcp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {
  (void) handle; (void) name; (void) namelen;
  return UV_ENOSYS;
}


int uv_tcp_getpeername(const uv_tcp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {
  (void) handle; (void) name; (void) namelen;
  return UV_ENOSYS;
}


int uv_tcp_close_reset(uv_tcp_t* handle, uv_close_cb close_cb) {
  (void) handle; (void) close_cb;
  return UV_ENOSYS;
}


/* uv__tcp_bind / uv__tcp_connect — internal helpers, referenced by
 * uv_tcp_bind / uv_tcp_connect wrappers. Match upstream signatures.
 */
int uv__tcp_bind(uv_tcp_t* tcp,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 unsigned int flags) {
  (void) tcp; (void) addr; (void) addrlen; (void) flags;
  return UV_ENOSYS;
}


int uv__tcp_connect(uv_connect_t* req,
                    uv_tcp_t* handle,
                    const struct sockaddr* addr,
                    unsigned int addrlen,
                    uv_connect_cb cb) {
  (void) req; (void) handle; (void) addr; (void) addrlen; (void) cb;
  return UV_ENOSYS;
}


int uv__tcp_listen(uv_tcp_t* tcp, int backlog, uv_connection_cb cb) {
  (void) tcp; (void) backlog; (void) cb;
  return UV_ENOSYS;
}


/* PUBLIC uv_tcp_nodelay / uv_tcp_keepalive variants (take uv_tcp_t*,
 * not fd). The earlier uv__tcp_nodelay / uv__tcp_keepalive are the
 * fd-taking internal variants. Both are referenced by tcp_wrap.cc.
 */
int uv_tcp_nodelay(uv_tcp_t* handle, int enable) {
  (void) handle; (void) enable;
  return UV_ENOSYS;
}


int uv_tcp_keepalive(uv_tcp_t* handle, int enable, unsigned int delay) {
  (void) handle; (void) enable; (void) delay;
  return UV_ENOSYS;
}


/* -- uv_udp_* (public + a few internal helpers) -- */

int uv_udp_getsockname(const uv_udp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {
  (void) handle; (void) name; (void) namelen;
  return UV_ENOSYS;
}


int uv_udp_getpeername(const uv_udp_t* handle,
                       struct sockaddr* name,
                       int* namelen) {
  (void) handle; (void) name; (void) namelen;
  return UV_ENOSYS;
}


int uv_udp_set_membership(uv_udp_t* handle,
                          const char* multicast_addr,
                          const char* interface_addr,
                          uv_membership membership) {
  (void) handle; (void) multicast_addr;
  (void) interface_addr; (void) membership;
  return UV_ENOSYS;
}


int uv_udp_set_source_membership(uv_udp_t* handle,
                                 const char* multicast_addr,
                                 const char* interface_addr,
                                 const char* source_addr,
                                 uv_membership membership) {
  (void) handle; (void) multicast_addr; (void) interface_addr;
  (void) source_addr; (void) membership;
  return UV_ENOSYS;
}


int uv_udp_set_multicast_loop(uv_udp_t* handle, int on) {
  (void) handle; (void) on;
  return UV_ENOSYS;
}


int uv_udp_set_multicast_ttl(uv_udp_t* handle, int ttl) {
  (void) handle; (void) ttl;
  return UV_ENOSYS;
}


int uv_udp_set_multicast_interface(uv_udp_t* handle,
                                   const char* interface_addr) {
  (void) handle; (void) interface_addr;
  return UV_ENOSYS;
}


int uv_udp_set_broadcast(uv_udp_t* handle, int on) {
  (void) handle; (void) on;
  return UV_ENOSYS;
}


int uv_udp_set_ttl(uv_udp_t* handle, int ttl) {
  (void) handle; (void) ttl;
  return UV_ENOSYS;
}


int uv__udp_init_ex(uv_loop_t* loop,
                    uv_udp_t* handle,
                    unsigned int flags,
                    int domain) {
  (void) loop; (void) handle; (void) flags; (void) domain;
  return UV_ENOSYS;
}


int uv__udp_bind(uv_udp_t* handle,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 unsigned int flags) {
  (void) handle; (void) addr; (void) addrlen; (void) flags;
  return UV_ENOSYS;
}


int uv__udp_connect(uv_udp_t* handle,
                    const struct sockaddr* addr,
                    unsigned int addrlen) {
  (void) handle; (void) addr; (void) addrlen;
  return UV_ENOSYS;
}


int uv__udp_disconnect(uv_udp_t* handle) {
  (void) handle;
  return UV_ENOSYS;
}


int uv__udp_send(uv_udp_send_t* req,
                 uv_udp_t* handle,
                 const uv_buf_t bufs[],
                 unsigned int nbufs,
                 const struct sockaddr* addr,
                 unsigned int addrlen,
                 uv_udp_send_cb send_cb) {
  (void) req; (void) handle; (void) bufs; (void) nbufs;
  (void) addr; (void) addrlen; (void) send_cb;
  return UV_ENOSYS;
}


int uv__udp_try_send(uv_udp_t* handle,
                     const uv_buf_t bufs[],
                     unsigned int nbufs,
                     const struct sockaddr* addr,
                     unsigned int addrlen) {
  (void) handle; (void) bufs; (void) nbufs; (void) addr; (void) addrlen;
  return UV_ENOSYS;
}


int uv__udp_try_send2(uv_udp_t* handle,
                      unsigned int count,
                      uv_buf_t* bufs[/*count*/],
                      unsigned int nbufs[/*count*/],
                      struct sockaddr* addrs[/*count*/]) {
  (void) handle; (void) count; (void) bufs; (void) nbufs; (void) addrs;
  return UV_ENOSYS;
}


int uv__udp_recv_start(uv_udp_t* handle,
                       uv_alloc_cb alloc_cb,
                       uv_udp_recv_cb recv_cb) {
  (void) handle; (void) alloc_cb; (void) recv_cb;
  return UV_ENOSYS;
}


int uv__udp_recv_stop(uv_udp_t* handle) {
  (void) handle;
  return 0;
}


/* -- uv_poll_* (public) -- */

int uv_poll_init_socket(uv_loop_t* loop,
                        uv_poll_t* handle,
                        uv_os_sock_t socket) {
  (void) loop; (void) handle; (void) socket;
  return UV_ENOSYS;
}


int uv_poll_start(uv_poll_t* handle, int events, uv_poll_cb cb) {
  (void) handle; (void) events; (void) cb;
  return UV_ENOSYS;
}


int uv_poll_stop(uv_poll_t* handle) {
  (void) handle;
  return UV_ENOSYS;
}


/* -- uv_sem_* (referenced by threadpool / async worker code) -- */

int uv_sem_init(uv_sem_t* sem, unsigned int value) {
  (void) sem; (void) value;
  return 0;
}


void uv_sem_destroy(uv_sem_t* sem) {
  (void) sem;
}


void uv_sem_post(uv_sem_t* sem) {
  (void) sem;
}


void uv_sem_wait(uv_sem_t* sem) {
  (void) sem;
}


/* -- uv_dl* (dynamic loader; static-linked WASM has no dlopen) -- */

int uv_dlopen(const char* filename, uv_lib_t* lib) {
  (void) filename;
  if (lib != NULL) {
    /* Match upstream pattern: zero out the handle. uv_lib_t has
     * `handle` (void*) and `errmsg` (char*); both POD. */
    memset(lib, 0, sizeof(*lib));
  }
  return UV_ENOSYS;
}


void uv_dlclose(uv_lib_t* lib) {
  (void) lib;
}


int uv_dlsym(uv_lib_t* lib, const char* name, void** ptr) {
  (void) lib; (void) name;
  if (ptr != NULL) *ptr = NULL;
  return UV_ENOSYS;
}


const char* uv_dlerror(const uv_lib_t* lib) {
  (void) lib;
  return "uv_dlopen not supported on WASI";
}


/* -- uv_thread_setname / DNS surface / interface_addresses --
 * referenced by Node's worker / net / dns wrappers.
 */

int uv_thread_setname(const char* name) {
  (void) name;
  return UV_ENOSYS;
}


int uv_getaddrinfo(uv_loop_t* loop,
                   uv_getaddrinfo_t* req,
                   uv_getaddrinfo_cb getaddrinfo_cb,
                   const char* node,
                   const char* service,
                   const struct addrinfo* hints) {
  (void) loop; (void) req; (void) getaddrinfo_cb;
  (void) node; (void) service; (void) hints;
  return UV_ENOSYS;
}


void uv_freeaddrinfo(struct addrinfo* ai) {
  (void) ai;
}


int uv_getnameinfo(uv_loop_t* loop,
                   uv_getnameinfo_t* req,
                   uv_getnameinfo_cb getnameinfo_cb,
                   const struct sockaddr* addr,
                   int flags) {
  (void) loop; (void) req; (void) getnameinfo_cb;
  (void) addr; (void) flags;
  return UV_ENOSYS;
}


int uv_interface_addresses(uv_interface_address_t** addresses, int* count) {
  if (addresses != NULL) *addresses = NULL;
  if (count != NULL) *count = 0;
  return UV_ENOSYS;
}


void uv_free_interface_addresses(uv_interface_address_t* addresses, int count) {
  (void) addresses; (void) count;
}
