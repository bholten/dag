#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/core/task.h"
#include "../src/core/t_map.h"

bool test_insert_and_retrieve(void) {
  printf("Can insert and retrieve a value\n");
  dagwood_task* t = malloc(sizeof(*t));
  t->name = "example-task";
  t_map* table = t_map_new();
  
  bool result = t_map_set(table, "example-task", t);

  if (!result) {
    fprintf(stderr, "Failed to insert a task\n");
    return false;
  }

  dagwood_task* s = t_map_get(table, "example-task");

  if (s == NULL) {
    fprintf(stderr, "Failed to retrieve task\n");
    return false;
  }
  
  if (s != t) {
    fprintf(stderr, "Retrieved different task from original\n");
    return false;
  }

  free(t);
  t_map_delete(table);
  puts(" - Test Completed");
  return true;
}

int main(void) {
  printf("Runing dagwood_t_map.h tests\n");
  if (!test_insert_and_retrieve()) return -1;
  
  puts("Tests complete");
  return 0;
}
