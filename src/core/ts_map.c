#include <stdio.h>
#include <stdlib.h>

#include "ts_map.h"

#include "../../lib/klib/khash.h"
#include <string.h>

KHASH_MAP_INIT_STR(task_memo_by_name, task_state);

struct ts_map {
  khash_t(task_memo_by_name)* map;
};

struct ts_map* ts_map_new(void) {
  struct ts_map* ts = malloc(sizeof(*ts));
  khash_t(task_memo_by_name)* map = kh_init_task_memo_by_name();
  ts->map = map;
  return ts;
}


void ts_map_delete(struct ts_map* ts) {
  kh_destroy_task_memo_by_name(ts->map);
  free(ts);
}

task_state ts_map_get(struct ts_map* ts, const char* name) {
  khint_t k = kh_get_task_memo_by_name(ts->map, name);

  if (k != kh_end(ts->map)) {
    return kh_value(ts->map, k);
  }

  return -1;
}

bool ts_map_set(ts_map* ts, const char* name, task_state state) {
  int ret;
  khint_t k = kh_put_task_memo_by_name(ts->map, strdup(name), &ret);

  if (ret == -1) {
    fprintf(stderr, "[dagwood] out of memory inserting memoized task state\n");
    return false;
  }

  kh_value(ts->map, k) = state;
  return true;
}
