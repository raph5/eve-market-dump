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

void handle_sigint_sigterm(int _) {
  exit(1);
}

void global_deinit(void) {
  curl_global_cleanup();
}

err_t global_init(void) {
  atexit(global_deinit);
  struct sigaction action = {
    .sa_handler = handle_sigint_sigterm,
    .sa_mask = 0,
    .sa_flags = 0,
  };
  int rv = sigaction(SIGINT, &action, NULL);
  if (rv != 0) {
    errmsg_fmt("sigaction: %s", strerror(errno));
    return E_ERR;
  }
  rv = sigaction(SIGTERM, &action, NULL);
  if (rv != 0) {
    errmsg_fmt("sigaction: %s", strerror(errno));
    return E_ERR;
  }

  CURLcode crv = curl_global_init(CURL_GLOBAL_ALL);
  if (crv != CURLE_OK) {
    errmsg_fmt("curl_global_init: error %d", (int) rv);
    return E_ERR;
  }
  err_t err = timezone_set("GMT");
  if (err != E_OK) {
    errmsg_prefix("timezone_set: ");
    return E_ERR;
  }
  secret_table_create();
  srand(time(NULL));
  return E_OK;
}

// NOTE: the indended to exit the main function is to receive a SIGINT/SIGTERM
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

  struct ptr_fifo chan_orders_to_locations = {0};
  err = ptr_fifo_init(&chan_orders_to_locations, 32);
  if (err != E_OK) {
    errmsg_prefix("ptr_fifo_init: ");
    errmsg_print();
    return 1;
  }

  pthread_attr_t attr;
  int rv = pthread_attr_init(&attr);
  if (rv != 0) {
    errmsg_fmt("pthread_attr_init: %s", strerror(errno));
    errmsg_print();
    return 1;
  }
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  pthread_t hoardling_locations_thread;
  struct hoardling_locations_args hoardling_locations_args = {
    .dump_dir = args.dump_dir,
    .chan_orders_to_locations = &chan_orders_to_locations,
  };
  rv = pthread_create(&hoardling_locations_thread, NULL, hoardling_locations,
                      &hoardling_locations_args);
  if (rv != 0) {
    errmsg_fmt("pthread_create: %s", strerror(errno));
    errmsg_print();
    return 1;
  }

  // main thread is used to run hoardling_orders
  struct hoardling_orders_args hoardling_orders_args = {
    .dump_dir = args.dump_dir,
    .chan_orders_to_locations = &chan_orders_to_locations,
  };
  hoardling_orders(&hoardling_orders_args);
}
