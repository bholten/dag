#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "dagwood.h"
#include "data.h"

dagwood_task *dagwood_task_new(void) {
  dagwood_task *t = calloc(1, sizeof(*t));
  t->id = NULL;
  t->name = NULL;
  t->description = NULL;
  t->run = "exit 0";
  t->shell = NULL;
  t->shell_arg = NULL;
  t->wd = NULL;

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
  s_arr_delete(task->depends_on);
  s_arr_delete(task->inputs);
  s_arr_delete(task->outputs);

  t_arr_delete(task->edges);
  t_arr_delete(task->reverse_edges);

  free(task);
}

void dagwood_task_add_edge(dagwood_task *dep, dagwood_task *dest) {
  t_arr_push(dep->edges, dest);
  t_arr_push(dest->reverse_edges, dep);
  dep->in_degree++;
}

void dagwood_task_add_input(dagwood_task *task, const char *input) {
  s_arr_push(task->inputs, input);
}

void dagwood_task_add_output(dagwood_task *task, const char *output) {
  s_arr_push(task->outputs, output);
}

void dagwood_task_add_depends_on(dagwood_task *task, const char *depends_on) {
  s_arr_push(task->depends_on, depends_on);
}
