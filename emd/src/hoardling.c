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
  err = dump_open(&dump, string_new("loc.dump"), DUMP_TYPE_LOCATIONS, time(NULL), time(NULL));
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
  time_t hoardling_expiration = 0;

  err_t err = order_vec_create(&order_vec, 2048);
  if (err != E_OK) {
    errmsg_prefix("order_vec_create: ");
    goto cleanup;
  }

  while (true) {
    if (time(NULL) < hoardling_expiration) {
      sleep(hoardling_expiration - time(NULL));
      continue;
    }
    order_vec.len = 0;

    time_t expiration;
    time_t snapshot;
    err = order_download_universe(&order_vec, &snapshot, &expiration);
    if (err != E_OK) {
      errmsg_prefix("order_fetch_page: ");
      goto cleanup;
    }

    // TODO: add a dump root param
    const size_t DUMP_PATH_LEN_MAX = 128;
    char dump_path_buf[DUMP_PATH_LEN_MAX];
    struct string dump_path = string_fmt(dump_path_buf, DUMP_PATH_LEN_MAX,
                                         "./orders-%llu.dump",
                                         (uint64_t) snapshot);

    struct dump dump;
    err = dump_open(&dump, dump_path, DUMP_TYPE_ORDERS, snapshot, expiration);
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
  struct history_day_vec day_vec = {};
  
  err_t err = history_day_vec_create(&day_vec, 512);
  if (err != E_OK) {
    errmsg_prefix("history_day_vec_create: ");
    goto cleanup;
  }

  uint64_t ids[] = { 601, 602, 603, 605, 606, 607, 608, 609, 615 };
  size_t ids_count = sizeof(ids) / sizeof(*ids);

  for (size_t i = 0; i < ids_count; ++i) {
    struct history_market market = { .region_id = 10000002, .type_id = ids[i] };
    log_print("download %llu", ids[i]);
    err = history_download(&day_vec, market, NULL, NULL);
    if (err != E_OK) {
      errmsg_prefix("histroy_download: ");
      goto cleanup;
    }
  }

  history_day_print(day_vec.buf + day_vec.len - 1);

  struct dump dump;
  err = dump_open(&dump, string_new("day.dump"), DUMP_TYPE_HISTORIES, time(NULL), time(NULL));
  if (err != E_OK) {
    errmsg_prefix("dump_open: ");
    goto cleanup;
  }
  err = dump_write_history_day(&dump, day_vec.buf + day_vec.len - 1);
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
  log_warn("hoardling_histories quitting");
  if (res != E_OK) {
    errmsg_prefix("hoardling_histories: ");
    errmsg_print();
  }
  for (size_t i = 0; i < day_vec.len; ++i) {
    history_day_destroy(day_vec.buf + i);
  }
  history_day_vec_destroy(&day_vec);
  return NULL;
}
