#ifndef T_ARR
#define T_ARR

#include <stddef.h>

typedef struct dagwood_task dagwood_task;
typedef struct t_arr t_arr;

t_arr* t_arr_new();
void t_arr_delete(t_arr* arr);
void t_arr_push(t_arr* arr, dagwood_task* task);
size_t t_arr_len(t_arr* arr);
dagwood_task* t_arr_get(t_arr* arr, size_t index);
dagwood_task* t_arr_pop(t_arr* arr);

#endif
