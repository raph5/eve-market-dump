
const err_t E_LOC_BASE = 3000;
const err_t E_LOC_FOBIDDEN = E_LOC_BASE + 1;

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

// WARN: the retirned string is valid UNTIL the next call to loc_name_collec_push
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
  uint64_t owner_id;  // station corporation id
  uint64_t system_id;
  float    security;
  size_t   name_index;
};

IMPLEMENT_VEC(struct loc_loc, loc);

void loc_loc_print(const struct loc_loc *loc,
                   const struct loc_name_collec *name_collec) {
  assert(loc != NULL);
  assert(name_collec != NULL);
  struct string name = loc_name_collec_get(name_collec, loc->name_index);

  printf("{\n"
         "\t.id = %llu\n"
         "\t.type_id = %llu\n"
         "\t.owner_id = %llu\n"
         "\t.system_id = %llu\n"
         "\t.security = %f\n"
         "\t.name = %.*s\n"
         "}\n",
         loc->id,
         loc->type_id,
         loc->owner_id,
         loc->system_id,
         loc->security,
         (int) name.len, name.buf);
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

  if (csv_line_end(rdr) != E_OK) {
    errmsg_prefix("csv_line_end: ");
    return E_ERR;
  }
  return E_OK;

header_error:
  errmsg_fmt("stations.csv header do not match");
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

  intmax_t id, type_id, owner_id, system_id;
  if (csv_read_intmax(rdr, &id) != E_OK) goto read_error;
  if (csv_read_float32(rdr, &loc->security) != E_OK) goto read_error;
  if (csv_read_intmax(rdr, &type_id) != E_OK) goto read_error;
  if (csv_read_intmax(rdr, &owner_id) != E_OK) goto read_error;
  if (csv_read_intmax(rdr, &system_id) != E_OK) goto read_error;
  if (csv_read_string(rdr, name) != E_OK) goto read_error;

  if (id < 0 || type_id < 0 || owner_id < 0 || system_id < 0 ||
      id > UINT64_MAX || type_id > UINT64_MAX || owner_id > UINT64_MAX ||
      system_id > UINT64_MAX) {
    errmsg_fmt("location values out of range");
    return E_ERR;
  }
  loc->id = (uint64_t) id;
  loc->type_id = (uint64_t) type_id;
  loc->owner_id = (uint64_t) owner_id;
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

// in order not to trigger an esi error timeout, I have to record every
// location info request that respond with an E_ESI_ERR to add the 
// corresponding location id to loc_forbidden_locs
// NOTE: loc_forbidden_locs vector will not be free-ed as it's endended to live
// for the hole program lifetime
struct uint64_vec loc_forbidden_locs = { .cap = 64 };
mutex_t           loc_forbidden_locs_mu = MUTEX_INIT;

err_t loc_forbidden_locs_add(uint64_t id) {
  mutex_lock(&loc_forbidden_locs_mu, 5);
  err_t err = uint64_vec_push(&loc_forbidden_locs, id);
  if (err != E_OK) {
    errmsg_fmt("uint64_vec_push: ");
    mutex_unlock(&loc_forbidden_locs_mu);
    return E_ERR;
  }
  mutex_unlock(&loc_forbidden_locs_mu);
  return E_OK;
}

bool loc_forbidden_locs_check(uint64_t id) {
  mutex_lock(&loc_forbidden_locs_mu, 5);
  for (size_t i = 0; i < loc_forbidden_locs.len; ++i) {
    if (loc_forbidden_locs.buf[i] == id)  {
      mutex_unlock(&loc_forbidden_locs_mu);
      return true;
    }
  }
  mutex_unlock(&loc_forbidden_locs_mu);
  return false;
}

// WARN: this function does not set the `id` and `security` field of loc
// WARN: name is read only
err_t loc_parse_location_info(struct loc_loc *loc, struct string *name,
                              struct string loc_data) {
  err_t res = E_ERR;
  json_error_t json_err;
  json_t *root = json_loadb(loc_data.buf, loc_data.len, 0, &json_err);
  if (root == NULL) {
    errmsg_fmt("json error on line %d: %s", json_err.line, json_err.text);
    goto cleanup;
  }
  if (!json_is_object(root)) {
    errmsg_fmt("json error: root is not an object");
    goto cleanup;
  }

  json_t *json_name = json_object_get(root, "name");
  if (!json_is_string(json_name)) {
    errmsg_fmt("json error: name is not an string");
    goto cleanup;
  }
  json_t *json_owner_id = json_object_get(root, "owner_id");
  if (!json_is_integer(json_owner_id)) {
    errmsg_fmt("json error: owner_id is not an integer");
    goto cleanup;
  }
  json_t *json_system_id = json_object_get(root, "system_id");
  if (!json_is_integer(json_system_id)) {
    errmsg_fmt("json error: system_id is not an integer");
    goto cleanup;
  }
  json_t *json_type_id = json_object_get(root, "type_id");
  if (!json_is_integer(json_type_id)) {
    errmsg_fmt("json error: type_id is not an integer");
    goto cleanup;
  }

  json_int_t owner_id = json_integer_value(json_owner_id);
  if (owner_id < 0 || owner_id > UINT64_MAX) {
    errmsg_fmt("json error: owner_id out of range");
    goto cleanup;
  }
  json_int_t system_id = json_integer_value(json_system_id);
  if (system_id < 0 || system_id > UINT64_MAX) {
    errmsg_fmt("json error: system_id out of range");
    goto cleanup;
  }
  json_int_t type_id = json_integer_value(json_type_id);
  if (type_id < 0 || type_id > UINT64_MAX) {
    errmsg_fmt("json error: type_id out of range");
    goto cleanup;
  }

  res = E_OK;
  *name = string_new((char *) json_string_value(json_name));
  *loc = (struct loc_loc) {
    .type_id = type_id,
    .owner_id = owner_id,
    .system_id = system_id,
  };

cleanup:
  json_decref(root);
  return res;
}

// TODO: test this function
// WARN: name is read only
err_t loc_fetch_location_info(struct loc_loc *loc, struct string *name,
                              struct system_vec *sys_vec, uint64_t id) {
  assert(loc != NULL);
  assert(name != NULL);

  if (loc_forbidden_locs_check(id)) {
    return E_LOC_FOBIDDEN;
  }

  err_t res = E_ERR;
  struct esi_response response;

  const size_t URI_LEN_MAX = 2048;
  char uri_buf[URI_LEN_MAX];
  struct string uri = string_fmt(uri_buf, URI_LEN_MAX,
                                 "/universe/structures/%llu", id);
  err_t err = esi_fetch(&response, string_new("GET"), uri, (struct string) {},
                        true, 1);

  if (err == E_ESI_ERR) {
    err = loc_forbidden_locs_add(id);
    if (err != E_OK) {
      errmsg_prefix("loc_forbidden_locs_add: ");
      goto cleanup;
    }
  } else if (err != E_OK) {
    errmsg_prefix("esi_fetch: ");
    goto cleanup;
  }

  err = loc_parse_location_info(loc, name, response.body);
  if (err != E_OK) {
    errmsg_prefix("loc_parse_location_info: ");
    goto cleanup;
  }
  loc->security = system_get_security(sys_vec, id);
  loc->id = id;
  res = E_OK;

cleanup:
  esi_response_destroy(&response);
  return res;
}

err_t dump_write_loc(struct dump *dump, struct loc_name_collec *nc,
                     struct loc_loc *loc) {
  assert(loc != NULL);
  assert(nc != NULL);

  if (dump_write_uint64(dump, loc->id) != E_OK) goto error;
  if (dump_write_uint64(dump, loc->type_id) != E_OK) goto error;
  if (dump_write_uint64(dump, loc->owner_id) != E_OK) goto error;
  if (dump_write_uint64(dump, loc->system_id) != E_OK) goto error;
  if (dump_write_float32(dump, loc->security) != E_OK) goto error;
  struct string name = loc_name_collec_get(nc, loc->name_index);
  if (dump_write_string(dump, name) != E_OK) goto error;
  return E_OK;

error:
  errmsg_prefix("dump_write_uint64/float32/string");
  return E_ERR;
}

err_t dump_write_loc_table(struct dump *dump, struct loc_name_collec *nc,
                           struct loc_loc *loc, size_t loc_len) {
  if (dump_write_uint64(dump, loc_len) != E_OK) {
    errmsg_prefix("dump_write_uint64: ");
    return E_ERR;
  }

  for (size_t i = 0; i < loc_len; ++i) {
    if (dump_write_loc(dump, nc, loc + i) != E_OK) {
      errmsg_prefix("dump_write_loc :");
      return E_ERR;
    }
  }

  return E_OK;
}
