// This is a csv parser. Multiline strings are not supported.

const char CSV_SEPARATOR = ',';
const char CSV_NEWLINE   = '\n';

struct csv_reader {
  const char *buf;
  size_t buf_len;
  size_t idx;
  size_t line_start_idx;
};

struct csv_reader csv_reader_create(const char *buf, size_t buf_len) {
  return (struct csv_reader) {
    .buf = buf,
    .buf_len = buf_len,
    .idx = 0,
    .line_start_idx = 0
  };
}

// call this before reading a newline. that helps to assert you read the csv
// correctly
err_t csv_line_end(struct csv_reader *rdr) {
  assert(rdr != NULL);

  if (rdr->idx > rdr->buf_len) {
    errmsg_fmt("csv error: reader index out of range");
    return E_ERR;
  }
  if (rdr->idx != rdr->buf_len && rdr->buf[rdr->idx] != CSV_NEWLINE) {
    errmsg_fmt("csv error: reader not at the end of the line");
    return E_ERR;
  }

  if (rdr->idx != rdr->buf_len) {
    rdr->idx += 1;
    rdr->line_start_idx = rdr->idx;
  }
  return E_OK;
}

// NOTE: I might new to add a csv_skip_field in the future

// copy field from csv to `out`, `out` is then null terminated
err_t csv_read_field(struct csv_reader *rdr, char *out, size_t out_len) {
  assert(rdr != NULL);
  assert(out != NULL);

  if (rdr->idx >= rdr->buf_len) {
    errmsg_fmt("csv error: reader index out of range");
    return E_ERR;
  }
  if (rdr->idx != rdr->line_start_idx &&
      rdr->buf[rdr->idx] != CSV_SEPARATOR) {
    if (rdr->buf[rdr->idx] == CSV_NEWLINE) {
      errmsg_fmt("csv error: reader index out of position (you probably forgot to call `csv_line_end`)");
    } else {
      errmsg_fmt("csv error: reader index out of position");
    }
    return E_ERR;
  }

  if (rdr->idx != rdr->line_start_idx) {
    rdr->idx += 1;
  }

  size_t out_idx = 0;
  for (; rdr->idx < rdr->buf_len; ++rdr->idx) {
    if (rdr->buf[rdr->idx] == CSV_SEPARATOR || rdr->buf[rdr->idx] == CSV_NEWLINE) {
      break;
    }
    if (out_idx >= out_len - 1) {
      if (out_len <= 128) {
        errmsg_fmt("csv error: field \"%.*s\" is larger that out_len-1 %zu",
                   (int) out_len, out, out_len - 1);
      } else {
        errmsg_fmt("csv error: field is larger that out_len-1 %zu", out_len - 1);
      }
      return E_ERR;
    }
    out[out_idx++] = rdr->buf[rdr->idx];
  }

  assert(out_idx < out_len);
  out[out_idx] = '\0'; 
  return E_OK;
}

err_t csv_read_int(struct csv_reader *rdr, intmax_t *n_ptr) {
  assert(rdr != NULL);
  assert(n_ptr != NULL);

  char field[64];
  err_t err = csv_read_field(rdr, field, 64);
  if (err != E_OK) return err;
  if (field[0] == '\0') {
    errmsg_fmt("csv error: field is empty");
    return E_ERR;
  }
  if (isspace(field[0])) {
    errmsg_fmt("csv error: whitespace is not allowed");
    return E_ERR;
  }

  char *endptr;
  intmax_t n = strtoimax(field, &endptr, 10);
  if (*endptr != '\0' || errno == ERANGE || errno == EINVAL) {
    errmsg_fmt("csv error: field \"%s\" is not a valid intmax (whitespace is not allowed)");
    return E_ERR;
  }

  *n_ptr = n;
  return E_OK;
}

err_t csv_read_float(struct csv_reader *rdr, double *x_ptr) {
  assert(rdr != NULL);
  assert(x_ptr != NULL);

  char field[64];
  err_t err = csv_read_field(rdr, field, 64);
  if (err != E_OK) return err;
  if (field[0] == '\0') {
    errmsg_fmt("csv error: field is empty");
    return E_ERR;
  }
  if (isspace(field[0])) {
    errmsg_fmt("csv error: whitespace is not allowed");
    return E_ERR;
  }

  char *endptr;
  double x = strtod(field, &endptr);
  if (*endptr != '\0' || errno == ERANGE) {
    errmsg_fmt("csv error: field \"%s\" is not a valid intmax (whitespace is not allowed)");
    return E_ERR;
  }

  *x_ptr = x;
  return E_OK;
}

// WARN: returned string is read-only
err_t csv_read_string(struct csv_reader *rdr, struct string *str_ptr) {
  assert(rdr != NULL);
  assert(str_ptr != NULL);

  if (rdr->idx >= rdr->buf_len) {
    errmsg_fmt("csv error: reader index out of range");
    return E_ERR;
  }
  if (rdr->idx != rdr->line_start_idx &&
      rdr->buf[rdr->idx] != CSV_SEPARATOR) {
    if (rdr->buf[rdr->idx] == CSV_NEWLINE) {
      errmsg_fmt("csv error: reader index out of position (you probably forgot to call `csv_line_end`)");
    } else {
      errmsg_fmt("csv error: reader index out of position");
    }
    return E_ERR;
  }

  if (rdr->idx != rdr->line_start_idx) {
    rdr->idx += 1;
  }

  size_t start_idx = rdr->idx;
  for (; rdr->idx < rdr->buf_len; ++rdr->idx) {
    if (rdr->buf[rdr->idx] == CSV_SEPARATOR || rdr->buf[rdr->idx] == CSV_NEWLINE) {
      break;
    }
  }

  *str_ptr = (struct string) {
    .buf = (char *) rdr->buf + start_idx,
    .len = rdr->idx - start_idx
  };
  return E_OK;
}
