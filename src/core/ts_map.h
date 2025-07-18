#ifndef TS_MAP_H
#define TS_MAP_H

#include <stdbool.h>

typedef enum task_state {
  TASK_UNKNOWN = 0,
  TASK_CLEAN = 1,
  TASK_STALE = 2
} task_state;

typedef struct ts_map ts_map;

ts_map* ts_map_new(void);
void ts_map_delete(ts_map* ts);
task_state ts_map_get(ts_map* ts, const char* name);
bool ts_map_set(ts_map* ts, const char* name, task_state state);

#endif
