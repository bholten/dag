#ifndef DAGWOOD_H
#define DAGWOOD_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>

#include "data.h"

typedef struct dagwood_task {
  const char *id;
  const char *name;
  const char *description;
  const char *shell;
  const char *shell_arg;
  const char *run;
  const char *wd;
  s_arr *inputs;
  s_arr *outputs;
  s_arr *depends_on;
  t_arr *edges;
  t_arr *reverse_edges;
  bool always_run;
  bool visited;
  size_t in_degree;
} dagwood_task;

dagwood_task *dagwood_task_new(void);
void dagwood_task_delete(dagwood_task *task);

void dagwood_task_add_input(dagwood_task *task, const char *input);
void dagwood_task_add_output(dagwood_task *task, const char *output);
void dagwood_task_add_depends_on(dagwood_task *task, const char *depends_on);
void dagwood_task_add_edge(dagwood_task *task, dagwood_task *dest);

typedef struct dagwood_project {
  const char *name;
  const char *shell;
  const char *shell_arg;
  const char *description;
  const char *cwd;
  bool always_run;
} dagwood_project;

dagwood_project *dagwood_project_new(void);
void dagwood_project_delete(dagwood_project *project);

typedef struct dagwood_graph {
  t_arr *tasks;

  t_map *command_by_name;
  t_map *task_by_name;
  t_map *task_by_output;
  ts_map *memo;

  pid_t *pidsv_unsafe;
  size_t pidsc;

  bool force_run;
  bool graph_built;
} dagwood_graph;

dagwood_graph *dagwood_graph_new(t_map *task_registry);
void dagwood_graph_delete(dagwood_graph *g);

bool dagwood_graph_execute(dagwood_graph *g);
bool dagwood_graph_execute_task(dagwood_graph *g, const char *task_name);

void dagwood_graph_dry_run(dagwood_graph *g);
void dagwood_graph_to_dot(dagwood_graph *g);

const char *dagwood_platform_shell(void);
const char *dagwood_platform_shell_arg(void);

#endif
