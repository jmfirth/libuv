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
 * WASI / WASIX platform file for libuv. Provides the small set of
 * "per-platform utility" entry points every libuv backend must define
 * (analogous to src/unix/haiku.c or src/unix/qnx.c). The event loop
 * itself comes from src/unix/posix-poll.c, and monotonic time from
 * src/unix/posix-hrtime.c — both portable files that build unchanged
 * on top of wasix-libc's poll(2) + clock_gettime(CLOCK_MONOTONIC).
 *
 * The Firebox-patched wasix-libc sysroot provides every primitive
 * libuv's cross-platform cores touch (open/close/read/write, poll,
 * posix_spawnp, waitpid, kill, sigaction, pthread_*, fcntl F_GETFL/
 * F_SETFL with O_NONBLOCK, etc.). Concepts that WASI does not expose
 * — load average, CPU topology, process RSS, UID/GID-addressable
 * interface enumeration — are stubbed here with conservative values
 * so code that queries them compiles and runs without trapping.
 *
 * Scope target: the CMake bootstrap path (uv_spawn + uv_pipe_open +
 * uv_read_start + uv_run + uv_timer + uv_idle + uv_fs_* sync APIs).
 * Anything beyond that (threadpool, async, UDP, DNS) is stubbed in
 * src/unix/wasi-bootstrap.c.
 */

#include "uv.h"
#include "internal.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* WASI does not expose a traditional notion of load average. Zero it
 * like Haiku does.
 */
void uv_loadavg(double avg[3]) {
  avg[0] = 0;
  avg[1] = 0;
  avg[2] = 0;
}


/* WASI modules do not have a stable "executable path" the way POSIX
 * programs do — they're loaded by name from a runtime-managed module
 * registry. wasix-libc does expose `argv[0]` via __wasi_args_get and
 * typically sets it to the module name. We report that by way of
 * /proc/self/argv[0] equivalent: a compile-time fallback string for
 * now. Callers like CMake use this for relocation hints; returning
 * a plausible-looking path keeps configure-phase happy.
 *
 * TODO(libuv-wasix-port Phase 3+): wire to __wasi_args_get so the
 * real argv[0] is reflected. For v1 the static string is fine.
 */
int uv_exepath(char* buffer, size_t* size) {
  static const char kFallback[] = "/proc/self/exe";
  size_t n;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  n = sizeof(kFallback) - 1;
  if (n >= *size)
    n = *size - 1;

  memcpy(buffer, kFallback, n);
  buffer[n] = '\0';
  *size = n;
  return 0;
}


/* Free / total memory: WASI's linear memory model doesn't map neatly
 * onto host RAM figures. WASIX adds sysconf(_SC_*) but the returned
 * values are host-provided and often zero in sandboxed contexts.
 * CMake/Node treat a zero from these as "unknown" so returning zero
 * is safe.
 */
uint64_t uv_get_free_memory(void) {
  return 0;
}


uint64_t uv_get_total_memory(void) {
  return 0;
}


uint64_t uv_get_constrained_memory(void) {
  return 0;
}


uint64_t uv_get_available_memory(void) {
  return uv_get_free_memory();
}


/* No RSS on WASI — a WASM instance's heap is its linear memory, not a
 * kernel-tracked working set. We return 0 + success, which callers
 * interpret as "unknown".
 */
int uv_resident_set_memory(size_t* rss) {
  if (rss == NULL)
    return UV_EINVAL;
  *rss = 0;
  return 0;
}


/* Uptime: WASIX has clock_gettime(CLOCK_MONOTONIC) which on wasmer is
 * implemented as time-since-instance-start, not time-since-host-boot.
 * Close enough for libuv callers that just want a monotonically
 * increasing figure.
 */
int uv_uptime(double* uptime) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return UV__ERR(errno);
  *uptime = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
  return 0;
}


/* CPU info: WASI does not expose the host's CPU topology. Report a
 * single logical CPU named "wasm32" with speed 0. Callers that key on
 * count > 0 behave correctly; callers that read model/speed see
 * sensible placeholder data.
 */
int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  uv_cpu_info_t* info;

  if (cpu_infos == NULL || count == NULL)
    return UV_EINVAL;

  info = uv__calloc(1, sizeof(*info));
  if (info == NULL)
    return UV_ENOMEM;

  info->model = uv__strdup("wasm32");
  if (info->model == NULL) {
    uv__free(info);
    return UV_ENOMEM;
  }
  info->speed = 0;
  /* cpu_times fields are already zeroed by uv__calloc. */

  *cpu_infos = info;
  *count = 1;
  return 0;
}
