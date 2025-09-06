#include "base.c"
#include "secrets.c"
#include "esi.c"
#include "server.c"

// by opposition with thread_init
err_t global_init(void) {
  CURLcode rv = curl_global_init(CURL_GLOBAL_ALL);
  if (rv) {
    errmsg_fmt("curl_global_init: error %d", (int) rv);
    return E_ERR;
  }
  secret_table_create();
  return E_OK;
}

// by opposition with thread_deinit
// TODO: make sure global_deinit is called at every way out of the program
// possible
void global_deinit(void) {
  curl_global_cleanup();
  secret_table_destroy();
}

int main(int argc, char *argv[]) {
  err_t err = global_init();
  if (err != E_OK) {
    log_error("global_init");
    errmsg_print();
    return 1;
  }

  struct args args;
  err = args_parse(argc, argv, &args);
  if (err == E_ERR) {
    return 1;
  }

  err = secret_table_parse(args.secrets);
  if (err == E_ERR) {
    log_error("secret_table_parse");
    errmsg_print();
    return 1;
  }

  // TEST:
  esi_thread_init();
  struct esi_response res = {};
  err = esi_fetch(&res, string_new("GET"), string_new("/universe/structures/1042499803831"), (struct string) {}, true, 1);
  if (err != E_OK) {
    errmsg_prefix("esi_fetch: ");
    errmsg_print();
  }
  printf("pages: %zu\n", res.pages);
  printf("body:\n\n%.*s\n", (int) res.body.len, res.body.buf);
  esi_response_destroy(&res);
  esi_thread_deinit();

  global_deinit();
  return 0;
}
