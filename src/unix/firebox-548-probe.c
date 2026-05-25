/* firebox#548 — libuv mutex / cv-lock state probe (implementation).
 *
 * See firebox-548-probe.h for design rationale. This TU exists so the
 * emit function lives in a single object file that links into libuv.a
 * via the WASI-build CMakeLists branch (see CMakeLists.txt's
 * `if(CMAKE_SYSTEM_NAME STREQUAL "WASI")` source set).
 *
 * Per emission-discipline (mirrors firebox_526_abort_trace):
 *   - stack-allocated buffer, no malloc
 *   - hand-rolled itoa, no stdio
 *   - one __wasi_fd_write per event for per-line atomicity
 *   - no pthread_mutex_lock / pthread_cond_wait / locale lock
 *   - no pthread_self (it IS in the wedge actor surface — workers use
 *     the cooperative TLS tid stamp instead)
 *
 * --- C90 conformance ---
 *
 * The libuv WASI build uses `-std=gnu90` (set in CMakeLists.txt). All
 * local declarations must be at the top of their containing block.
 * `for (size_t i = 0; ...)` is rejected — we declare loop iterators
 * separately.
 */

#if defined(__wasi__) || defined(__wasix__)

#include "firebox-548-probe.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* wasi-libc's <wasi/api.h> defines __wasi_size_t / __wasi_errno_t /
 * __wasi_ciovec_t and the imported `__wasi_fd_write`. Include it
 * directly — libuv's WASI build already has the wasixcc-bundled
 * sysroot on the include path. */
#include <wasi/api.h>

/* TLS tid stamp; workers set this in their entry trampoline. */
__attribute__((used, visibility("default")))
__thread uint64_t firebox_548_self_tid = 0;

/* Gate state. -1 = unread, 0 = disabled, 1 = enabled.
 * Read-once via getenv(); subsequent stamps see the cached value. The
 * gate read is not perfectly thread-safe but the only race is during
 * startup — multiple threads may simultaneously call getenv() and
 * write 0 or 1; both writes have the same value, so the race is
 * benign. */
static int firebox_548_gate = -1;

/* itoa for unsigned 64-bit values, hex. Writes up to 16 chars. */
static size_t firebox_548_uitoa16(uint64_t v, char *out) {
    static const char hex[] = "0123456789abcdef";
    char tmp[16];
    size_t n;
    size_t i;
    if (v == 0) { out[0] = '0'; return 1; }
    n = 0;
    while (v > 0 && n < sizeof(tmp)) {
        tmp[n++] = hex[v & 0xf];
        v >>= 4;
    }
    /* Reverse into out. */
    for (i = 0; i < n; ++i) {
        out[i] = tmp[n - 1 - i];
    }
    return n;
}

/* itoa for signed 32-bit decimal values. Writes up to 12 chars. */
static size_t firebox_548_itoa10(int v, char *out) {
    uint32_t u;
    size_t off;
    char tmp[12];
    size_t n;
    size_t i;
    off = 0;
    if (v < 0) {
        out[off++] = '-';
        u = (uint32_t)(-(int64_t)v);
    } else {
        u = (uint32_t)v;
    }
    if (u == 0) { out[off++] = '0'; return off; }
    n = 0;
    while (u > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char)('0' + (u % 10));
        u /= 10;
    }
    for (i = 0; i < n; ++i) {
        out[off++] = tmp[n - 1 - i];
    }
    return off;
}

static size_t firebox_548_copy(const char *src, char *dst, size_t cap) {
    size_t i = 0;
    while (i < cap && src[i] != 0) { dst[i] = src[i]; ++i; }
    return i;
}

void firebox_548_probe_emit(const char *verb,
                            uint64_t tid,
                            uint64_t mutex_addr,
                            uint64_t cv_addr,
                            int rc,
                            int aux) {
    char buf[256];
    size_t off;
    __wasi_ciovec_t iov;
    __wasi_size_t nwritten;

    /* Re-enabled gate per #548 Phase 1 acceptance — probe arrival
     * was verified during initial validation (the no-gate variant
     * emitted thousands of lines and confirmed the function is
     * reached + __wasi_fd_write works in worker context). Production
     * cost: one TLS load + one branch per stamp site when gate is 0.
     *
     * NOTE: the initial "no FIREBOX_548 output" issue was a stale
     * image cache, NOT a gate or emit-path bug. The native image
     * (~/.firebox/images/node/) ships a frozen list of layer SHAs in
     * its manifest.json; `firebox cache import` updates the blob
     * cache but does NOT re-extract layers into the image directory.
     * Per the deployment-corollary discipline: build artifact →
     * cache import → `firebox build images/node/Fireboxfile` → run.
     * (See class lesson candidate: firebox_image_layer_freeze.) */
    if (firebox_548_gate == -1) {
        const char *v = getenv("FIREBOX_548_PROBE");
        firebox_548_gate = (v != NULL && v[0] == '1' && v[1] == 0) ? 1 : 0;
    }
    if (firebox_548_gate == 0) return;
    if (verb == NULL) verb = "?";

    /* Schema: FIREBOX_548_PROBE event=<verb> tid=0x<hex> mutex=0x<hex>
     *         cv=0x<hex> rc=<int> aux=<int>\n */
    off = 0;
    off += firebox_548_copy("FIREBOX_548_PROBE event=", buf + off, sizeof(buf) - off);
    /* Reserve 80 bytes after verb for the rest of the line. */
    off += firebox_548_copy(verb,                       buf + off,
                            (off + 80 < sizeof(buf)) ? sizeof(buf) - off - 80 : 0);
    off += firebox_548_copy(" tid=0x",                   buf + off, sizeof(buf) - off);
    off += firebox_548_uitoa16(tid,        buf + off);
    off += firebox_548_copy(" mutex=0x",                 buf + off, sizeof(buf) - off);
    off += firebox_548_uitoa16(mutex_addr, buf + off);
    off += firebox_548_copy(" cv=0x",                    buf + off, sizeof(buf) - off);
    off += firebox_548_uitoa16(cv_addr,    buf + off);
    off += firebox_548_copy(" rc=",                      buf + off, sizeof(buf) - off);
    off += firebox_548_itoa10(rc,          buf + off);
    off += firebox_548_copy(" aux=",                     buf + off, sizeof(buf) - off);
    off += firebox_548_itoa10(aux,         buf + off);
    if (off < sizeof(buf)) buf[off++] = '\n';

    iov.buf = (const uint8_t *)buf;
    iov.buf_len = (__wasi_size_t)off;
    (void)__wasi_fd_write(2, &iov, 1, &nwritten);
}

#else  /* non-wasi: prevent empty TU warning */
typedef int firebox_548_probe_empty_tu;
#endif
