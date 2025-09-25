// TODO: group days together

struct history_day {
  double average;
  double highest;
  double lowest;
  uint64_t order_count;
  uint64_t volume;
  uint16_t year;  // iso 8601 ordinal date
  uint16_t day;
};

struct history {
  uint64_t region_id;
  uint64_t type_id;
  size_t len;
  struct history_day *buf;
};

IMPLEMENT_VEC(struct history, history);

void history_destroy(struct history *history) {
  assert(history != NULL);
  free(history->buf);
  *history = (struct history) {};
}

// WARN: on successful return, history need to be destroyed
err_t history_parse(struct history *history, struct string raw,
                 uint64_t region_id, uint64_t type_id) {
  err_t res = E_ERR;
  json_t *root;
  struct history_day *days = NULL;

  json_error_t json_err;
  root = json_loadb(raw.buf, raw.len, 0, &json_err);
  if (root == NULL) {
    errmsg_fmt("json error on line %d: %s", json_err.line, json_err.text);
    goto cleanup;
  }
  if (!json_is_array(root)) {
    errmsg_fmt("json error: root is not an array");
    goto cleanup;
  }

  size_t day_count = json_array_size(root);
  days = malloc(day_count * sizeof(*days));
  if (days == NULL) {
    errmsg_fmt("malloc: %s", strerror(errno));
    goto cleanup;
  }

  for (size_t i = 0; i < day_count; ++i) {
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

    uint16_t year;
    uint16_t day;
    err_t err = date_parse(ESI_DATE, json_string_value(json_date), &year, &day);
    if (err != E_OK) {
      errmsg_prefix("date_parse: ");
      goto cleanup;
    }

    days[i] = (struct history_day) {
      .average = json_real_value(json_average),
      .highest = json_real_value(json_highest),
      .lowest = json_real_value(json_lowest),
      .order_count = order_count,
      .volume = volume,
      .year = year,
      .day = day,
    };
  }

  *history = (struct history) {
    .region_id = region_id,
    .type_id = type_id,
    .len = day_count,
    .buf = days,
  };
  res = E_OK;

cleanup:
  json_decref(root);
  if (res != E_OK) free(days);
  return res;
}

// WARN: on successful return, history need to be destroyed
err_t history_download(struct history *history, uint64_t region_id,
                       uint64_t type_id, time_t *expires, time_t *modified) {
  assert(history != NULL);

  err_t res = E_ERR;
  struct esi_response response = {};
  *history = (struct history) {};

  const size_t URI_LEN_MAX = 2048;
  char uri_buf[URI_LEN_MAX];
  struct string uri = string_fmt(uri_buf, URI_LEN_MAX, 
                                 "/markets/%llu/history?type_id=%llu",
                                 region_id, type_id);
  err_t err = esi_fetch(&response, string_new("GET"), uri, (struct string) {},
                        false, 5);
  if (err != E_OK) {
    errmsg_prefix("esi_fetch: ");
    goto cleanup;
  }

  err = history_parse(history, response.body, region_id, type_id);
  if (err != E_OK) {
    errmsg_prefix("history_parse: ");
    goto cleanup;
  }

  if (expires != NULL && response.expires == 0) {
    errmsg_fmt("expires is null, that likely mean esi_fetch could not get expires");
    goto cleanup;
  }
  if (modified != NULL && response.modified == 0) {
    errmsg_fmt("modified is null, that likely mean esi_fetch could not get modified");
    goto cleanup;
  }
  if (expires != NULL) *expires = response.expires;
  if (modified != NULL) *modified = response.modified;
  res = E_OK;

cleanup:
  esi_response_destroy(&response);
  if (res != E_OK) history_destroy(history);
  return res;
}

void history_print(struct history *history) {
  assert(history != NULL);
  printf("[\n");
  for (size_t i = 0; i < history->len; ++i) {
    struct history_day *day = history->buf + i;
    printf("\t{\n"
           "\t\t.average = %f,\n"
           "\t\t.highest = %f,\n"
           "\t\t.lowest = %f,\n"
           "\t\t.order_count = %llu,\n"
           "\t\t.volume = %llu,\n"
           "\t\t.year = %hu,\n"
           "\t\t.day = %hu,\n"
           "\t}\n",
           day->average, day->highest, day->lowest, day->order_count,
           day->volume, day->year, day->day);
  }
  printf("]\n");
}
