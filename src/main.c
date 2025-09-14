#include "base.c"
#include "secrets.c"
#include "csv.c"
#include "locations.c"
#include "esi.c"
#include "server.c"
#include "hoardling.c"

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
    errmsg_prefix("global_init: ");
    errmsg_print();
    return 1;
  }

  struct args args;
  err = args_parse(argc, argv, &args);
  if (err != E_OK) {
    return 1;
  }

  err = secret_table_parse(args.secrets);
  if (err != E_OK) {
    errmsg_prefix("secret_table_parse: ");
    errmsg_print();
    return 1;
  }

  hoardling_locations(NULL);

  global_deinit();
  return 0;
}
