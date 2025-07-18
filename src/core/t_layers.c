#include "t_layers.h"
#include "../../lib/klib/kvec.h"

struct t_layers {
  kvec_t(t_arr*) vec;
};

t_layers* t_layers_new() {
  struct t_layers* layers = malloc(sizeof(*layers));

  if (!layers) return NULL;

  kv_init(layers->vec);
  return layers;
}

void t_layers_delete(t_layers* layers) {
  if (!layers) return;
  
  for (size_t i = 0; i < t_layers_len(layers); i++) {
    t_arr* arr = t_layers_get(layers, i);

    if (!arr) continue;
    
    t_arr_delete(arr);
  }

  kv_destroy(layers->vec);
}

void t_layers_push(t_layers* layers, t_arr* layer) {
  kv_push(t_arr*, layers->vec, layer);
}

size_t t_layers_len(t_layers* layers) {
  return kv_size(layers->vec);
}

t_arr* t_layers_get(t_layers* layers, size_t index) {
  if (index >= kv_size(layers->vec)) return NULL;

  return kv_A(layers->vec, index);
}

t_arr* t_layers_pop(t_layers* layers) {
  return kv_pop(layers->vec);
}
