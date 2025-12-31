#ifndef DATA_H
#define DATA_H

#include <stdbool.h>
#include <stddef.h>

typedef struct s_arr s_arr;

s_arr *s_arr_new();
void s_arr_delete(s_arr *arr);
void s_arr_push(s_arr *arr, const char *str);
size_t s_arr_len(s_arr *arr);
const char *s_arr_get(s_arr *arr, size_t index);
const char *s_arr_pop(s_arr *arr);
s_arr *s_arr_copy(s_arr *arr);

typedef struct dagwood_task dagwood_task;
typedef struct t_arr t_arr;

t_arr *t_arr_new();
void t_arr_delete(t_arr *arr);
void t_arr_push(t_arr *arr, const dagwood_task *task);
size_t t_arr_len(t_arr *arr);
dagwood_task *t_arr_get(t_arr *arr, size_t index);
dagwood_task *t_arr_pop(t_arr *arr);

typedef struct t_layers t_layers;

t_layers *t_layers_new();
void t_layers_delete(t_layers *layers);
void t_layers_push(t_layers *layers, const t_arr *layer);
size_t t_layers_len(t_layers *layers);
t_arr *t_layers_get(t_layers *layers, size_t index);
t_arr *t_layers_pop(t_layers *layers);

bool dag_build(t_arr *arr, t_layers *out);
bool dag_build_from_task(const dagwood_task *task, t_layers *out);

typedef struct t_map_entity t_map_entry;
typedef struct t_map t_map;

t_map *t_map_new(void);
void t_map_delete(t_map *table);
dagwood_task *t_map_get(t_map *table, const char *name);
bool t_map_set(t_map *table, const char *name, const dagwood_task *task);
bool t_map_exists(t_map *table, const char *name);
size_t t_map_begin(t_map *p);
size_t t_map_end(t_map *p);
dagwood_task *t_map_value(t_map *p, size_t index);

typedef enum task_state {
  TASK_UNKNOWN = 0,
  TASK_CLEAN = 1,
  TASK_STALE = 2
} task_state;

typedef struct ts_map ts_map;

ts_map *ts_map_new(void);
void ts_map_delete(ts_map *ts);
task_state ts_map_get(ts_map *ts, const char *name);
bool ts_map_set(ts_map *ts, const char *name, const task_state state);

typedef struct dagwood_project dagwood_project;
typedef struct p_map p_map;

p_map *p_map_new(void);
void p_map_delete(p_map *p);
dagwood_project *p_map_get(p_map *p, const char *name);
bool p_map_set(p_map *p, const char *name, const dagwood_project *project);
size_t p_map_begin(p_map *p);
size_t p_map_end(p_map *p);
dagwood_project *p_map_value(p_map *p, size_t index);

#endif
