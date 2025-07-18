#ifndef PROJECT
#define PROJECT

#include <stdbool.h>
#include <stddef.h>

#include "t_arr.h"
#include "t_layers.h"
#include "t_map.h"
#include "ts_map.h"

#include "task.h"

typedef struct {
  const char* name;
  const char* default_shell;
  const char* description;

  t_arr* tasks;

  t_map* task_by_name;
  t_map* task_by_output;
  ts_map* memo;
  
  bool graph_built;
  bool always_run;
} dagwood_project;

dagwood_project* dagwood_project_new(void);
void dagwood_project_delete(dagwood_project* project);

/** These are conviences for Tcl **/
void dagwood_project_set_name(dagwood_project* project, const char* name);
void dagwood_project_set_shell(dagwood_project* project, const char* shell);
void dagwood_project_set_always_run(dagwood_project* project, bool always_run);
void dagwood_project_set_description(dagwood_project* project, bool always_run);

void dagwood_project_add_task(dagwood_project* project, dagwood_task* t);
dagwood_task* dagwood_project_get_task(dagwood_project* project, const char* name);

bool dagwood_project_validate(dagwood_project* project);
t_layers* dagwood_project_build_dag(dagwood_project* project);
int dagwood_project_execute(dagwood_project* project);

#endif
