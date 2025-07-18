#ifndef S_ARR
#define S_ARR

#include <stddef.h>

typedef struct s_arr s_arr;

s_arr* s_arr_new();
void s_arr_delete(s_arr* arr);
void s_arr_push(s_arr* arr, const char* str);
size_t s_arr_len(s_arr* arr);
const char* s_arr_get(s_arr* arr, size_t index);
const char* s_arr_pop(s_arr* arr);
s_arr* s_arr_copy(s_arr* arr);

#endif
