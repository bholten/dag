#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dagwood.h"
#include "data.h"

STR_MAP_IMPL(idx_map, size_t, SIZE_MAX, "index")

static bool dag_validate_scoped(t_arr *arr, size_t *scoped_in_degree) {
  for (size_t e = 0; e < t_arr_len(arr); e++) {
    if (scoped_in_degree[e] > 0) {
      dagwood_task *t = t_arr_get(arr, e);
      fprintf(stderr, "[dagwood] cycle detected involving task: %s\n", t->name);
      return false;
    }
  }
  return true;
}

bool dag_build(t_arr *arr, t_layers *out) {
  size_t n = t_arr_len(arr);

  if (n == 0) {
    return true;
  }

  size_t *local_in_degree = calloc(n, sizeof(size_t));

  if (!local_in_degree) {
    return false;
  }

  for (size_t i = 0; i < n; i++) {
    dagwood_task *task = t_arr_get(arr, i);
    local_in_degree[i] = task->in_degree;
  }

  idx_map *index_of = idx_map_new();

  if (!index_of) {
    free(local_in_degree);
    return false;
  }

  t_arr *queue = t_arr_new();

  for (size_t i = 0; i < n; i++) {
    dagwood_task *task = t_arr_get(arr, i);
    idx_map_set(index_of, task->id, i);

    if (local_in_degree[i] == 0) {
      t_arr_push(queue, task);
    }
  }

  size_t processed = 0;

  while (t_arr_len(queue) > 0) {
    t_arr *layer = t_arr_new();
    t_arr *next_queue = t_arr_new();

    for (size_t i = 0; i < t_arr_len(queue); i++) {
      dagwood_task *task = t_arr_get(queue, i);
      t_arr_push(layer, task);
      processed++;

      for (size_t j = 0; j < t_arr_len(task->reverse_edges); j++) {
        dagwood_task *dependent = t_arr_get(task->reverse_edges, j);
        size_t k = idx_map_get(index_of, dependent->id);

        if (k != SIZE_MAX) {
          local_in_degree[k]--;

          if (local_in_degree[k] == 0) {
            t_arr_push(next_queue, dependent);
          }
        }
      }
    }

    t_layers_push(out, layer);
    t_arr_delete(queue);
    queue = next_queue;
  }

  t_arr_delete(queue);
  idx_map_delete(index_of);

  if (processed < n) {
    for (size_t i = 0; i < n; i++) {
      if (local_in_degree[i] > 0) {
        dagwood_task *t = t_arr_get(arr, i);
        fprintf(stderr, "[dagwood] cycle detected involving task: %s\n",
                t->name);
      }
    }

    free(local_in_degree);

    return false;
  }

  free(local_in_degree);
  return true;
}

static void collect_dependencies(const dagwood_task *task, t_map *out) {
  if (t_map_exists(out, task->id)) {
    return;
  }
  t_map_set(out, task->id, task);

  for (size_t i = 0; i < t_arr_len(task->edges); i++) {
    dagwood_task *dep = t_arr_get(task->edges, i);
    collect_dependencies(dep, out);
  }
}

bool dag_build_from_task(const dagwood_task *task, t_layers *out) {
  t_map *scope = t_map_new();
  collect_dependencies(task, scope);
  t_arr *targets = t_arr_new();

  for (size_t i = t_map_begin(scope); i < t_map_end(scope); i++) {
    dagwood_task *t = t_map_value(scope, i);

    if (!t) {
      continue;
    }

    t_arr_push(targets, t);
  }

  size_t n = t_arr_len(targets);

  if (n == 0) {
    t_map_delete(scope);
    t_arr_delete(targets);
    return true;
  }

  size_t *scoped_in_degree = calloc(n, sizeof(size_t));

  if (!scoped_in_degree) {
    t_map_delete(scope);
    t_arr_delete(targets);
    return false;
  }

  for (size_t i = 0; i < n; i++) {
    dagwood_task *t = t_arr_get(targets, i);

    for (size_t j = 0; j < t_arr_len(t->edges); j++) {
      dagwood_task *dep = t_arr_get(t->edges, j);

      if (t_map_exists(scope, dep->id)) {
        scoped_in_degree[i]++;
      }
    }
  }

  idx_map *index_of = idx_map_new();

  if (!index_of) {
    free(scoped_in_degree);
    t_map_delete(scope);
    t_arr_delete(targets);
    return false;
  }

  t_arr *queue = t_arr_new();

  for (size_t i = 0; i < n; i++) {
    dagwood_task *t = t_arr_get(targets, i);
    idx_map_set(index_of, t->id, i);

    if (scoped_in_degree[i] == 0) {
      t_arr_push(queue, t);
    }
  }

  while (t_arr_len(queue) > 0) {
    t_arr *layer = t_arr_new();
    t_arr *next_queue = t_arr_new();

    for (size_t i = 0; i < t_arr_len(queue); i++) {
      dagwood_task *t = t_arr_get(queue, i);
      t_arr_push(layer, t);

      for (size_t j = 0; j < t_arr_len(t->reverse_edges); j++) {
        dagwood_task *dependent = t_arr_get(t->reverse_edges, j);
        size_t k = idx_map_get(index_of, dependent->id);

        if (k == SIZE_MAX) {
          continue;
        }

        scoped_in_degree[k]--;

        if (scoped_in_degree[k] == 0) {
          t_arr_push(next_queue, dependent);
        }
      }
    }

    t_layers_push(out, layer);
    t_arr_delete(queue);
    queue = next_queue;
  }

  bool valid = dag_validate_scoped(targets, scoped_in_degree);

  t_arr_delete(queue);
  idx_map_delete(index_of);
  free(scoped_in_degree);
  t_map_delete(scope);
  t_arr_delete(targets);

  return valid;
}
