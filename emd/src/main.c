#include "base.c"
#include "dump.c"
#include "secrets.c"
#include "csv.c"
#include "esi.c"
#include "regions.c"
#include "systems.c"
#include "locations.c"
#include "orders.c"
#include "histories.c"
#include "server.c"
#include "hoardling.c"

// cli
const char* MAN =
"NAME\n"
"\temd - Eve Market Dump\n"
"\n"
"SYNOPSIS\n"
"\temd [options]\n"
"\n"
"OPTIONS\n"
"\t--secrets STRING\n"
"\t\tjson string containing the secrets in foramt {key: value} (default \"{}\")\n"
"\t--dump_dir STRING\n"
"\t\tthe directory in which dumps will be created (default \".\")\n";

struct args {
  struct string secrets;
  struct string dump_dir;
};

err_t args_parse(int argc, char *argv[], struct args *args) {
  *args = (struct args) {
    .secrets = string_new("{}"),
    .dump_dir = string_new("."),
  };

  struct option opt_table[] = {
    { .name = "secrets", .has_arg = required_argument },
    { .name = "dump_dir", .has_arg = required_argument },
    { 0 },  // shitty api
  };

  while (true) {
    int opt_index;
    int rv = getopt_long(argc, argv, "", opt_table, &opt_index);
    if (rv == -1) {
      break;
    } else if (rv != 0) {
      printf("Unrecognized or malformed options\n\n%s", MAN);
      return E_ERR;
    }

    switch (opt_index) {
      case 0:
        args->secrets = string_new(optarg);
        break;
      case 1:
        args->dump_dir = string_new(optarg);
        break;
      default:
        panic("unreachable");
    }
  }

  if (optind < argc) {
    printf("Unrecognized or malformed options\n\n%s", MAN);
    return E_ERR;
  }
  return E_OK;
}

// by opposition with thread_init
err_t global_init(void) {
  CURLcode rv = curl_global_init(CURL_GLOBAL_ALL);
  if (rv) {
    errmsg_fmt("curl_global_init: error %d", (int) rv);
    return E_ERR;
  }
  err_t err = timezone_set("GMT");
  if (err != E_OK) {
    errmsg_prefix("timezone_set: ");
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

  struct hoardling_orders_args hoardling_orders_args = {
    .dump_dir = args.dump_dir,
  };
  hoardling_orders(&hoardling_orders_args);

  global_deinit();
  return 0;
}
