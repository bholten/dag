#ifndef T_MAP
#define T_MAP

#include <stdbool.h>

#include "task.h"

typedef struct t_map_entity t_map_entry;
typedef struct t_map t_map;

t_map* t_map_new(void);
void t_map_delete(t_map* table);
dagwood_task* t_map_get(t_map* table, const char* name);
bool t_map_set(t_map* table, const char* name, dagwood_task* task);

#endif
