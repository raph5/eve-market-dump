
#define SECRET_COUNT_MAX 16

struct secret {
  struct string key;
  struct string value;
};

struct secret_table {
  size_t count;
  struct secret t[SECRET_COUNT_MAX];
};

struct secret_table *global_secret_table = NULL;
mutex_t global_secret_table_mu = MUTEX_INIT;

void secret_table_create(void) {
  struct secret_table *st = malloc(sizeof(struct secret_table));
  if (st == NULL) {
    panic("Can't even malloc a small table!");
  }
  st->count = 0;
  mutex_lock(&global_secret_table_mu, 5);
  global_secret_table = st;
  mutex_unlock(&global_secret_table_mu);
}

void secret_table_destroy(void) {
  mutex_lock(&global_secret_table_mu, 5);
  assert(global_secret_table != NULL);
  for (size_t i = 0; i < global_secret_table->count; ++i) {
    string_destroy(&global_secret_table->t[i].key);
    string_destroy(&global_secret_table->t[i].value);
  }
  free(global_secret_table);
  mutex_unlock(&global_secret_table_mu);
}

struct string secret_table_get(struct string key) {
  struct string value = {0};
  mutex_lock(&global_secret_table_mu, 5);
  assert(global_secret_table != NULL);
  for (size_t i = 0; i < global_secret_table->count; ++i) {
    if (string_cmp(global_secret_table->t[i].key, key) == 0) {
      value = global_secret_table->t[i].value;
      break;
    }
  }
  mutex_unlock(&global_secret_table_mu);
  if (value.len == 0) {
    log_error("secret \"%.*s\" not found", (int) key.len, key.buf);
    panic("could not get the secret you are looking for");
  }
  return value;
}

void secret_table_add(struct string key, struct string value) {
  mutex_lock(&global_secret_table_mu, 5);
  assert(global_secret_table != NULL);
  assert(global_secret_table->count < SECRET_COUNT_MAX);
  global_secret_table->t[global_secret_table->count++] = (struct secret) {
    .key = string_alloc_cpy(key),
    .value = string_alloc_cpy(value),
  };
  mutex_unlock(&global_secret_table_mu);
}

// prase json secrets (of format {key: value, ...}) and add the corresponding
// secrets to the secrets table
err_t secret_table_parse(struct string json_secrets) {
  mutex_lock(&global_secret_table_mu, 5);
  assert(global_secret_table != NULL);
  mutex_unlock(&global_secret_table_mu);
  json_error_t error;
  json_t *root = json_loadb(json_secrets.buf, json_secrets.len, 0, &error);
  if (root == NULL) {
    errmsg_fmt("json: on line %d: %s", error.line, error.text);
    json_decref(root);
    return E_ERR;
  }

  if (!json_is_object(root)) {
    errmsg_fmt("json: invalid secrets format, want {key: value, ...}");
    json_decref(root);
    return E_ERR;
  }

  void *iter = json_object_iter(root);
  while (iter) {
    mutex_lock(&global_secret_table_mu, 5);
    size_t secret_count = global_secret_table->count;
    mutex_unlock(&global_secret_table_mu);

    if (secret_count >= SECRET_COUNT_MAX) {
      errmsg_fmt("secrets: you got too many secrets...");
      json_decref(root);
      return E_ERR;
    }

    json_t *json_value = json_object_iter_value(iter);
    if (!json_is_string(json_value)) {
      errmsg_fmt("json: invalid secrets format, want {key: value, ...}");
      json_decref(root);
      return E_ERR;
    }

    struct string value = string_new((char *) json_string_value(json_value));
    struct string key = string_new((char *) json_object_iter_key(iter));
    secret_table_add(key, value);

    iter = json_object_iter_next(root, iter);
  }

  return E_OK;
}
