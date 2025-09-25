// each hoardling is responsible for regularly emitting one type of dump
// each hoardling run on a separate pthread

#include "stations.csv.h"

void *hoardling_locations(void *args) {
  err_t res = E_ERR;
  struct system_vec sys_vec = {};
  struct loc_collec loc_collec = {};

  err_t err = system_vec_load(&sys_vec);
  if (err != E_OK) {
    errmsg_prefix("system_vec_load: ");
    goto cleanup;
  }
  err = loc_collec_create(&loc_collec);
  if (err != E_OK) {
    errmsg_prefix("loc_collec_create: ");
    goto cleanup;
  }

  struct csv_reader rdr;
  err = loc_csv_init((char *) stations_csv, stations_csv_len, &rdr);
  if (err != E_OK) {
    errmsg_prefix("loc_csv_init: ");
    goto cleanup;
  }

  bool eof = false;
  while (!eof) {
    struct loc loc;
    err = loc_csv_read(&rdr, &loc);
    if (err == E_CSV_EOF) {
      eof = true;
    } else if (err != E_OK) {
      errmsg_prefix("loc_csv_read: ");
      goto cleanup;
    }
    err = loc_collec_push(&loc_collec, &loc);
    if (err != E_OK) {
      errmsg_prefix("loc_collec_push: ");
      goto cleanup;
    }
    loc = (struct loc) {};
  }

  /* {
    struct loc loc;
    err = loc_fetch_location_info(&loc, &sys_vec, 1041052520530ULL);
    if (err != E_OK) {
      errmsg_prefix("loc_fetch_location_info: ");
      goto cleanup;
    }
    loc_collec_push(&loc_collec, &loc);
    loc = (struct loc) {};
  } */

  {
    struct loc loc = loc_collec_get(&loc_collec, loc_collec.lv.len - 1);
    loc_print(&loc);
    loc = (struct loc) {};
  }

  struct dump dump;
  err = dump_open(&dump, string_new("loc.dump"), DUMP_TYPE_LOCATIONS, time(NULL));
  if (err != E_OK) {
    errmsg_prefix("dump_open: ");
    goto cleanup;
  }
  err = dump_write_loc_collec(&dump, &loc_collec);
  if (err != E_OK) {
    errmsg_prefix("dump_write_loc_collec: ");
    goto cleanup;
  }
  err = dump_close(&dump);
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
  system_vec_destroy(&sys_vec);
  loc_collec_destroy(&loc_collec);
  return NULL;
}

void *hoardling_orders(void *args) {
  err_t res = E_ERR;
  struct order_vec order_vec = {};

  err_t err = order_vec_create(&order_vec, 2048);
  if (err != E_OK) {
    errmsg_prefix("order_vec_create: ");
    goto cleanup;
  }

  err = order_download_universe(&order_vec);
  if (err != E_OK) {
    errmsg_prefix("order_fetch_page: ");
    goto cleanup;
  }

  order_print(order_vec.buf);

  struct dump dump;
  err = dump_open(&dump, string_new("order.dump"), DUMP_TYPE_ORDERS, time(NULL));
  if (err != E_OK) {
    errmsg_prefix("dump_open: ");
    goto cleanup;
  }
  err = dump_write_order_table(&dump, order_vec.buf, order_vec.len);
  if (err != E_OK) {
    errmsg_prefix("dump_write_order_table: ");
    goto cleanup;
  }
  err = dump_close(&dump);
  if (err != E_OK) {
    errmsg_prefix("dump_close: ");
    goto cleanup;
  }

  res = E_OK;

cleanup:
  log_warn("hoardling_orders quitting");
  if (res != E_OK) {
    errmsg_prefix("hoardling_orders: ");
    errmsg_print();
  }
  order_vec_destroy(&order_vec);
  return NULL;
}

void *hoardling_histories(void *args) {
  err_t res = E_ERR;
  struct history history = {};

  err_t err = history_download(&history, 10000002, 45, NULL, NULL);
  if (err != E_OK) {
    errmsg_prefix("histroy_download: ");
    goto cleanup;
  }
  history_print(&history);
  res = E_OK;

cleanup:
  log_warn("hoardling_histories quitting");
  if (res != E_OK) {
    errmsg_prefix("hoardling_histories: ");
    errmsg_print();
  }
  history_destroy(&history);
  return NULL;
}
