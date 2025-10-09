
const uint8_t DUMP_VERSION       = 1;
const char    DUMP_ASCII_ART[32] = "இ}ڿڰۣ-ڰۣ~—";

const uint8_t DUMP_TYPE_LOCATIONS = 0;
const uint8_t DUMP_TYPE_ORDERS    = 1;
const uint8_t DUMP_TYPE_HISTORIES = 2;

struct dump {
  FILE *file;
  uint32_t checksum;
};

// `expiration` is expiration date of said data
err_t dump_open(struct dump *dump, struct string path, uint8_t type,
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
  return E_OK;
}

err_t dump_close(struct dump *dump) {
  assert(dump != NULL);
  assert(dump->file != NULL);

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
  dump->file = NULL;
  dump->checksum = 0;

  return E_OK;
}

err_t dump_write(struct dump *dump, const unsigned char *buf, size_t buf_len) {
  assert(dump != NULL);
  assert(dump->file != NULL);
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
  err = dump_write(dump, (unsigned char *) s.buf, s.len);
  if (err != E_OK) {
    errmsg_prefix("dump_write: ");
    return E_ERR;
  }
  return E_OK;
}
