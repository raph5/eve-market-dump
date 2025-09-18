#include "systems.csv.h"

struct system {
  uint64_t id;
  float security;
};

IMPLEMENT_VEC(struct system, system);

err_t system_load_from_csv(struct system_vec *system_vec) {
  assert(system_vec != NULL);
  struct csv_reader rdr = csv_reader_create((char *) systems_csv, systems_csv_len);

  struct string str;
  if (csv_read_string(&rdr, &str) != E_OK) {
    errmsg_fmt("csv_read_string: ");
    return E_ERR;
  }
  if (string_cmp(str, string_new("solarSystemID")) != 0) {
    errmsg_fmt("systems.csv header do not match");
    return E_ERR;
  }
  if (csv_read_string(&rdr, &str) != E_OK) {
    errmsg_fmt("csv_read_string: ");
    return E_ERR;
  }
  if (string_cmp(str, string_new("security")) != 0) {
    errmsg_fmt("systems.csv header do not match");
    return E_ERR;
  }

  if (csv_line_end(&rdr) != E_OK) {
    errmsg_prefix("csv_line_end: ");
    return E_ERR;
  }

  bool eof = false;
  while (!eof) {
    intmax_t id;
    if (csv_read_intmax(&rdr, &id) != E_OK) {
      errmsg_prefix("csv_read_intmax: ");
      return E_ERR;
    }
    if (id < 0 || id > UINT64_MAX) {
      errmsg_fmt("id out of range");
      return E_ERR;
    }

    float security;
    if (csv_read_float32(&rdr, &security) != E_OK) {
      errmsg_prefix("csv_read_float32: ");
      return E_ERR;
    }

    struct system system = { .id = id, .security = security};
    err_t err = system_vec_push(system_vec, system);
    if (err != E_OK) {
      errmsg_prefix("system_vec_push: ");
      return E_ERR;
    }
  }

  return E_OK;
}

// return a security of 0 if system is not found within system_vec
float system_get_security(struct system_vec *system_vec, uint64_t system_id) {
  assert(system_vec != NULL);
  for (size_t i = 0; i < system_vec->len; ++i) {
    if (system_vec->buf[i].id == system_id) {
      return system_vec->buf[i].security;
    }
  }
  return 0;
}
