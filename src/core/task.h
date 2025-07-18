#ifndef TASK_H
#define TASK_H

#include <stdbool.h>
#include <stddef.h>

#include "s_arr.h"
#include "t_arr.h"

typedef struct dagwood_task dagwood_task;

typedef int (*task_executor_fn)(const dagwood_task *task);

typedef struct dagwood_task {
  const char *name;
  const char* description;
  const char* shell;
  const char* run;
  s_arr* inputs;
  s_arr* outputs;
  s_arr* depends_on;
  t_arr* edges;
  t_arr* reverse_edges;
  task_executor_fn executor;
  bool always_run;
  bool visited;
  int timeout;
  size_t in_degree;
  size_t layer;
} dagwood_task;

dagwood_task *dagwood_task_new(void);

/* Conveniences for Tcl */
void dagwood_task_set_name(dagwood_task* task, const char* name);
void dagwood_task_set_description(dagwood_task *task, const char *description);
void dagwood_task_set_shell(dagwood_task *task, const char *shell);
void dagwood_task_set_run(dagwood_task *task, const char *run);
void dagwood_task_add_input(dagwood_task* task, const char* input);
void dagwood_task_add_output(dagwood_task *task, const char *output);
void dagwood_task_add_depends_on(dagwood_task* task, const char* depends_on);
void dagwood_task_set_executor(dagwood_task *task, task_executor_fn executor);
void dagwood_task_set_always_run(dagwood_task *task, bool always_run);
void dagwood_task_set_timeout(dagwood_task *task, int timeout);

void dagwood_task_delete(dagwood_task* task);
void dagwood_task_add_edge(dagwood_task* task, dagwood_task* dest);

#endif
