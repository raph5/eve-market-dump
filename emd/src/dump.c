
const uint8_t DUMP_VERSION       = 1;
const char    DUMP_ASCII_ART[32] = "இ}ڿڰۣ-ڰۣ~—";

const uint8_t DUMP_TYPE_LOCATIONS = 0;
const uint8_t DUMP_TYPE_ORDERS    = 1;
const uint8_t DUMP_TYPE_HISTORIES = 2;
const uint8_t DUMP_TYPE_INTERNAL  = 3;

struct dump_record_entry {
  FILE *fp;
  char *path;
};

#define DUMP_RECORD_CAP 16
struct dump_record_entry global_dump_record[DUMP_RECORD_CAP];
size_t                   global_dump_record_len = 0;
mutex_t                  global_dump_record_mu = MUTEX_INIT;

// entry.path string is copyed so its ownership doesn't change
void dump_record_push(FILE *fp, struct string path) {
  mutex_lock(&global_dump_record_mu, 3);
  assert(global_dump_record_len < DUMP_RECORD_CAP);

  char *path_cpy;
  err_t err = string_alloc_null_terminated_cpy(&path_cpy, path);
  if (err != E_OK) {
    errmsg_prefix("string_alloc_null_terminated_cpy: ");
    errmsg_print();
    panic("I don't want to handle this error");
  }

  global_dump_record[global_dump_record_len] = (struct dump_record_entry) {
    .fp = fp,
    .path = path_cpy,
  };
  global_dump_record_len += 1;
  mutex_unlock(&global_dump_record_mu);
}

void dump_record_pop(FILE* fp) {
  mutex_lock(&global_dump_record_mu, 3);
  for (size_t i = 0; i < global_dump_record_len; ++i) {
    if (global_dump_record[i].fp == fp) {
      free(global_dump_record[i].path);
      global_dump_record[global_dump_record_len - 1] = global_dump_record[i];
      global_dump_record_len -= 1;
      mutex_unlock(&global_dump_record_mu);
      return;
    }
  }
  mutex_unlock(&global_dump_record_mu);
}

// close and unlink (remove) every files that are still in the dump_record
// any error will be printed to stdout
void dump_record_burn(void) {
  mutex_lock(&global_dump_record_mu, 3);
  for (size_t i = 0; i < global_dump_record_len; ++i) {
    FILE *fp = global_dump_record[i].fp;
    char *path = global_dump_record[i].path;
    log_warn("store was closed while writing to file %s, to avoid producing a corrupted dump this file will be removed", path);
    size_t path_len = strlen(path);
    int rv = fclose(fp);
    if (rv != 0) {
      log_error("failed to close file %.*s: %s", path_len, path, strerror(errno));
    }
    rv = unlink(path);
    if (rv != 0) {
      log_error("failed to unlink file %.*s: %s", path_len, path, strerror(errno));
    }
    free(path);
  }
  global_dump_record_len = 0;
  mutex_unlock(&global_dump_record_mu);
}

enum dump_mode {
  DUMP_WRITE,
  DUMP_READ,
};

struct dump {
  FILE *file;
  uint32_t checksum;
  enum dump_mode mode;
};

// `expiration` is expiration date of said data
// WARN: Don't open a file twice
err_t dump_open_write(struct dump *dump, struct string path, uint8_t type,
                      time_t expiration) {
  assert(dump != NULL);

  const size_t PATH_LEN_MAX = 2048;
  char path_nt[PATH_LEN_MAX];
  string_null_terminate(path, path_nt, PATH_LEN_MAX);
  FILE *file = fopen(path_nt, "w");
  if (file == NULL) {
    errmsg_fmt("fopen: %s", strerror(errno));
    return E_ERR;
  }
  dump_record_push(file, path);

  // version
  err_t err = serialize_uint8(file, DUMP_VERSION);
  if (err != E_OK) {
    errmsg_prefix("serialize_uint8: ");
    fclose(file);
    return E_ERR;
  }

  // type
  err = serialize_uint8(file, type);
  if (err != E_OK) {
    errmsg_prefix("serialize_uint8: ");
    fclose(file);
    return E_ERR;
  }

  // checksum
  err = serialize_uint32(file, 0);
  if (err != E_OK) {
    errmsg_prefix("serialize_uint32: ");
    fclose(file);
    return E_ERR;
  }

  // expiration
  err = serialize_uint64(file, (uint64_t) expiration);
  if (err != E_OK) {
    errmsg_prefix("serialize_uint64: ");
    fclose(file);
    return E_ERR;
  }

  // ascii art
  size_t count = fwrite(DUMP_ASCII_ART, 32, 1, file);
  if (count < 1) {
    errmsg_fmt("fwrite: %s", strerror(errno));
    fclose(file);
    return E_ERR;
  }

  dump->file = file;
  dump->checksum = 0;
  dump->mode = DUMP_WRITE;
  return E_OK;
}

err_t dump_close_write(struct dump *dump) {
  assert(dump != NULL);
  assert(dump->file != NULL);
  assert(dump->mode == DUMP_WRITE);

  // update checksum
  int rv = fseek(dump->file, 2, SEEK_SET);
  if (rv != 0) {
    errmsg_fmt("fseek: %s", strerror(errno));
    fclose(dump->file);
    return E_ERR;
  }
  err_t err = serialize_uint32(dump->file, dump->checksum);
  if (err != E_OK) {
    errmsg_prefix("serialize_uint32: ");
    fclose(dump->file);
    return E_ERR;
  }

  // close file
  rv = fclose(dump->file);
  if (rv != 0) {
    errmsg_fmt("fclose: %s", strerror(errno));
    return E_ERR;
  }
  dump_record_pop(dump->file);
  dump->file = NULL;
  dump->checksum = 0;

  return E_OK;
}

// WARN: Don't open a file twice
err_t dump_open_read(struct dump *dump, struct string path) {
  assert(dump != NULL);

  const size_t PATH_LEN_MAX = 2048;
  char path_nt[PATH_LEN_MAX];
  string_null_terminate(path, path_nt, PATH_LEN_MAX);
  FILE *file = fopen(path_nt, "r");
  if (file == NULL) {
    errmsg_fmt("fopen: %s", strerror(errno));
    return E_ERR;
  }

  int rv = fseek(file, 46, SEEK_SET);  // seek the begin of the body
  if (rv != 0) {
    errmsg_fmt("fseek: %s", strerror(errno));
    fclose(dump->file);
    return E_ERR;
  }

  // NOTE: here, I didn't checked the dump version nor checksum
  dump->file = file;
  dump->checksum = 0;
  dump->mode = DUMP_READ;
  return E_OK;
}

err_t dump_close_read(struct dump *dump) {
  assert(dump != NULL);
  assert(dump->file != NULL);
  assert(dump->mode == DUMP_READ);

  int rv = fclose(dump->file);
  if (rv != 0) {
    errmsg_fmt("fclose: %s", strerror(errno));
    return E_ERR;
  }
  dump->file = NULL;
  dump->checksum = 0;
  return E_OK;
}

err_t dump_seek_start(struct dump *dump) {
  assert(dump != NULL);
  int rv = fseek(dump->file, 46, SEEK_SET);  // seek the begin of the body
  if (rv != 0) {
    errmsg_fmt("fseek: %s", strerror(errno));
    return E_ERR;
  }
  return E_OK;
}

err_t dump_write(struct dump *dump, const unsigned char *buf, size_t buf_len) {
  assert(dump != NULL);
  assert(dump->file != NULL);
  assert(dump->mode == DUMP_WRITE);
  assert(buf != NULL);

  size_t items = fwrite(buf, buf_len, 1, dump->file);
  if (items < 1) {
    errmsg_fmt("fwrite: %s", strerror(errno));
    return E_ERR;
  }
  dump->checksum = crc32_z(dump->checksum, buf, buf_len);

  return E_OK;
}

err_t dump_write_uint8(struct dump *dump, uint8_t n) {
  unsigned char bytes[] = { n };
  err_t err = dump_write(dump, bytes, 1);
  if (err != E_OK) {
    errmsg_prefix("dump_write: ");
    return E_ERR;
  }
  return E_OK;
}

err_t dump_write_uint16(struct dump *dump, uint16_t n) {
  unsigned char bytes[] = { n >> 8, n };
  err_t err = dump_write(dump, bytes, 2);
  if (err != E_OK) {
    errmsg_prefix("dump_write: ");
    return E_ERR;
  }
  return E_OK;
}

err_t dump_write_uint32(struct dump *dump, uint32_t n) {
  unsigned char bytes[] = { n >> 24, n >> 16, n >> 8, n };
  err_t err = dump_write(dump, bytes, 4);
  if (err != E_OK) {
    errmsg_prefix("dump_write: ");
    return E_ERR;
  }
  return E_OK;
}

err_t dump_write_uint64(struct dump *dump, uint64_t n) {
  unsigned char bytes[] = { n >> 56, n >> 48, n >> 40, n >> 32, n >> 24, n >> 16, n >> 8, n };
  err_t err = dump_write(dump, bytes, 8);
  if (err != E_OK) {
    errmsg_prefix("dump_write: ");
    return E_ERR;
  }
  return E_OK;
}

err_t dump_write_int8(struct dump *dump, int8_t n) {
  return dump_write_uint8(dump, (uint8_t) n);
}

err_t dump_write_int16(struct dump *dump, int16_t n) {
  return dump_write_uint16(dump, (uint16_t) n);
}

err_t dump_write_int32(struct dump *dump, int32_t n) {
  return dump_write_uint32(dump, (uint32_t) n);
}

err_t dump_write_int64(struct dump *dump, int64_t n) {
  return dump_write_uint64(dump, (uint64_t) n);
}

err_t dump_write_float32(struct dump *dump, float x) {
  uint32_t n;
  memcpy(&n, &x, 4);  // to pease c aliasing rules
  return dump_write_uint32(dump, n);
}

err_t dump_write_float64(struct dump *dump, double x) {
  uint64_t n;
  memcpy(&n, &x, 8);  // to pease c aliasing rules
  return dump_write_uint64(dump, n);
}

err_t dump_write_string(struct dump *dump, struct string s) {
  err_t err = dump_write_uint64(dump, s.len);
  if (err != E_OK) {
    errmsg_prefix("dump_write_uint64: ");
    return E_ERR;
  }
  if (s.len > 0) {
    err = dump_write(dump, (unsigned char *) s.buf, s.len);
    if (err != E_OK) {
      errmsg_prefix("dump_write: ");
      return E_ERR;
    }
  }
  return E_OK;
}

err_t dump_write_date(struct dump *dump, struct date date) {
  if (dump_write_uint16(dump, date.year) != E_OK) goto error;
  if (dump_write_uint16(dump, date.day) != E_OK) goto error;
  return E_OK;
error:
  errmsg_prefix("dump_write_uint16: ");
  return E_ERR;
}

err_t dump_read(struct dump *dump, unsigned char *buf, size_t buf_len) {
  assert(dump != NULL);
  assert(dump->file != NULL);
  assert(dump->mode == DUMP_READ);
  assert(buf != NULL);

  size_t items = fread(buf, buf_len, 1, dump->file);
  if (items < 1) {
    if (feof(dump->file)) {
      errmsg_fmt("fread: end of file");
      return E_EOF;
    } else {
      errmsg_fmt("fread: %s", strerror(errno));
      return E_ERR;
    }
  }

  return E_OK;
}

err_t dump_read_uint8(struct dump *dump, uint8_t *n) {
  assert(n != NULL);
  unsigned char bytes[1] = {0};
  err_t err = dump_read(dump, bytes, 1);
  if (err != E_OK) return err;
  *n = (uint8_t) bytes[0];
  return E_OK;
}

err_t dump_read_uint16(struct dump *dump, uint16_t *n) {
  assert(n != NULL);
  unsigned char bytes[2] = {0};
  err_t err = dump_read(dump, bytes, 2);
  if (err != E_OK) return err;
  *n = ((uint16_t) bytes[0] << 8) + (uint16_t) bytes[1];
  return E_OK;
}

err_t dump_read_uint32(struct dump *dump, uint32_t *n) {
  assert(n != NULL);
  unsigned char bytes[4] = {0};
  err_t err = dump_read(dump, bytes, 4);
  if (err != E_OK) return err;
  *n = ((uint32_t) bytes[0] << 24) + ((uint32_t) bytes[1] << 16) + \
       ((uint32_t) bytes[2] << 8) + (uint32_t) bytes[3];
  return E_OK;
}

err_t dump_read_uint64(struct dump *dump, uint64_t *n) {
  assert(n != NULL);
  unsigned char bytes[8] = {0};
  err_t err = dump_read(dump, bytes, 8);
  if (err != E_OK) return err;
  *n = ((uint64_t) bytes[0] << 56) + ((uint64_t) bytes[1] << 48) + \
       ((uint64_t) bytes[2] << 40) + ((uint64_t) bytes[3] << 32) + \
       ((uint64_t) bytes[4] << 24) + ((uint64_t) bytes[5] << 16) + \
       ((uint64_t) bytes[6] << 8) + (uint64_t) bytes[7];
  return E_OK;
}

err_t dump_read_int8(struct dump *dump, int8_t *n) {
  return dump_read_uint8(dump, (uint8_t *) n);
}

err_t dump_read_int16(struct dump *dump, int16_t *n) {
  return dump_read_uint16(dump, (uint16_t *) n);
}

err_t dump_read_int32(struct dump *dump, int32_t *n) {
  return dump_read_uint32(dump, (uint32_t *) n);
}

err_t dump_read_int64(struct dump *dump, int64_t *n) {
  return dump_read_uint64(dump, (uint64_t *) n);
}

err_t dump_read_float32(struct dump *dump, float *x) {
  assert(x != NULL);
  uint32_t n;
  err_t res = dump_read_uint32(dump, &n);
  if (res != E_OK) return E_ERR;
  memcpy(x, &n, 4);  // to pease c aliasing rules
  return E_OK;
}

err_t dump_read_float64(struct dump *dump, double *x) {
  assert(x != NULL);
  uint64_t n;
  err_t res = dump_read_uint64(dump, &n);
  if (res != E_OK) return E_ERR;
  memcpy(x, &n, 8);  // to pease c aliasing rules
  return E_OK;
}

err_t dump_read_date(struct dump *dump, struct date *date) {
  assert(date != NULL);
  err_t res;
  if ((res = dump_read_uint16(dump, &date->year)) != E_OK) return res;
  if ((res = dump_read_uint16(dump, &date->day)) != E_OK) return res;
  return E_OK;
}
