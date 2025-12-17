// Each hoardling is responsible for regularly emitting one type of dump
// Each hoardling run on a separate pthread
// WARN: A hoardling thread can receive a SIGTERM at any point
// If you need a more delicate exit mechanism condider using pthread_cancel

struct hoardling_locations_args {
  struct string dump_dir;
  struct ptr_fifo *chan_orders_to_locations;
};

err_t hoardling_locations_dump(struct string dump_dir, struct loc_vec *loc_vec, time_t now) {
  char dump_path_buf[DUMP_PATH_LEN_MAX];
  struct string dump_path = string_fmt(dump_path_buf, DUMP_PATH_LEN_MAX, "%.*s/loc-%" PRIu64 ".dump",
                                       (int) dump_dir.len, dump_dir.buf, now);

  struct dump dump;
  err_t err = dump_open_write(&dump, dump_path, DUMP_TYPE_LOCATIONS, 0);
  if (err != E_OK) {
    errmsg_prefix("dump_open_write: ");
    return E_ERR;
  }
  err = dump_write_loc_vec(&dump, loc_vec);
  if (err != E_OK) {
    errmsg_prefix("dump_write_loc_vec: ");
    return E_ERR;
  }
  err = dump_close_write(&dump);
  if (err != E_OK) {
    errmsg_prefix("dump_close_write: ");
    return E_ERR;
  }

  return E_OK;
}

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
    struct uint64_vec *locid_vec;
    err = ptr_fifo_pop(args.chan_orders_to_locations, (void **) &locid_vec, 0);
    if (err != E_OK) {
      errmsg_prefix("ptr_fifo_pop: ");
      goto cleanup;
    }
    assert(locid_vec != NULL);

    bool new_loc_info = false;
    for (size_t i = 0; i < locid_vec->len; ++i) {
      uint64_t loc_id = locid_vec->buf[i];

      if (!loc_vec_includes(&loc_vec, loc_id) && !forbidden_locs_includes(&forbidden_locs, loc_id)) {
        struct loc loc = {0};
        err = loc_fetch_location_info(&loc, &sys_vec, loc_id);

        if (err == E_OK) {
          new_loc_info = true;
          err = loc_vec_push(&loc_vec, loc);  // ownership loc is passed to loc_vec
          if (err != E_OK) {
            errmsg_prefix("loc_vec_push: ");
            goto cleanup;
          }
          loc = (struct loc) {0};
        } else if (err == E_LOC_FORBIDDEN) {
          err = uint64_vec_push(&forbidden_locs, loc_id);
          if (err != E_OK) {
            errmsg_prefix("uint64_vec_push: ");
            goto cleanup;
          }
        } else {
          log_error("locations hoardling: unable to fetch %" PRIu64 " location info", loc_id);
          errmsg_prefix("loc_fetch_location_info: ");
          errmsg_print();
        }
      }
    }

    uint64_vec_destroy(locid_vec);
    free(locid_vec);

    if (new_loc_info) {
      time_t now = time(NULL);
      err = hoardling_locations_dump(args.dump_dir ,&loc_vec, now);
      if (err != E_OK) {
        log_error("locations hoardling: unable to emit location dump");
        errmsg_prefix("hoardling_locations_dump: ");
        errmsg_print();
      } else {
        log_print("locations hoardling: new location dump");
      }
    }
  }

cleanup:
  log_warn("locations hoardling quitting");
  errmsg_prefix("hoardling_locations: ");
  errmsg_print();

  kill(getpid(), SIGTERM);
  return NULL;
}

struct hoardling_orders_args {
  struct string dump_dir;
  bool history;
  bool structure;
  struct ptr_fifo *chan_orders_to_locations;
  struct ptr_fifo *active_market_request;
  struct ptr_fifo *active_market_response;
};

err_t hoardling_orders_dump(struct string dump_dir, struct order_vec *order_vec, time_t now) {
  char dump_path_buf[DUMP_PATH_LEN_MAX];
  struct string dump_path = string_fmt(dump_path_buf, DUMP_PATH_LEN_MAX, "%.*s/orders-%" PRIu64 ".dump",
                                       (int) dump_dir.len, dump_dir.buf, now);

  struct dump dump;
  err_t err = dump_open_write(&dump, dump_path, DUMP_TYPE_ORDERS, now + 60 * 5);
  if (err != E_OK) {
    errmsg_prefix("dump_open_write: ");
    return E_ERR;
  }
  err = dump_write_order_table(&dump, order_vec->buf, order_vec->len);
  if (err != E_OK) {
    errmsg_prefix("dump_write_order_table: ");
    return E_ERR;
  }
  err = dump_close_write(&dump);
  if (err != E_OK) {
    errmsg_prefix("dump_close_write: ");
    return E_ERR;
  }

  return E_OK;
}

err_t hoardling_orders_send_location_id_vec(struct order_vec *order_vec,
                                            struct ptr_fifo *chan_orders_to_locations) {
  struct uint64_vec *locid_vec = malloc(sizeof(struct uint64_vec));
  if (locid_vec == NULL) {
    errmsg_fmt("malloc: %s", strerror(errno));
    return E_ERR;
  }
  *locid_vec = (struct uint64_vec) { .cap = 2048 };

  err_t err = order_fill_location_id_vec(locid_vec, order_vec);
  if (err != E_OK) {
    uint64_vec_destroy(locid_vec);
    free(locid_vec);
    errmsg_prefix("order_fill_location_id_vec: ");
    return E_ERR;
  }

  // pass ownership of locid_vec to chan_orders_to_locations
  err = ptr_fifo_push(chan_orders_to_locations, locid_vec, 15);
  if (err != E_OK) {
    uint64_vec_destroy(locid_vec);
    free(locid_vec);
    errmsg_prefix("ptr_fifo_push: ");
    return E_ERR;
  }

  return E_OK;
}

err_t hoardling_orders_respond_to_histories_hoardling(struct order_vec *order_vec,
                                                      struct ptr_fifo *active_market_request,
                                                      struct ptr_fifo *active_market_response) {
  struct history_market_vec *market_vec;
  err_t err = ptr_fifo_try_pop(active_market_request, (void **) &market_vec);
  if (err == E_EMPTY) {
    return E_OK;
  } else if (err != E_OK) {
    errmsg_prefix("ptr_fifo_try_pop: ");
    return E_ERR;
  }

  assert(market_vec != NULL);
  err = order_fill_active_market_vec(market_vec, order_vec);
  if (err != E_OK) {
    errmsg_prefix("order_fill_active_market_vec: ");
    return E_ERR;
  }

  err = ptr_fifo_push(active_market_response, (void *) market_vec, 15);
  if (err != E_OK) {
    errmsg_prefix("ptr_fifo_push: ");
    return E_ERR;
  }

  return E_OK;
}

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
      log_print("orders hoardling: 2 minutes backoff");
      errmsg_prefix("order_download_universe: ");
      errmsg_print();
      sleep(2 * 60);
      continue;
    }

    err = hoardling_orders_dump(args.dump_dir, &order_vec, now);
    if (err != E_OK) {
      log_error("orders hoardling: unable to emit order dump");
      errmsg_prefix("hoardling_orders_dump: ");
      errmsg_print();
    } else {
      log_print("orders hoardling: new order dump");
    }

    if (args.structure) {
      err = hoardling_orders_send_location_id_vec(&order_vec, args.chan_orders_to_locations);
      if (err != E_OK) {
        log_error("orders hoardling: unable to locations id to locations hoardling");
        errmsg_prefix("hoardling_orders_send_location_id_vec: ");
        errmsg_print();
      }
    }

    if (args.history) {
      // response to active market requests from histories hoardling
      err = hoardling_orders_respond_to_histories_hoardling(&order_vec,
                                                            args.active_market_request,
                                                            args.active_market_response);
      if (err != E_OK) {
        log_error("orders hoardling: unable to respond to histories hoardling");
        errmsg_prefix("hoardling_orders_send_location_id_vec: ");
        errmsg_print();
      }
    }

    expiration = now + 60 * 5;
  }

cleanup:
  log_warn("orders hoardling quitting");
  errmsg_prefix("hoardling_orders: ");
  errmsg_print();

  kill(getpid(), SIGTERM);
  return NULL;
}

struct hoardling_histories_args {
  struct string dump_dir;
  struct ptr_fifo *active_market_request;
  struct ptr_fifo *active_market_response;
};

bool hoardling_histories_dump_does_exist(struct string dump_dir, struct date date) {
  char dump_path_buf[DUMP_PATH_LEN_MAX];
  struct string last_dump_path = string_fmt(dump_path_buf, DUMP_PATH_LEN_MAX, "%.*s/history-day-%" PRIu16 "-%" PRIu16 ".dump",
                                            (int) dump_dir.len, dump_dir.buf, date.year, date.day);
  return dump_does_exist(last_dump_path);
}

err_t hoardling_histories_dump(struct string dump_dir, struct history_bit_vec *bit_vec, struct date date) {
  char dump_path_buf[DUMP_PATH_LEN_MAX];
  struct string dump_path = string_fmt(dump_path_buf, DUMP_PATH_LEN_MAX, "%.*s/history-day-%" PRIu16 "-%" PRIu16 ".dump",
                                       (int) dump_dir.len, dump_dir.buf, date.year, date.day);
  if (dump_does_exist(dump_path)) {
    errmsg_fmt("there is already a dump at %.*s", dump_path.len, dump_path.buf);
    return E_FULL;
  }

  struct dump dump;
  err_t err = dump_open_write(&dump, dump_path, DUMP_TYPE_HISTORIES, 0);
  if (err != E_OK) {
    errmsg_prefix("dump_open_write: ");
    return E_ERR;
  }
  err = dump_write_history_dump(&dump, date, bit_vec);
  if (err != E_OK) {
    errmsg_prefix("dump_write_order_table: ");
    return E_ERR;
  }
  err = dump_close_write(&dump);
  if (err != E_OK) {
    errmsg_prefix("dump_close_write: ");
    return E_ERR;
  }

  return E_OK;
}

err_t hoardling_histories_request_active_markets(struct history_market_vec *market_vec,
                                                 struct ptr_fifo *active_market_request,
                                                 struct ptr_fifo *active_market_response) {
  err_t err = ptr_fifo_push(active_market_request, (void *) market_vec, 15);
  if (err != E_OK) {
    errmsg_prefix("ptr_fifo_push: ");
    return E_ERR;
  }
  struct history_market_vec *market_vec_ptr;
  err = ptr_fifo_pop(active_market_response, (void **) &market_vec_ptr, 3*TIME_HOUR);
  if (err != E_OK) {
    errmsg_prefix("ptr_fifo_pop: ");
    return E_ERR;
  }
  assert(market_vec == market_vec_ptr);

  return E_OK;
}

void *hoardling_histories(void *args_ptr) {
  err_t err;

  assert(args_ptr != NULL);
  struct hoardling_histories_args args = *(struct hoardling_histories_args *) args_ptr;

  time_t now = time(NULL);
  time_t eleven_fifteen_today = time_eleven_fifteen_today(now);
  time_t eleven_fifteen_tomorrow = time_eleven_fifteen_tomorrow(now);
  time_t expiration = now < eleven_fifteen_today ? eleven_fifteen_today : eleven_fifteen_tomorrow;

  // In some cases this date might be day preceding the last dump but this does
  // not realy mater
  struct date date_last_dump = date_utc(now - 2*TIME_DAY);
  bool is_initial_download_needed = !hoardling_histories_dump_does_exist(args.dump_dir, date_last_dump);

  if (is_initial_download_needed) {
    log_print("histories hoardling: performing initial download");

    log_print("histories hoardling: requesting active markets");
    struct history_market_vec market_vec = { .cap = 2048 };
    err = hoardling_histories_request_active_markets(&market_vec,
                                                     args.active_market_request,
                                                     args.active_market_response);
    if (err != E_OK) {
      errmsg_prefix("hoardling_histories_request_active_markets: ");
      goto cleanup;
    }

    struct dump snapshot_dump;
    struct string snapshot_dump_path = string_new("/tmp/emd_snapshot_dump");
    err = dump_open_write(&snapshot_dump, snapshot_dump_path, DUMP_TYPE_INTERNAL, 0);
    if (err != E_OK) {
      errmsg_prefix("dump_open_write: ");
      goto cleanup;
    }

    struct date first_day = {0};
    struct date last_day = {0};
    struct history_bit_vec bit_vec = { .cap = 512 };
    for (size_t i = 0; i < market_vec.len; ++i) {
      struct history_market market = market_vec.buf[i];
      bit_vec.len = 0;

      int try = 1;
      while (1) {
        err = history_download(&bit_vec, market);
        if (err == E_OK || err == E_NOT_FOUND) {
          break;
        } else if (try <= 6) {
          try += 1;
          errmsg_prefix("history_download: ");
          errmsg_print();
          time_t backoff;
          switch (try) {
            case 1:
            case 2:
            case 3:
              log_error("histories hoardling: history download failed, retrying in 5 minutes");
              backoff = 5 * TIME_MINUTE;
              break;
            case 4:
            case 5:
              log_error("histories hoardling: history download failed, retrying in 30 minutes");
              backoff = 30 * TIME_MINUTE;
              break;
            case 6:
              log_error("histories hoardling: history download failed, retrying in 2 hours");
              backoff = 2 * TIME_HOUR;
              break;
          }
          sleep(backoff);
        } else {
          log_error("histories hoardling: history download failed, out of trails");
          errmsg_prefix("history_download: ");
          goto cleanup;
        }
      }

      if (err == E_NOT_FOUND) {
        // skip it
        continue;
      }

      if (bit_vec.len > 0) {
        struct date history_first_day = bit_vec.buf[0].date;
        struct date history_last_day = bit_vec.buf[bit_vec.len-1].date;
        if (first_day.year == 0 || date_is_before(history_first_day, first_day)) {
          first_day = history_first_day;
        }
        if (last_day.year == 0 || date_is_after(history_last_day, last_day)) {
          last_day = history_last_day;
        }

        err = dump_write_history_bit_vec(&snapshot_dump, &bit_vec);
        if (err != E_OK) {
          errmsg_prefix("dump_write_history_bit_vec: ");
          goto cleanup;
        }
      }
    }

    if (first_day.year == 0 || last_day.year == 0) {
      assert(bit_vec.len == 0);
      errmsg_fmt("bit_vec is empty");
      goto cleanup;
    }

    log_print("histories hoardling: initial download finished");

    err = dump_close_write(&snapshot_dump);
    if (err != E_OK) {
      errmsg_prefix("dump_close_write: ");
      goto cleanup;
    }
    err = dump_open_read(&snapshot_dump, snapshot_dump_path);
    if (err != E_OK) {
      errmsg_prefix("dump_open_read: ");
      goto cleanup;
    }

    struct history_bit_vec bit_chunk = { .cap = 10000 };
    for (struct date date = first_day;
         date_is_before(date, last_day) || date_is_equal(date, last_day);
         date_incr(&date)) {
      bit_vec.len = 0;
      err = dump_seek_start(&snapshot_dump);
      if (err != E_OK) {
        errmsg_prefix("dump_seek_start: ");
        goto cleanup;
      }

      bool eof = false;
      while (!eof) {
        bit_chunk.len = 0;
        err = dump_read_history_bit_vec(&snapshot_dump, &bit_chunk, 10000);
        if (err == E_EOF) {
          eof = true;
        } else if (err != E_OK) {
          errmsg_prefix("dump_read_history_bit_vec: ");
          goto cleanup;
        }

        for (size_t i = 0; i < bit_chunk.len; ++i) {
          if (date_is_equal(date, bit_chunk.buf[i].date)) {
            // log_print("date match: (%" PRIu64 ", %" PRIu64 ")", bit_chunk.buf[i].date.year, bit_chunk.buf[i].date.day);
            err = history_bit_vec_push(&bit_vec, bit_chunk.buf[i]);
            if (err != E_OK) {
              errmsg_prefix("history_bit_vec_push: ");
              goto cleanup;
            }
          }
        }
      }

      err = hoardling_histories_dump(args.dump_dir, &bit_vec, date);
      if (err == E_FULL) {
        log_warn("histories hoardling: unable to emit history dump because there is already a dump at this path");
      } else if (err != E_OK) {
        log_error("histories hoardling: unable to emit history dump");
        errmsg_prefix("hoardling_histories_dump: ");
        errmsg_print();
      } else {
        log_print("histories hoardling: new history dump");
      }
    }

    err = dump_close_read(&snapshot_dump);
    if (err != E_OK) {
      errmsg_prefix("dump_close_read: ");
      goto cleanup;
    }

    history_bit_vec_destroy(&bit_vec);
    history_bit_vec_destroy(&bit_chunk);
  }

  while (1) {
    time_t now = time(NULL);
    if (now < expiration) {
      log_print("histories hoardling: up to date");
      sleep(expiration - now);
      continue;
    }

    log_print("histories hoardling: requesting active markets");
    struct history_market_vec market_vec = { .cap = 2048 };
    err = hoardling_histories_request_active_markets(&market_vec,
                                                     args.active_market_request,
                                                     args.active_market_response);
    if (err != E_OK) {
      errmsg_prefix("hoardling_histories_request_active_markets: ");
      goto cleanup;
    }

    // WARN: here we assume that every market is up to date at 11:15 as stated
    // at https://developers.eveonline.com/api-explorer#/operations/GetMarketsRegionIdHistory
    struct date date = now < eleven_fifteen_today ? date_utc(now - 2*TIME_DAY) : date_utc(now - TIME_DAY);
    log_print("histories hoardling: downloading histories of day (%" PRIu64 ", %" PRIu64 ")", date.year, date.day);

    struct history_bit_vec bit_vec = { .cap = 4096 };
    struct history_bit_vec market_bit_vec = { .cap = 512 };
    for (size_t i = 0; i < market_vec.len; ++i) {
      struct history_market market = market_vec.buf[i];
      market_bit_vec.len = 0;

      int try = 1;
      while (1) {
        err = history_download(&bit_vec, market);
        if (err == E_OK) {
          break;
        } else if (try <= 6) {
          try += 1;
          errmsg_prefix("history_download: ");
          errmsg_print();
          time_t backoff;
          switch (try) {
            case 1:
            case 2:
            case 3:
              log_error("histories hoardling: history download failed, retrying in 5 minutes");
              backoff = 5 * TIME_MINUTE;
              break;
            case 4:
            case 5:
              log_error("histories hoardling: history download failed, retrying in 30 minutes");
              backoff = 30 * TIME_MINUTE;
              break;
            case 6:
              log_error("histories hoardling: history download failed, retrying in 2 hours");
              backoff = 2 * TIME_HOUR;
              break;
          }
          sleep(backoff);
        } else {
          log_error("histories hoardling: history download failed, out of trails");
          errmsg_prefix("history_download: ");
          goto cleanup;
        }
      }

      // NOTE: this could be done quicker
      for (size_t i = 0; i < market_bit_vec.len; ++i) {
        if (date_is_equal(market_bit_vec.buf[i].date, date)) {
          err = history_bit_vec_push(&bit_vec, market_bit_vec.buf[i]);
          if (err != E_OK) {
            errmsg_prefix("history_bit_vec_push: ");
            goto cleanup;
          }
        }
      }
    }

    log_print("histories hoardling: history download finished");

    // dump it like it's hot
    err = hoardling_histories_dump(args.dump_dir, &bit_vec, date);
    if (err == E_FULL) {
      log_warn("histories hoardling: unable to emit history dump because there is already a dump at this path");
    } else if (err != E_OK) {
      log_error("histories hoardling: unable to emit history dump");
      errmsg_prefix("hoardling_histories_dump: ");
      errmsg_print();
    } else {
      log_print("histories hoardling: new history dump");
    }

    expiration += TIME_DAY;
    history_bit_vec_destroy(&market_bit_vec);
    history_bit_vec_destroy(&bit_vec);
  }

cleanup:
  log_warn("histories hoardling quitting");
  errmsg_prefix("hoardling_histories: ");
  errmsg_print();

  kill(getpid(), SIGTERM);
  return NULL;
}
