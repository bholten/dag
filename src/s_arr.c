#include <stdlib.h>
#include <string.h>

#include "data.h"

struct s_arr {
  const char **data;
  size_t len;
  size_t cap;
};

s_arr *s_arr_new(void) {
  s_arr *arr = malloc(sizeof(*arr));

  if (!arr) {
    return NULL;
  }

  arr->data = NULL;
  arr->len = 0;
  arr->cap = 0;
  return arr;
}

void s_arr_delete(s_arr *arr) {
  if (!arr) {
    return;
  }
  free(arr->data);
  free(arr);
}

void s_arr_push(s_arr *arr, const char *str) {
  if (arr->len >= arr->cap) {
    size_t new_cap = arr->cap == 0 ? 4 : arr->cap * 2;
    const char **new_data = realloc(arr->data, new_cap * sizeof(*new_data));
    if (!new_data) {
      return;
    }
    arr->data = new_data;
    arr->cap = new_cap;
  }
  arr->data[arr->len++] = str;
}

size_t s_arr_len(s_arr *arr) {
  return arr->len;
}

const char *s_arr_get(s_arr *arr, size_t index) {
  if (index >= arr->len) {
    return NULL;
  }
  return arr->data[index];
}

const char *s_arr_pop(s_arr *arr) {
  if (arr->len == 0) {
    return NULL;
  }
  return arr->data[--arr->len];
}

s_arr *s_arr_copy(s_arr *arr) {
  s_arr *out = s_arr_new();
  if (!out) {
    return NULL;
  }

  if (arr->len > 0) {
    out->data = malloc(arr->len * sizeof(*out->data));
    if (!out->data) {
      free(out);
      return NULL;
    }
    memcpy(out->data, arr->data, arr->len * sizeof(*out->data));
    out->len = arr->len;
    out->cap = arr->len;
  }

  return out;
}
