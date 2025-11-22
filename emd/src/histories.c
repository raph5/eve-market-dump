
struct history_market {
  uint64_t region_id;
  uint64_t type_id;
};

struct history_stats {
  double average;
  double highest;
  double lowest;
  uint64_t order_count;
  uint64_t volume;
};

struct history_bit {
  struct date date;
  struct history_market market;
  struct history_stats stats;
};

IMPLEMENT_VEC(struct history_market, history_market)
IMPLEMENT_VEC(struct history_stats, history_stats)
IMPLEMENT_VEC(struct history_bit, history_bit)

struct history_day {
  struct date date;
  struct history_market_vec key;
  struct history_stats_vec val;
};

IMPLEMENT_VEC(struct history_day, history_day)

void history_day_print(struct history_day *day) {
  assert(day != NULL);
  assert(day->key.len == day->val.len);
  printf("{\n"
         "\t.year = %" PRIu16 "\n"
         "\t.day = %" PRIu16 "\n"
         "\t.data = [\n",
         day->date.year, day->date.day);
  for (size_t i = 0; i < day->key.len; ++i) {
    struct history_market market = day->key.buf[i];
    struct history_stats stats = day->val.buf[i];
    printf("\t\t{\n"
           "\t\t\t.market.region_id = %" PRIu64 ",\n"
           "\t\t\t.market.type_id = %" PRIu64 ",\n"
           "\t\t\t.stats.average = %f,\n"
           "\t\t\t.stats.highest = %f,\n"
           "\t\t\t.stats.lowest = %f,\n"
           "\t\t\t.stats.order_count = %" PRIu64 ",\n"
           "\t\t\t.stats.volume = %" PRIu64 ",\n"
           "\t\t}\n",
           market.region_id, market.type_id, stats.average, stats.highest,
           stats.lowest, stats.order_count, stats.volume);
  }
  printf("\t],\n"
         "}\n");
}

err_t history_day_create(struct history_day *day, struct date date) {
  assert(day != NULL);
  err_t err = history_market_vec_create(&day->key, 1024);
  if (err != E_OK) {
    errmsg_prefix("history_market_vec_create: ");
    return E_ERR;
  }
  err = history_stats_vec_create(&day->val, 1024);
  if (err != E_OK) {
    errmsg_prefix("history_stats_vec_create: ");
    history_market_vec_destroy(&day->key);
    return E_ERR;
  }
  day->date = date;
  return E_OK;
}

void history_day_destroy(struct history_day *day) {
  assert(day != NULL);
  assert(day->key.len == day->val.len);
  history_market_vec_destroy(&day->key);
  history_stats_vec_destroy(&day->val);
  *day = (struct history_day) {0};
}

struct history_stats *history_day_get(struct history_day *day, 
                                      struct history_market market) {
  assert(day != NULL);
  for (size_t i = 0; i < day->key.len; ++i) {
    struct history_market day_market = day->key.buf[i];
    if (
      day_market.region_id == market.region_id &&
      day_market.type_id == market.type_id
    ) {
      return day->val.buf + i;
    }
  }
  return NULL;
}

err_t history_day_push(struct history_day *day, struct history_market market,
                       struct history_stats stats) {
  assert(day != NULL);
  assert(day->key.len == day->val.len);
  err_t err = history_market_vec_push(&day->key, market);
  if (err != E_OK) {
    errmsg_prefix("history_market_vec_push: ");
    return E_ERR;
  }
  err = history_stats_vec_push(&day->val, stats);
  if (err != E_OK) {
    errmsg_prefix("history_stats_vec_push: ");
    day->key.len -= 1;
    return E_ERR;
  }
  return E_OK;
}

struct history_day *history_day_vec_get_by_date(struct history_day_vec *vec,
                                                struct date date) {
  assert(vec != NULL);
  for (size_t i = 0; i < vec->len; ++i) {
    struct date vec_date = vec->buf[i].date;
    if (vec_date.day == date.day && vec_date.year == date.year) {
      return vec->buf + i;
    }
  }
  return NULL;
}

// market_vec should already be initialized
err_t get_active_markets(struct history_market_vec *market_vec,
                         struct order_vec *order_vec) {
  assert(market_vec != NULL);
  assert(order_vec != NULL);

  // fuck big O
  for (size_t i = 0; i < order_vec->len; ++i) {
    struct history_market market = {
      .region_id = order_vec->buf[i].region_id,
      .type_id = order_vec->buf[i].type_id,
    };
    size_t j;
    for (j = 0; j < market_vec->len; ++j) {
      if (
        market_vec->buf[j].type_id == market.type_id &&
        market_vec->buf[j].region_id == market.region_id
      ) {
        break;
      }
    }
    if (j >= market_vec->len) {
      err_t err = history_market_vec_push(market_vec, market);
      if (err != E_OK) {
        errmsg_prefix("history_market_vec_push: ");
        market_vec->len = 0;
        return E_ERR;
      }
    }
  }

  return E_OK;
}

err_t history_parse(struct history_bit_vec *bit_vec, struct string raw_json,
                    struct history_market market) {
  assert(bit_vec != NULL);

  err_t res = E_ERR;
  size_t bit_vec_initial_len = bit_vec->len;
  json_t *root;

  json_error_t json_err;
  root = json_loadb(raw_json.buf, raw_json.len, 0, &json_err);
  if (root == NULL) {
    errmsg_fmt("json error on line %d: %s", json_err.line, json_err.text);
    goto cleanup;
  }
  if (!json_is_array(root)) {
    errmsg_fmt("json error: root is not an array");
    goto cleanup;
  }

  for (size_t i = 0; i < json_array_size(root); ++i) {
    json_t *json_day = json_array_get(root, i);
    if (!json_is_object(json_day)) {
      errmsg_fmt("json error: json_day is not an object");
      goto cleanup;
    }

    json_t *json_average = json_object_get(json_day, "average");
    if (!json_is_real(json_average)) {
      errmsg_fmt("json error: json_average is not a real");
      goto cleanup;
    }
    json_t *json_highest = json_object_get(json_day, "highest");
    if (!json_is_real(json_highest)) {
      errmsg_fmt("json error: json_highest is not a real");
      goto cleanup;
    }
    json_t *json_lowest = json_object_get(json_day, "lowest");
    if (!json_is_real(json_lowest)) {
      errmsg_fmt("json error: json_lowest is not a real");
      goto cleanup;
    }
    json_t *json_order_count = json_object_get(json_day, "order_count");
    if (!json_is_integer(json_order_count)) {
      errmsg_fmt("json error: json_order_count is not an integer");
      goto cleanup;
    }
    json_t *json_volume = json_object_get(json_day, "volume");
    if (!json_is_integer(json_volume)) {
      errmsg_fmt("json error: json_volume is not an integer");
      goto cleanup;
    }
    json_t *json_date = json_object_get(json_day, "date");
    if (!json_is_string(json_date)) {
      errmsg_fmt("json error: json_date is not a string");
      goto cleanup;
    }

    json_int_t order_count = json_integer_value(json_order_count);
    if (order_count < 0 || order_count > UINT64_MAX) {
      errmsg_fmt("json error: order_count is out of range");
      goto cleanup;
    }
    json_int_t volume = json_integer_value(json_volume);
    if (volume < 0 || volume > UINT64_MAX) {
      errmsg_fmt("json error: volume is out of range");
      goto cleanup;
    }

    struct date date;
    err_t err = date_parse(ESI_DATE, json_string_value(json_date), &date);
    if (err != E_OK) {
      errmsg_prefix("date_parse: ");
      goto cleanup;
    }

    struct history_bit bit = {
      .date = date,
      .market = market,
      .stats = {
        .average = json_real_value(json_average),
        .highest = json_real_value(json_highest),
        .lowest = json_real_value(json_lowest),
        .order_count = order_count,
        .volume = volume,
      },
    };
    err = history_bit_vec_push(bit_vec, bit);
    if (err != E_OK) {
      errmsg_prefix("history_bit_vec_push: ");
      goto cleanup;
    }
  }

  res = E_OK;

cleanup:
  if (res != E_OK) bit_vec->len = bit_vec_initial_len;
  json_decref(root);
  return res;
}

err_t history_download(struct history_bit_vec *bit_vec,
                       struct history_market market) {
  assert(bit_vec != NULL);

  // TODO: remove log
  log_print("download market (%" PRIu64 ", %" PRIu64 ")", market.region_id, market.type_id);

  err_t res = E_ERR;
  struct esi_response response = {0};

  const size_t URI_LEN_MAX = 2048;
  char uri_buf[URI_LEN_MAX];
  struct string uri = string_fmt(uri_buf, URI_LEN_MAX, 
                                 "/markets/%" PRIu64 "/history?type_id=%" PRIu64,
                                 market.region_id, market.type_id);
  err_t err = esi_fetch(&response, string_new("GET"), uri, (struct string) {0},
                        false, 5);
  if (err != E_OK) {
    errmsg_prefix("esi_fetch: ");
    goto cleanup;
  }

  err = history_parse(bit_vec, response.body, market);
  if (err != E_OK) {
    errmsg_prefix("history_parse: ");
    goto cleanup;
  }

  res = E_OK;

cleanup:
  esi_response_destroy(&response);
  return res;
}

err_t dump_write_history_market(struct dump *dump,
                                struct history_market *market) {
  assert(market != NULL);
  if (dump_write_uint64(dump, market->region_id) != E_OK) goto error;
  if (dump_write_uint64(dump, market->type_id) != E_OK) goto error;
  return E_OK;

error:
  errmsg_prefix("dump_write_uint64: ");
  return E_ERR;
}

err_t dump_write_history_stats(struct dump *dump,
                               struct history_stats *stats) {
  assert(stats != NULL);
  if (dump_write_float64(dump, stats->average) != E_OK) goto error;
  if (dump_write_float64(dump, stats->highest) != E_OK) goto error;
  if (dump_write_float64(dump, stats->lowest) != E_OK) goto error;
  if (dump_write_uint64(dump, stats->order_count) != E_OK) goto error;
  if (dump_write_uint64(dump, stats->volume) != E_OK) goto error;
  return  E_OK;

error:
  errmsg_prefix("dump_write_uint64/float64: ");
  return E_ERR;
}

err_t dump_write_history_day(struct dump *dump, struct history_day *day) {
  assert(day != NULL);
  assert(day->key.len == day->val.len);

  if (dump_write_uint16(dump, day->date.year) != E_OK) goto error;
  if (dump_write_uint16(dump, day->date.day) != E_OK) goto error;
  if (dump_write_uint64(dump, day->key.len) != E_OK) goto error;

  for (size_t i = 0; i < day->key.len; ++i) {
    if (dump_write_history_market(dump, &day->key.buf[i]) != E_OK) {
      errmsg_prefix("dump_write_history_market: ");
      return E_ERR;
    }
    if (dump_write_history_stats(dump, &day->val.buf[i]) != E_OK) {
      errmsg_prefix("dump_write_history_stats: ");
      return E_ERR;
    }
  }

  return E_OK;

error:
  errmsg_prefix("dump_write_uint16/uint64: ");
  return E_ERR;
}

err_t dump_write_history_bit(struct dump *dump, struct history_bit *bit) {
  assert(bit != NULL);
  if (dump_write_date(dump, bit->date) != E_OK) {
    errmsg_prefix("dump_write_date: ");
    return E_ERR;
  }
  if (dump_write_history_market(dump, &bit->market) != E_OK) {
    errmsg_prefix("dump_write_history_market: ");
    return E_ERR;
  }
  if (dump_write_history_stats(dump, &bit->stats) != E_OK) {
    errmsg_prefix("dump_write_history_stats: ");
    return E_ERR;
  }
  return E_OK;
}

err_t dump_write_history_bit_vec(struct dump *dump,
                                 struct history_bit_vec *history_bit_vec) {
  assert(history_bit_vec != NULL);
  for (size_t i = 0; i < history_bit_vec->len; ++i) {
    if (dump_write_history_bit(dump, history_bit_vec->buf + i) != E_OK) {
      errmsg_prefix("dump_write_history_bit: ");
      return E_ERR;
    }
  }
  return E_OK;
}
