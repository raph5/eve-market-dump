#include "stations.csv.h"

void *hoardling_locations(void *args) {
  err_t res = E_ERR;

  struct loc_name_collec name_collec;
  struct loc_vec         loc_vec;  

  err_t err = loc_name_collec_create(&name_collec);
  if (err != E_OK) {
    errmsg_prefix("loc_name_collec_create: ");
    goto cleanup;
  }
  err = loc_vec_create(&loc_vec, 1024);
  if (err != E_OK) {
    errmsg_prefix("loc_vec_create: ");
    goto cleanup;
  }

  struct csv_reader rdr;
  err = loc_csv_init((char *) stations, stations_len, &rdr);
  if (err != E_OK) {
    errmsg_prefix("loc_csv_init: ");
    goto cleanup;
  }

  bool eof = false;
  while (!eof) {
    struct loc_loc loc;
    struct string  name;
    err = loc_csv_read(&rdr, &loc, &name);
    if (err == E_CSV_EOF) {
      eof = true;
    } else if (err != E_OK) {
      errmsg_prefix("loc_csv_read: ");
      goto cleanup;
    }

    err = loc_name_collec_push(&name_collec, name, &loc.name_index);
    if (err != E_OK) {
      errmsg_prefix("loc_name_collec_push: ");
      goto cleanup;
    }

    err = loc_vec_push(&loc_vec, loc);
    if (err != E_OK) {
      errmsg_prefix("loc_vec_push: ");
      goto cleanup;
    }
  }

  loc_loc_print(loc_vec.buf, &name_collec);

  uint64_t date = (uint64_t) time(NULL);
  struct dump loc_dump;
  err = dump_open(&loc_dump, string_new("./loc.dump"), date);
  if (err != E_OK) {
    errmsg_prefix("dump_open: ");
    goto cleanup;
  }
  err = dump_write_loc_table(&loc_dump, &name_collec, loc_vec.buf, loc_vec.len);
  if (err != E_OK) {
    errmsg_prefix("dump_write_loc_table: ");
    goto cleanup;
  }
  err = dump_close(&loc_dump);
  if (err != E_OK) {
    errmsg_prefix("dump_close: ");
    goto cleanup;
  }

  res = E_OK;

cleanup:
  log_warn("hoardling_locations quitting");
  if (res != E_OK) {
    errmsg_prefix("hoardling_locations: ");
    errmsg_print();
  }
  loc_name_collec_destroy(&name_collec);
  loc_vec_destroy(&loc_vec);
  return NULL;
}
