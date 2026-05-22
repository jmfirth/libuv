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
 *   - UDP (uv_udp_open / close). Out-of-scope for v1.
 *
 *   - Dynamic linking (uv_dlopen / dlerror / dlsym / dlclose). Out-of-
 *     scope for v1; our WASM binaries are statically linked.
 *
 *   - pthread_atfork / pthread_sigmask. Firebox-specific helper symbols
 *     supplied as trivial no-ops (no fork-time pthread re-init hook;
 *     signal masks handled at the WASIX layer).
 *
 *   - uv__fs_poll_close (so stat-polling fs watchers don't undef).
 *
 * What this file NO LONGER stubs (firebox#438):
 *
 *   - Threads / mutex / rwlock / cond / once / sem. src/unix/thread.c
 *     (libuv's portable pthread backend) is now in the WASI CMake
 *     source list — the Firebox sysroot exports the full pthread
 *     surface over shared linear memory, so libuv gets real worker
 *     threads. This is what lets libuv's threadpool.c init_threads()
 *     succeed instead of abort()-ing on a uv_thread_create_ex ENOSYS
 *     stub (the cascade-7 npm-install SIGABRT). Re-stubbing those
 *     symbols here would clash with thread.c at link time.
 *
 *   - Async handles (uv_async_init / send / close / fork / stop).
 *     src/unix/async.c is now in the WASI source list too. With a real
 *     threadpool the worker's uv_async_send wakeup to the loop MUST
 *     fire, otherwise the threadpool completion handshake deadlocks —
 *     a no-op stub is no longer acceptable. async.c uses a self-pipe;
 *     re-stubbing uv_async_* here would clash with it at link.
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
#include <string.h> /* memset */
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
 * Async — REAL implementation
 *
 * firebox#438: uv_async_init / uv_async_send / uv__async_close /
 * uv__async_fork / uv__async_stop are NO LONGER stubbed here.
 * src/unix/async.c (libuv's portable self-pipe async backend) is now in
 * the WASI CMake source list.
 *
 * Why it became load-bearing: the threadpool worker, on finishing a
 * job, calls uv_async_send(&loop->wq_async) to wake the event loop so
 * it runs uv__work_done(). The old no-op uv_async_send stub meant a
 * REAL threadpool worker completed its job but the loop never learned
 * of it — the main thread blocked forever on the threadpool completion
 * handshake. async.c's real cross-thread wakeup (a self-pipe + the
 * loop's posix-poll watcher) closes that. Re-stubbing any uv_async_*
 * symbol here would clash with async.c at link.
 * ======================================================================== */


/* ========================================================================
 * Threadpool / work queue
 *
 * Provided by src/threadpool.c (portable, already in the source list).
 * We don't re-stub uv__work_submit / uv__work_done here — doing so
 * would produce duplicate-symbol link errors. firebox#438: the
 * threadpool path IS now exercised at runtime — threadpool.c's
 * init_threads() spawns real worker threads via thread.c's
 * uv_thread_create_ex (backed by pthread_create on the Firebox
 * sysroot). npm-cli.js startup triggers this the first time it issues
 * an async libuv primitive (e.g. uv_fs_open).
 * ======================================================================== */


/* ========================================================================
 * pthread trampolines
 *
 * uv__pthread_atfork / uv__pthread_sigmask are Firebox-specific helper
 * symbols (referenced by the WASI-gated paths in core.c / process.c).
 * They are NOT provided by src/unix/thread.c, so they stay here. The
 * single-threaded no-op shape is still correct for the WASI port: there
 * is no fork-time pthread re-init hook to run, and signal masks are
 * handled at the WASIX layer.
 * ======================================================================== */

int uv__pthread_atfork(void (*prepare)(void), void (*parent)(void),
                       void (*child)(void)) {
  return 0;
}


int uv__pthread_sigmask(int how, const sigset_t* set, sigset_t* oset) {
  return 0;
}


/* ========================================================================
 * Threads / mutex / rwlock / cond / once / sem — REAL implementations
 *
 * firebox#438: these are NO LONGER stubbed here. src/unix/thread.c (the
 * portable libuv pthread backend) is now in the WASI CMake source list
 * (see the `CMAKE_SYSTEM_NAME STREQUAL "WASI"` block in CMakeLists.txt).
 * The Firebox sysroot exports the full pthread surface — pthread_create,
 * pthread_join, pthread_mutex_*, pthread_cond_*, pthread_once, sem_* —
 * over shared linear memory, so libuv gets real worker threads,
 * mutexes, condvars and semaphores.
 *
 * Why this matters: libuv's threadpool.c init_threads() calls
 * uv_thread_create_ex once per worker and abort()s on any non-zero
 * return. The old single-threaded uv_thread_create_ex ENOSYS stub here
 * made that abort() fire the first time any async libuv primitive (e.g.
 * uv_fs_open from an npm-cli.js NAPI binding) lazily initialized the
 * threadpool via uv_once(init_threads). That was the cascade-7
 * npm-install SIGABRT (exit 134). With thread.c's real
 * uv_thread_create_ex the pool initializes and npm-install runs.
 *
 * Re-stubbing uv_mutex_* / uv_rwlock_* / uv_cond_* / uv_once /
 * uv_sem_* / uv_thread_* here would now produce duplicate-symbol link
 * errors against thread.c — that is intentional: thread.c is the one
 * canonical home for those symbols.
 * ======================================================================== */


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


/* -- uv_sem_* — REAL implementations live in src/unix/thread.c
 * (firebox#438). On the WASI target uv_sem_t is the POSIX sem_t and the
 * Firebox sysroot exports sem_init / sem_post / sem_wait / sem_destroy /
 * sem_trywait, so libuv's threadpool worker handshake (uv_sem_post in
 * worker(), uv_sem_wait in init_threads()) uses genuine semaphores. Not
 * stubbed here — that would clash with thread.c. -- */


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


/* -- DNS surface / interface_addresses — referenced by Node's net / dns
 * wrappers.
 *
 * uv_thread_setname is REAL now (firebox#438): src/unix/thread.c routes
 * it through wasix-libc's pthread_setname_np, so libuv-worker and V8
 * thread names are set correctly. It is not stubbed here — that would
 * clash with thread.c. (uv__thread_getname returns UV_ENOSYS via a
 * __wasi__ branch in thread.c since wasix-libc has no
 * pthread_getname_np.)
 */

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
