
struct order {
  bool     is_buy_order;
  int8_t   range;
  uint32_t duration;
  uint64_t issued;
  uint64_t min_volume;
  uint64_t volume_remain;
  uint64_t volume_total;
  uint64_t location_id;
  uint64_t system_id;
  uint64_t type_id;
  uint64_t region_id;
  uint64_t order_id;
  double   price;
};

IMPLEMENT_VEC(struct order, order)

void order_print(struct order *order) {
  assert(order != NULL);
  printf("{\n"
         "\t.is_buy_order = %d\n"
         "\t.range = %" PRIu8 "\n"
         "\t.duration = %" PRIu8 "\n"
         "\t.issued = %" PRIu64 "\n"
         "\t.min_volume = %" PRIu64 "\n"
         "\t.volume_remain = %" PRIu64 "\n"
         "\t.volume_total = %" PRIu64 "\n"
         "\t.location_id = %" PRIu64 "\n"
         "\t.system_id = %" PRIu64 "\n"
         "\t.type_id = %" PRIu64 "\n"
         "\t.region_id = %" PRIu64 "\n"
         "\t.order_id = %" PRIu64 "\n"
         "\t.price = %f\n"
         "}\n",
         order->is_buy_order,
         order->range,
         order->duration,
         order->issued,
         order->min_volume,
         order->volume_remain,
         order->volume_total,
         order->location_id,
         order->system_id,
         order->type_id,
         order->region_id,
         order->order_id,
         order->price);
}

err_t order_range_str_to_code(const char *str, int8_t *code) {
  assert(str != NULL);
  assert(code != NULL);
  if      (strcmp(str, "station") == 0) *code = -2;
  else if (strcmp(str, "solarsystem") == 0) *code = -1;
  else if (strcmp(str, "region") == 0) *code = 0;
  else if (strcmp(str, "1") == 0) *code = 1;
  else if (strcmp(str, "2") == 0) *code = 2;
  else if (strcmp(str, "3") == 0) *code = 3;
  else if (strcmp(str, "4") == 0) *code = 4;
  else if (strcmp(str, "5") == 0) *code = 5;
  else if (strcmp(str, "10") == 0) *code = 10;
  else if (strcmp(str, "20") == 0) *code = 20;
  else if (strcmp(str, "30") == 0) *code = 30;
  else if (strcmp(str, "40") == 0) *code = 40;
  else return E_ERR;
  return E_OK;
}

err_t order_parse_page(struct order_vec *order_vec, struct string raw,
                       uint64_t region_id) {
  err_t res = E_ERR;
  json_error_t json_err;
  json_t *root = json_loadb(raw.buf, raw.len, 0, &json_err); 
  if (root == NULL) {
    errmsg_fmt("json error on line %d: %s", json_err.line, json_err.text);
    goto cleanup;
  }
  if (!json_is_array(root)) {
    errmsg_fmt("json error: root is not an array");
    goto cleanup;
  }

  for (size_t i = 0; i < json_array_size(root); ++i) {
    json_t *json_order = json_array_get(root, i);
    if (!json_is_object(json_order)) {
      errmsg_fmt("json error: json_order is not an object");
      goto cleanup;
    }

    json_t *json_duration = json_object_get(json_order, "duration");
    if (!json_is_integer(json_duration)) {
      errmsg_fmt("json error: json_duration is not an integer");
      goto cleanup;
    }
    json_t *json_is_buy_order = json_object_get(json_order, "is_buy_order");
    if (!json_is_boolean(json_is_buy_order)) {
      errmsg_fmt("json error: json_is_buy_order is not a boolean");
      goto cleanup;
    }
    json_t *json_issued = json_object_get(json_order, "issued");
    if (!json_is_string(json_issued)) {
      errmsg_fmt("json error: json_issued is not a string");
      goto cleanup;
    }
    json_t *json_location_id = json_object_get(json_order, "location_id");
    if (!json_is_integer(json_location_id)) {
      errmsg_fmt("json error: json_location_id is not an integer");
      goto cleanup;
    }
    json_t *json_min_volume = json_object_get(json_order, "min_volume");
    if (!json_is_integer(json_min_volume)) {
      errmsg_fmt("json error: json_min_volume is not an integer");
      goto cleanup;
    }
    json_t *json_order_id = json_object_get(json_order, "order_id");
    if (!json_is_integer(json_order_id)) {
      errmsg_fmt("json error: json_order_id is not an integer");
      goto cleanup;
    }
    json_t *json_price = json_object_get(json_order, "price");
    if (!json_is_real(json_price)) {
      errmsg_fmt("json error: json_price is not a real");
      goto cleanup;
    }
    json_t *json_range = json_object_get(json_order, "range");
    if (!json_is_string(json_range)) {
      errmsg_fmt("json error: json_range is not a string");
      goto cleanup;
    }
    json_t *json_system_id = json_object_get(json_order, "system_id");
    if (!json_is_integer(json_system_id)) {
      errmsg_fmt("json error: json_system_id is not an integer");
      goto cleanup;
    }
    json_t *json_type_id = json_object_get(json_order, "type_id");
    if (!json_is_integer(json_type_id)) {
      errmsg_fmt("json error: json_type_id is not an integer");
      goto cleanup;
    }
    json_t *json_volume_remain = json_object_get(json_order, "volume_remain");
    if (!json_is_integer(json_volume_remain)) {
      errmsg_fmt("json error: json_volume_remain is not an integer");
      goto cleanup;
    }
    json_t *json_volume_total = json_object_get(json_order, "volume_total");
    if (!json_is_integer(json_volume_total)) {
      errmsg_fmt("json error: json_volume_total is not an integer");
      goto cleanup;
    }

    json_int_t duration = json_integer_value(json_duration);
    if (duration < 0 || duration > UINT32_MAX) {
      errmsg_fmt("json error: duration is out of range");
      goto cleanup;
    }
    json_int_t location_id = json_integer_value(json_location_id);
    if (location_id < 0 || location_id > UINT64_MAX) {
      errmsg_fmt("json error: location_id is out of range");
      goto cleanup;
    }
    json_int_t min_volume = json_integer_value(json_min_volume);
    if (min_volume < 0 || min_volume > UINT64_MAX) {
      errmsg_fmt("json error: min_volume is out of range");
      goto cleanup;
    }
    json_int_t order_id = json_integer_value(json_order_id);
    if (order_id < 0 || order_id > UINT64_MAX) {
      errmsg_fmt("json error: order_id is out of range");
      goto cleanup;
    }
    json_int_t system_id = json_integer_value(json_system_id);
    if (system_id < 0 || system_id > UINT64_MAX) {
      errmsg_fmt("json error: system_id is out of range");
      goto cleanup;
    }
    json_int_t type_id = json_integer_value(json_type_id);
    if (type_id < 0 || type_id > UINT64_MAX) {
      errmsg_fmt("json error: type_id is out of range");
      goto cleanup;
    }
    json_int_t volume_remain = json_integer_value(json_volume_remain);
    if (volume_remain < 0 || volume_remain > UINT64_MAX) {
      errmsg_fmt("json error: volume_remain is out of range");
      goto cleanup;
    }
    json_int_t volume_total = json_integer_value(json_volume_total);
    if (volume_total < 0 || volume_total > UINT64_MAX) {
      errmsg_fmt("json error: volume_total is out of range");
      goto cleanup;
    }

    int8_t range_code;
    err_t err = order_range_str_to_code(json_string_value(json_range), 
                                        &range_code);
    if (err != E_OK) {
      errmsg_prefix("order_range_str_to_code: ");
      goto cleanup;
    }

    time_t issued;
    err = time_parse(ESI_TIME, json_string_value(json_issued), &issued);
    if (err != E_OK) {
      errmsg_prefix("time_parse: ");
      goto cleanup;
    }

    struct order order = {
      .is_buy_order = json_boolean_value(json_is_buy_order),
      .duration = duration,
      .range = range_code,
      .issued = issued,
      .min_volume = min_volume,
      .volume_remain = volume_remain,
      .volume_total = volume_total,
      .location_id = location_id,
      .system_id = system_id,
      .type_id = type_id,
      .region_id = region_id,
      .order_id = order_id,
      .price = json_real_value(json_price),
    };

    err = order_vec_push(order_vec, order);
    if (err != E_OK) {
      errmsg_prefix("order_vec_push: ");
      goto cleanup;
    }
  }

  res = E_OK;

cleanup:
  json_decref(root);
  return res;
}

// page_count can be NULL. If it's not NULL, order_download_page will error in case 
// esi_fetch return a 0 page_count
err_t order_download_page(struct order_vec *order_vec, uint64_t region_id,
                          size_t page, size_t *page_count) {
  assert(order_vec != NULL);
  assert(page >= 1);

  log_print("order_download region %" PRIu64 " page %zu", region_id, page);

  err_t res = E_ERR;
  struct esi_response response = {0};

  const size_t URI_LEN_MAX = 2048;
  char uri_buf[URI_LEN_MAX];
  struct string uri = string_fmt(uri_buf, URI_LEN_MAX, 
                                 "/markets/%" PRIu64 "/orders?page=%zu",
                                 region_id, page);
  err_t err = esi_fetch(&response, string_new("GET"), uri, (struct string) {0},
                        false, 5);
  if (err != E_OK) {
    errmsg_prefix("esi_fetch: ");
    goto cleanup;
  }

  err = order_parse_page(order_vec, response.body, region_id);
  if (err != E_OK) {
    errmsg_prefix("order_parse_page: ");
    goto cleanup;
  }

  if (page_count != NULL && response.pages == 0) {
    errmsg_fmt("page_count is null, that likely mean esi_fetch could not get page_count");
    goto cleanup;
  }
  if (page_count != NULL) *page_count = response.pages;
  res = E_OK;

cleanup:
  esi_response_destroy(&response);
  return res;
}

// order_vec is empty on error
// out_expiration is the expiration date of the returned data
err_t order_download_universe(struct order_vec *order_vec, uint64_t regions[],
                              size_t regions_len) {
  err_t res = E_ERR;

  for (size_t i = 0; i < regions_len; ++i) {
    uint64_t region_id = regions[i];
    size_t region_page_count;

    err_t err = order_download_page(order_vec, region_id, 1, &region_page_count);
    if (err != E_OK) {
      errmsg_prefix("order_download_page: ");
      goto cleanup;
    }

    for (size_t page = 2; page <= region_page_count; ++page) {
      size_t page_count;

      err_t err = order_download_page(order_vec, region_id, page, &page_count);
      if (err != E_OK) {
        errmsg_prefix("order_download_page: ");
        goto cleanup;
      }

      if (page_count != region_page_count) {
        log_warn("order_download: page_count changed during the download");
        region_page_count = page_count;
      }
    }
  }

  res = E_OK;

cleanup:
  if (res != E_OK) order_vec->len = 0;
  return res;
}

// NOTE: using a struct of array for order_vec would improve the performances here
// WARN: locid_vec should not be initialized
err_t order_create_location_id_vec(struct uint64_vec *locid_vec,
                                   struct order_vec *order_vec) {
  assert(locid_vec != NULL);
  assert(order_vec != NULL);

  // TODO: remove
  time_t debug_start = time(NULL);

  err_t err = uint64_vec_create(locid_vec, 2048);
  if (err != E_OK) {
    errmsg_prefix("uint64_vec_create: ");
    return E_ERR;
  }

  for (size_t i = 0; i < order_vec->len; ++i) {
    size_t j;
    for (j = 0; j < locid_vec->len; ++j) {
      if (locid_vec->buf[j] == order_vec->buf[i].location_id) break;
    }
    if (j >= locid_vec->len) {
      err = uint64_vec_push(locid_vec, order_vec->buf[i].location_id);
      if (err != E_OK) {
        errmsg_prefix("uint64_vec_push: ");
        return E_ERR;
      }
    }
  }

  // TODO: remove
  time_t debug_end = time(NULL);
  log_print("DEBUG: order_create_location_id_vec duration %d", debug_end - debug_start);

  return E_OK;
}

err_t dump_write_order(struct dump *dump, struct order *order) {
  assert(order != NULL);
  if (dump_write_uint8(dump, order->is_buy_order) != E_OK) goto error;
  if (dump_write_int8(dump, order->range) != E_OK) goto error;
  if (dump_write_uint32(dump, order->duration) != E_OK) goto error;
  if (dump_write_uint64(dump, order->issued) != E_OK) goto error;
  if (dump_write_uint64(dump, order->min_volume) != E_OK) goto error;
  if (dump_write_uint64(dump, order->volume_remain) != E_OK) goto error;
  if (dump_write_uint64(dump, order->volume_total) != E_OK) goto error;
  if (dump_write_uint64(dump, order->location_id) != E_OK) goto error;
  if (dump_write_uint64(dump, order->system_id) != E_OK) goto error;
  if (dump_write_uint64(dump, order->type_id) != E_OK) goto error;
  if (dump_write_uint64(dump, order->region_id) != E_OK) goto error;
  if (dump_write_uint64(dump, order->order_id) != E_OK) goto error;
  if (dump_write_float64(dump, order->price) != E_OK) goto error;
  return E_OK;

error:
  errmsg_prefix("dump_write_uint8/int8/uint64/float64: ");
  return E_ERR;
}

err_t dump_write_order_table(struct dump *dump, struct order *order,
                             size_t order_len) {
  if (dump_write_uint64(dump, order_len) != E_OK) {
    errmsg_prefix("dump_write_uint64: ");
    return E_ERR;
  }
  for (size_t i = 0; i < order_len; ++i) {
    if (dump_write_order(dump, order + i) != E_OK) {
      errmsg_prefix("dump_write_order :");
      return E_ERR;
    }
  }
  return E_OK;
}
