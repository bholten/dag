#include <stdlib.h>

#include "data.h"

struct t_layers {
  t_arr **data;
  size_t len;
  size_t cap;
};

t_layers *t_layers_new(void) {
  t_layers *layers = malloc(sizeof(*layers));

  if (!layers) {
    return NULL;
  }

  layers->data = NULL;
  layers->len = 0;
  layers->cap = 0;

  return layers;
}

void t_layers_delete(t_layers *layers) {
  size_t i;

  if (!layers) {
    return;
  }

  for (i = 0; i < layers->len; i++) {
    t_arr *arr = layers->data[i];
    if (arr) {
      t_arr_delete(arr);
    }
  }

  free(layers->data);
  free(layers);
}

void t_layers_push(t_layers *layers, const t_arr *layer) {
  if (layers->len >= layers->cap) {
    size_t new_cap = layers->cap == 0 ? 4 : layers->cap * 2;
    t_arr **new_data = realloc(layers->data, new_cap * sizeof(*new_data));
    if (!new_data) {
      return;
    }
    layers->data = new_data;
    layers->cap = new_cap;
  }
  layers->data[layers->len++] = (t_arr *)layer;
}

size_t t_layers_len(t_layers *layers) {
  return layers->len;
}

t_arr *t_layers_get(t_layers *layers, size_t index) {
  if (index >= layers->len) {
    return NULL;
  }
  return layers->data[index];
}

t_arr *t_layers_pop(t_layers *layers) {
  if (layers->len == 0) {
    return NULL;
  }
  return layers->data[--layers->len];
}
