#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <inttypes.h>
#include <semaphore.h>
#include <fcntl.h>
#include <signal.h>

/******************************************************************************
 * prayers to the POSIX gods                                                  *
 ******************************************************************************/

// Oh mighty POSIX, guardian of threads and time,
// Keeper of mutexes, semaphores, and signals divine,
// Grant me clean compiles and deadlock-free nights.
// Please POSIX, give me the pthread functions I humbly ask for. Amen.

/******************************************************************************
 * 3rd party dependencies                                                     *
 ******************************************************************************/
#include <curl/curl.h>
#include <zlib.h>
#include <jansson.h>

#if LIBCURL_VERSION_NUM != 0x080f00
#error "You are not building against curl version 8.15.0. May bob be with you"
#endif

#if JANSSON_MAJOR_VERSION != 2 || JANSSON_MINOR_VERSION != 13 || \
    JANSSON_MICRO_VERSION != 1
#error "You are not building against jansson version 2.13.1. May bob be with you"
#endif

#if ZLIB_VERNUM != 0x1310
#error "You are not building against zlib version 1.3.1. May bob be with you"
#endif

/******************************************************************************
 * panic                                                                      *
 ******************************************************************************/
void global_cleanup(void);

#define panic(msg) _panic(msg, __LINE__, __FILE__)
#define assert(test) _assert(test, __LINE__, __FILE__)

#if FANCY_PANIC
#include <unistd.h>
#include <execinfo.h>

void _panic(const char *msg, int line, char *file) {
  fprintf(stderr, "\x1b[1;35mpanic:\x1b[0m file %s, line %d, %s\n\nstracktrace:\n",
          file, line, msg);
  
  void *trace[4096];
  int len = backtrace(trace, 4096);
  backtrace_symbols_fd(trace, len, STDERR_FILENO);
  global_cleanup();
  kill(getpid(), SIGTERM);
}

void _assert(bool test, int line, char *file) {
  if (test) return;

  fprintf(stderr, "\x1b[1;35massertion failed:\x1b[0m file %s, line %d\n\nstracktrace:\n",
          file, line);
  
  void *trace[4096];
  int len = backtrace(trace, 4096);
  backtrace_symbols_fd(trace, len, STDERR_FILENO);
  global_cleanup();
  kill(getpid(), SIGTERM);
}
#else
void _panic(const char *msg, int line, char *file) {
  fprintf(stderr, "\x1b[1;35mpanic:\x1b[0m file %s, line %d, %s\n", file, line,
          msg);
  global_cleanup();
  kill(getpid(), SIGTERM);
}

void _assert(bool test, int line, char *file) {
  if (test) return;

  fprintf(stderr, "\x1b[1;35massertion failed:\x1b[0m file %s, line %d:\n",
          file, line);
  global_cleanup();
  kill(getpid(), SIGTERM);
}
#endif

/******************************************************************************
 * logs                                                                       *
 ******************************************************************************/
void log_print(char *fmt, ...) {
  time_t t;
  struct tm tm = {0};
  time(&t);
  localtime_r(&t, &tm);
  fprintf(stdout, "%04d/%02d/%02d %02d:%02d:%02d ", 1900 + tm.tm_year,
          tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stdout, fmt, ap);
  va_end(ap);

  fputc('\n', stdout);
}

void log_warn(char *fmt, ...) {
  time_t t;
  struct tm tm = {0};
  time(&t);
  localtime_r(&t, &tm);
  fprintf(stdout, "%04d/%02d/%02d %02d:%02d:%02d \x1b[1;33mwarn:\x1b[0m ",
          1900 + tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min,
          tm.tm_sec);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stdout, fmt, ap);
  va_end(ap);

  fputc('\n', stdout);
}

void log_error(char *fmt, ...) {
  time_t t;
  struct tm tm = {0};
  time(&t);
  localtime_r(&t, &tm);
  fprintf(stdout, "%04d/%02d/%02d %02d:%02d:%02d \x1b[1;31merror:\x1b[0m ",
          1900 + tm.tm_year, tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min,
          tm.tm_sec);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stdout, fmt, ap);
  va_end(ap);

  fputc('\n', stdout);
}

/******************************************************************************
 * strings                                                                    *
 ******************************************************************************/
struct string {
  char *buf;
  size_t len;
};

struct string string_new(char *s) {
  return (struct string) { .buf = s, .len = s == NULL ? 0 : strlen(s) };
}

// for static initialization
#define STRING_NEW(s) (struct string) { .buf = s, .len = strlen(s) }

void string_destroy(struct string *str) {
  assert(str != NULL);
  free(str->buf);
  str->buf = NULL;
  str->len = 0;
}

void string_null_terminate(struct string str, char *dest, size_t dest_size) {
  if (str.len + 1 > dest_size) {
    panic("given string length exceed allowed limit");
  }
  memcpy(dest, str.buf, str.len);
  dest[str.len] = '\0';
}

struct string string_vfmt(char *buf, size_t buf_len, const char *fmt,
                          va_list ap) {
  int len = vsnprintf(buf, buf_len, fmt, ap);
  if (len >= buf_len) {
    return (struct string) { .buf = buf, .len = buf_len };
  }
  return (struct string) { .buf = buf, .len = len };
}

struct string string_fmt(char *buf, size_t buf_len, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  struct string str = string_vfmt(buf, buf_len, fmt, ap);
  va_end(ap);
  return str;
}

void string_cpy(struct string dest, struct string src) {
  if (dest.len != src.len) {
    panic("incompatible string sizes");
  }
  memcpy(dest.buf, src.buf, src.len);
}

// returns < 0  if a < b
// returns == 0 if a == b
// returns > 0  if a > b
// compares chars as unisgned char
int string_cmp(struct string a, struct string b) {
  for (size_t i = 0; i < a.len && i < b.len; ++i) {
    if ((unsigned char) a.buf[i] < (unsigned char) b.buf[i]) {
      return -1;
    } else if ((unsigned char) a.buf[i] > (unsigned char) b.buf[i]) {
      return 1;
    }
  }
  if (a.len < b.len) {
    return -1;
  } else if (a.len > b.len) {
    return 1;
  } else {
    return 0;
  }
}

/******************************************************************************
 * errors                                                                     *
 ******************************************************************************/
typedef int err_t;
const err_t E_OK = 0;
const err_t E_ERR = 1;
const err_t E_EOF = 2;
const err_t E_EMPTY = 3;
const err_t E_FULL = 4;

#define THREAD_ERROR_LEN 8192
__thread char thread_errmsg_buf[THREAD_ERROR_LEN + 1];
__thread struct string thread_errmsg = {0};

void errmsg_set(struct string msg) {
  if (msg.len > THREAD_ERROR_LEN) {
    log_warn("error message too long (len = %zu)", msg.len);
  }
  memcpy(thread_errmsg_buf, msg.buf, msg.len);
  thread_errmsg = (struct string) { .buf = thread_errmsg_buf, .len = msg.len };
}

void errmsg_fmt(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  struct string msg = string_vfmt(thread_errmsg_buf, THREAD_ERROR_LEN, fmt, ap);
  va_end(ap);
  errmsg_set(msg);
}

void errmsg_prefix(const char *prefix) {
  if (thread_errmsg.len == 0) {
    errmsg_fmt("%s", prefix);
  } else {
    assert(thread_errmsg.buf != NULL);
    size_t prefix_len = strlen(prefix);
    if (thread_errmsg.len + prefix_len > THREAD_ERROR_LEN) {
      log_warn("error message too long (len = %zu)", thread_errmsg.len + prefix_len);
    }
    for (ssize_t i = thread_errmsg.len; i >= 0; --i) {
      thread_errmsg.buf[i + prefix_len] = thread_errmsg.buf[i];
    }
    memcpy(thread_errmsg.buf, prefix, prefix_len);
    thread_errmsg.len += prefix_len;
  }
}

struct string errmsg_get(void) {
  return thread_errmsg;
}

void errmsg_print(void) {
  log_error("%.*s", (int) thread_errmsg.len, thread_errmsg.buf);
}

void errmsg_panic(void) {
  thread_errmsg_buf[thread_errmsg.len] = '\0';
  panic(thread_errmsg_buf);
}

void errmsg_print_prefix(const char *prefix) {
  errmsg_prefix(prefix);
  errmsg_print();
}

void errmsg_panic_prefix(const char *prefix) {
  errmsg_prefix(prefix);
  errmsg_panic();
}


/******************************************************************************
 * strings alloc                                                              *
 ******************************************************************************/

// string allocation function can return err_t so they need to be defined after
// err_t

// returned string memory is not initialized
err_t string_alloc(struct string *str, size_t len) {
  assert(str != NULL);
  char *buf = malloc(len);
  if (buf == NULL) {
    errmsg_fmt("malloc: %s", strerror(errno));
    return E_ERR;
  }
  *str = (struct string) { .buf = buf, .len = len };
  return E_OK;
}

err_t string_alloc_cpy(struct string *dst, struct string src) {
  assert(dst != NULL);
  char *buf = malloc(src.len);
  if (buf == NULL) {
    errmsg_fmt("malloc: %s", strerror(errno));
    return E_ERR;
  }
  memcpy(buf, src.buf, src.len);
  *dst = (struct string) { .buf = buf, .len = src.len };
  return E_OK;
}

err_t string_alloc_null_terminated_cpy(char **dst, struct string src) {
  assert(dst != NULL);
  char *buf = malloc(src.len + 1);
  if (buf == NULL) {
    errmsg_fmt("malloc: %s", strerror(errno));
    return E_ERR;
  }
  memcpy(buf, src.buf, src.len);
  buf[src.len] = '\0';
  *dst = buf;
  return E_OK;
}


/******************************************************************************
 * dynamic string                                                             *
 ******************************************************************************/
struct dstring {
  char *buf;
  size_t len;
  size_t cap;
};

err_t dstring_create(struct dstring *ds, size_t cap) {
  assert(ds != NULL);
  char *buf = malloc(cap);
  if (buf == NULL) {
    errmsg_fmt("malloc error: %s", strerror(errno));
    return E_ERR;
  }
  *ds = (struct dstring) { .buf = buf, .len = 0, .cap = cap };
  return E_OK;
}

void dstring_destroy(struct dstring *ds) {
  assert(ds != NULL);
  free(ds->buf);
  ds->buf = NULL;
  ds->len = 0;
  ds->cap = 0;
}

err_t dstring_push(struct dstring *ds, struct string s) {
  assert(ds != NULL);
  size_t new_cap = ds->cap <= 0 ? 1 : ds->cap;
  while (new_cap < ds->len + s.len) {
    new_cap *= 2;
  }
  if (new_cap > ds->cap) {
    ds->buf = realloc(ds->buf, new_cap);
    if (ds->buf == NULL) {
      errmsg_fmt("realloc error: %s", strerror(errno));
      return E_ERR;
    }
    ds->cap = new_cap;
  }
  memcpy(ds->buf + ds->len, s.buf, s.len);
  ds->len += s.len;
  return E_OK;
}

void dstring_pop(struct dstring *ds, size_t len) {
  assert(ds != NULL);
  if (ds->len < len) {
    log_error("dstring_pop: can't pop %zu bytes from dstring of len %zu", len, ds->len);
    panic("dstring_pop: can't pop");
  }
  ds->len -= len;
}

struct string string_slice(struct dstring ds, size_t begin, size_t end) {
  assert(begin <= end);
  if (end > ds.len) {
    panic("string_slice: out of bounds");
  }
  return (struct string) { .buf = ds.buf + begin, .len = end - begin };
}

/******************************************************************************
 * vector (dynamic array)                                                     *
 ******************************************************************************/
#define IMPLEMENT_VEC(vec_type, vec_name) \
struct vec_name##_vec { \
  vec_type *buf; \
  size_t len; \
  size_t cap; \
}; \
 \
/* NOTE: a vec_create call is not necessary, a zeroed vector will */ \
/* initialize itself at the first call to vec_push */ \
err_t vec_name##_vec_create(struct vec_name##_vec *da, size_t cap) { \
  assert(da != NULL); \
  vec_type *buf = malloc(cap * sizeof(vec_type)); \
  if (buf == NULL) { \
    errmsg_fmt("malloc error: %s", strerror(errno)); \
    return E_ERR; \
  } \
  *da = (struct vec_name##_vec) { .buf = buf, .len = 0, .cap = cap }; \
  return E_OK; \
} \
 \
void vec_name##_vec_destroy(struct vec_name##_vec *da) { \
  assert(da != NULL); \
  free(da->buf); \
  da->buf = 0; \
  da->len = 0; \
} \
 \
err_t vec_name##_vec_push(struct vec_name##_vec *da, vec_type x) { \
  assert(da != NULL); \
  if (da->buf == NULL || da->cap == 0) { \
    if (da->cap == 0) { \
      da->cap = 4; \
    } \
    da->buf = malloc(da->cap * sizeof(vec_type)); \
    if (da->buf == NULL) { \
      errmsg_fmt("realloc error: %s", strerror(errno)); \
      return E_ERR; \
    } \
  } else if (da->len + 1 > da->cap) { \
    da->buf = realloc(da->buf, 2 * da->cap * sizeof(vec_type)); \
    if (da->buf == NULL) { \
      errmsg_fmt("realloc error: %s", strerror(errno)); \
      return E_ERR; \
    } \
    da->cap *= 2; \
  } \
  da->buf[da->len] = x; \
  da->len += 1; \
  return E_OK; \
} \
 \
vec_type vec_name##_vec_get(const struct vec_name##_vec *da, size_t i) { \
  assert(da != NULL); \
  if (i >= da->len) { \
    panic("out of bounds"); \
  } \
  return da->buf[i]; \
} \
 \
vec_type *vec_name##_vec_getp(const struct vec_name##_vec *da, size_t i) { \
  assert(da != NULL); \
  if (i >= da->len) { \
    panic("out of bounds"); \
  } \
  return da->buf + i; \
}

IMPLEMENT_VEC(size_t, size)
IMPLEMENT_VEC(uint64_t, uint64)

/******************************************************************************
 * string pool                                                                *
 ******************************************************************************/
struct string_pool {
  struct dstring  dstr;
  struct size_vec bounds;
};

err_t string_pool_create(struct string_pool *sp, size_t cap) {
  assert(sp != NULL);
  err_t err = dstring_create(&sp->dstr, cap);
  if (err != E_OK) {
    errmsg_prefix("dstring_create: ");
    return E_ERR;
  }
  err = size_vec_create(&sp->bounds, 32);
  if (err != E_OK) {
    errmsg_prefix("size_vec_create: ");
    dstring_destroy(&sp->dstr);
    return E_ERR;
  }
  err = size_vec_push(&sp->bounds, 0);
  if (err != E_OK) panic("unreachable");
  return E_OK;
}

void string_pool_destroy(struct string_pool *sp) {
  assert(sp != NULL);
  dstring_destroy(&sp->dstr);
  size_vec_destroy(&sp->bounds);
}

err_t string_pool_push(struct string_pool *sp, struct string s, size_t *s_idx) {
  assert(sp != NULL);
  assert(s_idx != NULL);
  err_t err = dstring_push(&sp->dstr, s);
  if (err != E_OK) {
    errmsg_prefix("dstring_push: ");
    return E_ERR;
  }
  size_t bound = sp->bounds.buf[sp->bounds.len - 1] + s.len;
  err = size_vec_push(&sp->bounds, bound);
  if (err != E_OK) {
    dstring_pop(&sp->dstr, s.len);
    errmsg_prefix("size_vec_push: ");
    return E_ERR;
  }
  assert(sp->bounds.len >= 2);
  *s_idx = sp->bounds.len - 2;
  return E_OK;
}

// WARN: the returned string is valid UNTIL the next call to string_pool_push
struct string string_pool_get(const struct string_pool *sp, size_t s_idx) {
  assert(sp != NULL);
  if (s_idx >= sp->bounds.len - 1) {
    panic("string_pool_get: s_idx out of bounds");
  }
  return (struct string) {
    .buf = sp->dstr.buf + sp->bounds.buf[s_idx],
    .len = sp->bounds.buf[s_idx + 1] - sp->bounds.buf[s_idx]
  };
}

/******************************************************************************
 * mutexs                                                                     *
 ******************************************************************************/
#define MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
typedef pthread_mutex_t mutex_t;

void mutex_lock(mutex_t *mu, time_t timeout_sec) {
#if defined(_POSIX_TIMEOUTS) && _POSIX_TIMEOUTS > 0
  struct timespec timeout = {
    .tv_sec = timeout_sec,
    .tv_nsec = 0,
  };
  int rv = pthread_mutex_timedlock(mu, &timeout);
#else
  int rv = pthread_mutex_lock(mu);
#endif
  if (rv != 0) {
    log_error("mutex_lock failed: %s", strerror(errno));
    panic("o rage, o desespoir!");
  }
}

void mutex_trylock(mutex_t *mu) {
  int rv = pthread_mutex_trylock(mu);
  if (rv != 0) {
    log_error("mutex_trylock failed: %s", strerror(errno));
    panic("o rage, o desespoir!");
  }
}

void mutex_unlock(mutex_t *mu) {
  int rv = pthread_mutex_unlock(mu);
  if (rv != 0) {
    log_error("mutex_unlock failed: %s", strerror(errno));
    panic("o rage, o desespoir!");
  }
}

/******************************************************************************
 * semaphore                                                                  *
 ******************************************************************************/
#define SEMAPHORE_UID_LEN 16
void semaphore_get_uid(char out[SEMAPHORE_UID_LEN + 1]) {
  memcpy(out, "/emd_", 5);
  for (size_t i = 5; i < SEMAPHORE_UID_LEN; ++i) {
    out[i] = 'a' + rand() % 26;
  }
  out[SEMAPHORE_UID_LEN] = '\0';
}

void semaphore_destroy(sem_t **sem_ptr) {
  assert(sem_ptr != NULL);

  if (*sem_ptr != NULL && *sem_ptr != SEM_FAILED) {
    int rv = sem_close(*sem_ptr);
    if (rv != 0) log_error("sem_close failed");
  }
  *sem_ptr = NULL;
}

// NOTE: in order to be compatible with MacOS I use sem_open instead of sem_init
err_t semaphore_create(sem_t **sem_ptr, size_t value) {
  assert(sem_ptr != NULL);
  assert(value <= UINT_MAX);

  char name[SEMAPHORE_UID_LEN + 1];
  semaphore_get_uid(name);
  sem_t *sem = sem_open(name, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, value);
  if (sem == NULL || sem == SEM_FAILED) {
    errmsg_fmt("sem_open: %s", strerror(errno));
    semaphore_destroy(&sem);
    return E_ERR;
  }
  int rv = sem_unlink(name);
  if (rv != 0) {
    errmsg_fmt("sem_unlink: %s", strerror(errno));
    semaphore_destroy(&sem);
    return E_ERR;
  }
  *sem_ptr = sem;
  return E_OK;
}

/******************************************************************************
 * FIFO                                                                       *
 ******************************************************************************/

// here unsafe mean not thread safe
// unsafe_ptr_fifo can be zero initialized as soon as `cap` is set
struct unsafe_ptr_fifo {
  void **ptrs;  // ptrs == NULL means fifo as not been initialized yet
  uint32_t cap;
  uint32_t len;
  uint32_t read_idx;
  uint32_t write_idx;
};

void unsafe_ptr_fifo_init(struct unsafe_ptr_fifo *fifo, size_t cap) {
  assert(fifo != NULL);
  assert(cap < UINT32_MAX);
  if (cap == 0 && fifo->cap == 0) {
    panic("unsafe_ptr_fifo capacity can't be null");
  }
  if (cap == 0) {
    cap = fifo->cap;
  }
  void **ptrs = calloc(cap, sizeof(void *));
  if (ptrs == NULL) panic("calloc failed?");
  fifo->ptrs = ptrs;
  fifo->cap = cap;
  fifo->len = 0;
  fifo->read_idx = 0;
  fifo->write_idx = 0;
}

void unsafe_ptr_fifo_destroy(struct unsafe_ptr_fifo *fifo) {
  assert(fifo != NULL);
  free(fifo->ptrs);
  fifo->ptrs = NULL;
}

void unsafe_ptr_fifo_push(struct unsafe_ptr_fifo *fifo, void *ptr) {
  if (fifo->ptrs == NULL) {
    unsafe_ptr_fifo_init(fifo, 0);
  }
  if (fifo->len >= fifo->cap) {
    panic("unsafe_ptr_fifo is full");
  }
  fifo->ptrs[fifo->write_idx] = ptr;
  fifo->write_idx = (fifo->write_idx + 1) % fifo->cap;
  fifo->len += 1;
}

void *unsafe_ptr_fifo_pop(struct unsafe_ptr_fifo *fifo) {
  if (fifo->ptrs == NULL) {
    unsafe_ptr_fifo_init(fifo, 0);
  }
  if (fifo->len == 0) {
    panic("unsafe_ptr_fifo is empty");
  }
  uint32_t old_idx = fifo->read_idx;
  fifo->read_idx = (fifo->read_idx + 1) % fifo->cap;
  fifo->len -= 1;
  return fifo->ptrs[old_idx];
}

// `struct ptr_fifo` can be zero initialized as soon as `cap` is set
struct ptr_fifo {
  mutex_t mu;
  sem_t *pop;
  sem_t *push;
  char pop_name[SEMAPHORE_UID_LEN + 1];
  char push_name[SEMAPHORE_UID_LEN + 1];
  struct unsafe_ptr_fifo unsafe;
};

void ptr_fifo_destroy(struct ptr_fifo *fifo) {
  assert(fifo != NULL);
  semaphore_destroy(&fifo->push);
  semaphore_destroy(&fifo->pop);
  unsafe_ptr_fifo_destroy(&fifo->unsafe);
}

// WARN: fifo must be zeroed beforehand otherwise assert(fifo->unsafe.ptrs ==
// NULL) will fail
err_t ptr_fifo_init(struct ptr_fifo *fifo, size_t cap) {
  assert(fifo != NULL);
  assert(fifo->unsafe.ptrs == NULL);  // avoid double initialization
  
  err_t err = semaphore_create(&fifo->push, cap);
  if (err != E_OK) {
    errmsg_prefix("semaphore_create: ");
    return E_ERR;
  }
  err = semaphore_create(&fifo->pop, 0);
  if (err != E_OK) {
    errmsg_prefix("semaphore_create: ");
    return E_ERR;
  }
  unsafe_ptr_fifo_init(&fifo->unsafe, cap);
  fifo->mu = (mutex_t) MUTEX_INIT;
  return E_OK;
}

// ownership of buf is passed to fifo
// if timeout_sec == 0, ptr_fifo_pop will not timeout
err_t ptr_fifo_push(struct ptr_fifo *fifo, void *ptr, time_t timeout_sec) {
  assert(fifo != NULL);
  assert(fifo->unsafe.ptrs != NULL);
  assert(fifo->push != NULL && fifo->push != SEM_FAILED);
  assert(fifo->pop != NULL && fifo->pop != SEM_FAILED);
  assert(ptr != NULL);

  int rv;
#if defined(_POSIX_TIMEOUTS) && _POSIX_TIMEOUTS > 0
  if (timeout_sec == 0) {
    rv = sem_wait(fifo->push);
  } else {
    struct timespec timeout = {
      .tv_sec = timeout_sec,
      .tv_nsec = 0,
    };
    rv = sem_timedwait(fifo->push, &timeout);
  }
#else
  rv = sem_wait(fifo->push);
#endif
  if (rv != 0) {
    errmsg_fmt("sem_timedwait/wait: %s", strerror(errno));
    return E_ERR;
  }
  mutex_lock(&fifo->mu, timeout_sec);

  unsafe_ptr_fifo_push(&fifo->unsafe, ptr);

  mutex_unlock(&fifo->mu);
  rv = sem_post(fifo->pop);
  if (rv != 0) panic("should not happen");
  return E_OK;
}

// ownership of buf is taken from fifo
// if timeout_sec == 0, ptr_fifo_pop will not timeout
err_t ptr_fifo_pop(struct ptr_fifo *fifo, void **ptr, time_t timeout_sec) {
  assert(fifo != NULL);
  assert(fifo->unsafe.ptrs != NULL);
  assert(ptr != NULL);

  int rv;
#if defined(_POSIX_TIMEOUTS) && _POSIX_TIMEOUTS > 0
  if (timeout_sec == 0) {
    rv = sem_wait(fifo->pop);
  } else {
    struct timespec timeout = {
      .tv_sec = timeout_sec,
      .tv_nsec = 0,
    };
    rv = sem_timedwait(fifo->pop, &timeout);
  }
#else
  rv = sem_wait(fifo->pop);
#endif
  if (rv != 0) {
    errmsg_fmt("sem_timedwait/wait: %s", strerror(errno));
    return E_ERR;
  }
  mutex_lock(&fifo->mu, timeout_sec);

  *ptr = unsafe_ptr_fifo_pop(&fifo->unsafe);

  mutex_unlock(&fifo->mu);
  rv = sem_post(fifo->push);
  if (rv != 0) panic("should not happen");
  return E_OK;
}

// returns E_EMPTY if the fifo is empty
err_t ptr_fifo_try_pop(struct ptr_fifo *fifo, void **ptr) {
  assert(fifo != NULL);
  assert(fifo->unsafe.ptrs != NULL);
  assert(ptr != NULL);

  int rv = sem_trywait(fifo->pop);
  if (rv == EAGAIN) {
    return E_EMPTY;
  } else if (rv != E_OK) {
    errmsg_fmt("sem_trywait: %s", strerror(errno));
    return E_ERR;
  }

  mutex_lock(&fifo->mu, 3);
  *ptr = unsafe_ptr_fifo_pop(&fifo->unsafe);
  mutex_unlock(&fifo->mu);
  rv = sem_post(fifo->push);
  if (rv != 0) panic("should not happen");
  return E_OK;
}

/******************************************************************************
 * time and date                                                              *
 ******************************************************************************/
const time_t TIME_MINUTE = 60;
const time_t TIME_HOUR = 60 * 60;
const time_t TIME_DAY = 60 * 60 * 24;
err_t timezone_set(const char *tz) {
  int rv = setenv("TZ", tz, 1);
  if (rv != 0) {
    errmsg_fmt("setenv: %s", strerror(errno));
    return E_ERR;
  }
  tzset();
  return E_OK;
}

err_t time_parse(const char *format, const char *str, time_t *time) {
  assert(str != NULL);
  assert(time != NULL);
  assert(format != NULL);
  struct tm tm = {0};
  char *endptr = strptime(str, format, &tm);
  if (endptr == NULL || *endptr != '\0') {
    errmsg_fmt("strptime: invalid date format");
    return E_ERR;
  }
  *time = mktime(&tm);  // timezone_set("GMT") must be call beforehand
  if (*time == (time_t) -1) {
    errmsg_fmt("mktime: %s", strerror(errno));
    return E_ERR;
  }
  return E_OK;
}

time_t time_eleven_fifteen_today(time_t now) {
  struct tm tm;
  struct tm *rv = gmtime_r(&now, &tm);
  assert(rv != NULL);
  tm.tm_sec = 0;
  tm.tm_min = 15;
  tm.tm_hour = 11;
  return mktime(&tm);  // timezone_set("GMT") must be call beforehand
}

time_t time_eleven_fifteen_tomorrow(time_t now) {
  struct tm tm;
  struct tm *rv = gmtime_r(&now, &tm);
  assert(rv != NULL);
  tm.tm_sec = 0;
  tm.tm_min = 15;
  tm.tm_hour = 11;
  tm.tm_mday += 1;  // mday will be normalized by `timegm`
  return mktime(&tm);  // timezone_set("GMT") must be call beforehand
}

struct date {
  uint16_t year;
  uint16_t day;
};

// return iso 8601 ordinal date
err_t date_parse(const char *format, const char *str, struct date *date) {
  assert(format != NULL);
  assert(str != NULL);
  assert(date != NULL);
  struct tm tm = {0};
  char *endptr = strptime(str, format, &tm);
  if (endptr == NULL || *endptr != '\0') {
    errmsg_fmt("strptime: invalid date format");
    return E_ERR;
  }
  if (tm.tm_year < 0 || tm.tm_year > UINT16_MAX - 1900) {
    errmsg_fmt("strptime: year %d out of range", tm.tm_year);
    return E_ERR;
  }
  if (tm.tm_yday < 0 || tm.tm_yday > UINT16_MAX) {
    errmsg_fmt("strptime: day %d out of range", tm.tm_yday);
    return E_ERR;
  }
  date->year = tm.tm_year + 1900;
  date->day = tm.tm_yday;
  return E_OK;
}

// is date a strictly before date b (return false if a == b)
bool date_is_before(struct date a, struct date b) {
  return a.year < b.year || (a.year == b.year && a.day < b.day);
}

// is date a strictly after date b (return false if a == b)
bool date_is_after(struct date a, struct date b) {
  return a.year > b.year || (a.year == b.year && a.day > b.day);
}

bool date_is_equal(struct date a, struct date b) {
  return a.year == b.year && a.day == b.day;
}

bool date_is_leap_year(uint16_t year) {
  return year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
}

void date_incr(struct date *date) {
  assert(date != NULL);
  uint64_t day_in_year = date_is_leap_year(date->year) ? 366 : 365;
  if (date->day >= day_in_year) {
    date->day = 1;
    date->year += 1;
  } else {
    date->day += 1;
  }
}

struct date date_utc(time_t time) {
  struct tm tm = {0};
  struct tm *res = gmtime_r(&time, &tm);
  if (res == NULL) {
    panic("gmtime_r returned an error");
  }
  return (struct date) {
    .year = tm.tm_year + 1900,
    .day = tm.tm_yday + 1,  // POSIX api is so weird
  };
}

/******************************************************************************
 * serialization                                                              *
 ******************************************************************************/
err_t serialize_uint8(FILE *stream, uint8_t n) {
  assert(stream != NULL);
  unsigned char bytes[] = { n };
  size_t count = fwrite(bytes, 1, 1, stream);
  if (count < 1) {
    errmsg_fmt("fwrite: %s", strerror(errno));
    return E_ERR;
  }
  return E_OK;
}

err_t serialize_uint16(FILE *stream, uint16_t n) {
  assert(stream != NULL);
  unsigned char bytes[] = { n >> 8, n };
  size_t count = fwrite(bytes, 2, 1, stream);
  if (count < 1) {
    errmsg_fmt("fwrite: %s", strerror(errno));
    return E_ERR;
  }
  return E_OK;
}

err_t serialize_uint32(FILE *stream, uint32_t n) {
  assert(stream != NULL);
  unsigned char bytes[] = { n >> 24, n >> 16, n >> 8, n };
  size_t count = fwrite(bytes, 4, 1, stream);
  if (count < 1) {
    errmsg_fmt("fwrite: %s", strerror(errno));
    return E_ERR;
  }
  return E_OK;
}

err_t serialize_uint64(FILE *stream, uint64_t n) {
  assert(stream != NULL);
  unsigned char bytes[] = { n >> 56, n >> 48, n >> 40, n >> 32, n >> 24, n >> 16, n >> 8, n };
  size_t count = fwrite(bytes, 8, 1, stream);
  if (count < 1) {
    errmsg_fmt("fwrite: %s", strerror(errno));
    return E_ERR;
  }
  return E_OK;
}

err_t serialize_int8(FILE *stream, int8_t n) {
  return serialize_uint8(stream, (uint8_t) n);
}

err_t serialize_int16(FILE *stream, int16_t n) {
  return serialize_uint16(stream, (uint16_t) n);
}

err_t serialize_int32(FILE *stream, int32_t n) {
  return serialize_uint32(stream, (uint32_t) n);
}

err_t serialize_int64(FILE *stream, int64_t n) {
  return serialize_uint64(stream, (uint64_t) n);
}

err_t serialize_float32(FILE *stream, float x) {
  uint32_t n;
  memcpy(&n, &x, 4);  // to pease c aliasing rules
  return serialize_uint32(stream, n);
}

err_t serialize_float64(FILE *stream, double x) {
  uint64_t n;
  memcpy(&n, &x, 8);  // to pease c aliasing rules
  return serialize_uint64(stream, n);
}

err_t serialize_string(FILE *stream, struct string s) {
  assert(stream != NULL);
  err_t err = serialize_uint64(stream, s.len);
  if (err != E_OK) {
    errmsg_prefix("serialize_uint64: ");
    return E_ERR;
  }
  size_t count = fwrite(s.buf, s.len, 1, stream);
  if (count < 1) {
    errmsg_fmt("serialize_string: write: %s", strerror(errno));
    return E_ERR;
  }
  return E_OK;
}
