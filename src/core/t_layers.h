#ifndef T_LAYERS
#define T_LAYERS

#include "t_arr.h"

typedef struct t_layers t_layers;

t_layers* t_layers_new();
void t_layers_delete(t_layers* layers);
void t_layers_push(t_layers* layers, t_arr* layer);
size_t t_layers_len(t_layers* layers);
t_arr* t_layers_get(t_layers* layers, size_t index);
t_arr* t_layers_pop(t_layers* layers);

#endif
