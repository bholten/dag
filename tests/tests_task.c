#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/core/task.h"
#include "../src/core/t_map.h"

int main(void) {
  printf("[test] [tests_task.c] dagwood_task.h tests\n");

  {
    printf("[test] [tests_task.c] task_stale - always stale when task is set to always run = true\n");
    dagwood_task* t = malloc(sizeof(*t));
    t->name = "test-1";
    t->always_run = true;
    
    tt* table = tt_new();
    tt_set(table, t->name, t);
    
    task_state ts[1];
    
    bool result = task_stale(t);

    if (!result) {
      exit(1);
    }
    printf("[test] [tests_task.c] test passed\n");
    free(t);
    tt_delete(table);
  }
  
  {
    printf("[test] [tests_task.c] task_stale - task is always stale when no outputs (phony)\n");

    dagwood_task* t = malloc(sizeof(*t));

    t->name = "example-task";
    t->always_run = false;
    t->outputs = NULL;

    tt* table = tt_new();
    tt_set(table, t->name, t);
    
    task_state ts[1];
    
    
    bool result = task_stale(t);
    free(t);
    tt_delete(table);
    
    if (!result) {
      exit(1);
    }
  }
  
  printf("[test] [tests_task.c] ");
  
  
  return 0;
}
