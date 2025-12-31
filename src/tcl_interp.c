#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jim.h>

#include "dagwood.h"
#include "dagwood_stdlib.h"
#include "data.h"
#include "interpreter.h"

#define DAGWOOD_COMMANDS "::___dagwood::commands"
#define DAGWOOD_PROJECTS "::___dagwood::projects"
#define DAGWOOD_TASKS "::___dagwood::tasks"
#define PROC_LIST_ALL "::list_all"

struct interpreter {
  Jim_Interp *interp;
  p_map *project_registry;
  t_map *task_registry;
  t_map *command_registry;
  bool built;
};

static int jim_platform_shell_cmd(Jim_Interp *interp, int objc,
                                  Jim_Obj *const objv[]) {
  (void)objc;
  (void)objv;
  Jim_SetResultString(interp, dagwood_platform_shell(), -1);
  return JIM_OK;
}

static int jim_platform_shell_arg(Jim_Interp *interp, int objc,
                                  Jim_Obj *const objv[]) {
  (void)objc;
  (void)objv;
  Jim_SetResultString(interp, dagwood_platform_shell_arg(), -1);
  return JIM_OK;
}

typedef bool (*map_set_fn)(void *ctx, const char *name, dagwood_task *t);

static interp_result sync_registry(interpreter *interp, map_set_fn set_fn,
                                   void *ctx, Jim_Obj *registry_dict) {
  int len;
  Jim_Obj **pairs = Jim_DictPairs(interp->interp, registry_dict, &len);

  if (len == 0) {
    return INTERP_OK;
  }

  if (pairs == NULL) {
    fprintf(stderr, "[dagwood] registry was not a dictionary\n");
    return INTERP_ERROR;
  }

  for (int i = 0; i + 1 < len; i += 2) {
    Jim_Obj *key = pairs[i];
    Jim_Obj *task_dict = pairs[i + 1];
    dagwood_project *parent;

    int key_len;
    const char *task_id = Jim_GetString(key, &key_len);

    dagwood_task *t = dagwood_task_new();
    t->id = task_id;

    int task_len;
    Jim_Obj **task_pairs = Jim_DictPairs(interp->interp, task_dict, &task_len);

    if (task_pairs == NULL) {
      fprintf(stderr, "[dagwood] task %s not a dictionary\n", task_id);
      return INTERP_ERROR;
    }

    for (int k = 0; k + 1 < task_len; k += 2) {
      Jim_Obj *attrib_name = task_pairs[k];
      Jim_Obj *attrib = task_pairs[k + 1];

      int attrib_name_len;
      const char *attrib_str = Jim_GetString(attrib_name, &attrib_name_len);

      if (strcmp(attrib_str, "parent-project") == 0) {
        int pp_len;
        const char *parent_project_name = Jim_GetString(attrib, &pp_len);
        parent = p_map_get(interp->project_registry, parent_project_name);
      }

      else if (strcmp(attrib_str, "id") == 0) {
        int id_len;
        const char *id = Jim_GetString(attrib, &id_len);
        t->id = id;
      }

      else if (strcmp(attrib_str, "name") == 0) {
        int name_len;
        const char *name = Jim_GetString(attrib, &name_len);
        t->name = name;
      }

      else if (strcmp(attrib_str, "always-run") == 0) {
        int ar;
        Jim_GetBoolean(interp->interp, attrib, &ar);
        t->always_run = ar;
      }

      else if (strcmp(attrib_str, "description") == 0) {
        int desc_len;
        const char *desc = Jim_GetString(attrib, &desc_len);
        t->description = desc;
      }

      else if (strcmp(attrib_str, "shell") == 0) {
        int shell_len;
        const char *shell = Jim_GetString(attrib, &shell_len);
        t->shell = shell;
      }

      else if (strcmp(attrib_str, "shell-arg") == 0) {
        int shell_arg_len;
        const char *shell_arg = Jim_GetString(attrib, &shell_arg_len);
        t->shell_arg = shell_arg;
      }

      else if (strcmp(attrib_str, "wd") == 0) {
        int wd_len;
        const char *wd = Jim_GetString(attrib, &wd_len);
        t->wd = wd;
      }

      else if (strcmp(attrib_str, "run") == 0) {
        int run_len;
        const char *run = Jim_GetString(attrib, &run_len);
        t->run = run;
      }

      else if (strcmp(attrib_str, "inputs") == 0) {
        int inputs_len = Jim_ListLength(interp->interp, attrib);

        for (int j = 0; j < inputs_len; j++) {
          Jim_Obj *item;
          if (Jim_ListIndex(interp->interp, attrib, j, &item, JIM_NONE) !=
              JIM_OK) {
            return INTERP_ERROR;
          }
          int item_len;
          const char *input = Jim_GetString(item, &item_len);
          dagwood_task_add_input(t, input);
        }
      }

      else if (strcmp(attrib_str, "depends-on") == 0) {
        int deps_len = Jim_ListLength(interp->interp, attrib);

        for (int j = 0; j < deps_len; j++) {
          Jim_Obj *item;
          if (Jim_ListIndex(interp->interp, attrib, j, &item, JIM_NONE) !=
              JIM_OK) {
            return INTERP_ERROR;
          }
          int item_len;
          const char *dep = Jim_GetString(item, &item_len);
          dagwood_task_add_depends_on(t, dep);
        }
      }

      else if (strcmp(attrib_str, "outputs") == 0) {
        int out_len = Jim_ListLength(interp->interp, attrib);

        for (int j = 0; j < out_len; j++) {
          Jim_Obj *item;
          if (Jim_ListIndex(interp->interp, attrib, j, &item, JIM_NONE) !=
              JIM_OK) {
            return INTERP_ERROR;
          }
          int item_len;
          const char *output = Jim_GetString(item, &item_len);
          dagwood_task_add_output(t, output);
        }
      }
    }

    assert(parent != NULL);

    if (!t->shell || !t->shell_arg) {
      assert(parent->shell != NULL);
      assert(parent->shell_arg != NULL);

      if (parent->shell && parent->shell_arg) {
        t->shell = parent->shell;
        t->shell_arg = parent->shell_arg;
      } else {
        t->shell = dagwood_platform_shell();
        t->shell_arg = dagwood_platform_shell_arg();
      }
    }

    if (!t->wd) {
      assert(parent->cwd != NULL);
      t->wd = parent->cwd;
    }

    if (parent->always_run) {
      t->always_run = true;
    }

    if (set_fn(ctx, t->id, t) != INTERP_OK) {
      fprintf(stderr, "[dagwood] failed to normalize %s\n", t->id);
      return INTERP_ERROR;
    }
  }

  return INTERP_OK;
}

static bool t_map_set_cb(void *ctx, const char *name, dagwood_task *t) {
  t_map *map = (t_map *)ctx;

  if (!t_map_set(map, name, t)) {
    return INTERP_ERROR;
  }

  return INTERP_OK;
}

static interp_result sync_tasks(interpreter *interp) {
  Jim_Obj *tasks_dict =
      Jim_GetVariableStr(interp->interp, DAGWOOD_TASKS, JIM_NONE);

  return sync_registry(interp, t_map_set_cb, interp->task_registry, tasks_dict);
}

static interp_result sync_commands(interpreter *interp) {
  Jim_Obj *cmds_dict =
      Jim_GetVariableStr(interp->interp, DAGWOOD_COMMANDS, JIM_NONE);

  return sync_registry(interp, t_map_set_cb, interp->command_registry,
                       cmds_dict);
}

static interp_result sync_projects(interpreter *interp) {
  Jim_Obj *projects_dict =
      Jim_GetVariableStr(interp->interp, DAGWOOD_PROJECTS, JIM_NONE);

  int len;
  Jim_Obj **pairs = Jim_DictPairs(interp->interp, projects_dict, &len);

  for (int i = 0; i + 1 < len; i += 2) {
    dagwood_project *project = dagwood_project_new();
    Jim_Obj *project_id_obj = pairs[i];
    Jim_Obj *project_dict = pairs[i + 1];

    int key_str_len;
    const char *proj_id = Jim_GetString(project_id_obj, &key_str_len);

    int projs_len;
    Jim_Obj **proj_pairs =
        Jim_DictPairs(interp->interp, project_dict, &projs_len);

    for (int j = 0; j + 1 < projs_len; j += 2) {
      Jim_Obj *key_obj = proj_pairs[j];
      Jim_Obj *value = proj_pairs[j + 1];

      int key_len;
      const char *key_str = Jim_GetString(key_obj, &key_len);

      if (strcmp(key_str, "always-run") == 0) {
        int ar;
        Jim_GetBoolean(interp->interp, value, &ar);
        project->always_run = ar;
      }

      else if (strcmp(key_str, "cwd") == 0) {
        int cwd_len;
        const char *cwd = Jim_GetString(value, &cwd_len);
        project->cwd = cwd;
      }

      else if (strcmp(key_str, "description") == 0) {
        int desc_len;
        const char *description = Jim_GetString(value, &desc_len);
        project->description = description;
      }

      else if (strcmp(key_str, "shell") == 0) {
        int shell_len;
        const char *shell = Jim_GetString(value, &shell_len);
        project->shell = shell;
      }

      else if (strcmp(key_str, "shell-arg") == 0) {
        int shell_arg_len;
        const char *shell_arg = Jim_GetString(value, &shell_arg_len);
        project->shell_arg = shell_arg;
      }
    }

    p_map_set(interp->project_registry, proj_id, project);
  }

  if (sync_tasks(interp) != INTERP_OK) {
    fprintf(stderr, "[dagwood] failed to sync tasks\n");
    return INTERP_ERROR;
  }

  if (sync_commands(interp) != INTERP_OK) {
    fprintf(stderr, "[dagwood] failed to sync commands\n");
    return INTERP_ERROR;
  }

  return INTERP_OK;
}

const char *interpreter_get_error(interpreter *interp) {
  if (!interp) return NULL;
  if (!interp->interp) return NULL;

  Jim_Obj *result = Jim_GetResult(interp->interp);
  int len;
  return Jim_GetString(result, &len);
}

interpreter *interpreter_new(void) {
  interpreter *wrapper = calloc(1, sizeof(*wrapper));

  if (!wrapper) return NULL;

  p_map *project_registry = p_map_new();

  if (project_registry == NULL) {
    free(wrapper);
    return NULL;
  }

  wrapper->project_registry = project_registry;

  t_map *task_registry = t_map_new();

  if (!task_registry) {
    p_map_delete(project_registry);
    free(wrapper);
    return NULL;
  }

  wrapper->task_registry = task_registry;

  t_map *command_registry = t_map_new();

  if (!command_registry) {
    p_map_delete(project_registry);
    t_map_delete(task_registry);
    free(wrapper);
  }

  wrapper->command_registry = command_registry;

  Jim_Interp *interp = Jim_CreateInterp();

  if (!interp) {
    p_map_delete(wrapper->project_registry);
    t_map_delete(wrapper->task_registry);
    t_map_delete(wrapper->command_registry);
    free(wrapper);
    return NULL;
  }

  Jim_RegisterCoreCommands(interp);

  if (Jim_InitStaticExtensions(interp) != JIM_OK) {
    Jim_FreeInterp(interp);
    p_map_delete(wrapper->project_registry);
    t_map_delete(wrapper->task_registry);
    t_map_delete(wrapper->command_registry);
    free(wrapper);
    return NULL;
  }

  Jim_CreateCommand(interp, "platform-shell", jim_platform_shell_cmd, NULL,
                    NULL);
  Jim_CreateCommand(interp, "platform-shell-arg", jim_platform_shell_arg, NULL,
                    NULL);
  wrapper->interp = interp;

  Jim_Obj *script_obj =
      Jim_NewStringObj(interp, (const char *)tcl_dagwood_stdlib_tcl,
                       (int)tcl_dagwood_stdlib_tcl_len);

  if (Jim_EvalObj(interp, script_obj) != JIM_OK) {
    Jim_MakeErrorMessage(interp);
    fprintf(stderr, "[dagwood] Tcl Error: %s\n",
            Jim_GetString(Jim_GetResult(interp), NULL));
    Jim_FreeInterp(interp);
    p_map_delete(wrapper->project_registry);
    t_map_delete(wrapper->task_registry);
    t_map_delete(wrapper->command_registry);
    free(wrapper);
    return NULL;
  }

  return wrapper;
}

static interp_result build(interpreter *interp, const char *file) {
  if (!interp || !file) {
    fprintf(stderr, "[dagwood] bad file or interpreter\n");
    return INTERP_ERROR;
  }

  int code = Jim_EvalFile(interp->interp, file);

  if (code != JIM_OK) {
    Jim_MakeErrorMessage(interp->interp);
    fprintf(stderr, "[dagwood] Tcl Error: %s\n",
            Jim_GetString(Jim_GetResult(interp->interp), NULL));
    return INTERP_ERROR;
  }

  interp_result result = sync_projects(interp);

  if (result != INTERP_OK) {
    fprintf(stderr, "[dagwood] failed to synchronize projects\n");
    return INTERP_ERROR;
  }

  return INTERP_OK;
}

void interpreter_delete(interpreter *interp) {
  p_map *p = interp->project_registry;

  for (size_t i = p_map_begin(p); i < p_map_end(p); i++) {
    dagwood_project *project = p_map_value(p, i);

    if (!project) continue;

    dagwood_project_delete(project);
    project = NULL;
  }

  t_map *t = interp->task_registry;
  t_map *c = interp->command_registry;

  for (size_t j = t_map_begin(c); j < t_map_end(c); j++) {
    dagwood_task *task = t_map_value(c, j);

    if (!task) continue;
    // Skip tasks cross-linked in the task registry.
    // Assume task registry is the primary owner.
    if (t_map_exists(t, task->name)) continue;

    dagwood_task_delete(task);
  }

  for (size_t j = t_map_begin(t); j < t_map_end(t); j++) {
    dagwood_task *task = t_map_value(t, j);

    if (!task) continue;

    dagwood_task_delete(task);
  }

  t_map_delete(t);
  t_map_delete(c);
  p_map_delete(p);
  Jim_FreeInterp(interp->interp);
  free(interp);
}

t_map *interpreter_command_registry(interpreter *interp, const char *file) {
  if (!interp->built) {
    if (build(interp, file) != INTERP_OK) {
      return NULL;
    }
    interp->built = true;
  }

  return interp->command_registry;
}

t_map *interpreter_task_registry(interpreter *interp, const char *file) {
  if (!interp->built) {
    if (build(interp, file) != INTERP_OK) {
      return NULL;
    }
    interp->built = true;
  }

  return interp->task_registry;
}

interp_result interpreter_list_all(interpreter *interp) {
  Jim_Eval(interp->interp, PROC_LIST_ALL);
  return INTERP_OK;
}

interp_result interpreter_repl(interpreter *interp) {
  char line[1024];
  printf("Dagwood REPL (type 'exit' to quit)\n");

  while (true) {
    printf("dagwood> ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
      break;
    }

    if (strncmp(line, "exit", 4) == 0) {
      break;
    }

    int code = Jim_Eval(interp->interp, line);

    if (code == JIM_OK) {
      int len;
      const char *result = Jim_GetString(Jim_GetResult(interp->interp), &len);

      if (result && *result) {
        printf("%s\n", result);
      }
    } else {
      Jim_MakeErrorMessage(interp->interp);
      fprintf(stderr, "[dagwood] Tcl Error: %s\n",
              Jim_GetString(Jim_GetResult(interp->interp), NULL));
    }
  }

  return INTERP_OK;
}
