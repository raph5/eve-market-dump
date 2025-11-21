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
#include <sys/semaphore.h>

void test_zeroed_vec(void) {
  struct size_vec vec = {0};
  size_vec_push(&vec, 123);
  assert(vec.len == 1);
  assert(vec.cap == 4);
  assert(vec.buf[0] == 123);
  assert(size_vec_get(&vec, 0) == 123);
  assert(*size_vec_getp(&vec, 0) == 123);
  size_vec_destroy(&vec);
}

void test_zeroed_vec_with_cap(void) {
  struct size_vec vec = { .cap = 16 };
  size_vec_push(&vec, 123);
  assert(vec.len == 1);
  assert(vec.cap == 16);
  assert(vec.buf[0] == 123);
  assert(size_vec_get(&vec, 0) == 123);
  assert(*size_vec_getp(&vec, 0) == 123);
  size_vec_destroy(&vec);
}

void test_order_download_page(void) {
  struct order_vec vec = {0};
  size_t page_count;
  err_t err = order_download_page(&vec, 10000002, 1, &page_count);
  assert(err == E_OK);
  assert(page_count > 10);  // we are in jita, there should be more that 10 pages
  assert(vec.len == 1000);
  assert(vec.buf[0].region_id == 10000002);

  printf("sample order: ");
  order_print(vec.buf);
}

void test_order_download_universe(void) {
  uint64_t regions[] = {10000058, 10000060};
  size_t regions_len = sizeof(regions) / sizeof(*regions);

  size_t r1_page_count;
  struct order_vec r1_vec = {0};
  err_t err = order_download_page(&r1_vec, regions[0], 1, &r1_page_count);
  assert(err == E_OK);

  size_t r2_page_count;
  struct order_vec r2_vec = {0};
  err = order_download_page(&r2_vec, regions[1], 1, &r2_page_count);
  assert(err == E_OK);

  struct order_vec vec = {0};
  err = order_download_universe(&vec, regions, regions_len);
  assert(err == E_OK);
  assert((r1_page_count + r2_page_count) * 1000 - 2000 < vec.len);
  assert((r1_page_count + r2_page_count) * 1000 >= vec.len);

  printf("sample order: ");
  order_print(vec.buf);
}

void test_unsafe_ptr_fifo(void) {
  struct unsafe_ptr_fifo fifo = { .cap = 5 };
  unsafe_ptr_fifo_push(&fifo, (void *) 1);
  unsafe_ptr_fifo_push(&fifo, (void *) 2);
  unsafe_ptr_fifo_push(&fifo, (void *) 3);
  unsafe_ptr_fifo_push(&fifo, (void *) 4);
  assert(unsafe_ptr_fifo_pop(&fifo) == (void *) 1);
  assert(unsafe_ptr_fifo_pop(&fifo) == (void *) 2);
  assert(unsafe_ptr_fifo_pop(&fifo) == (void *) 3);
  assert(unsafe_ptr_fifo_pop(&fifo) == (void *) 4);
  unsafe_ptr_fifo_push(&fifo, (void *) 5);
  unsafe_ptr_fifo_push(&fifo, (void *) 6);
  unsafe_ptr_fifo_push(&fifo, (void *) 7);
  unsafe_ptr_fifo_push(&fifo, (void *) 8);
  unsafe_ptr_fifo_push(&fifo, (void *) 9);
  assert(unsafe_ptr_fifo_pop(&fifo) == (void *) 5);
  assert(unsafe_ptr_fifo_pop(&fifo) == (void *) 6);
  assert(unsafe_ptr_fifo_pop(&fifo) == (void *) 7);
  assert(unsafe_ptr_fifo_pop(&fifo) == (void *) 8);
  assert(unsafe_ptr_fifo_pop(&fifo) == (void *) 9);
}

// WARN: make sure to change the srand seed after a failed test to avoid
// semaphore name conflicts
void test_ptr_fifo(void) {
  srand(3);
  struct ptr_fifo fifo = {0};
  void *ptr;
  assert(ptr_fifo_init(&fifo, 5) == E_OK);
  ptr_fifo_destroy(&fifo);
  assert(ptr_fifo_init(&fifo, 5) == E_OK);
  assert(ptr_fifo_push(&fifo, (void *) 1, 1) == E_OK);
  assert(ptr_fifo_push(&fifo, (void *) 2, 1) == E_OK);
  assert(ptr_fifo_push(&fifo, (void *) 3, 1) == E_OK);
  assert(ptr_fifo_push(&fifo, (void *) 4, 1) == E_OK);
  assert(ptr_fifo_pop(&fifo, &ptr, 1) == E_OK);
  assert(ptr == (void *) 1);
  assert(ptr_fifo_pop(&fifo, &ptr, 1) == E_OK);
  assert(ptr == (void *) 2);
  assert(ptr_fifo_pop(&fifo, &ptr, 1) == E_OK);
  assert(ptr == (void *) 3);
  assert(ptr_fifo_pop(&fifo, &ptr, 1) == E_OK);
  assert(ptr == (void *) 4);
  assert(ptr_fifo_push(&fifo, (void *) 5, 1) == E_OK);
  assert(ptr_fifo_push(&fifo, (void *) 6, 1) == E_OK);
  assert(ptr_fifo_push(&fifo, (void *) 7, 1) == E_OK);
  assert(ptr_fifo_push(&fifo, (void *) 8, 1) == E_OK);
  assert(ptr_fifo_push(&fifo, (void *) 9, 1) == E_OK);
  assert(ptr_fifo_pop(&fifo, &ptr, 1) == E_OK);
  assert(ptr == (void *) 5);
  assert(ptr_fifo_pop(&fifo, &ptr, 1) == E_OK);
  assert(ptr == (void *) 6);
  assert(ptr_fifo_pop(&fifo, &ptr, 1) == E_OK);
  assert(ptr == (void *) 7);
  assert(ptr_fifo_pop(&fifo, &ptr, 1) == E_OK);
  assert(ptr == (void *) 8);
  assert(ptr_fifo_pop(&fifo, &ptr, 1) == E_OK);
  assert(ptr == (void *) 9);
  ptr_fifo_destroy(&fifo);
}

// TODO: test dump_record

int main(void) {
  printf("---------- TEST START ----------\n");

  printf("---------- test_unsafe_ptr_fifo ----------\n");
  test_unsafe_ptr_fifo();
  printf("---------- test_ptr_fifo ----------\n");
  test_ptr_fifo();
  printf("---------- test_zeroed_vec ----------\n");
  test_zeroed_vec();
  // TODO: remove
  return 0;

  printf("---------- test_order_download_page ----------\n");
  test_order_download_page();
  printf("---------- test_order_download_universe ----------\n");
  test_order_download_universe();

  printf("---------- TEST SUCCESS ----------\n");
}
