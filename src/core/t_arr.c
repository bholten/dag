#include "t_arr.h"
#include "../../lib/klib/kvec.h"

struct t_arr {
  kvec_t(dagwood_task*) vec;
};

t_arr* t_arr_new() {
  struct t_arr* arr = malloc(sizeof(*arr));

  if (!arr) return NULL;

  kv_init(arr->vec);
  return arr;
}

void t_arr_delete(t_arr* arr) {
  if (!arr) return;
  kv_destroy(arr->vec);
  free(arr);
}

void t_arr_push(t_arr* arr, dagwood_task* task) {
  kv_push(dagwood_task*, arr->vec, task);
}

dagwood_task* t_arr_pop(t_arr* arr) {
  return kv_pop(arr->vec);
}

dagwood_task *t_arr_get(t_arr *arr, size_t index) {
  if (index >= kv_size(arr->vec)) return NULL;
  return kv_A(arr->vec, index);
}

size_t t_arr_len(t_arr* arr) {
  return kv_size(arr->vec);
}
