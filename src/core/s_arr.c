#include <stdlib.h>
#include <string.h>

#include "../../lib/klib/kvec.h"

#include "s_arr.h"

struct s_arr {
  kvec_t(const char*) vec;
};

s_arr* s_arr_new() {
  s_arr* arr = malloc(sizeof(*arr));

  if (!arr) return NULL;

  kv_init(arr->vec);
  return arr;
};


void s_arr_delete(s_arr *arr) {
  kv_destroy(arr->vec);
  free(arr);
}

void s_arr_push(s_arr *arr, const char* str) {
  const char* dup = strdup(str);
  kv_push(const char*, arr->vec, dup);
}

size_t s_arr_len(s_arr *arr) {
  return kv_size(arr->vec);
}

const char* s_arr_get(s_arr *arr, size_t index) {
  return kv_A(arr->vec, index);
}

const char* s_arr_pop(s_arr *arr) {
  return kv_pop(arr->vec);
}

s_arr* s_arr_copy(s_arr* arr) {
  s_arr* out = s_arr_new();
  kv_copy(const char*, arr->vec, out->vec);
  return out;
}
