/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
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

#include "uv-common.h"

#if !defined(_WIN32)
# include "unix/internal.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_THREADPOOL_SIZE 1024

/* firebox#511 — libuv-worker-startup probe (cascade-9 RCA aid).
 *
 * Sibling to the wasmer-wasix `firebox_511_probe` module — the wasmer
 * side traces `wasi_thread_start` entry / layout / pre-dispatch /
 * dispatch_err; this side traces what the new worker thread actually
 * reaches inside libuv's threadpool loop.
 *
 * #505's wave-13 evidence: the abort fires IMMEDIATELY after the
 * wasmer-side `thread_spawn` event — before any tar-pipeline read. We
 * need to know whether the worker even reaches `worker()` entry, makes
 * it past `uv_thread_setname` / `uv_sem_post`, picks up its first work
 * item, and (if so) what that work item is. Each of those steps is a
 * separate candidate wedge.
 *
 * Gated by the same env var the wasmer side uses (FIREBOX_511_PROBE):
 * setting it to anything other than empty / "0" / "false" / "off" flips
 * the probe on for the whole run. Cached on first call to avoid a
 * per-probe getenv (`getenv` is not async-signal-safe; running it from
 * inside a worker thread that may itself be in mid-startup is the kind
 * of incident this probe is supposed to catch, not cause).
 *
 * Output goes to stderr with a `[firebox-511 libuv/threadpool]` prefix
 * so it's trivially grep-able from a transcript that also contains the
 * `[firebox-511 wasmer/wasix/thread_spawn]` lines from the wasmer side.
 *
 * Cost when disabled: one byte load + branch. Cost when enabled:
 * `fprintf(stderr, ...)` per probe call — fine for a debug-only build.
 */
static int firebox_511_probe_init = 0;
static int firebox_511_probe_on = 0;

static void firebox_511_probe_init_once(void) {
  const char* v;
  if (firebox_511_probe_init)
    return;
  v = getenv("FIREBOX_511_PROBE");
  if (v != NULL && v[0] != '\0' &&
      strcmp(v, "0") != 0 && strcmp(v, "false") != 0 &&
      strcmp(v, "FALSE") != 0 && strcmp(v, "off") != 0 &&
      strcmp(v, "OFF") != 0) {
    firebox_511_probe_on = 1;
  }
  firebox_511_probe_init = 1;
}

/* Probe macro mirroring the wasmer-side `firebox_511!` shape so callers
 * stay uniform. `__VA_ARGS__` carries the printf format + args. */
#define FIREBOX_511_PROBE(...)                                                  \
  do {                                                                          \
    firebox_511_probe_init_once();                                              \
    if (firebox_511_probe_on) {                                                 \
      fprintf(stderr, "[firebox-511 libuv/threadpool] " __VA_ARGS__);           \
      fputc('\n', stderr);                                                      \
      fflush(stderr);                                                           \
    }                                                                           \
  } while (0)

static uv_once_t once = UV_ONCE_INIT;
static uv_cond_t cond;
static uv_mutex_t mutex;
static unsigned int idle_threads;
static unsigned int slow_io_work_running;
static unsigned int nthreads;
static uv_thread_t* threads;
static uv_thread_t default_threads[4];
static struct uv__queue exit_message;
static struct uv__queue wq;
static struct uv__queue run_slow_work_message;
static struct uv__queue slow_io_pending_wq;

static unsigned int slow_work_thread_threshold(void) {
  return (nthreads + 1) / 2;
}

static void uv__cancelled(struct uv__work* w) {
  /* firebox#511 — this is one of libuv's two `abort()` paths that runs
   * inside the worker. If a `[firebox-511] uv__cancelled-abort` line
   * appears in a transcript, the worker received a cancelled work item
   * (uv_cancel was called on a request that had already started
   * executing). Surfacing the abort layer this way distinguishes
   * cascade-9 from the well-understood uv_cancel-race shape. */
  FIREBOX_511_PROBE("uv__cancelled-abort: w=%p — libuv aborting via uv__cancelled",
                    (void*) w);
  abort();
}


/* To avoid deadlock with uv_cancel() it's crucial that the worker
 * never holds the global mutex and the loop-local mutex at the same time.
 */
static void worker(void* arg) {
  struct uv__work* w;
  struct uv__queue* q;
  int is_slow_work;

  /* firebox#511 — earliest possible point inside the worker thread. If
   * this line never appears in a `FIREBOX_511_PROBE=1` transcript but
   * the wasmer-side `pre_dispatch` does, the wedge is in the runtime's
   * thread-startup glue (stack mmap / sigaltstack / TLS init) and the
   * worker is dying before reaching its own entry. */
  FIREBOX_511_PROBE("worker: entry arg=%p", arg);

  uv_thread_setname("libuv-worker");
  FIREBOX_511_PROBE("worker: post-setname");
  uv_sem_post((uv_sem_t*) arg);
  FIREBOX_511_PROBE("worker: post-sem_post (handshake with init_threads complete)");
  arg = NULL;

  uv_mutex_lock(&mutex);
  FIREBOX_511_PROBE("worker: post-initial-mutex_lock");
  for (;;) {
    /* `mutex` should always be locked at this point. */

    /* Keep waiting while either no work is present or only slow I/O
       and we're at the threshold for that. */
    while (uv__queue_empty(&wq) ||
           (uv__queue_head(&wq) == &run_slow_work_message &&
            uv__queue_next(&run_slow_work_message) == &wq &&
            slow_io_work_running >= slow_work_thread_threshold())) {
      idle_threads += 1;
      FIREBOX_511_PROBE("worker: cond_wait (idle_threads=%u)", idle_threads);
      uv_cond_wait(&cond, &mutex);
      idle_threads -= 1;
      FIREBOX_511_PROBE("worker: cond_wait wake (idle_threads=%u)", idle_threads);
    }

    q = uv__queue_head(&wq);
    if (q == &exit_message) {
      FIREBOX_511_PROBE("worker: exit_message received");
      uv_cond_signal(&cond);
      uv_mutex_unlock(&mutex);
      break;
    }

    uv__queue_remove(q);
    uv__queue_init(q);  /* Signal uv_cancel() that the work req is executing. */

    is_slow_work = 0;
    if (q == &run_slow_work_message) {
      /* If we're at the slow I/O threshold, re-schedule until after all
         other work in the queue is done. */
      if (slow_io_work_running >= slow_work_thread_threshold()) {
        uv__queue_insert_tail(&wq, q);
        continue;
      }

      /* If we encountered a request to run slow I/O work but there is none
         to run, that means it's cancelled => Start over. */
      if (uv__queue_empty(&slow_io_pending_wq))
        continue;

      is_slow_work = 1;
      slow_io_work_running++;

      q = uv__queue_head(&slow_io_pending_wq);
      uv__queue_remove(q);
      uv__queue_init(q);

      /* If there is more slow I/O work, schedule it to be run as well. */
      if (!uv__queue_empty(&slow_io_pending_wq)) {
        uv__queue_insert_tail(&wq, &run_slow_work_message);
        if (idle_threads > 0)
          uv_cond_signal(&cond);
      }
    }

    uv_mutex_unlock(&mutex);

    w = uv__queue_data(q, struct uv__work, wq);
    /* firebox#511 — this is THE critical point: the worker has its first
     * work item in hand and is about to call into the user-supplied
     * `w->work` callback. If the abort fires AFTER "worker: entry" and
     * BEFORE "work-item: pre-call", the wedge is in libuv's loop body
     * (e.g. the queue-dispatch logic itself, or a cond/mutex race during
     * wake). If it fires INSIDE `w->work(w)` — i.e. after "pre-call" and
     * with no matching "post-call" — the wedge is in the work-item
     * callback itself (the wasi-libc / uv_fs / napi glue).
     *
     * `w->work` is a function pointer; under wasi the wasm-fnptr-as-
     * table-index lesson (see `class_lesson_wasm_fnptr_is_table_index_
     * _not_address.md`) means we print the integer value so anyone
     * grepping the transcript sees the same value libuv resolved via the
     * indirect-call table.
     */
    FIREBOX_511_PROBE("work-item: pre-call work=%p w=%p loop=%p is_slow_work=%d",
                      (void*) w->work, (void*) w, (void*) w->loop, is_slow_work);
    w->work(w);
    FIREBOX_511_PROBE("work-item: post-call work=%p w=%p (work pointer now %p)",
                      (void*) w->work, (void*) w, (void*) w->work);

    uv_mutex_lock(&w->loop->wq_mutex);
    w->work = NULL;  /* Signal uv_cancel() that the work req is done
                        executing. */
    uv__queue_insert_tail(&w->loop->wq, &w->wq);
    uv_async_send(&w->loop->wq_async);
    uv_mutex_unlock(&w->loop->wq_mutex);

    /* Lock `mutex` since that is expected at the start of the next
     * iteration. */
    uv_mutex_lock(&mutex);
    if (is_slow_work) {
      /* `slow_io_work_running` is protected by `mutex`. */
      slow_io_work_running--;
    }
  }
}


static void post(struct uv__queue* q, enum uv__work_kind kind) {
  uv_mutex_lock(&mutex);
  if (kind == UV__WORK_SLOW_IO) {
    /* Insert into a separate queue. */
    uv__queue_insert_tail(&slow_io_pending_wq, q);
    if (!uv__queue_empty(&run_slow_work_message)) {
      /* Running slow I/O tasks is already scheduled => Nothing to do here.
         The worker that runs said other task will schedule this one as well. */
      uv_mutex_unlock(&mutex);
      return;
    }
    q = &run_slow_work_message;
  }

  uv__queue_insert_tail(&wq, q);
  if (idle_threads > 0)
    uv_cond_signal(&cond);
  uv_mutex_unlock(&mutex);
}


#ifdef __MVS__
/* TODO(itodorov) - zos: revisit when Woz compiler is available. */
__attribute__((destructor))
#endif
void uv__threadpool_cleanup(void) {
  unsigned int i;

  if (nthreads == 0)
    return;

#ifndef __MVS__
  /* TODO(gabylb) - zos: revisit when Woz compiler is available. */
  post(&exit_message, UV__WORK_CPU);
#endif

  for (i = 0; i < nthreads; i++)
    if (uv_thread_join(threads + i))
      abort();

  if (threads != default_threads)
    uv__free(threads);

  uv_mutex_destroy(&mutex);
  uv_cond_destroy(&cond);

  threads = NULL;
  nthreads = 0;
}


static void init_threads(void) {
  uv_thread_options_t config;
  unsigned int i;
  const char* val;
  uv_sem_t sem;

  nthreads = ARRAY_SIZE(default_threads);
  val = getenv("UV_THREADPOOL_SIZE");
  if (val != NULL)
    nthreads = atoi(val);
  if (nthreads == 0)
    nthreads = 1;
  if (nthreads > MAX_THREADPOOL_SIZE)
    nthreads = MAX_THREADPOOL_SIZE;

  threads = default_threads;
  if (nthreads > ARRAY_SIZE(default_threads)) {
    threads = uv__malloc(nthreads * sizeof(threads[0]));
    if (threads == NULL) {
      nthreads = ARRAY_SIZE(default_threads);
      threads = default_threads;
    }
  }

  if (uv_cond_init(&cond))
    abort();

  if (uv_mutex_init(&mutex))
    abort();

  uv__queue_init(&wq);
  uv__queue_init(&slow_io_pending_wq);
  uv__queue_init(&run_slow_work_message);

  if (uv_sem_init(&sem, 0))
    abort();

  config.flags = UV_THREAD_HAS_STACK_SIZE;
  config.stack_size = 8u << 20;  /* 8 MB */

  /* firebox#511 — covers init_threads bookkeeping: we record the planned
   * pool size + stack size so a transcript with N worker entries can be
   * verified against the expected N. */
  FIREBOX_511_PROBE("init_threads: nthreads=%u stack_size=%zu", nthreads,
                    (size_t) config.stack_size);

  for (i = 0; i < nthreads; i++) {
    FIREBOX_511_PROBE("init_threads: spawning worker[%u]", i);
    if (uv_thread_create_ex(threads + i, &config, worker, &sem)) {
      FIREBOX_511_PROBE("init_threads: uv_thread_create_ex FAILED for worker[%u] — aborting", i);
      abort();
    }
  }

  for (i = 0; i < nthreads; i++) {
    FIREBOX_511_PROBE("init_threads: waiting for worker[%u] handshake", i);
    uv_sem_wait(&sem);
  }
  FIREBOX_511_PROBE("init_threads: all %u workers handshook", nthreads);

  uv_sem_destroy(&sem);
}


#ifndef _WIN32
static void reset_once(void) {
  uv_once_t child_once = UV_ONCE_INIT;
  memcpy(&once, &child_once, sizeof(child_once));
}
#endif


static void init_once(void) {
#ifndef _WIN32
  /* Re-initialize the threadpool after fork.
   * Note that this discards the global mutex and condition as well
   * as the work queue.
   */
  if (pthread_atfork(NULL, NULL, &reset_once))
    abort();
#endif
  init_threads();
}


void uv__work_submit(uv_loop_t* loop,
                     struct uv__work* w,
                     enum uv__work_kind kind,
                     void (*work)(struct uv__work* w),
                     void (*done)(struct uv__work* w, int status)) {
  uv_once(&once, init_once);
  w->loop = loop;
  w->work = work;
  w->done = done;
  post(&w->wq, kind);
}


/* TODO(bnoordhuis) teach libuv how to cancel file operations
 * that go through io_uring instead of the thread pool.
 */
static int uv__work_cancel(uv_loop_t* loop, uv_req_t* req, struct uv__work* w) {
  int cancelled;

  uv_once(&once, init_once);  /* Ensure |mutex| is initialized. */
  uv_mutex_lock(&mutex);
  uv_mutex_lock(&w->loop->wq_mutex);

  cancelled = !uv__queue_empty(&w->wq) && w->work != NULL;
  if (cancelled)
    uv__queue_remove(&w->wq);

  uv_mutex_unlock(&w->loop->wq_mutex);
  uv_mutex_unlock(&mutex);

  if (!cancelled)
    return UV_EBUSY;

  w->work = uv__cancelled;
  uv_mutex_lock(&loop->wq_mutex);
  uv__queue_insert_tail(&loop->wq, &w->wq);
  uv_async_send(&loop->wq_async);
  uv_mutex_unlock(&loop->wq_mutex);

  return 0;
}


void uv__work_done(uv_async_t* handle) {
  struct uv__work* w;
  uv_loop_t* loop;
  struct uv__queue* q;
  struct uv__queue wq;
  int err;
  int nevents;

  loop = container_of(handle, uv_loop_t, wq_async);
  uv_mutex_lock(&loop->wq_mutex);
  uv__queue_move(&loop->wq, &wq);
  uv_mutex_unlock(&loop->wq_mutex);

  nevents = 0;

  while (!uv__queue_empty(&wq)) {
    q = uv__queue_head(&wq);
    uv__queue_remove(q);

    w = container_of(q, struct uv__work, wq);
    err = (w->work == uv__cancelled) ? UV_ECANCELED : 0;
    w->done(w, err);
    nevents++;
  }

  /* This check accomplishes 2 things:
   * 1. Even if the queue was empty, the call to uv__work_done() should count
   *    as an event. Which will have been added by the event loop when
   *    calling this callback.
   * 2. Prevents accidental wrap around in case nevents == 0 events == 0.
   */
  if (nevents > 1) {
    /* Subtract 1 to counter the call to uv__work_done(). */
    uv__metrics_inc_events(loop, nevents - 1);
    if (uv__get_internal_fields(loop)->current_timeout == 0)
      uv__metrics_inc_events_waiting(loop, nevents - 1);
  }
}


static void uv__queue_work(struct uv__work* w) {
  uv_work_t* req = container_of(w, uv_work_t, work_req);

  req->work_cb(req);
}


static void uv__queue_done(struct uv__work* w, int err) {
  uv_work_t* req;

  req = container_of(w, uv_work_t, work_req);
  uv__req_unregister(req->loop);

  if (req->after_work_cb == NULL)
    return;

  req->after_work_cb(req, err);
}


int uv_queue_work(uv_loop_t* loop,
                  uv_work_t* req,
                  uv_work_cb work_cb,
                  uv_after_work_cb after_work_cb) {
  if (work_cb == NULL)
    return UV_EINVAL;

  uv__req_init(loop, req, UV_WORK);
  req->loop = loop;
  req->work_cb = work_cb;
  req->after_work_cb = after_work_cb;
  uv__work_submit(loop,
                  &req->work_req,
                  UV__WORK_CPU,
                  uv__queue_work,
                  uv__queue_done);
  return 0;
}


int uv_cancel(uv_req_t* req) {
  struct uv__work* wreq;
  uv_loop_t* loop;

  switch (req->type) {
  case UV_FS:
    loop =  ((uv_fs_t*) req)->loop;
    wreq = &((uv_fs_t*) req)->work_req;
    break;
  case UV_GETADDRINFO:
    loop =  ((uv_getaddrinfo_t*) req)->loop;
    wreq = &((uv_getaddrinfo_t*) req)->work_req;
    break;
  case UV_GETNAMEINFO:
    loop = ((uv_getnameinfo_t*) req)->loop;
    wreq = &((uv_getnameinfo_t*) req)->work_req;
    break;
  case UV_RANDOM:
    loop = ((uv_random_t*) req)->loop;
    wreq = &((uv_random_t*) req)->work_req;
    break;
  case UV_WORK:
    loop =  ((uv_work_t*) req)->loop;
    wreq = &((uv_work_t*) req)->work_req;
    break;
  default:
    return UV_EINVAL;
  }

  return uv__work_cancel(loop, req, wreq);
}
