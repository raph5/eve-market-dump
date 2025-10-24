
const err_t E_LOC_BASE = 3000;
const err_t E_LOC_FORBIDDEN = E_LOC_BASE + 1;

// NOTE: returned locations name should be zeroed after use to avoid dangling
// pointers
struct loc {
  uint64_t      id;
  uint64_t      type_id;  // station type id
  uint64_t      owner_id;  // station corporation id
  uint64_t      system_id;
  float         security;
  struct string name;
};

struct _loc {
  uint64_t id;
  uint64_t type_id;  // station type id
  uint64_t owner_id;  // station corporation id
  uint64_t system_id;
  float    security;
  size_t   name_index;
};

IMPLEMENT_VEC(struct _loc, _loc)

// NOTE: I think using a string_pool there was a bit overkill
struct loc_collec {
  struct string_pool sp;
  struct _loc_vec    lv;
};

err_t loc_collec_create(struct loc_collec *collec) {
  assert(collec != NULL);
  err_t err = string_pool_create(&collec->sp, 2048);
  if (err != E_OK) {
    errmsg_prefix("string_pool_create: ");
    return E_ERR;
  }
  err = _loc_vec_create(&collec->lv, 128);
  if (err != E_OK) {
    errmsg_prefix("_loc_vec: ");
    string_pool_destroy(&collec->sp);
    return E_ERR;
  }
  return E_OK;
}

void loc_collec_destroy(struct loc_collec *collec) {
  assert(collec != NULL);
  string_pool_destroy(&collec->sp);
  _loc_vec_destroy(&collec->lv);
}

err_t loc_collec_push(struct loc_collec *collec, struct loc *loc) {
  assert(collec != NULL);
  assert(loc != NULL);
  size_t name_index;
  err_t err = string_pool_push(&collec->sp, loc->name, &name_index);
  if (err != E_OK) {
    errmsg_prefix("string_pool_push: ");
    return E_ERR;
  }
  struct _loc _loc = {
    .id = loc->id,
    .type_id = loc->type_id,
    .owner_id = loc->owner_id,
    .system_id = loc->system_id,
    .security = loc->security,
    .name_index = name_index,
  };
  err = _loc_vec_push(&collec->lv, _loc);
  if (err != E_OK) {
    errmsg_prefix("_loc_vec_push: ");
    return E_ERR;
  }
  return E_OK;
}

// WARN: the returned `struct loc` must be zeroed after use to avoid dangling
// pointers
struct loc loc_collec_get(struct loc_collec *collec, size_t i) {
  assert(collec != NULL);
  if (i >= collec->lv.len) {
    panic("loc_collec_get index out of bounds");
  }
  return (struct loc) {
    .id = collec->lv.buf[i].id,
    .type_id = collec->lv.buf[i].type_id,
    .owner_id = collec->lv.buf[i].owner_id,
    .system_id = collec->lv.buf[i].system_id,
    .security = collec->lv.buf[i].security,
    .name = string_pool_get(&collec->sp, i),
  };
}

size_t loc_collec_len(struct loc_collec *collec) {
  assert(collec != NULL);
  return collec->lv.len;
}

bool loc_collec_includes(struct loc_collec *collec, uint64_t locid) {
  assert(collec != NULL);
  for (size_t i = 0; i < collec->lv.len; ++i) {
    if (collec->lv.buf[i].id == locid) return true;
  }
  return false;
}

void loc_print(const struct loc *loc) {
  assert(loc != NULL);

  printf("{\n"
         "\t.id = %" PRIu64 "\n"
         "\t.type_id = %" PRIu64 "\n"
         "\t.owner_id = %" PRIu64 "\n"
         "\t.system_id = %" PRIu64 "\n"
         "\t.security = %f\n"
         "\t.name = %.*s\n"
         "}\n",
         loc->id,
         loc->type_id,
         loc->owner_id,
         loc->system_id,
         loc->security,
         (int) loc->name.len, loc->name.buf);
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

// WARN: returned loc->name is readonly
err_t loc_csv_read(struct csv_reader *rdr, struct loc *loc) {
  assert(loc != NULL);
  assert(rdr != NULL);

  intmax_t id, type_id, owner_id, system_id;
  if (csv_read_intmax(rdr, &id) != E_OK) goto read_error;
  if (csv_read_float32(rdr, &loc->security) != E_OK) goto read_error;
  if (csv_read_intmax(rdr, &type_id) != E_OK) goto read_error;
  if (csv_read_intmax(rdr, &owner_id) != E_OK) goto read_error;
  if (csv_read_intmax(rdr, &system_id) != E_OK) goto read_error;
  if (csv_read_string(rdr, &loc->name) != E_OK) goto read_error;

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

// this function does not set the `id` and `security` field of loc
// WARN: upon successful retrun, name need to be free-ed
err_t loc_parse_location_info(struct loc *loc, struct string loc_data) {
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
  json_t *json_system_id = json_object_get(root, "solar_system_id");
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
  *loc = (struct loc) {
    .type_id = type_id,
    .owner_id = owner_id,
    .system_id = system_id,
    .name = string_alloc_cpy(string_new((char *) json_string_value(json_name))),
  };

cleanup:
  json_decref(root);
  return res;
}

// WARN: loc->name is read only
err_t loc_fetch_location_info(struct loc *loc, struct system_vec *sys_vec,
                              uint64_t id) {
  assert(loc != NULL);
  assert(sys_vec != NULL);

  if (loc_forbidden_locs_check(id)) {
    return E_LOC_FORBIDDEN;
  }

  err_t res = E_ERR;
  struct esi_response response = {0};

  const size_t URI_LEN_MAX = 2048;
  char uri_buf[URI_LEN_MAX];
  struct string uri = string_fmt(uri_buf, URI_LEN_MAX,
                                 "/universe/structures/%" PRIu64, id);
  err_t err = esi_fetch(&response, string_new("GET"), uri, (struct string) {0},
                        true, 1);

  if (err == E_ESI_ERR) {
    err = loc_forbidden_locs_add(id);
    if (err != E_OK) {
      errmsg_prefix("loc_forbidden_locs_add: ");
      goto cleanup;
    }
    res = E_LOC_FORBIDDEN;
    goto cleanup;
  } else if (err != E_OK) {
    errmsg_prefix("esi_fetch: ");
    goto cleanup;
  }

  err = loc_parse_location_info(loc, response.body);
  if (err != E_OK) {
    errmsg_prefix("loc_parse_location_info: ");
    goto cleanup;
  }
  loc->security = system_vec_get_security(sys_vec, loc->system_id);
  loc->id = id;
  res = E_OK;

cleanup:
  esi_response_destroy(&response);
  return res;
}

err_t dump_write_loc(struct dump *dump, struct loc *loc) {
  assert(loc != NULL);
  if (dump_write_uint64(dump, loc->id) != E_OK) goto error;
  if (dump_write_uint64(dump, loc->type_id) != E_OK) goto error;
  if (dump_write_uint64(dump, loc->owner_id) != E_OK) goto error;
  if (dump_write_uint64(dump, loc->system_id) != E_OK) goto error;
  if (dump_write_float32(dump, loc->security) != E_OK) goto error;
  if (dump_write_string(dump, loc->name) != E_OK) goto error;
  return E_OK;

error:
  errmsg_prefix("dump_write_uint64/float32/string: ");
  return E_ERR;
}

err_t dump_write_loc_collec(struct dump *dump, struct loc_collec *collec) {
  size_t loc_count = loc_collec_len(collec);
  if (dump_write_uint64(dump, loc_count) != E_OK) {
    errmsg_prefix("dump_write_uint64: ");
    return E_ERR;
  }
  for (size_t i = 0; i < loc_count; ++i) {
    struct loc loc = loc_collec_get(collec, i);
    if (dump_write_loc(dump, &loc) != E_OK) {
      errmsg_prefix("dump_write_loc :");
      return E_ERR;
    }
    loc = (struct loc) {0};
  }
  return E_OK;
}
