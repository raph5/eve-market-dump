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

// cli business
const char* MAN =
"NAME\n"
"\temd - Eve Market Dump\n"
"\n"
"SYNOPSIS\n"
"\temd [options]\n"
"\n"
"OPTIONS\n"
"\t--secrets STRING\n"
"\t\tJson string containing the secrets in foramt {key: value} (default \"{}\")\n"
"\t--dump_dir STRING\n"
"\t\tThe directory in which dumps will be created (default \".\")\n"
"\t--history BOOLEAN\n"
"\t\tEnable histories update (default true)\n"
"\t--structure BOOLEAN\n"
"\t\tEnable fetching of public player structures (requires ssoClientId, ssoClientSecret and ssoRefreshToken secrets) (default true)\n";

struct args {
  struct string secrets;
  struct string dump_dir;
  bool history;
  bool structure;
};

err_t args_parse(int argc, char *argv[], struct args *args) {
  *args = (struct args) {
    .secrets = string_new("{}"),
    .dump_dir = string_new("."),
    .history = true,
    .structure = true,
  };

  struct option opt_table[] = {
    { .name = "secrets", .has_arg = required_argument },
    { .name = "dump_dir", .has_arg = required_argument },
    { .name = "history", .has_arg = optional_argument },
    { .name = "structure", .has_arg = optional_argument },
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
      case 2:
        if (asgs_prase_bool(&args->history, optarg) != E_OK) {
          printf("--history takes a BOOLEAN value\n\n%s", MAN);
          return E_ERR;
        }
        break;
      case 3:
        if (asgs_prase_bool(&args->structure, optarg) != E_OK) {
          printf("--structure takes a BOOLEAN value\n\n%s", MAN);
          return E_ERR;
        }
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

// should not be called, but just in case..
void handle_atexit(void) {
  log_error("atexit reached");
}

err_t global_init(void) {
  CURLcode crv = curl_global_init(CURL_GLOBAL_ALL);
  if (crv != CURLE_OK) {
    errmsg_fmt("curl_global_init: error %d", (int) crv);
    return E_ERR;
  }
  err_t err = timezone_set("GMT");
  if (err != E_OK) {
    errmsg_prefix("timezone_set: ");
    return E_ERR;
  }
  secret_table_create();
  srand(time(NULL));
  int rv = atexit(handle_atexit);
  if (rv != 0) {
    errmsg_fmt("atexit: %s", strerror(errno));
    return E_ERR;
  }
  return E_OK;
}

// WARN: `global_cleanup` symbol is defined at the top of base.c so it can
// be called by panic and assert. If you want to rename `global_cleanup` you
// need to rename the definition in base.c also.
void global_cleanup(void) {
  curl_global_cleanup();
  dump_record_burn();
}

// NOTE: the indended to way exit the main function is to receive a SIGINT/SIGTERM
int main(int argc, char *argv[]) {
  err_t err = global_init();
  if (err != E_OK) {
    errmsg_prefix("global_init: ");
    goto print_error_and_exit;
  }

  struct args args;
  err = args_parse(argc, argv, &args);
  if (err != E_OK) return 1;

  err = secret_table_parse(args.secrets);
  if (err != E_OK) {
    errmsg_prefix("secret_table_parse: ");
    goto print_error_and_exit;
  }

  struct ptr_fifo chan_orders_to_locations = {0};
  struct ptr_fifo active_market_request = {0};
  struct ptr_fifo active_market_response = {0};
  err = ptr_fifo_init(&chan_orders_to_locations, 32);
  if (err != E_OK) {
    errmsg_prefix("ptr_fifo_init: ");
    goto print_error_and_exit;
  }
  err = ptr_fifo_init(&active_market_request, 32);
  if (err != E_OK) {
    errmsg_prefix("ptr_fifo_init: ");
    goto print_error_and_exit;
  }
  err = ptr_fifo_init(&active_market_response, 32);
  if (err != E_OK) {
    errmsg_prefix("ptr_fifo_init: ");
    goto print_error_and_exit;
  }

  // first block sigint and sigterm so worker threads inherit from that sigmask
  sigset_t blocker_mask;
  sigemptyset(&blocker_mask);
  sigaddset(&blocker_mask, SIGINT);
  sigaddset(&blocker_mask, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &blocker_mask, NULL);

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  // start worker threads
  pthread_t hoardling_orders_thread;
  struct hoardling_orders_args hoardling_orders_args = {
    .dump_dir = args.dump_dir,
    .history = args.history,
    .structure = args.structure,
    .chan_orders_to_locations = &chan_orders_to_locations,
    .active_market_request = &active_market_request,
    .active_market_response = &active_market_response,
  };
  int rv = pthread_create(&hoardling_orders_thread, NULL, hoardling_orders,
                      &hoardling_orders_args);
  if (rv != 0) {
    errmsg_fmt("pthread_create: %s", strerror(errno));
    errmsg_print();
    return 1;
  }

  struct hoardling_locations_args hoardling_locations_args = {
    .dump_dir = args.dump_dir,
    .chan_orders_to_locations = &chan_orders_to_locations,
  };
  pthread_t hoardling_locations_thread;
  if (args.structure) {
    rv = pthread_create(&hoardling_locations_thread, NULL, hoardling_locations,
                        &hoardling_locations_args);
    if (rv != 0) {
      errmsg_fmt("pthread_create: %s", strerror(errno));
      errmsg_print();
      return 1;
    }
  }

  struct hoardling_histories_args hoardling_histories_args = {
    .dump_dir = args.dump_dir,
    .active_market_request = &active_market_request,
    .active_market_response = &active_market_response,
  };
  pthread_t hoardling_histories_thread;
  if (args.history) {
    rv = pthread_create(&hoardling_histories_thread, NULL, hoardling_histories,
                        &hoardling_histories_args);
    if (rv != 0) {
      errmsg_fmt("pthread_create: %s", strerror(errno));
      goto print_error_and_exit;
    }
  }

  int sig;
  sigwait(&blocker_mask, &sig);
  // note that here sigint and sigterm are still blocked
  // I could unblock them but I don't realy need to

  rv = pthread_kill(hoardling_orders_thread, SIGTERM);
  if (rv != 0 && rv != ESRCH) log_error("orders hoardling thread kill failed: %s", strerror(errno));
  if (args.structure) {
    rv = pthread_kill(hoardling_locations_thread, SIGTERM);
    if (rv != 0 && rv != ESRCH) log_error("locations hoardling thread kill failed: %s", strerror(errno));
  }
  if (args.history) {
    rv = pthread_kill(hoardling_histories_thread, SIGTERM);
    if (rv != 0 && rv != ESRCH) log_error("histories hoardling thread kill failed: %s", strerror(errno));
  }

  global_cleanup();
  log_print("graceful exit");
  return 0;

print_error_and_exit:
  errmsg_print();
  return 1;
}
