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
 *   - (UDP is NOT stubbed any more: src/unix/udp.c is in the source list,
 *     firebox#46C.)
 *
 *   - (uv_poll_* and uv_getnameinfo are NOT stubbed any more: src/unix/poll.c
 *     and src/unix/getnameinfo.c are in the source list, firebox#46C. Edge's
 *     c-ares wrap needs the first, dns.lookupService the second.)
 *
 *   - Dynamic linking (uv_dlopen / dlerror / dlsym / dlclose). firebox#759
 *     re-points these onto wasix-libc's dlopen/dlsym (<dlfcn.h>), which
 *     lower to the WASIX `dlopen` syscall and instantiate a PIC wasm
 *     side-module against a `dylink.0` PIC dynamic-main (the QuickJS edge
 *     built that way). Real for a dynamic-main; on a static main the
 *     syscall yields NULL → a normal dlerror() string. See the uv_dl*
 *     block below.
 *
 *   - (uv__pthread_atfork / uv__pthread_sigmask, always-0 no-ops with no
 *     caller anywhere in the tree, were removed: firebox#46C audit.)
 *
 *   - uv__fs_poll_close (so stat-polling fs watchers don't undef).
 *
 *   - getifaddrs / freeifaddrs are NOT stubbed here any more: wasix-libc
 *     defines both since firebox#YZN (67b8cbd9), and a second definition
 *     is a duplicate-symbol link error against libc.a. See the note below.
 *
 *   - if_indextoname / if_nametoindex are NOT stubbed here any more:
 *     wasix-libc defines both since firebox#25D (real names and indices
 *     from the host interface list; ENXIO / ENODEV on a miss), and a second
 *     definition is a duplicate-symbol link error against libc.a
 *     (firebox#17D). See the note below.
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
 *   - TCP (uv_tcp_init / uv_tcp_open / uv_tcp_getsockname /
 *     uv_tcp_getpeername / uv_tcp_close_reset / uv__tcp_bind /
 *     uv__tcp_connect / uv__tcp_listen / uv__tcp_close / uv__tcp_nodelay
 *     / uv__tcp_keepalive / uv_tcp_nodelay / uv_tcp_keepalive /
 *     uv_tcp_simultaneous_accepts / uv_tcp_init_ex / uv_socketpair).
 *     firebox#582: src/unix/tcp.c (libuv's portable BSD-sockets TCP
 *     backend) is now in the WASI CMake source list. The Firebox
 *     wasmer runtime's InodeSocket::Pollable impl drives AF_INET
 *     sockets through posix-poll.c equivalently to AF_UNIX sockets
 *     (verified via #580 spike — 30/30 PASS connect→POLLOUT→SO_ERROR=0
 *     to 1.1.1.1:80). Re-stubbing any uv_tcp_* / uv_socketpair symbol
 *     here would clash with tcp.c at link. This is what unblocks
 *     cascade-9 Mechanism C (Edge.js TcpCtor early-return on
 *     UV_ENOSYS — now uv_tcp_init returns 0 and the connect path runs
 *     through to wasmer's sock_connect).
 *     - firebox#585 §13.2 discriminator: uv_tcp_bind PASS 90/90
 *       (clean wire-through to wasix-libc bind(2); no fork delta).
 *     - firebox#590 §13.3 discriminator: uv_write / uv_read_start
 *       PASS 90/90 against 1.1.1.1:80 with HTTP/1.x response received
 *       (clean wire-through to wasix-libc write(2)/writev(2)/read(2);
 *       no fork delta). stream.c's uv__try_write → uv__writev path
 *       resolves: n==1 → write(2); n>1 → writev(2). The sendmsg-based
 *       SCM_RIGHTS fd-passing branch is already #if !__wasi__ gated
 *       upstream so it's compiled out.
 *     - firebox#594 §13.6 discriminator: TLS handshake (https.get to
 *       registry.npmjs.org:443) PASS 90/90 (30 iter × 3 shells) against
 *       Edge.js's bundled Node BoringSSL/OpenSSL. Spec §4.6's
 *       `[predicted]` zero-work claim CONFIRMED end-to-end: Node's TLS
 *       layer reads/writes through the same uv_tcp_t stream interface
 *       wired by §13.1-§13.3 with NO additional binding work. This is
 *       a metadata-only entry — no fork delta in this file or anywhere
 *       else in libuv; the TLS code is in Node's source tree, compiles
 *       into the Edge.js binary, and rides on the already-shipped TCP
 *       + stream wireup. Disclaimers: validated against
 *       `registry.npmjs.org:443` specifically (TLS 1.2/1.3 mainline,
 *       CA-trusted endpoint); other endpoints / mTLS / SNI edge cases
 *       not exercised by the discriminator (deferred to §G6 integration
 *       canaries). `https.get` simplest path exercised; `https.request`
 *       with bodies, keep-alive pooling, ALPN exercised in §G6, not
 *       here. IPv6 fallback acceptable (the discriminator routinely
 *       resolves AAAA records first per #592 §13.5).
 *     - firebox#594 §G6 (`npm install left-pad`) STRETCH: TLS body
 *       received byte-perfectly (sha256 of compressed .tgz matches
 *       upstream npmjs sha), zlib gunzipSync of the buffered body
 *       produces correct decompressed bytes. HOWEVER the streaming-
 *       pipe path (`res.pipe(zlib.createGunzip())` as used by npm's
 *       tar extractor) produces 17920 decompressed bytes with
 *       *non-deterministic sha mismatch* — different runs produce
 *       different wrong-byte streams while buffered+sync is reliably
 *       correct. The wedge is in stream chunk ORDERING (libuv stream
 *       chunk callback delivery), NOT in TLS itself. §13.6 closure is
 *       NOT contingent on §G6 — TLS handshake + body integrity are
 *       proven. §G6 surfaces a follow-up data-corruption class in
 *       libuv's stream chunk callback path (sibling to cascade-9 Mech
 *       B silent-data-corruption pattern; see firebox#600).
 *
 *   - Stream I/O (uv_write / uv_write2 / uv_read_start / uv_read_stop /
 *     uv__write / uv__try_write / uv__writev / uv_try_write /
 *     uv_try_write2 / uv_listen / uv_accept / uv__handle_fd /
 *     uv__io_feed and the rest of the portable stream surface).
 *     src/unix/stream.c was pulled into the WASI CMake source list as
 *     a transitive dep of tcp.c at #582 (uv_tcp_init → uv__stream_init
 *     lives in stream.c). firebox#590 closes the write+read
 *     discriminator side. On WASI the sendmsg branch in uv__try_write
 *     is #if !__wasi__ gated upstream; the unconditional path is
 *     uv__writev → write(2) (n==1) or writev(2) (n>1), both real
 *     exports in the Firebox wasix-libc sysroot. Re-stubbing any
 *     uv_write_* / uv__write_* / uv_read_* / uv_listen / uv_accept
 *     symbol here would clash with stream.c at link. listen/accept
 *     are linked but unexercised until §13.4 wires the inbound side.
 *
 *   - Forward DNS resolution (uv_getaddrinfo / uv_freeaddrinfo /
 *     uv_if_indextoname / uv_if_indextoiid /
 *     uv__getaddrinfo_translate_error). firebox#592: src/unix/
 *     getaddrinfo.c is now in the WASI CMake source list. The Firebox
 *     wasix-libc sysroot exports real getaddrinfo / freeaddrinfo /
 *     gai_strerror / getnameinfo via <netdb.h>; underneath, wasix-libc's
 *     __lookup_name routes through the `__wasi_resolve` import
 *     (wasix_32v1.resolve), implemented in the Firebox wasmer fork at
 *     lib/wasix/src/syscalls/wasix/resolve.rs as an async host-side DNS
 *     lookup. libuv's getaddrinfo.c thunks uv_getaddrinfo through
 *     uv__work_submit(UV__WORK_SLOW_IO) onto the threadpool, where the
 *     worker calls getaddrinfo(3) → __lookup_name → __wasi_resolve →
 *     host resolver; uv__getaddrinfo_done then fires the user's
 *     uv_getaddrinfo_cb on the loop thread via async.c's self-pipe
 *     wakeup. Re-stubbing uv_getaddrinfo / uv_freeaddrinfo /
 *     uv_if_indextoname / uv_if_indextoiid /
 *     uv__getaddrinfo_translate_error here would clash with
 *     getaddrinfo.c at link.
 *
 *     NOT wired by this entry:
 *       - (uv_getnameinfo: wired by firebox#46C, getnameinfo.c over libc's
 *         getnameinfo.)
 *       - (if_indextoname / if_nametoindex, the libc primitives
 *         uv_if_indextoname calls, are real in wasix-libc since
 *         firebox#25D and are no longer stubbed here -- firebox#17D.)
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
 * UDP -- REAL (firebox#46C): src/unix/udp.c is in the WASI source list.
 * The uv_udp_* / uv__udp_* ENOSYS stubs that stood here were removed; a
 * second definition would clash with udp.c at link.
 * ======================================================================== */


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
 * TCP / uv_socketpair / stream I/O — REAL implementations
 *
 * firebox#582: src/unix/tcp.c (libuv's portable BSD-sockets TCP
 * backend) is now in the WASI CMake source list, with src/unix/stream.c
 * pulled in as a transitive dep (uv_tcp_init → uv__stream_init).
 * tcp.c is the one canonical home for uv_tcp_* / uv__tcp_* /
 * uv_socketpair; stream.c is the one canonical home for uv_write /
 * uv_write2 / uv_read_start / uv_read_stop / uv__try_write / uv_listen /
 * uv_accept. Re-stubbing any of those symbols here would produce
 * duplicate-symbol link errors.
 *
 * Discriminator coverage (all "clean wire-through, no fork delta"):
 *   - firebox#582 §13.1: uv_tcp_init + uv_tcp_connect — PASS 60/60
 *   - firebox#585 §13.2: uv_tcp_bind                  — PASS 90/90
 *   - firebox#590 §13.3: uv_write + uv_read_start     — PASS 90/90
 *   - firebox#592 §13.5: uv_getaddrinfo (DNS)         — PASS 90/90
 *     (chained discriminator: resolve registry.npmjs.org → connect →
 *      write → read; HTTPS-port lookup, plain-HTTP request to port 80
 *      to keep TLS out of the loop; the resolution gate alone fires
 *      30/30 per shell × 3 shells).
 *
 * §13.4 (uv_listen / uv_accept) and §13.6+ (TLS) remain ahead. §13.5
 * (DNS) is wired via getaddrinfo.c — see the next block.
 * ======================================================================== */


/* ========================================================================
 * DNS forward resolution — REAL implementations
 *
 * firebox#592: src/unix/getaddrinfo.c (libuv's portable threadpool-based
 * DNS-resolver shim) is now in the WASI CMake source list. The Firebox
 * wasix-libc sysroot exports real getaddrinfo / freeaddrinfo /
 * gai_strerror; underneath, wasix-libc's __lookup_name routes through
 * the `__wasi_resolve` import (wasix_32v1.resolve), implemented in the
 * Firebox wasmer fork at lib/wasix/src/syscalls/wasix/resolve.rs as an
 * async host-side DNS lookup.
 *
 * libuv's getaddrinfo.c thunks uv_getaddrinfo through
 * uv__work_submit(UV__WORK_SLOW_IO) onto the threadpool — the worker
 * calls getaddrinfo(3) → __lookup_name → __wasi_resolve → host
 * resolver, then uv__getaddrinfo_done fires the user's
 * uv_getaddrinfo_cb on the loop thread via the async self-pipe wakeup.
 *
 * Re-stubbing uv_getaddrinfo / uv_freeaddrinfo / uv_if_indextoname /
 * uv_if_indextoiid / uv__getaddrinfo_translate_error here would clash
 * with getaddrinfo.c at link.
 *
 * NOT wired by this block:
 *   - (uv_getnameinfo: wired by firebox#46C, getnameinfo.c.)
 *   - (if_indextoname / if_nametoindex, the libc primitives
 *     uv_if_indextoname calls, are provided by wasix-libc -- see the
 *     "provided by wasix-libc (firebox#17D)" block below.)
 * ======================================================================== */


/* ========================================================================
 * poll -- REAL (firebox#46C): src/unix/poll.c is in the WASI source list over
 * posix-poll.c; uv__poll_close and uv_poll_* are no longer stubbed here.
 * ======================================================================== */


/* ========================================================================
 * getifaddrs / freeifaddrs — provided by wasix-libc (firebox#YZN)
 *
 * This file used to stub both (firebox#582: the header existed, the symbols
 * did not). wasix-libc 67b8cbd9 defines them, failing honestly (-1,
 * errno=ENOTSUP) where the host has no interface list, so
 * uv__ipv6_link_local_scope_id still falls through to rv=0 exactly as it did
 * on the stub's ENOSYS. The stub is removed rather than kept because a
 * second strong definition is a duplicate-symbol link error against libc.a,
 * and a libuv that shadowed libc would keep answering ENOSYS after the host
 * gains a real interface list.
 * ======================================================================== */


/* ========================================================================
 * if_indextoname / if_nametoindex — provided by wasix-libc (firebox#17D)
 *
 * This file used to stub both to errno=ENOSYS (firebox#592: the <net/if.h>
 * prototypes existed, the symbols did not). wasix-libc defines them since
 * firebox#25D, over the same host interface list as getifaddrs: real
 * names and indices, if_indextoname fails with ENXIO and if_nametoindex
 * with ENODEV on a miss (POSIX / Linux), and both fail with ENOTSUP where
 * the host has no interface model (no --net; the browser). The stubs are
 * removed rather than kept because a second strong definition is a
 * duplicate-symbol link error against libc.a -- every edgejs fat link
 * failed on it -- and a libuv that shadowed libc would keep answering
 * ENOSYS to uv_if_indextoname after the host gained a real list.
 * uv_if_indextoname (getaddrinfo.c) maps whatever errno libc reports
 * through UV__ERR.
 * ======================================================================== */


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


/* -- uv_sem_* — REAL implementations live in src/unix/thread.c
 * (firebox#438). On the WASI target uv_sem_t is the POSIX sem_t and the
 * Firebox sysroot exports sem_init / sem_post / sem_wait / sem_destroy /
 * sem_trywait, so libuv's threadpool worker handshake (uv_sem_post in
 * worker(), uv_sem_wait in init_threads()) uses genuine semaphores. Not
 * stubbed here — that would clash with thread.c. -- */


/* -- uv_dl* (dynamic loader) — firebox#759 -----------------------------
 *
 * Re-pointed from the historical UV_ENOSYS stubs onto wasix-libc's
 * dlopen/dlsym/dlclose/dlerror (<dlfcn.h>), which lower to the WASIX
 * `dlopen` syscall (__wasi_dlopen / __wasi_dlsym -> the host
 * `__imported_wasix_32v1_dlopen` import). That syscall instantiates a PIC
 * wasm side-module against the dynamically-linked main via the WASIX
 * dynamic linker. It only works when the MAIN module is a `dylink.0` PIC
 * dynamic-main (firebox#759 builds the QuickJS edge that way); on a static
 * main the syscall returns "not a dynamically-linked instance" and dlopen
 * yields NULL, which is surfaced as a normal dlerror() string -- the same
 * shape real Node gives a failed `require('./foo.node')`.
 *
 * This is the body of upstream libuv's src/unix/dl.c, restored for the
 * WASI/WASIX backend now that dlopen is real. It is what makes
 * `process.dlopen` -> uv_dlopen -> uv_dlsym("napi_register_module_v1")
 * reach a loaded native addon (the _ssl.so / python3.wasm pattern applied
 * to N-API addons). See work/tasks/759-...  */

#include <dlfcn.h>

static int uv__dlerror(uv_lib_t* lib);

int uv_dlopen(const char* filename, uv_lib_t* lib) {
  dlerror(); /* Reset error status. */
  lib->errmsg = NULL;
  lib->handle = dlopen(filename, RTLD_LAZY);
  return lib->handle ? 0 : uv__dlerror(lib);
}


void uv_dlclose(uv_lib_t* lib) {
  if (lib->errmsg) {
    uv__free(lib->errmsg);
    lib->errmsg = NULL;
  }

  if (lib->handle) {
    /* Ignore errors. No good way to signal them without leaking memory. */
    dlclose(lib->handle);
    lib->handle = NULL;
  }
}


int uv_dlsym(uv_lib_t* lib, const char* name, void** ptr) {
  dlerror(); /* Reset error status. */
  *ptr = dlsym(lib->handle, name);
  return uv__dlerror(lib);
}


const char* uv_dlerror(const uv_lib_t* lib) {
  return lib->errmsg ? lib->errmsg : "no error";
}


static int uv__dlerror(uv_lib_t* lib) {
  const char* errmsg;

  if (lib->errmsg)
    uv__free(lib->errmsg);

  errmsg = dlerror();

  if (errmsg) {
    lib->errmsg = uv__strdup(errmsg);
    return -1;
  }
  else {
    lib->errmsg = NULL;
    return 0;
  }
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

/* uv_getaddrinfo / uv_freeaddrinfo are NOT stubbed here — firebox#592
 * pulled src/unix/getaddrinfo.c into the WASI CMake source list. See
 * the "DNS forward resolution — REAL implementations" block above for
 * the wire-through narrative. Re-stubbing here would clash with
 * getaddrinfo.c at link.
 */


/* uv_interface_addresses / uv_free_interface_addresses are real: see src/unix/wasi.c
 * (firebox#46C). They were ENOSYS stubs here. */
