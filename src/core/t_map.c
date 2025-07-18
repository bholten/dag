#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t_map.h"
#include "../../lib/klib/khash.h"

KHASH_MAP_INIT_STR(task_by_name, struct dagwood_task *);

struct t_map_entry {
  const char* name;
  dagwood_task* task;
};

struct t_map {
  khash_t(task_by_name) *map;
};

t_map* t_map_new(void) {
  struct t_map* table = malloc(sizeof(*table));
  khash_t(task_by_name) *map = kh_init_task_by_name();
  table->map = map;
  return table;
}

void t_map_delete(t_map* table) {
  kh_destroy_task_by_name(table->map);
  free(table);
}

dagwood_task* t_map_get(t_map* table, const char* name) {
  khint_t k = kh_get_task_by_name(table->map, name);

  if (k != kh_end(table->map)) {
    return kh_value(table->map, k);
  }

  return NULL;
}

bool t_map_set(t_map* table, const char* name, dagwood_task* task) {
  assert(task != NULL);

  int ret;
  khint_t k = kh_put_task_by_name(table->map, strdup(name), &ret);

  if (ret == -1) {
    fprintf(stderr, "[dagwood] out of memory inserting task\n");
    return false;
  }

  kh_value(table->map, k) = task;
  return true;
}
