#include <stdbool.h>
#include <stdlib.h>

#include "data.h"

struct t_arr {
  dagwood_task **data;
  size_t len;
  size_t cap;
};

t_arr *t_arr_new(void) {
  struct t_arr *arr = malloc(sizeof(*arr));

  if (!arr) {
    return NULL;
  }

  arr->data = NULL;
  arr->len = 0;
  arr->cap = 0;
  return arr;
}

void t_arr_delete(t_arr *arr) {
  if (!arr) {
    return;
  }

  free(arr->data);
  free(arr);
}

bool t_arr_push(t_arr *arr, const dagwood_task *task) {
  if (arr->len >= arr->cap) {
    size_t new_cap = arr->cap == 0 ? 4 : arr->cap * 2;
    dagwood_task **new_data = realloc(arr->data, new_cap * sizeof(*new_data));

    if (!new_data) {
      return false;
    }

    arr->data = new_data;
    arr->cap = new_cap;
  }

  arr->data[arr->len++] = (dagwood_task *)task;

  return true;
}

dagwood_task *t_arr_pop(t_arr *arr) {
  if (arr->len == 0) {
    return NULL;
  }

  return arr->data[--arr->len];
}

dagwood_task *t_arr_get(t_arr *arr, size_t index) {
  if (index >= arr->len) {
    return NULL;
  }

  return arr->data[index];
}

size_t t_arr_len(t_arr *arr) {
  return arr->len;
}
