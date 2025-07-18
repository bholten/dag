#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tcl/tcl.h>
#include <tcl/tclDecls.h>

#include "../core/project.h"
#include "../core/task.h"
#include "dagwood_stdlib.h"
#include "interpreter.h"

struct interpreter {
  Tcl_Interp* interp;
};

static dagwood_project* current_project = NULL;

static int Tcl_ProjectAccessor(ClientData cd, Tcl_Interp* interp, int argc,
			       const char** argv) {
  dagwood_project* p = (dagwood_project*) cd;

  if (argc < 2) {
    Tcl_SetResult(interp, "Usage: <project> <field>", TCL_STATIC);
    return TCL_ERROR;
  }

  const char* field = argv[1];

  if (strcmp(field, "name") == 0) {
    Tcl_SetResult(interp, (char*) p->name, TCL_VOLATILE);
  } else if (strcmp(field, "project_description") == 0) {
    Tcl_SetResult(interp, (char*) p->description, TCL_VOLATILE);
  } else if (strcmp(field, "project_always_run") == 0) {
    Tcl_SetResult(interp, (char*) p->always_run, TCL_VOLATILE);
  } else if (strcmp(field, "project_default_shell") == 0) {
    Tcl_SetResult(interp, (char*) p->default_shell, TCL_VOLATILE);
  } else {
    Tcl_SetResult(interp, "Unknown task field", TCL_STATIC);
    return TCL_ERROR;
  }

  return TCL_OK;
}

static int Tcl_DefineProject(ClientData cd, Tcl_Interp* interp, int argc,
			     const char** argv) {
  if (argc < 2) {
    Tcl_SetResult(interp, "Usage: define_project name", TCL_STATIC);
    return TCL_ERROR;
  }

  current_project = dagwood_project_new();
  
  if (current_project->name != NULL) {
    free((void*) current_project->name);
  }
  
  current_project->name = strdup(argv[1]);

  Tcl_CreateCommand(interp, current_project->name,
		    Tcl_ProjectAccessor, (ClientData) current_project, NULL);

  return TCL_OK;
}

static int Tcl_Project_SetDescription(ClientData cd, Tcl_Interp* interp,
				      int argc, const char** argv) {
  if (argc < 3) {
    Tcl_SetResult(interp,
		  "Usage: project_set_description <project> <default_shell>",
		  TCL_STATIC);
    return TCL_ERROR;
  }
  const char* project_name = argv[1];
  const char* description = argv[2];

  assert(strcmp(project_name, current_project->name) == 0);

  if (current_project->description != NULL) {
    free((void*)current_project->description);
  }
  
  current_project->description = strdup(description);

  return TCL_OK;
}

static int Tcl_Project_SetDefaultShell(ClientData cd, Tcl_Interp* interp,
				       int argc, const char** argv) {
  if (argc < 3) {
    Tcl_SetResult(interp,
		  "Usage: set_default_shell <project> <default_shell>", TCL_STATIC);
    return TCL_ERROR;
  }
  const char* project_name = argv[1];
  const char* default_shell = argv[2];

  assert(strcmp(project_name, current_project->name) == 0);

  if (current_project->default_shell != NULL) {
    free((void*)current_project->default_shell);
  }
  current_project->default_shell = strdup(default_shell);

  return TCL_OK;
}

static int Tcl_Project_SetAlwaysRun(ClientData cd, Tcl_Interp* interp,
				    int argc, const char** argv) {
  if (argc < 3) {
    Tcl_SetResult(interp, "Usage: set_always_run <project> <always_run>", TCL_STATIC);
    return TCL_ERROR;
  }
  const char* project_name = argv[1];
  const char* always_run = argv[2];

  bool always_run_b;

  if (strcmp(always_run, "true") == 0) {
    always_run_b = true;
  } else if (strcmp(always_run, "false") == 0) {
    always_run_b = false;
  } else {
    always_run_b = false; // TODO default behavior?
  }
  
  assert(strcmp(project_name, current_project->name) == 0);
  current_project->always_run = always_run_b;

  return TCL_OK;
}

static int Tcl_TaskAccessor(ClientData cd, Tcl_Interp* interp,
			    int argc, const char** argv) {
  dagwood_task* t = (dagwood_task*) cd;

  if (argc < 2) {
    Tcl_SetResult(interp, "Usage: <task> <field>", TCL_STATIC);
    return TCL_ERROR;
  }

  const char* field = argv[1];

  if (strcmp(field, "name") == 0) {
    Tcl_SetResult(interp, (char*) t->name, TCL_VOLATILE);
  } else if (strcmp(field, "description") == 0) {
    Tcl_SetResult(interp, (char*) t->description, TCL_VOLATILE);
  } else if (strcmp(field, "run") == 0) {
    Tcl_SetResult(interp, (char*) t->run, TCL_VOLATILE);
  } else if (strcmp(field, "always_run") == 0) {
    Tcl_SetResult(interp, (char*) t->always_run, TCL_VOLATILE);
  } else if (strcmp(field, "inputs") == 0) {
    Tcl_Obj *list = Tcl_NewListObj(0, NULL);

    for (size_t i = 0; i < s_arr_len(t->inputs); ++i) {
      Tcl_ListObjAppendElement(interp,
			       list,
			       Tcl_NewStringObj(s_arr_get(t->inputs, i), -1));
    }
    
    Tcl_SetObjResult(interp, list);
  } else if (strcmp(field, "outputs") == 0) {
    Tcl_Obj* list = Tcl_NewListObj(0, NULL);


    for (size_t i = 0; i < s_arr_len(t->outputs); ++i) {
      Tcl_ListObjAppendElement(interp,
			       list,
			       Tcl_NewStringObj(s_arr_get(t->outputs, i), -1));
    }
    
    Tcl_SetObjResult(interp, list);
  } else {
    Tcl_SetResult(interp, "Unknown task field", TCL_STATIC);
    return TCL_ERROR;
  }

  return TCL_OK;
}

static int Tcl_Project_Execute(ClientData cd, Tcl_Interp* interp,
			       int argc, const char** argv) {
  if (argc < 2) {
    Tcl_SetResult(interp, "Usage: execute_project <project>", TCL_STATIC);
    return TCL_ERROR;
  }

  int result = dagwood_project_execute(current_project);

  return result;
}

static int Tcl_DefineTask(ClientData cd, Tcl_Interp *interp,
			  int argc, const char** argv) {
  if (argc < 2) {
    Tcl_SetResult(interp, "Usage: define_task name", TCL_STATIC);
    return TCL_ERROR;
  }
  
  dagwood_task* t = dagwood_task_new();
  t->name = strdup(argv[1]);
  Tcl_CreateCommand(interp, t->name, Tcl_TaskAccessor, (ClientData) t, NULL);
  dagwood_project_add_task(current_project, t);
  
  return TCL_OK;
}


static int Tcl_Task_SetRun(ClientData cd, Tcl_Interp *interp,
			   int argc, const char **argv) {
  if (argc < 3) {
    Tcl_SetResult(interp, "Usage: run task name", TCL_STATIC);
    return TCL_ERROR;
  }

  const char* name = argv[1];
  const char* cmd = argv[2];
  dagwood_task* t = dagwood_project_get_task(current_project, name);
  
  if (!t) {
    Tcl_SetResult(interp, "Task not found SetRun", TCL_STATIC);
    return TCL_ERROR;
  }

  dagwood_task_set_run(t, cmd);
  
  return TCL_OK;
}

static int Tcl_Task_SetDescription(ClientData cd, Tcl_Interp* interp,
				   int argc, const char** argv) {
  if (argc < 3) {
    Tcl_SetResult(interp, "Usage: task description ...", TCL_STATIC);
    return TCL_ERROR;
  }

  const char* name = argv[1];
  const char* desc = argv[2];
  dagwood_task* t = dagwood_project_get_task(current_project, name);

  if (!t) {
    Tcl_SetResult(interp, "Task not found SetDescription", TCL_STATIC);
    return TCL_ERROR;
  }

  dagwood_task_set_description(t, desc);
  
  return TCL_OK;
}

static int Tcl_Task_SetShell(ClientData cd, Tcl_Interp *interp,
			     int argc, const char **argv) {
  if (argc < 2) {
    Tcl_SetResult(interp, "Usage: shell ...", TCL_STATIC);
    return TCL_ERROR;
  }

  const char* name = argv[1];
  const char* shell = argv[2];
  dagwood_task* t = dagwood_project_get_task(current_project, name);

  if (!t) {
    Tcl_SetResult(interp, "Task not found SetShell", TCL_STATIC);
    return TCL_ERROR;
  }

  dagwood_task_set_shell(t, shell);
  
  return TCL_OK;
}

static int Tcl_Task_SetTimeout(ClientData cd, Tcl_Interp *interp,
			       int argc, const char **argv) {
  if (argc < 2) {
    Tcl_SetResult(interp, "Usage: timeout ...", TCL_STATIC);
    return TCL_ERROR;
  }

  const char* name = argv[1];
  int timeout = atoi(argv[2]);
  dagwood_task* t = dagwood_project_get_task(current_project, name);

  if (!t) {
    Tcl_SetResult(interp, "Task not found SetTimeout", TCL_STATIC);
    return TCL_ERROR;
  }

  dagwood_task_set_timeout(t, timeout);

  return TCL_OK;
}

static int Tcl_Task_SetAlwaysRun(ClientData cd, Tcl_Interp *interp,
				 int argc, const char **argv) {
  if (argc < 2) {
    Tcl_SetResult(interp, "Usage: always_run ...", TCL_STATIC);
    return TCL_ERROR;
  }

  const char* name = argv[1];
  const char* always_run = argv[2];
  dagwood_task* t = dagwood_project_get_task(current_project, name);

  if (!t) {
    Tcl_SetResult(interp, "Task not found SetAlwaysRun", TCL_STATIC);
    return TCL_ERROR;
  }

  // TODO this is terrible
  bool always_run_b = false;

  if (strcmp(always_run, "true") == 0) {
    always_run_b = true;
  }

  printf("[%s] setting always_run_b = %s", t->name, always_run);
  dagwood_task_set_always_run(t, always_run_b); // TODO 

  return TCL_OK;
}

static int Tcl_Task_SetDependsOn(ClientData cd, Tcl_Interp *interp,
				 int argc, const char **argv) {
  if (argc < 2) {
    Tcl_SetResult(interp, "Usage: depends-on ...", TCL_STATIC);
    return TCL_ERROR;
  }

  const char* name = argv[1];
  dagwood_task* t = dagwood_project_get_task(current_project, name);

  if (!t) {
    Tcl_SetResult(interp, "Task not found SetDependsOn", TCL_STATIC);
    return TCL_ERROR;
  }

  for (size_t i = 2; i < argc; i++) {
    const char* dep = argv[i];
    dagwood_task_add_depends_on(t, dep);
  }

  return TCL_OK;
}

static int Tcl_Task_SetInputs(ClientData cd, Tcl_Interp *interp,
			      int argc, const char **argv) {
  if (argc < 2) {
    Tcl_SetResult(interp, "Usage: inputs ...", TCL_STATIC);
    return TCL_ERROR;
  }

  const char* name = argv[1];
  dagwood_task* t = dagwood_project_get_task(current_project, name);

  if (!t) {
    Tcl_SetResult(interp, "Task not found SetInputs", TCL_STATIC);
    return TCL_ERROR;
  }

  for (size_t i = 2; i < argc; i++) {
    const char* dep = argv[i];
    dagwood_task_add_input(t, dep);
  }
  
  return TCL_OK;
}

static int Tcl_Task_SetOutputs(ClientData cd, Tcl_Interp *interp,
			       int argc, const char **argv) {
  if (argc < 2) {
    Tcl_SetResult(interp, "Usage: outputs ...", TCL_STATIC);
    return TCL_ERROR;
  }

  const char* name = argv[1];
  dagwood_task* t = dagwood_project_get_task(current_project, name);

  if (!t) {
    Tcl_SetResult(interp, "Task not found SetOutputs", TCL_STATIC);
    return TCL_ERROR;
  }

  for (size_t i = 2; i < argc; i++) {
    const char* dep = argv[i];
    dagwood_task_add_output(t, dep);
  }
  
  return TCL_OK;
}

static int Tcl_ListTasks(ClientData cd, Tcl_Interp *interp,
			 int argc, const char **argv) {
  printf("| Listing Tasks for Project: %s\n", current_project->name);
  printf("| Project Shell: %s\n", current_project->default_shell);
  for (int i = 0; i < t_arr_len(current_project->tasks); ++i) {
    dagwood_task *t = t_arr_get(current_project->tasks, i);;
    printf("--| Task: %s\n", t->name);
    printf("----|  Shell:       %s\n", t->shell);    
    printf("----|  Description: %s\n", t->description);
    printf("----|  Command:\n%s\n", t->run);
    for (size_t j = 0; j < s_arr_len(t->inputs); j++) {
      printf("------| Inputs:  %s\n", s_arr_get(t->inputs, j)); 
    }

    for (size_t k = 0; k < s_arr_len(t->outputs); k++) {
      printf("------| Outputs: %s\n", s_arr_get(t->outputs, k)); 
    }
  }
  
  return TCL_OK;
}

const char* interpreter_get_error(interpreter* interp) {
  if (!interp) return NULL;
  if (!interp->interp) return NULL;
  return Tcl_GetStringResult(interp->interp);
}

interpreter* interpreter_new(void) {
  interpreter* wrapper = malloc(sizeof(*wrapper));

  if (!wrapper) return NULL;
  
  Tcl_Interp *interp = Tcl_CreateInterp();

  if (!interp) {
    free(wrapper);
    return NULL;
  }

  wrapper->interp = interp;
  
  Tcl_Init(interp);
  
  Tcl_CreateCommand(interp, "define_project", Tcl_DefineProject, NULL, NULL);
  Tcl_CreateCommand(interp, "project_set_description", Tcl_Project_SetDescription, NULL, NULL);
  Tcl_CreateCommand(interp, "project_set_default_shell", Tcl_Project_SetDefaultShell, NULL, NULL);
  Tcl_CreateCommand(interp, "project_set_always_run", Tcl_Project_SetAlwaysRun, NULL, NULL);
  Tcl_CreateCommand(interp, "project_execute", Tcl_Project_Execute, NULL, NULL);
  
  Tcl_CreateCommand(interp, "define_task", Tcl_DefineTask, NULL, NULL);
  Tcl_CreateCommand(interp, "task_set_run", Tcl_Task_SetRun, NULL, NULL);
  Tcl_CreateCommand(interp, "task_set_description", Tcl_Task_SetDescription, NULL, NULL);
  Tcl_CreateCommand(interp, "task_set_shell", Tcl_Task_SetShell, NULL, NULL);
  Tcl_CreateCommand(interp, "task_set_timeout", Tcl_Task_SetTimeout, NULL, NULL);
  Tcl_CreateCommand(interp, "task_set_always_run", Tcl_Task_SetAlwaysRun, NULL, NULL);
  Tcl_CreateCommand(interp, "task_set_depends_on", Tcl_Task_SetDependsOn, NULL, NULL);
  Tcl_CreateCommand(interp, "task_set_inputs", Tcl_Task_SetInputs, NULL, NULL);
  Tcl_CreateCommand(interp, "task_set_outputs", Tcl_Task_SetOutputs, NULL, NULL);
  Tcl_CreateCommand(interp, "list_tasks", Tcl_ListTasks, NULL, NULL);

  Tcl_Eval(interp, (const char*) src_tcl_dagwood_stdlib_tcl);
  return wrapper;
}

int interpreter_eval(interpreter* interp, const char* file) {
  if (!interp || !file) return TCL_ERROR;

  int code = Tcl_EvalFile(interp->interp, file);

  if (code != TCL_OK) {
    fprintf(stderr, "Tcl Error: %s\n", Tcl_GetStringResult(interp->interp));
  }

  return code;
}

void interpreter_delete(interpreter* interp) {
  if (!interp) return;
  Tcl_DeleteInterp(interp->interp);
  free(interp);
}
