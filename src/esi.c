
const err_t E_ESI_BASE = 1000;

const struct string ESI_ROOT = STRING_NEW("https://esi.evetech.net/latest");
const uint64_t      ESI_REQUEST_TIMEOUT = 7;

__thread CURL *esi_curl_thread_handle = NULL;

void esi_thread_init() {
  esi_curl_thread_handle = curl_easy_init();
  if (esi_curl_thread_handle == NULL) panic("curl_easy_init error");
}

void esi_thread_deinit() {
  curl_easy_cleanup(esi_curl_thread_handle);
}

const size_t  SSO_ACCESS_TOKEN_LEN_MAX = 4096;
char          sso_access_token[SSO_ACCESS_TOKEN_LEN_MAX];
uint64_t      sso_access_token_expiry = 0;
mutex_t       sso_access_token_mu = MUTEX_INIT;

// Will the curl handle passed as argument
err_t sso_access_token_acquire(CURL *handle) {
  assert(handle != NULL);

  uint64_t now = (uint64_t) time(NULL);
  mutex_lock(&sso_access_token_mu, 5);
  if (now + 10 < sso_access_token_expiry) {
    mutex_unlock(&sso_access_token_mu);
    return E_OK;
  }
  mutex_unlock(&sso_access_token_mu);

  // get the little secrets
	struct string client_id = secret_table_get(string_new("ssoClientId"));
	struct string client_secret = secret_table_get(string_new("ssoClientSecret"));
	struct string refresh_token = secret_table_get(string_new("ssoRefreshToken"));

  const size_t CLIENT_ID_LEN_MAX = 128;
  char client_id_nt[CLIENT_ID_LEN_MAX];
  string_null_terminate(client_id, client_id_nt, CLIENT_ID_LEN_MAX);

  const size_t CLIENT_SECRET_LEN_MAX = 128;
  char client_secret_nt[CLIENT_SECRET_LEN_MAX];
  string_null_terminate(client_secret, client_secret_nt, CLIENT_SECRET_LEN_MAX);

  CURLcode rv;
  err_t err = E_ERR;

  struct curl_slist *header_list = NULL;
  FILE *res_body_file = NULL;
  json_t *res_root = NULL;

  const size_t REQ_BODY_LEN_MAX = 8192;
  char req_body[REQ_BODY_LEN_MAX];
  const size_t RES_BODY_LEN_MAX = 8192;
  char res_body[RES_BODY_LEN_MAX];
  // WARN: do not call return passed this line, set `err` and goto cleanup

  // reset handle
  curl_easy_reset(handle);

  // setopts
  char *url = "https://login.eveonline.com/v2/oauth/token";
  rv = curl_easy_setopt(handle, CURLOPT_URL, url);
  if (rv != CURLE_OK) {
    errmsg_fmt("CURLOPT_URL error: %s", curl_easy_strerror(rv));
    goto cleanup;
  }

  header_list = curl_slist_append(header_list, "Content-Type: application/x-www-form-urlencoded");
  rv = curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header_list);
  if (rv != CURLE_OK) {
    errmsg_fmt("CURLOPT_HTTPHEADER error: %s", curl_easy_strerror(rv));
    goto cleanup;
  }

  rv = curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
  if (rv != CURLE_OK) {
    errmsg_fmt("CURLOPT_HTTPAUTH error: %s", curl_easy_strerror(rv));
    goto cleanup;
  }

  rv = curl_easy_setopt(handle, CURLOPT_USERNAME, client_id_nt);
  if (rv != CURLE_OK) {
    errmsg_fmt("CURLOPT_USERNAME error: %s", curl_easy_strerror(rv));
    goto cleanup;
  }

  rv = curl_easy_setopt(handle, CURLOPT_PASSWORD, client_secret_nt);
  if (rv != CURLE_OK) {
    errmsg_fmt("CURLOPT_PASSWORD error: %s", curl_easy_strerror(rv));
    goto cleanup;
  }

  // request body
  size_t req_body_len = snprintf(req_body, REQ_BODY_LEN_MAX,
                                 "grant_type=refresh_token&refresh_token=%.*s",
                                 (int) refresh_token.len, refresh_token.buf);
  if (req_body_len >= REQ_BODY_LEN_MAX) {
    panic("req_body too long");
  }
  rv = curl_easy_setopt(handle, CURLOPT_POSTFIELDS, req_body);
  if (rv != CURLE_OK) {
    errmsg_fmt("CURLOPT_POSTFIELDS error: %s", curl_easy_strerror(rv));
    goto cleanup;
  }

  // response body
  res_body_file = fmemopen(res_body, RES_BODY_LEN_MAX, "w");  // text-mode
  if (res_body_file == NULL) {
    errmsg_fmt("res_body_file fmemopen: %s", strerror(errno));
    goto cleanup;
  }
  rv = curl_easy_setopt(handle, CURLOPT_WRITEDATA, res_body_file);
  if (rv != CURLE_OK) {
    errmsg_fmt("CURLOPT_WRITEDATA error: %s", curl_easy_strerror(rv));
    goto cleanup;
  }

  // finally perform
  rv = curl_easy_perform(handle);
  if (rv != CURLE_OK) {
    errmsg_fmt("curl_easy_perform error: %s", curl_easy_strerror(rv));
    goto cleanup;
  }
  fclose(res_body_file);  // null terminates `res_body` buffer
  res_body_file = NULL;

  // reponse code
  long res_code;
  rv = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &res_code);
  if (rv != CURLE_OK) {
    errmsg_fmt("CURLINFO_RESPONSE_CODE error: %s", curl_easy_strerror(rv));
    goto cleanup;
  }
  if (res_code != 200) {
    errmsg_fmt("response code %d", res_code);
    goto cleanup;
  }

  // decode json repsonse
  json_error_t json_err;
  res_root = json_loads(res_body, 0, &json_err);
  if (res_root == NULL) {
    errmsg_fmt("json error on line %d: %s", json_err.line, json_err.text);
    goto cleanup;
  }
  if (!json_is_object(res_root)) {
    errmsg_fmt("json error: res_root is not an object");
    goto cleanup;
  }

  json_t *token_type = json_object_get(res_root, "token_type");
  if (!json_is_string(token_type)) {
    errmsg_fmt("json error: token_type is not a string");
    goto cleanup;
  }
  if (strcmp(json_string_value(token_type), "Bearer") != 0) {
    errmsg_fmt("sso error: token_type not equal too 'Bearer' ??");
    goto cleanup;
  }

  json_t *new_refresh_token = json_object_get(res_root, "refresh_token");
  if (!json_is_string(new_refresh_token)) {
    errmsg_fmt("json error: new_refresh_token is not a string");
    goto cleanup;
  }
  if (memcmp(json_string_value(new_refresh_token), refresh_token.buf, refresh_token.len) != 0 ||
      json_string_value(new_refresh_token)[refresh_token.len] != '\0') {
    errmsg_fmt("sso error: new_refresh_token not equal too refresh_token, behaviour of SSO might have changed");
    goto cleanup;
  }

  json_t *expires_in = json_object_get(res_root, "expires_in");
  if (!json_is_number(expires_in)) {
    errmsg_fmt("json error: expires_in is not a number");
    goto cleanup;
  }
  double expires_in_value = json_number_value(expires_in);
  if (expires_in_value < 0 || expires_in_value > INT_MAX) {
    errmsg_fmt("sso error: expires_in out of bounds");
    goto cleanup;
  }
  int expires_in_secs = (int) expires_in_value;

  json_t *access_token = json_object_get(res_root, "access_token");
  if (!json_is_string(access_token)) {
    errmsg_fmt("json error: access_token is not a string");
    goto cleanup;
  }
  if (strlen(json_string_value(access_token)) > SSO_ACCESS_TOKEN_LEN_MAX) {
    errmsg_fmt("sso error: what did ccp manage to stick in this token so that it would get so big");
    goto cleanup;
  }

  // we have it!!!!, we got our access token!!
  mutex_lock(&sso_access_token_mu, 5);
  strcpy(sso_access_token, json_string_value(access_token));
  sso_access_token_expiry = (uint64_t) time(NULL) + expires_in_secs - 7;
  mutex_unlock(&sso_access_token_mu);
  err = E_OK;

cleanup:
  curl_slist_free_all(header_list);
  if (res_body_file != NULL) fclose(res_body_file);
  json_decref(res_root);
  return err;
}

uint64_t esi_timeout = 0;
mutex_t esi_timeout_mu = MUTEX_INIT;

void esi_timeout_set(uint64_t duration) {
  uint64_t now = (uint64_t) time(NULL);
  mutex_lock(&esi_timeout_mu, 5);
  if (now + duration > esi_timeout) {
    esi_timeout = now + duration;
  }
  mutex_unlock(&esi_timeout_mu);
}

void esi_timeout_clear() {
  mutex_lock(&esi_timeout_mu, 5);
  uint64_t timeout = esi_timeout;
  mutex_unlock(&esi_timeout_mu);

  uint64_t now = (uint64_t) time(NULL);
  if (timeout > now) {
    assert(timeout - now < 60 * 60);  // just to be sure
    sleep(timeout - now);
    esi_timeout_clear();
  }
}

err_t esi_parse_error_timeout(struct string body, int *timeout_secs) {
  err_t err = E_ERR;

  json_error_t json_err;
  json_t *root = json_loadb(body.buf, body.len, 0, &json_err);
  if (root == NULL) {
    errmsg_fmt("json error on line %d: %s", json_err.line, json_err.text);
    goto cleanup;
  }
  if (!json_is_object(root)) {
    errmsg_fmt("json error: root is not an object");
    goto cleanup;
  }

  json_t *timeout_field = json_object_get(root, "timeout");
  if (!json_is_integer(root)){ 
    errmsg_fmt("json error: timeout is not a number");
    goto cleanup;
  }
  json_int_t timeout = json_integer_value(timeout_field);
  if (timeout <= 0 || timeout > INT_MAX) {
    errmsg_fmt("json error: timeout is out of range");
    goto cleanup;
  }

  *timeout_secs = (long) timeout;
  err = E_OK;

cleanup:
  json_decref(root);
  return err;
}

// WARN: Yo then need to free the string pointed to by message
err_t esi_parse_error_message(struct string body, struct string *message) {
  err_t err = E_ERR;

  json_error_t json_err;
  json_t *root = json_loadb(body.buf, body.len, 0, &json_err);
  if (root == NULL) {
    errmsg_fmt("json error on line %d: %s", json_err.line, json_err.text);
    goto cleanup;
  }
  if (!json_is_object(root)) {
    errmsg_fmt("json error: root is not an object");
    goto cleanup;
  }

  json_t *message_field = json_object_get(root, "error");
  if (!json_is_string(message_field)) {
    errmsg_fmt("json error: error is not a string");
    goto cleanup;
  }

  *message = string_alloc_cpy(string_new((char *) json_string_value(message_field)));
  err = E_OK;

cleanup:
  json_decref(root);
  return err;
}

const err_t E_ESI_ERR           = E_ESI_BASE + 1;  // esi returned an error

struct esi_response {
  struct string body;
  size_t pages;  // content of the X-Pages header
};

void esi_response_destroy(struct esi_response *res) {
  string_destroy(&res->body);
}

err_t esi_build_request(
  CURL *handle,
  struct string method,
  struct string uri,
  struct string body,
  bool authenticated
) {
  assert(handle != NULL);

  // get soo token
  if (authenticated) {
    err_t err = sso_access_token_acquire(handle);
    if (err != E_OK) {
      errmsg_prefix("soo_access_token_acquire: ");
      return E_ERR;
    }
  }

  // reset handle
  CURLcode rv;
  curl_easy_reset(handle);

  // url
  const size_t URL_LEN_MAX = 2048;
  char url[URL_LEN_MAX];
  int url_len = snprintf(url, URL_LEN_MAX, "%.*s%.*s", (int) ESI_ROOT.len,
                         ESI_ROOT.buf, (int) uri.len, uri.buf);
  if (url_len < 0 || url_len >= URL_LEN_MAX) {
    errmsg_fmt("URL_LEN_MAX reached");
    return E_ERR;
  }
  rv = curl_easy_setopt(handle, CURLOPT_URL, url);
  if (rv != CURLE_OK) {
    errmsg_fmt("CURLOPT_URL error: %s", curl_easy_strerror(rv));
    return E_ERR;
  }

  // method
  const size_t METHOD_LEN_MAX = 32;
  char method_nt[METHOD_LEN_MAX];
  string_null_terminate(method, method_nt, METHOD_LEN_MAX);
  rv = curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method_nt);
  if (rv != CURLE_OK) {
    errmsg_fmt("CURLOPT_CUSTOMREQUEST error: %s", curl_easy_strerror(rv));
    return E_ERR;
  }

  // request body
  if (string_cmp(method, string_new("POST")) == 0 ||
      string_cmp(method, string_new("PUT")) == 0) {
    assert(body.len < LONG_MAX);
    rv = curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, (long) body.len);
    if (rv != CURLE_OK) {
      errmsg_fmt("CURLOPT_POSTFIELDSIZE error: %s", curl_easy_strerror(rv));
      return E_ERR;
    }
    rv = curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.buf);
    if (rv != CURLE_OK) {
      errmsg_fmt("CURLOPT_POSTFIELDS error: %s", curl_easy_strerror(rv));
      return E_ERR;
    }
  }

  // authentication
  if (authenticated) {
    rv = curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
    if (rv != CURLE_OK) {
      errmsg_fmt("CURLOPT_HTTPAUTH error: %s", curl_easy_strerror(rv));
      return E_ERR;
    }

    mutex_lock(&sso_access_token_mu, 5);
    rv = curl_easy_setopt(handle, CURLOPT_XOAUTH2_BEARER, sso_access_token);
    if (rv != CURLE_OK) {
      errmsg_fmt("CURLOPT_XOAUTH2_BEARER error: %s", curl_easy_strerror(rv));
      return E_ERR;
    }
    mutex_unlock(&sso_access_token_mu);
  }

  return E_OK;
}

err_t esi_perform_request(CURL *handle, struct string *body, size_t *pages,
                          int trails) {
  err_t err = E_ERR;
  CURLcode rv;
  CURLHcode hrv;
  FILE *body_file = NULL;
  // WARN: do not call return passed this line, set `err` and goto cleanup

  rv = curl_easy_setopt(handle, CURLOPT_TIMEOUT, ESI_REQUEST_TIMEOUT);
  if (rv != CURLE_OK) {
    errmsg_fmt("CURLOPT_TIMEOUT error: %s", curl_easy_strerror(rv));
    goto cleanup;
  }

  while (trails > 0) {
    trails -= 1;

    // wait for the api to be clear of any timeout
    esi_timeout_clear();

    // reset body_file
    if (body_file != NULL) {
      fclose(body_file);
      string_destroy(body);
      body_file = NULL;
    }

    // create body memstream
    body_file = open_memstream(&body->buf, &body->len);
    if (body_file == NULL) {
      errmsg_fmt("open_memstream: %s", strerror(errno));
      goto cleanup;
    }
    rv = curl_easy_setopt(handle, CURLOPT_WRITEDATA, body_file);
    if (rv != CURLE_OK) {
      errmsg_fmt("CURLOPT_WRITEDATA error: %s", curl_easy_strerror(rv));
      goto cleanup;
    }

    // do perform the request
    rv = curl_easy_perform(handle);    
    if (rv != CURLE_OK) {
      errmsg_fmt("curl_easy_perform: %s", curl_easy_strerror(rv));
      continue;
    }

    long res_code;
    rv = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &res_code);
    if (rv != CURLE_OK) {
      errmsg_fmt("CURLINFO_RESPONSE_CODE error: %s", curl_easy_strerror(rv));
      continue;
    }

    // implicit timeout
    if (res_code == 500 || res_code == 503) {
      esi_timeout_set(20);

      log_print("esi_fetch: 20s implicit timeout %d", res_code);
      errmsg_fmt("20s implicit timeout %d", res_code);
      continue;
    }

    // request rate timeout
    if (res_code == 429) {
      // NOTE: one day, CCP will maybe add a `Retry-After` header to their
      // responses..
      esi_timeout_set(20);

      log_print("esi_fetch: 20s implicit timeout %d", res_code);
      errmsg_fmt("20s implicit timeout %d", res_code);
      continue;
    }

    // error rate timeout
    if (res_code == 420) {
      long timeout_secs;
      struct curl_header *timeout_header;
      hrv = curl_easy_header(handle, "X-Esi-Error-Limit-Reset", 0,
                             CURLH_HEADER, -1, &timeout_header);

      if (hrv != CURLHE_OK || timeout_header->value[0] == '\0') {
        log_warn("esi_fetch: X-Esi-Error-Limit-Reset header is not present");
        timeout_secs = 20;
      } else {
        char *endptr;
        timeout_secs = strtol(timeout_header->value, &endptr, 10);
        if (*endptr != '\0') {  // if header value is not an valid long
          log_warn("esi_fetch: X-Esi-Error-Limit-Reset header \"%s\" is not a valid integer", timeout_header->value);
          timeout_secs = 20;
        } else if (timeout_secs <= 0 || timeout_secs > 120) {
          log_warn("esi_fetch: X-Esi-Error-Limit-Reset header \"%s\" is out of range", timeout_header->value);
          timeout_secs = 20;
        }
      }

      esi_timeout_set(timeout_secs);

      log_print("esi_fetch: %ds explicit timeout %d", timeout_secs, res_code);
      errmsg_fmt("%ds explicit timeout %d", timeout_secs, res_code);
      continue;
    }

    // gateway timeout
    if (res_code == 504) {
      int frv = fflush(body_file);
      if (frv != 0) {
        errmsg_fmt("fflush: %s", strerror(errno));
        goto cleanup;
      }

      int timeout_secs;
      err_t err = esi_parse_error_timeout(*body, &timeout_secs);
      if (err != E_OK) {
        struct string err = errmsg_get();
        log_warn("esi_fetch: can't decode esi timeout: %.*s", (int) err.len, err.buf);
        timeout_secs = 20;
      }

      esi_timeout_set(timeout_secs);

      log_print("esi_fetch: %ds explicit timeout %d", timeout_secs, res_code);
      errmsg_fmt("%ds explicit timeout %d", timeout_secs, res_code);
      continue;
    }

    // esi error
    if (res_code != 200) {
      int frv = fflush(body_file);
      if (frv != 0) {
        errmsg_fmt("fflush: %s", strerror(errno));
        goto cleanup;
      }

      struct string message;
      err_t err = esi_parse_error_message(*body, &message);
      if (err == E_OK) {
        errmsg_fmt("esi error json: %.*s", (int) message.len, message.buf);
      } else {
        errmsg_fmt("esi error: %.*s", (int) body->len, body->buf);
      }

      string_destroy(&message);
      err = E_ESI_ERR;
      goto cleanup;
    }
    
    // parse X-Pages header
    *pages = 0;
    CURLHcode hrv;
    struct curl_header *pages_header;
    hrv = curl_easy_header(handle, "X-Pages", 0, CURLH_HEADER,
                           -1, &pages_header);
    if (hrv == CURLHE_OK && pages_header->value[0] != '\0') {
      char *endptr;
      long pages_long = strtol(pages_header->value, &endptr, 10);
      if (*endptr != '\0') {  // if header value is not an valid long
        log_warn("esi_fetch: X-Pages \"%s\" is not a valid int", pages_header->value);
      } else if (pages_long < 0 || pages_long > 10000) {
        log_warn("esi_fetch: X-Pages \"%s\" is out of range", pages_header->value);
      } else {
        *pages = pages_long;
      }
    }

    assert(res_code == 200);
    err = E_OK;
    goto cleanup;
  }

  // if you end up there, that mean you ran out of trails
  errmsg_prefix("out of trails: ");

cleanup:
  if (body_file != NULL) fclose(body_file);
  if (err != E_OK) string_destroy(body);
  return err;
}

// WARN: Yo then need to destroy the returned `esi_response`
err_t esi_fetch(
  struct esi_response *response,
  struct string method,
  struct string uri,
  struct string body,
  bool authenticated,
  int trails
) {
  assert(response != NULL);
  assert(method.len > 0);
  assert(uri.len > 0);
  assert(trails > 0);

  // Build the request upon the thread local handle
  assert(esi_curl_thread_handle != NULL);
  err_t err = esi_build_request(esi_curl_thread_handle, method, uri,
                                body, authenticated);
  if (err != E_OK) {
    errmsg_prefix("esi_build_request: ");
    return E_ERR;
  }

  err = esi_perform_request(esi_curl_thread_handle, &response->body,
                            &response->pages, trails);
  if (err != E_OK) {
    errmsg_prefix("esi_perform_request: ");
    return E_ERR;
  }

  return E_OK;
}
