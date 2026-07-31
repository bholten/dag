#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dagwood.h"
#include "data.h"

STR_MAP_IMPL(idx_map, size_t, SIZE_MAX, "index")

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
