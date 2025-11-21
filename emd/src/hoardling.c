// Each hoardling is responsible for regularly emitting one type of dump
// Each hoardling run on a separate pthread
// WARN: A hoardling thread can receive a SIGTERM at any point
// If you need a more delicate exit mechanism condider using pthread_cancel

struct hoardling_locations_args {
  struct string dump_dir;
  struct ptr_fifo *chan_orders_to_locations;
};

void *hoardling_locations(void *args_ptr) {
  assert(args_ptr != NULL);
  struct hoardling_locations_args args = *(struct hoardling_locations_args *) args_ptr;

  // in order not to trigger an esi error timeout, I have to record every
  // location info request that respond with an E_ESI_ERR to add the 
  // corresponding location id to forbidden_locs
  struct uint64_vec forbidden_locs = { .cap = 64 };

  struct system_vec sys_vec = {0};
  struct loc_vec loc_vec = {0};

  err_t err = system_vec_load(&sys_vec);
  if (err != E_OK) {
    errmsg_prefix("system_vec_load: ");
    goto cleanup;
  }
  err = loc_vec_load(&loc_vec);
  if (err != E_OK) {
    errmsg_prefix("loc_vec_load: ");
    goto cleanup;
  }

  while (true) {
    void *locid_vec_ptr;
    err = ptr_fifo_pop(args.chan_orders_to_locations, &locid_vec_ptr, 0);
    if (err != E_OK) {
      errmsg_prefix("ptr_fifo_pop: ");
      goto cleanup;
    }
    assert(locid_vec_ptr != NULL);
    struct uint64_vec *locid_vec = locid_vec_ptr;

    bool new_loc_info = false;
    for (size_t i = 0; i < locid_vec->len; ++i) {
      uint64_t loc_id = locid_vec->buf[i];

      if (!loc_vec_includes(&loc_vec, loc_id) && !forbidden_locs_includes(&forbidden_locs, loc_id)) {
        struct loc loc = {0};
        err = loc_fetch_location_info(&loc, &sys_vec, loc_id);

        if (err == E_LOC_FORBIDDEN) {
          err = uint64_vec_push(&forbidden_locs, loc_id);
          if (err != E_OK) {
            errmsg_prefix("uint64_vec_push: ");
            goto cleanup;
          }
        } else if (err != E_OK) {
          errmsg_prefix("loc_fetch_location_info: ");
          log_error("locations hoardling: unable to fetch %" PRIu64 " location info", loc_id);
          errmsg_print();
        } else if (err == E_OK) {
          new_loc_info = true;
          err = loc_vec_push(&loc_vec, loc);  // ownership loc is passed to loc_vec
          if (err != E_OK) {
            errmsg_prefix("loc_vec_push: ");
            goto cleanup;
          }
          loc = (struct loc) {0};
        }
      }
    }

    uint64_vec_destroy(locid_vec);

    if (new_loc_info) {
      time_t now = time(NULL);
      const size_t DUMP_PATH_LEN_MAX = 2048;
      char dump_path_buf[DUMP_PATH_LEN_MAX];
      struct string dump_path = string_fmt(dump_path_buf, DUMP_PATH_LEN_MAX,
                                           "%.*s/loc-%" PRIu64 ".dump",
                                           (int) args.dump_dir.len,
                                           args.dump_dir.buf, now);

      struct dump dump;
      err = dump_open(&dump, dump_path, DUMP_TYPE_LOCATIONS, time(NULL));
      if (err != E_OK) {
        errmsg_prefix("dump_open: ");
        log_error("locations hoardling: unable to emit location dump");
        errmsg_print();
        continue;
      }
      err = dump_write_loc_vec(&dump, &loc_vec);
      if (err != E_OK) {
        errmsg_prefix("dump_write_loc_vec: ");
        log_error("locations hoardling: unable to emit location dump");
        errmsg_print();
        continue;
      }
      err = dump_close(&dump);
      if (err != E_OK) {
        errmsg_prefix("dump_close: ");
        log_error("locations hoardling: unable to emit location dump");
        errmsg_print();
        continue;
      }
      log_error("locations hoardling: new location dump at %.*s",
                (int) dump_path.len, dump_path.buf);
    }
  }

cleanup:
  log_warn("locations hoardling quitting");
  errmsg_prefix("hoardling_locations: ");
  errmsg_print();

  exit(1);
  return NULL;
}

struct hoardling_orders_args {
  struct string dump_dir;
  struct ptr_fifo *chan_orders_to_locations;
};

void *hoardling_orders(void *args_ptr) {
  assert(args_ptr != NULL);
  struct hoardling_orders_args args = *(struct hoardling_orders_args *) args_ptr;

  struct order_vec order_vec = {0};
  time_t expiration = 0;

  err_t err = order_vec_create(&order_vec, 2048);
  if (err != E_OK) {
    errmsg_prefix("order_vec_create: ");
    goto cleanup;
  }

  while (true) {
    time_t now = time(NULL);
    if (now < expiration) {
      log_print("orders hoardling: up to date");
      sleep(expiration - now);
      continue;
    }

    log_print("orders hoardling: downloading orders and locations");
    order_vec.len = 0;

    err = order_download_universe(&order_vec, global_regions, global_regions_len);
    if (err != E_OK) {
      errmsg_prefix("order hoardling error: order_download_universe: ");
      log_print("orders hoardling: 2 minutes backoff");
      sleep(2 * 60);
      continue;
    }

    const size_t DUMP_PATH_LEN_MAX = 2048;
    char dump_path_buf[DUMP_PATH_LEN_MAX];
    struct string dump_path = string_fmt(dump_path_buf, DUMP_PATH_LEN_MAX,
                                         "%.*s/orders-%" PRIu64 ".dump",
                                         (int) args.dump_dir.len,
                                         args.dump_dir.buf, now);

    struct dump dump;
    err = dump_open(&dump, dump_path, DUMP_TYPE_ORDERS, now + 60 * 5);
    if (err != E_OK) {
      errmsg_prefix("dump_open: ");
      log_error("orders hoardling: unable to emit order dump");
      errmsg_print();
      continue;
    }
    err = dump_write_order_table(&dump, order_vec.buf, order_vec.len);
    if (err != E_OK) {
      errmsg_prefix("dump_write_order_table: ");
      log_error("orders hoardling: unable to emit order dump");
      errmsg_print();
      continue;
    }
    err = dump_close(&dump);
    if (err != E_OK) {
      errmsg_prefix("dump_close: ");
      log_error("orders hoardling: unable to emit order dump");
      errmsg_print();
      continue;
    }
    log_error("orders hoardling: new order dump at %.*s", (int) dump_path.len,
              dump_path.buf);

    struct uint64_vec locid_vec;
    err = order_create_location_id_vec(&locid_vec, &order_vec);
    if (err != E_OK) {
      errmsg_prefix("order_create_location_id_vec: ");
      errmsg_print();
      uint64_vec_destroy(&locid_vec);
    } else {
      // on success, pass ownership of locid_vec to chan_orders_to_locations
      err = ptr_fifo_push(args.chan_orders_to_locations, &locid_vec, 15);
      if (err != E_OK) {
        errmsg_prefix("ptr_fifo_push: ");
        errmsg_print();
        uint64_vec_destroy(&locid_vec);
      }
    }

    expiration = now + 60 * 5;
  }

cleanup:
  log_warn("orders hoardling quitting");
  errmsg_prefix("hoardling_orders: ");
  errmsg_print();

  exit(1);
  return NULL;
}

void *hoardling_histories(void *args) {
  struct history_day_vec day_vec = {0};
  
  err_t err = history_day_vec_create(&day_vec, 512);
  if (err != E_OK) {
    errmsg_prefix("history_day_vec_create: ");
    goto cleanup;
  }

  // TODO: request active_markets instead
  uint64_t ids[] = { 601, 602, 603, 605, 606, 607, 608, 609, 615 };
  size_t ids_count = sizeof(ids) / sizeof(*ids);

  for (size_t i = 0; i < ids_count; ++i) {
    struct history_market market = { .region_id = 10000002, .type_id = ids[i] };
    // TODO: remove log
    log_print("download %" PRIu64, ids[i]);
    err = history_download(&day_vec, market, NULL, NULL);
    if (err != E_OK) {
      errmsg_prefix("histroy_download: ");
      goto cleanup;
    }
  }

  history_day_print(day_vec.buf + day_vec.len - 1);

  struct dump dump;
  err = dump_open(&dump, string_new("day.dump"), DUMP_TYPE_HISTORIES, time(NULL));
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

cleanup:
  log_warn("histories hoardling quitting");
  errmsg_prefix("hoardling_histories: ");
  errmsg_print();

  exit(1);
  return NULL;
}
