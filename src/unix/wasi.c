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
 * interface enumeration -- are answered here from what wasix-libc and the
 * runtime actually expose (sysconf, getifaddrs, /proc, /sys), and where the
 * runtime exposes nothing the call fails the way Linux libuv fails, or says
 * "none" (load average and constrained memory stay 0). No value is invented
 * (firebox#46C).
 *
 * Scope target: the CMake bootstrap path (uv_spawn + uv_pipe_open +
 * uv_read_start + uv_run + uv_timer + uv_idle + uv_fs_* sync APIs), plus the
 * network surface edgejs needs (UDP, poll, getnameinfo, interface
 * enumeration). What is still stubbed lives in src/unix/wasi-bootstrap.c.
 */

#include "uv.h"
#include "internal.h"

#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <stddef.h>
#include <stdio.h>
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


/* Resident set size (firebox#469). A wasm instance has no kernel-tracked
 * working set and the runtime serves no /proc/self/statm, so there is no
 * measured RSS to report -- and "0, success" was a false answer (callers read
 * it as "this process uses no memory"). What IS known exactly is the size of
 * the instance's linear memory: every byte the guest can touch lives in it, so
 * it is the honest UPPER BOUND on resident memory (pages the guest never
 * touched may or may not be resident on the host, but none outside it can be
 * the guest's). memory.size counts 64 KiB wasm pages and is an i64 under
 * wasm64, hence the size_t-wide builtin and the 64-bit multiply.
 */
int uv_resident_set_memory(size_t* rss) {
  if (rss == NULL)
    return UV_EINVAL;
  *rss = (size_t) ((uint64_t) __builtin_wasm_memory_size(0) * 65536u);
  return 0;
}


/* Uptime (firebox#46C): clock_gettime(CLOCK_MONOTONIC). The runtime maps it to
 * the HOST's monotonic clock, not to time-since-instance-start. MEASURED
 * 2026-10-04 on Darwin: os.uptime() in the guest read 1446400.69 s while the
 * host's kern.boottime put boot 1446398 s before `date`, and
 * clock_gettime(CLOCK_MONOTONIC) on the host read 1446398 -- so this is
 * time-since-host-boot, which is what uptime means. linux.c reads /proc/uptime
 * (not served here) and falls back to CLOCK_BOOTTIME, which wasix-libc does not
 * define; on a Linux host CLOCK_MONOTONIC stops across suspend where
 * CLOCK_BOOTTIME does not, so there the figure can read low by the suspended
 * time. It is a measured clock, never a constant.
 */
int uv_uptime(double* uptime) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    return UV__ERR(errno);
  *uptime = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
  return 0;
}


/* CPU info (firebox#46C), after linux.c's uv_cpu_info and failing where it
 * fails. Sources, all read from the guest's own /proc and /sys:
 *
 *   /proc/stat      one `cpuN user nice sys idle iowait irq ...` line per CPU
 *                   -> the CPU list and cpu_times (jiffies at 100 Hz, so *10 for
 *                   ms). linux.c returns UV__ERR(errno) when it cannot open it.
 *                   MEASURED 2026-10-04: the runtime serves /proc/cpuinfo and
 *                   /proc/meminfo but NOT /proc/stat (proc_files.rs
 *                   install_proc_files), so today this returns UV_ENOENT, as
 *                   Linux libuv does on a system without /proc/stat -- and
 *                   Node's os.cpus() turns that into `[]`
 *                   (`getCPUs() || []`). The earlier version here returned one
 *                   entry per sysconf CPU with all-zero times, which is data
 *                   nothing measured. The parse below lights up on its own the
 *                   day the runtime serves /proc/stat.
 *   /proc/cpuinfo   `processor : N` blocks; model is the `model name` field
 *                   (the generic x86 marker linux.c uses where no per-arch
 *                   marker applies; the runtime renders `model name` for every
 *                   host). A CPU with no model reads "unknown", as on Linux.
 *   /sys/devices/system/cpu/cpuN/cpufreq/scaling_cur_freq   speed in MHz
 *                   (kHz / 1000). linux.c reads speed from nowhere else -- not
 *                   from cpuinfo's `cpu MHz` -- and the runtime serves no /sys,
 *                   so speed stays 0 here exactly as for a Linux CPU without
 *                   cpufreq.
 *
 * Unlike linux.c, every model is its own allocation: uv_free_cpu_info frees
 * per-entry models on every non-__linux__ platform, and linux.c's packed
 * single-allocation layout would make that a free of an interior pointer.
 */
#define UV__WASI_MAXCPU 8192  /* kernel maximum, as in linux.c */

int uv_cpu_info(uv_cpu_info_t** cpu_infos, int* count) {
  static const char model_marker[] = "model name\t: ";
  struct uv__wasi_cpu {
    unsigned long long freq, user, nice, sys, idle, irq;
    char model[64];
    char present;
  };
  struct uv__wasi_cpu* cpus;
  uv_cpu_info_t* info;
  unsigned long long skip;
  unsigned long long freq;
  unsigned long long u, ni, sy, id, irq;
  unsigned cpu;
  unsigned maxcpu;
  unsigned i;
  int n;
  int err;
  FILE* fp;
  char buf[1024];

  if (cpu_infos == NULL || count == NULL)
    return UV_EINVAL;

  cpus = uv__calloc(UV__WASI_MAXCPU, sizeof(*cpus));
  if (cpus == NULL)
    return UV_ENOMEM;

  fp = uv__open_file("/proc/stat");
  if (fp == NULL) {
    err = UV__ERR(errno);
    uv__free(cpus);
    return err;
  }

  maxcpu = 0;
  /* First line is the aggregate `cpu ` row; the per-CPU rows follow. */
  if (fgets(buf, sizeof(buf), fp) != NULL) {
    for (;;) {
      n = fscanf(fp, "cpu%u %llu %llu %llu %llu %llu %llu",
                 &cpu, &u, &ni, &sy, &id, &skip, &irq);
      if (n != 7)
        break;
      if (fgets(buf, sizeof(buf), fp) == NULL)
        break;
      if (cpu >= UV__WASI_MAXCPU)
        continue;
      cpus[cpu].user = u;
      cpus[cpu].nice = ni;
      cpus[cpu].sys = sy;
      cpus[cpu].idle = id;
      cpus[cpu].irq = irq;
      cpus[cpu].present = 1;
      if (cpu >= maxcpu)
        maxcpu = cpu + 1;
    }
  }
  fclose(fp);

  fp = uv__open_file("/proc/cpuinfo");
  if (fp != NULL) {
    while (fscanf(fp, "processor\t: %u\n", &cpu) == 1) {
      while (fgets(buf, sizeof(buf), fp) != NULL) {
        if (*buf == '\n')
          break;
        if (strncmp(buf, model_marker, sizeof(model_marker) - 1) != 0)
          continue;
        n = (int) strcspn(buf + sizeof(model_marker) - 1, "\n");
        if (cpu < maxcpu)
          snprintf(cpus[cpu].model, sizeof(cpus[cpu].model), "%.*s", n,
                   buf + sizeof(model_marker) - 1);
        /* Drain the rest of this processor's block. */
        while (fgets(buf, sizeof(buf), fp) != NULL)
          if (*buf == '\n')
            break;
        break;
      }
    }
    fclose(fp);
  }

  n = 0;
  for (cpu = 0; cpu < maxcpu; cpu++) {
    if (!cpus[cpu].present)
      continue;
    n++;
    snprintf(buf, sizeof(buf),
             "/sys/devices/system/cpu/cpu%u/cpufreq/scaling_cur_freq", cpu);
    fp = uv__open_file(buf);
    if (fp == NULL)
      continue;
    if (fscanf(fp, "%llu", &freq) == 1)
      cpus[cpu].freq = freq;
    fclose(fp);
  }

  info = uv__calloc(n > 0 ? n : 1, sizeof(*info));
  if (info == NULL) {
    uv__free(cpus);
    return UV_ENOMEM;
  }

  i = 0;
  for (cpu = 0; cpu < maxcpu; cpu++) {
    const struct uv__wasi_cpu* c = &cpus[cpu];
    if (!c->present)
      continue;
    info[i].model = uv__strdup(c->model[0] != '\0' ? c->model : "unknown");
    if (info[i].model == NULL) {
      while (i-- > 0)
        uv__free(info[i].model);
      uv__free(info);
      uv__free(cpus);
      return UV_ENOMEM;
    }
    info[i].speed = (int) (c->freq / 1000);
    info[i].cpu_times.user = 10 * c->user;
    info[i].cpu_times.nice = 10 * c->nice;
    info[i].cpu_times.sys = 10 * c->sys;
    info[i].cpu_times.idle = 10 * c->idle;
    info[i].cpu_times.irq = 10 * c->irq;
    i++;
  }

  uv__free(cpus);
  *cpu_infos = info;
  *count = n;
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
