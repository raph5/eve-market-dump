
struct loc_name_collec {
  struct dstring buf;
  struct size_vec bound;
};

err_t loc_name_collec_create(struct loc_name_collec *nc) {
  assert(nc != NULL);
  err_t err = dstring_create(&nc->buf, 1024);
  if (err != E_OK) {
    errmsg_prefix("dstring_create: ");
    return E_ERR;
  }
  err = size_vec_create(&nc->bound, 64);
  if (err != E_OK) {
    errmsg_prefix("size_vec_create: ");
    dstring_destroy(&nc->buf);
    return E_ERR;
  }
  err = size_vec_push(&nc->bound, 0);
  if (err != E_OK) panic("unreachable");
  return E_OK;
}

void loc_name_collec_destroy(struct loc_name_collec *nc) {
  assert(nc != NULL);
  dstring_destroy(&nc->buf);
  size_vec_destroy(&nc->bound);
}

err_t loc_name_collec_push(struct loc_name_collec *nc, struct string name,
                           size_t *name_index) {
  assert(nc != NULL);
  assert(name_index != NULL);
  err_t err = dstring_push(&nc->buf, name);
  if (err != E_OK) {
    errmsg_prefix("dstring_push: ");
    return E_ERR;
  }
  size_t bound = nc->bound.buf[nc->bound.len - 1] + name.len;
  err = size_vec_push(&nc->bound, bound);
  if (err != E_OK) {
    dstring_pop(&nc->buf, name.len);
    errmsg_prefix("size_vec_push: ");
    return E_ERR;
  }
  assert(nc->bound.len >= 2);
  *name_index = nc->bound.len - 2;
  return E_OK;
}

struct string loc_name_collec_get(const struct loc_name_collec *nc,
                                  size_t name_index) {
  assert(nc != NULL);
  if (name_index >= nc->bound.len - 1) {
    panic("loc_name_collec_get: name_index out of bounds");
  }
  return (struct string) {
    .buf = nc->buf.buf + nc->bound.buf[name_index],
    .len = nc->bound.buf[name_index + 1] - nc->bound.buf[name_index]
  };
}

struct loc_loc {
  uint64_t id;
  uint64_t type_id;  // station type id
  uint64_t corp_id;  // station corporation id
  uint64_t system_id;
  float    security;
  size_t   name_index;
};

IMPLEMENT_VEC(struct loc_loc, loc);

err_t serialize_loc(FILE *stream, struct loc_loc *loc,
                    struct loc_name_collec *nc) {
  assert(loc != NULL);
  assert(nc != NULL);

  if (serialize_uint64(stream, loc->id) != E_OK) goto error;
  if (serialize_uint64(stream, loc->type_id) != E_OK) goto error;
  if (serialize_uint64(stream, loc->corp_id) != E_OK) goto error;
  if (serialize_uint64(stream, loc->system_id) != E_OK) goto error;
  if (serialize_float32(stream, loc->security) != E_OK) goto error;
  struct string name = loc_name_collec_get(nc, loc->name_index);
  if (serialize_string(stream, name) != E_OK) goto error;
  return E_OK;

error:
  errmsg_prefix("serialize_uint64/float32/string");
  return E_ERR;
}

err_t loc_csv_init(char *buf, size_t buf_len, struct csv_reader *rdr) {
  assert(rdr != NULL);
  *rdr = csv_reader_create(buf, buf_len);

  struct string str;
  if (csv_read_string(rdr, &str) != E_OK) goto read_error;
  if (string_cmp(str, string_new("stationID")) != 0) goto header_error;
  if (csv_read_string(rdr, &str) != E_OK) goto read_error;
  if (string_cmp(str, string_new("security")) != 0) goto header_error;
  if (csv_read_string(rdr, &str) != E_OK) goto read_error;
  if (string_cmp(str, string_new("stationTypeID")) != 0) goto header_error;
  if (csv_read_string(rdr, &str) != E_OK) goto read_error;
  if (string_cmp(str, string_new("corporationID")) != 0) goto header_error;
  if (csv_read_string(rdr, &str) != E_OK) goto read_error;
  if (string_cmp(str, string_new("solarSystemID")) != 0) goto header_error;
  if (csv_read_string(rdr, &str) != E_OK) goto read_error;
  if (string_cmp(str, string_new("stationName")) != 0) goto header_error;

  if (csv_line_end(rdr) != E_OK) goto read_error;
  return E_OK;

header_error:
  errmsg_fmt("stations.csv header does not match");
  return E_ERR;

read_error:
  errmsg_prefix("csv_read_string: ");
  return E_ERR;
}

// WARN: returned name is readonly
// WARN: on return, loc->name_index is still undefined
err_t loc_csv_read(struct csv_reader *rdr, struct loc_loc *loc,
                   struct string *name) {
  assert(loc != NULL);
  assert(rdr != NULL);

  intmax_t id, type_id, corp_id, system_id;
  if (csv_read_intmax(rdr, &id) != E_OK) goto read_error;
  if (csv_read_float32(rdr, &loc->security) != E_OK) goto read_error;
  if (csv_read_intmax(rdr, &type_id) != E_OK) goto read_error;
  if (csv_read_intmax(rdr, &corp_id) != E_OK) goto read_error;
  if (csv_read_intmax(rdr, &system_id) != E_OK) goto read_error;
  if (csv_read_string(rdr, name) != E_OK) goto read_error;

  if (id < 0 || type_id < 0 || corp_id < 0 || system_id < 0 ||
      id > UINT64_MAX || type_id > UINT64_MAX || corp_id > UINT64_MAX ||
      system_id > UINT64_MAX) {
    errmsg_fmt("location values out of range");
    return E_ERR;
  }
  loc->id = (uint64_t) id;
  loc->type_id = (uint64_t) type_id;
  loc->corp_id = (uint64_t) corp_id;
  loc->system_id = (uint64_t) system_id;

  err_t err = csv_line_end(rdr);
  if (err == E_CSV_EOF) {
    return E_CSV_EOF;
  } else if (err != E_OK) {
    errmsg_prefix("csv_line_end: ");
    return E_ERR;
  }

  return E_OK;

read_error:
  errmsg_prefix("csv_read_int64/float32/string: ");
  return E_ERR;
}

void loc_loc_print(const struct loc_loc *loc,
                   const struct loc_name_collec *name_collec) {
  assert(loc != NULL);
  assert(name_collec != NULL);
  struct string name = loc_name_collec_get(name_collec, loc->name_index);

  printf("{\n"
         "\t.id = %llu\n"
         "\t.type_id = %llu\n"
         "\t.corp_id = %llu\n"
         "\t.system_id = %llu\n"
         "\t.security = %f\n"
         "\t.name = %.*s\n"
         "}\n",
         loc->id,
         loc->type_id,
         loc->corp_id,
         loc->system_id,
         loc->security,
         (int) name.len, name.buf);
}
