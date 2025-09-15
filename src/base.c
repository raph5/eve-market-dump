#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
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

// TODO: assert types length

/******************************************************************************
 * 3rd party dependencies                                                     *
 ******************************************************************************/
#include <curl/curl.h>
#include <zlib.h>
#include <jansson.h>

#if LIBCURL_VERSION_NUM != 0x080f00
#warning "You are not building against curl version 8.15.0. May bob be with you"
#endif

#if JANSSON_MAJOR_VERSION != 2 || JANSSON_MINOR_VERSION != 13 || \
    JANSSON_MICRO_VERSION != 1
#warning "You are not building against jansson version 2.13.1. May bob be with you"
#endif

#if ZLIB_VERNUM != 0x1310
#warning "You are not building against zlib version 1.3.1. May bob be with you"
#endif

/******************************************************************************
 * panic                                                                      *
 ******************************************************************************/
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
  // TODO: do, in fact, call cleanup
  abort();
}

void _assert(bool test, int line, char *file) {
  if (test) return;

  fprintf(stderr, "\x1b[1;35massertion failed:\x1b[0m file %s, line %d\n\nstracktrace:\n",
          file, line);
  
  void *trace[4096];
  int len = backtrace(trace, 4096);
  backtrace_symbols_fd(trace, len, STDERR_FILENO);
  // TODO: do, in fact, call cleanup
  abort();
}
#else
void _panic(const char *msg, int line, char *file) {
  fprintf(stderr, "\x1b[1;35mpanic:\x1b[0m file %s, line %d, %s\n", file, line,
          msg);
  // TODO: do, in fact, call cleanup
  abort();
}

void _assert(bool test, int line, char *file) {
  if (test) return;

  fprintf(stderr, "\x1b[1;35massertion failed:\x1b[0m file %s, line %d:\n",
          file, line);
  // TODO: do, in fact, call cleanup
  abort();
}
#endif

/******************************************************************************
 * logs                                                                       *
 ******************************************************************************/
void log_print(char *fmt, ...) {
  time_t t;
  struct tm tm;
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
  struct tm tm;
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
  struct tm tm;
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

struct string string_alloc(size_t len) {
  char *buf = malloc(len);
  if (buf == NULL) {
    return (struct string) { .buf = NULL, .len = 0 };
  } else {
    return (struct string) { .buf = buf, .len = len };
  }
}

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

struct string string_alloc_cpy(struct string src) {
  char *buf = malloc(src.len);
  if (buf == NULL) {
    return (struct string) { .buf = NULL, .len = 0 };
  } else {
    memcpy(buf, src.buf, src.len);
    return (struct string) { .buf = buf, .len = src.len };
  }
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
  size_t new_cap = ds->cap;
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
  if (da->len + 1 > da->cap) { \
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

IMPLEMENT_VEC(size_t, size);

/******************************************************************************
 * context                                                                    *
 ******************************************************************************/
/*
#define CONTEXT_VALUES_MAX 16

struct context_value {
  struct string key;
  void *value;
};

struct context_value_tab {
  size_t count;
  struct context_value t[CONTEXT_VALUES_MAX];
};

struct context {
  struct context_value_tab value_tab;
};

void context_init_value(struct context *ctx, struct string key, void *value) {
  assert(ctx != NULL);
  for (size_t i = 0; i < ctx->value_tab.count; ++i) {
    if (string_cmp(key, ctx->value_tab.t[i].key) == 0) {
      panic("context: key already in use");
    }
  }
  if (ctx->value_tab.count >= CONTEXT_VALUES_MAX) {
    panic("context: you ran out of values slots");
  }
  ctx->value_tab.t[ctx->value_tab.count].key = key;
  ctx->value_tab.t[ctx->value_tab.count].value = value;
  ++ctx->value_tab.count;
}

void context_set_value(struct context *ctx, struct string key, void *value) {
  assert(ctx != NULL);
  for (size_t i = 0; i < ctx->value_tab.count; ++i) {
    if (string_cmp(key, ctx->value_tab.t[i].key) == 0) {
      ctx->value_tab.t[i].value = value;
      return;
    }
  }
  panic("context: value not found");
}

void *context_get_value(const struct context *ctx, struct string key) {
  assert(ctx != NULL);
  for (size_t i = 0; i < ctx->value_tab.count; ++i) {
    if (string_cmp(key, ctx->value_tab.t[i].key) == 0) {
      return ctx->value_tab.t[i].value;
    }
  }
  panic("context: value not found");
  return NULL;
}
*/

/******************************************************************************
 * mutexs                                                                     *
 ******************************************************************************/
typedef pthread_mutex_t mutex_t;
const mutex_t MUTEX_INIT = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER;

void mutex_lock(mutex_t *mu, uint64_t timeout_sec) {
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
  return serialize_uint32(stream, (uint32_t) x);
}

err_t serialize_float64(FILE *stream, double x) {
  return serialize_uint64(stream, (uint64_t) x);
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

/******************************************************************************
 * cli interface                                                              *
 ******************************************************************************/
const char* MAN =
"NAME\n"
"\temd - Eve Market Dump\n"
"\n"
"SYNOPSIS\n"
"\temd [options]\n"
"\n"
"OPTIONS\n"
"\t--secrets STRING\n"
"\t\tjson string containing the secrets in foramt {key: value} (default \"{}\")\n";

struct args {
  struct string secrets;
};

err_t args_parse(int argc, char *argv[], struct args *args) {
  *args = (struct args) {
    .secrets = string_new("{}")
  };

  struct option opt_table[] = {
    { .name = "secrets", .has_arg = required_argument }
  };

  int rv, opt_index = -1;
  while ((rv = getopt_long(argc, argv, "", opt_table, &opt_index)) == 0) {
    switch (opt_index) {
      case 0:
        args->secrets = string_new(optarg);
        break;
      default:
        panic("unreachable");
    }
  }

  if (optind < argc) {
    printf("Unrecognized or malformed options\n\n%s", MAN);
    return E_ERR;
  }

  return E_OK;
}
