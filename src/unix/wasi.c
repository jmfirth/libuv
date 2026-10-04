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
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
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


/* uv_exepath: the Linux implementation. The Firebox runtime answers
 * readlink("/proc/self/exe") with the path of the running module (MEASURED
 * under `firebox run`: the .wasm's path), which is what Linux libuv returns,
 * so this no longer reports the literal string "/proc/self/exe" it used to
 * (firebox#46C). A failing readlink is the caller's UV__ERR(errno), as on Linux.
 */
int uv_exepath(char* buffer, size_t* size) {
  ssize_t n;

  if (buffer == NULL || size == NULL || *size == 0)
    return UV_EINVAL;

  n = readlink("/proc/self/exe", buffer, *size - 1);
  if (n == -1)
    return UV__ERR(errno);

  buffer[n] = '\0';
  *size = n;
  return 0;
}


/* Free / total memory (firebox#46C): sysconf(_SC_PHYS_PAGES / _SC_AVPHYS_PAGES)
 * x the page size. The runtime serves both (MEASURED: PHYS_PAGES 16777216,
 * AVPHYS_PAGES the host's live free pages). Where sysconf reports -1 the
 * answer is 0, which libuv documents as "unknown" and Linux libuv also
 * returns when /proc/meminfo is unreadable.
 */
static uint64_t uv__sysconf_pages(int name) {
  long pages = sysconf(name);
  long pagesize = sysconf(_SC_PAGESIZE);
  if (pages < 0 || pagesize < 0)
    return 0;
  return (uint64_t) pages * (uint64_t) pagesize;
}


uint64_t uv_get_free_memory(void) {
  return uv__sysconf_pages(_SC_AVPHYS_PAGES);
}


uint64_t uv_get_total_memory(void) {
  return uv__sysconf_pages(_SC_PHYS_PAGES);
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


/* CPU info (firebox#46C): one entry per online CPU as sysconf reports it
 * (MEASURED: _SC_NPROCESSORS_ONLN, the same figure uv_available_parallelism
 * reports). The runtime has no /proc/stat, so cpu_times stay zero and speed 0,
 * and the model is "unknown" -- exactly what Linux libuv fills in for a CPU it
 * cannot read a model for. It was one invented "wasm32" CPU before.
 */
int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  uv_cpu_info_t* info;
  long ncpu;
  long i;

  if (cpu_infos == NULL || count == NULL)
    return UV_EINVAL;

  ncpu = sysconf(_SC_NPROCESSORS_ONLN);
  if (ncpu < 1)
    return UV_ENOSYS;

  info = uv__calloc(ncpu, sizeof(*info));
  if (info == NULL)
    return UV_ENOMEM;

  for (i = 0; i < ncpu; i++) {
    info[i].model = uv__strdup("unknown");
    if (info[i].model == NULL) {
      while (i-- > 0)
        uv__free(info[i].model);
      uv__free(info);
      return UV_ENOMEM;
    }
    /* speed and cpu_times are already zeroed by uv__calloc. */
  }

  *cpu_infos = info;
  *count = (int) ncpu;
  return 0;
}


/* ========================================================================
 * uv_interface_addresses / uv_free_interface_addresses (firebox#46C)
 *
 * Was an UV_ENOSYS stub in wasi-bootstrap.c, which let edge's own
 * `DUMMY_UV_STUBS` branch invent a loopback row. wasix-libc's getifaddrs is
 * real since firebox#25D (one node per record of the host interface list:
 * AF_PACKET link records carry index/hardware address/hatype, AF_INET and
 * AF_INET6 records carry address, netmask and Linux IFF_* flags), so this is
 * libuv's own linux.c implementation over it: same exclusion rules (skip
 * interfaces that are not UP and RUNNING, skip nodes with no address, report
 * AF_PACKET nodes only as the source of phys_addr), same alias-name matching.
 *
 * Failure is honest: where the host has no interface model (no --net, the
 * browser) getifaddrs fails ENOTSUP and so does this (UV_ENOTSUP), which Node
 * surfaces as a SystemError exactly as it does for any failing
 * uv_interface_addresses. phys_addr is as wide as libc's sockaddr_ll carries
 * it (8 bytes; libuv's field is 6) and stays zero for an interface with no
 * link record, which is what Linux libuv does too.
 * ======================================================================== */

static int uv__ifaddr_exclude(struct ifaddrs* ent, int exclude_type) {
  if (!((ent->ifa_flags & IFF_UP) && (ent->ifa_flags & IFF_RUNNING)))
    return 1;
  if (ent->ifa_addr == NULL)
    return 1;
  if (ent->ifa_addr->sa_family == PF_PACKET)
    return exclude_type;
  return !exclude_type;
}


int uv_interface_addresses(uv_interface_address_t** addresses, int* count) {
  struct ifaddrs *addrs, *ent;
  uv_interface_address_t* address;
  int i;
  struct sockaddr_ll* sll;

  *count = 0;
  *addresses = NULL;

  if (getifaddrs(&addrs))
    return UV__ERR(errno);

  for (ent = addrs; ent != NULL; ent = ent->ifa_next) {
    if (uv__ifaddr_exclude(ent, UV__EXCLUDE_IFADDR))
      continue;
    (*count)++;
  }

  if (*count == 0) {
    freeifaddrs(addrs);
    return 0;
  }

  *addresses = uv__calloc(*count, sizeof(**addresses));
  if (!(*addresses)) {
    freeifaddrs(addrs);
    *count = 0;
    return UV_ENOMEM;
  }

  address = *addresses;

  for (ent = addrs; ent != NULL; ent = ent->ifa_next) {
    if (uv__ifaddr_exclude(ent, UV__EXCLUDE_IFADDR))
      continue;

    address->name = uv__strdup(ent->ifa_name);

    if (ent->ifa_addr->sa_family == AF_INET6)
      address->address.address6 = *((struct sockaddr_in6*) ent->ifa_addr);
    else
      address->address.address4 = *((struct sockaddr_in*) ent->ifa_addr);

    /* A node with no netmask leaves the calloc'd zero netmask in place. */
    if (ent->ifa_netmask != NULL) {
      if (ent->ifa_netmask->sa_family == AF_INET6)
        address->netmask.netmask6 = *((struct sockaddr_in6*) ent->ifa_netmask);
      else
        address->netmask.netmask4 = *((struct sockaddr_in*) ent->ifa_netmask);
    }

    address->is_internal = !!(ent->ifa_flags & IFF_LOOPBACK);

    address++;
  }

  /* Fill in physical addresses for each interface from its link record. */
  for (ent = addrs; ent != NULL; ent = ent->ifa_next) {
    if (uv__ifaddr_exclude(ent, UV__EXCLUDE_IFPHYS))
      continue;

    address = *addresses;

    for (i = 0; i < (*count); i++) {
      size_t namelen = strlen(ent->ifa_name);
      /* Alias interfaces share the same physical address. */
      if (strncmp(address->name, ent->ifa_name, namelen) == 0 &&
          (address->name[namelen] == 0 || address->name[namelen] == ':')) {
        sll = (struct sockaddr_ll*) ent->ifa_addr;
        memcpy(address->phys_addr, sll->sll_addr, sizeof(address->phys_addr));
      }
      address++;
    }
  }

  freeifaddrs(addrs);

  return 0;
}


void uv_free_interface_addresses(uv_interface_address_t* addresses,
                                 int count) {
  int i;

  for (i = 0; i < count; i++)
    uv__free(addresses[i].name);

  uv__free(addresses);
}
