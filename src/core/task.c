#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "shell.h"
#include "t_arr.h"
#include "task.h"

static int default_executor(const dagwood_task *task) {
    printf("[dagwood] [%s] executing: %s\n", task->name, task->run);
    return system(task->run);
}

dagwood_task* dagwood_task_new(void) {
  dagwood_task* t = (dagwood_task*) malloc(sizeof(*t));
  t->name = strdup("");
  t->description = strdup("");
  t->run = strdup("");
  t->shell = strdup(DEFAULT_SHELL);

  t->executor = &default_executor;

  t->depends_on = s_arr_new();
  t->inputs = s_arr_new();
  t->outputs = s_arr_new();

  t->edges = t_arr_new();
  t->reverse_edges = t_arr_new();
  t->in_degree = 0;
  t->always_run = false;
  
  return t;
}

void dagwood_task_delete(dagwood_task *task) {
  free((void*)task->name);
  free((void*)task->depends_on);
  free((void*)task->shell);
  free((void*)task->run);
  
  s_arr_delete(task->depends_on);
  s_arr_delete(task->inputs);
  s_arr_delete(task->outputs);

  t_arr_delete(task->edges);
  t_arr_delete(task->reverse_edges);
  
  free(task);
}

void dagwood_task_add_edge(dagwood_task* dep, dagwood_task* dest) {
  if (!dep->edges) return;
  if (!dest->reverse_edges) return;
  
  t_arr_push(dep->edges, dest);
  t_arr_push(dest->reverse_edges, dep);
  dep->in_degree++;
}

void dagwood_task_set_name(dagwood_task* task, const char* name) {
  if (task->name != NULL) {
    free((void*) task->name);
  }

  task->name = strdup(name);
}

void dagwood_task_set_description(dagwood_task *task, const char *description) {
  if (task->description != NULL) {
    free((void*) task->description);
  }

  task->description = strdup(description);
}
 
void dagwood_task_set_shell(dagwood_task *task, const char *shell) {
  if (task->shell != NULL) {
    free((void*) task->shell);
  }

  task->shell = strdup(shell);
}

void dagwood_task_set_run(dagwood_task *task, const char *run) {
  if (task->run != NULL) {
    free((void*)task->run);
  }

  task->run = strdup(run);
}

void dagwood_task_add_input(dagwood_task *task, const char *input) {
  s_arr_push(task->inputs, input);
}

void dagwood_task_add_output(dagwood_task *task, const char *output) {
  s_arr_push(task->outputs, output);
}

void dagwood_task_add_depends_on(dagwood_task* task, const char* depends_on) {
  s_arr_push(task->depends_on, depends_on);
}

void dagwood_task_set_executor(dagwood_task *task, task_executor_fn executor) {
  task->executor = executor;
}

void dagwood_task_set_always_run(dagwood_task *task, bool always_run) {
  task->always_run = always_run;
}

void dagwood_task_set_timeout(dagwood_task *task, int timeout) {
  task->timeout = timeout;
}

