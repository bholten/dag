#define _GNU_SOURCE

#include <assert.h>
#include <dirent.h>
#include <fnmatch.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Dagwood embeds a single Lcl interpreter per process. Only one piece
 * of state genuinely needs to live as a file-scope global:
 *
 *   active — singleton guard, set in interpreter_new and cleared in
 *            interpreter_delete. The assert in interpreter_new makes
 *            the constraint loud.
 *
 * The currently-executing project name is tracked entirely on the Lcl
 * side via $dagwood::current_project — maintained by `s_project` and
 * read by the pure-Lcl `def` and `arg` procs in lib/dagwood.lcl.
 */

/* clang-format off */
/* lcl.h must be included before lcl-io (or any libraries)
 * Turn clang-format off for being aggressive */
#include <lcl.h>
#include <lcl-io.h>
/* clang-format on */

#include "dagwood.h"
#include "data.h"
#include "generated/dagwood_dsl.h"
#include "interpreter.h"

static char *strdup_safe(const char *s);
static char *make_qualified_name(const char *project, const char *name);

static struct {
  bool active;
} g_ctx;

static const lcl_embedded_lib dagwood_lib = {"lib/dagwood.lcl", lib_dagwood_lcl,
                                             sizeof(lib_dagwood_lcl)};

static bool load_embedded_dsl(lcl_interp *interp) {
  if (lcl_register_embedded_lib(interp, &dagwood_lib) != 0) {
    fprintf(stderr, "Error: failed to load dagwood library\n");
    return false;
  }

  return true;
}

/*
 * Special form: project <name> { <body> }
 *
 * Calls _project with the name and body to set up the project namespace
 * and evaluate the body with task/command/etc procs in scope.
 */
static int s_project(lcl_interp *interp, int argc, const lcl_word **args,
                     lcl_value **out) {
  if (argc != 2) {
    fprintf(stderr, "[dagwood] project requires exactly 2 arguments: name and "
                    "body\n");
    return LCL_RC_ERR;
  }

  lcl_value *name_val = NULL;
  int rc = lcl_eval_word(interp, args[0], &name_val);

  if (rc != LCL_RC_OK || !name_val) {
    return LCL_RC_ERR;
  }

  const char *name_str = lcl_value_to_string(name_val);

  if (!name_str || !name_str[0]) {
    fprintf(stderr, "[dagwood] project name cannot be empty\n");
    lcl_ref_dec(name_val);
    return LCL_RC_ERR;
  }

  char *name = strdup_safe(name_str);

  if (!name) {
    lcl_ref_dec(name_val);
    return LCL_RC_ERR;
  }

  lcl_value *body_val = NULL;
  rc = lcl_eval_word(interp, args[1], &body_val);

  if (rc != LCL_RC_OK || !body_val) {
    fprintf(stderr, "[dagwood] failed to evaluate project body\n");
    lcl_ref_dec(name_val);
    free(name);
    return LCL_RC_ERR;
  }

  lcl_value *project_proc = NULL;

  if (lcl_get(interp, "_project", &project_proc) != LCL_OK || !project_proc) {
    fprintf(stderr, "[dagwood] _project proc not found (DSL not loaded?)\n");
    lcl_ref_dec(name_val);
    lcl_ref_dec(body_val);
    free(name);
    return LCL_RC_ERR;
  }

  lcl_value *result = NULL;
  lcl_value *call_args[2] = {name_val, body_val};
  rc = lcl_call_proc(interp, project_proc, 2, call_args, &result);
  lcl_ref_dec(project_proc);
  lcl_ref_dec(name_val);
  lcl_ref_dec(body_val);

  if (rc != LCL_RC_OK || !result) {
    fprintf(stderr, "[dagwood] project '%s' initialization failed\n", name);

    if (result) {
      lcl_ref_dec(result);
    }

    free(name);
    return LCL_RC_ERR;
  }

  lcl_value *setup_code = NULL;
  lcl_value *body_code = NULL;

  if (lcl_list_get(result, 0, &setup_code) != LCL_OK || !setup_code ||
      lcl_list_get(result, 1, &body_code) != LCL_OK || !body_code) {
    fprintf(stderr, "[dagwood] project '%s': invalid return from _project\n",
            name);

    if (setup_code) {
      lcl_ref_dec(setup_code);
    }

    if (body_code) {
      lcl_ref_dec(body_code);
    }

    lcl_ref_dec(result);
    free(name);
    return LCL_RC_ERR;
  }

  const char *setup_str = lcl_value_to_string(setup_code);

  if (setup_str && setup_str[0]) {
    lcl_value *eval_result = NULL;
    rc = lcl_eval_string(interp, setup_str, &eval_result);

    if (eval_result) {
      lcl_ref_dec(eval_result);
    }

    if (rc != LCL_RC_OK) {
      fprintf(stderr, "[dagwood] project '%s' setup failed\n", name);
      lcl_ref_dec(setup_code);
      lcl_ref_dec(body_code);
      lcl_ref_dec(result);
      free(name);
      return LCL_RC_ERR;
    }
  }

  char set_proj_cmd[512];
  snprintf(set_proj_cmd, sizeof(set_proj_cmd),
           "set! dagwood::current_project %s", name);
  lcl_value *set_result = NULL;
  lcl_eval_string(interp, set_proj_cmd, &set_result);

  if (set_result) {
    lcl_ref_dec(set_result);
  }

  const char *body_str = lcl_value_to_string(body_code);

  if (body_str && body_str[0]) {
    lcl_value *eval_result = NULL;
    rc = lcl_eval_string(interp, body_str, &eval_result);

    if (eval_result) {
      lcl_ref_dec(eval_result);
    }

    if (rc != LCL_RC_OK) {
      fprintf(stderr, "[dagwood] project '%s' body evaluation failed\n", name);
      lcl_eval_string(interp, "set! dagwood::current_project ()", NULL);
      lcl_ref_dec(setup_code);
      lcl_ref_dec(body_code);
      lcl_ref_dec(result);
      free(name);
      return LCL_RC_ERR;
    }
  }

  lcl_eval_string(interp, "set! dagwood::current_project ()", NULL);
  lcl_ref_dec(setup_code);
  lcl_ref_dec(body_code);
  lcl_ref_dec(result);

  *out = lcl_string_new(name);
  free(name);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

struct interpreter {
  lcl_interp *interp;
  p_map *project_registry;
  t_map *task_registry;
  t_map *command_registry;
  bool built;
};

static char *strdup_safe(const char *s) {
  if (!s) {
    return NULL;
  }

  size_t len = strlen(s);
  char *dup = malloc(len + 1);

  if (dup) {
    memcpy(dup, s, len + 1);
  }

  return dup;
}

static char *make_qualified_name(const char *project, const char *name) {
  size_t plen = strlen(project);
  size_t nlen = strlen(name);
  char *qname = malloc(plen + 2 + nlen + 1);

  if (qname) {
    memcpy(qname, project, plen);
    qname[plen] = ':';
    qname[plen + 1] = ':';
    memcpy(qname + plen + 2, name, nlen + 1);
  }

  return qname;
}

static int c_platform_shell(lcl_interp *interp, int argc, lcl_value **argv,
                            lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;
  *out = lcl_string_new(dagwood_platform_shell());
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_platform_shell_arg(lcl_interp *interp, int argc, lcl_value **argv,
                                lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;
  *out = lcl_string_new(dagwood_platform_shell_arg());
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_pwd(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  (void)interp;
  (void)argc;
  (void)argv;

  char buf[4096];

  if (getcwd(buf, sizeof(buf)) == NULL) {
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(buf);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_cd(lcl_interp *interp, int argc, lcl_value **argv,
                lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    fprintf(stderr, "[dagwood] cd requires exactly 1 argument\n");

    return LCL_RC_ERR;
  }

  const char *dir = lcl_value_to_string(argv[0]);

  if (chdir(dir) != 0) {
    fprintf(stderr, "[dagwood] cd: cannot change to '%s'\n", dir);

    return LCL_RC_ERR;
  }

  *out = lcl_string_new(dir);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static void glob_recursive(const char *base, const char *pattern,
                           lcl_value **list) {
  DIR *dir = opendir(base[0] ? base : ".");
  if (!dir) {
    return;
  }

  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.' &&
        (entry->d_name[1] == '\0' ||
         (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
      continue;
    }

    char path[4096];
    int written;

    if (base[0]) {
      written = snprintf(path, sizeof(path), "%s/%s", base, entry->d_name);
    } else {
      written = snprintf(path, sizeof(path), "%s", entry->d_name);
    }

    if (written < 0 || (size_t)written >= sizeof(path)) {
      continue;
    }

    struct stat st;

    if (stat(path, &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
        if (strncmp(pattern, "**/", 3) == 0) {
          glob_recursive(path, pattern, list);
          glob_recursive(path, pattern + 3, list);
        } else {
          const char *slash = strchr(pattern, '/');

          if (slash) {
            char component[256];
            size_t clen = (size_t)(slash - pattern);

            if (clen < sizeof(component)) {
              memcpy(component, pattern, clen);
              component[clen] = '\0';

              if (fnmatch(component, entry->d_name, 0) == 0) {
                glob_recursive(path, slash + 1, list);
              }
            }
          }
        }
      } else if (S_ISREG(st.st_mode)) {
        const char *file_pattern = strrchr(pattern, '/');
        file_pattern = file_pattern ? file_pattern + 1 : pattern;

        if (fnmatch(file_pattern, entry->d_name, 0) == 0) {
          if (strchr(pattern, '/') == NULL || strncmp(pattern, "**/", 3) == 0) {
            lcl_value *item = lcl_string_new(path);

            if (item) {
              lcl_list_push(list, item);
              lcl_ref_dec(item);
            }
          }
        }
      }
    }
  }

  closedir(dir);
}

static int c_glob(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  (void)interp;

  *out = lcl_list_new();
  if (!*out) {
    return LCL_RC_ERR;
  }

  for (int i = 0; i < argc; i++) {
    const char *pattern = lcl_value_to_string(argv[i]);

    if (!pattern) {
      continue;
    }

    const char *slash = strchr(pattern, '/');

    if (slash == NULL) {
      DIR *dir = opendir(".");

      if (dir) {
        struct dirent *entry;

        while ((entry = readdir(dir)) != NULL) {
          if (entry->d_name[0] == '.') {
            continue;
          }

          if (fnmatch(pattern, entry->d_name, 0) == 0) {
            lcl_value *item = lcl_string_new(entry->d_name);

            if (item) {
              lcl_list_push(out, item);
              lcl_ref_dec(item);
            }
          }
        }
        closedir(dir);
      }
    } else if (strncmp(pattern, "**/", 3) == 0) {
      glob_recursive("", pattern, out);
    } else {
      char base[4096];
      size_t base_len = (size_t)(slash - pattern);

      if (base_len < sizeof(base)) {
        memcpy(base, pattern, base_len);
        base[base_len] = '\0';

        DIR *dir = opendir(base);

        if (dir) {
          const char *file_pattern = slash + 1;
          struct dirent *entry;

          while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') {
              continue;
            }

            if (fnmatch(file_pattern, entry->d_name, 0) == 0) {
              char path[4096];
              int written =
                  snprintf(path, sizeof(path), "%s/%s", base, entry->d_name);

              if (written < 0 || (size_t)written >= sizeof(path)) {
                continue;
              }

              lcl_value *item = lcl_string_new(path);

              if (item) {
                lcl_list_push(out, item);
                lcl_ref_dec(item);
              }
            }
          }

          closedir(dir);
        }
      }
    }
  }

  return LCL_RC_OK;
}

static int c_file(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  (void)interp;

  if (argc < 1) {
    fprintf(stderr, "[dagwood] file requires a subcommand\n");
    return LCL_RC_ERR;
  }

  const char *subcmd = lcl_value_to_string(argv[0]);

  if (strcmp(subcmd, "exists") == 0) {
    if (argc != 2) {
      fprintf(stderr, "[dagwood] file exists requires 1 argument\n");
      return LCL_RC_ERR;
    }

    const char *path = lcl_value_to_string(argv[1]);
    struct stat st;
    *out = lcl_int_new(stat(path, &st) == 0 ? 1 : 0);
  } else if (strcmp(subcmd, "dirname") == 0) {
    if (argc != 2) {
      fprintf(stderr, "[dagwood] file dirname requires 1 argument\n");

      return LCL_RC_ERR;
    }

    const char *path = lcl_value_to_string(argv[1]);
    char *dup = strdup_safe(path);

    if (!dup) {
      return LCL_RC_ERR;
    }

    char *last_slash = strrchr(dup, '/');

    if (last_slash) {
      *last_slash = '\0';
      *out = lcl_string_new(dup[0] ? dup : "/");
    } else {
      *out = lcl_string_new(".");
    }
    free(dup);
  } else if (strcmp(subcmd, "join") == 0) {
    if (argc < 2) {
      fprintf(stderr, "[dagwood] file join requires at least 1 argument\n");

      return LCL_RC_ERR;
    }

    char result[4096] = "";

    for (int i = 1; i < argc; i++) {
      const char *part = lcl_value_to_string(argv[i]);

      if (i > 1 && result[0]) {
        strncat(result, "/", sizeof(result) - strlen(result) - 1);
      }

      strncat(result, part, sizeof(result) - strlen(result) - 1);
    }
    *out = lcl_string_new(result);
  } else if (strcmp(subcmd, "normalize") == 0) {
    if (argc != 2) {
      fprintf(stderr, "[dagwood] file normalize requires 1 argument\n");
      return LCL_RC_ERR;
    }

    const char *path = lcl_value_to_string(argv[1]);
    char resolved[4096];

    if (realpath(path, resolved)) {
      *out = lcl_string_new(resolved);
    } else {
      *out = lcl_string_new(path);
    }
  } else {
    fprintf(stderr, "[dagwood] file: unknown subcommand '%s'\n", subcmd);

    return LCL_RC_ERR;
  }

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* CLI args are stored at ::__dagwood_cli_args::<name> by
 * interpreter_set_cli_arg below. The Lcl-side `arg` proc (in
 * lib/dagwood.lcl) reads them via getvar. */
#define CLI_ARG_PREFIX "::__dagwood_cli_args::"

static char *extract_dict_string(lcl_value *dict, const char *key) {
  lcl_value *val = NULL;

  if (lcl_dict_get(dict, key, &val) != LCL_OK || !val) {
    return NULL;
  }

  const char *str = lcl_value_to_string(val);
  char *result = str ? strdup_safe(str) : NULL;
  lcl_ref_dec(val);
  return result;
}

static bool extract_dict_bool(lcl_value *dict, const char *key) {
  lcl_value *val = NULL;

  if (lcl_dict_get(dict, key, &val) != LCL_OK || !val) {
    return false;
  }

  const char *str = lcl_value_to_string(val);
  bool result = str && (strcmp(str, "true") == 0 || strcmp(str, "1") == 0);
  lcl_ref_dec(val);
  return result;
}

static bool is_ident_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == ':';
}

static char *substitute_namespace_vars(lcl_interp *interp, const char *script) {
  if (!script) {
    return NULL;
  }

  size_t script_len = strlen(script);
  size_t out_capacity = script_len * 2 + 16;
  char *out = malloc(out_capacity);

  if (!out) {
    return strdup_safe(script);
  }

  size_t out_len = 0;
  const char *p = script;

  while (*p) {
    if (*p == '$') {
      const char *var_start = p + 1;
      const char *var_end = var_start;
      bool braced = false;

      if (*var_start == '{') {
        braced = true;
        var_start++;
        var_end = var_start;

        while (*var_end && *var_end != '}') {
          var_end++;
        }
      } else {
        while (is_ident_char(*var_end)) {
          var_end++;
        }
      }

      size_t var_len = var_end - var_start;
      bool is_namespace_var = false;

      for (const char *c = var_start; c < var_end - 1; c++) {
        if (c[0] == ':' && c[1] == ':') {
          is_namespace_var = true;
          break;
        }
      }

      if (is_namespace_var && var_len > 0) {
        char *lookup = malloc(var_len + 2);

        if (lookup) {
          lookup[0] = '$';
          memcpy(lookup + 1, var_start, var_len);
          lookup[var_len + 1] = '\0';
          lcl_value *result = NULL;
          int rc = lcl_eval_string(interp, lookup, &result);
          free(lookup);

          const char *replacement = NULL;

          if (rc == LCL_RC_OK && result) {
            replacement = lcl_value_to_string(result);
          }

          if (!replacement) {
            char var_name[256];
            size_t copy_len =
                var_len < sizeof(var_name) - 1 ? var_len : sizeof(var_name) - 1;
            memcpy(var_name, var_start, copy_len);
            var_name[copy_len] = '\0';
            fprintf(stderr, "[dagwood] undefined namespace variable: $%s\n",
                    var_name);

            if (result) {
              lcl_ref_dec(result);
            }

            free(out);
            return NULL;
          }

          size_t repl_len = strlen(replacement);

          while (out_len + repl_len + 1 > out_capacity) {
            out_capacity *= 2;
            char *new_out = realloc(out, out_capacity);

            if (!new_out) {
              lcl_ref_dec(result);
              free(out);
              return strdup_safe(script);
            }

            out = new_out;
          }

          memcpy(out + out_len, replacement, repl_len);
          out_len += repl_len;
          lcl_ref_dec(result);

          p = braced ? (var_end + 1) : var_end;
          continue;
        }
      }
    }

    if (out_len + 2 > out_capacity) {
      out_capacity *= 2;
      char *new_out = realloc(out, out_capacity);

      if (!new_out) {
        free(out);
        return strdup_safe(script);
      }

      out = new_out;
    }

    out[out_len++] = *p++;
  }

  out[out_len] = '\0';
  return out;
}

static char *extract_run_script(lcl_interp *interp, lcl_value *dict,
                                bool *error) {
  *error = false;
  char *raw = extract_dict_string(dict, "run");

  if (!raw) {
    return NULL;
  }

  char *substituted = substitute_namespace_vars(interp, raw);
  free(raw);

  if (!substituted) {
    *error = true;
  }

  return substituted;
}

static void extract_string_list(lcl_value *val, s_arr **arr) {
  if (!val) {
    return;
  }

  size_t len = lcl_list_len(val);

  if (len > 0) {
    for (size_t i = 0; i < len; i++) {
      lcl_value *item = NULL;

      if (lcl_list_get(val, i, &item) == LCL_OK && item) {
        const char *str = lcl_value_to_string(item);

        if (str && str[0]) {
          if (!*arr) {
            *arr = s_arr_new();
          }

          s_arr_push(*arr, strdup_safe(str));
        }

        lcl_ref_dec(item);
      }
    }
  } else {
    const char *str = lcl_value_to_string(val);

    if (str && str[0]) {
      if (!*arr) {
        *arr = s_arr_new();
      }

      s_arr_push(*arr, strdup_safe(str));
    }
  }
}

static dagwood_task *extract_task(lcl_interp *interp, const char *project_name,
                                  const char *task_name, lcl_value *task_dict,
                                  dagwood_project *project) {
  dagwood_task *task = dagwood_task_new();

  if (!task) {
    return NULL;
  }

  free((void *)task->name);
  task->name = strdup_safe(task_name);
  free((void *)task->id);
  task->id = make_qualified_name(project_name, task_name);
  free((void *)task->description);
  task->description = extract_dict_string(task_dict, "description");
  bool run_error = false;
  char *run_script = extract_run_script(interp, task_dict, &run_error);

  if (run_error) {
    fprintf(stderr, "[dagwood] failed to resolve variables in task '%s::%s'\n",
            project_name, task_name);
    dagwood_task_delete(task);
    return NULL;
  }

  if (run_script) {
    free((void *)task->run);
    task->run = run_script;
  }

  task->always_run = extract_dict_bool(task_dict, "always_run");
  task->shell = project->shell ? strdup(project->shell) : NULL;
  task->shell_arg = project->shell_arg ? strdup(project->shell_arg) : NULL;
  task->wd = project->cwd ? strdup(project->cwd) : NULL;

  lcl_value *inputs_val = NULL;

  if (lcl_dict_get(task_dict, "inputs", &inputs_val) == LCL_OK && inputs_val) {
    extract_string_list(inputs_val, &task->inputs);
    lcl_ref_dec(inputs_val);
  }

  lcl_value *outputs_val = NULL;

  if (lcl_dict_get(task_dict, "outputs", &outputs_val) == LCL_OK &&
      outputs_val) {
    extract_string_list(outputs_val, &task->outputs);
    lcl_ref_dec(outputs_val);
  }

  lcl_value *depends_val = NULL;

  if (lcl_dict_get(task_dict, "depends-on", &depends_val) == LCL_OK &&
      depends_val) {
    extract_string_list(depends_val, &task->depends_on);
    lcl_ref_dec(depends_val);
  }

  return task;
}

static void extract_project_config(lcl_interp *interp, const char *project_name,
                                   dagwood_project *project) {
  char config_cmd[512];
  snprintf(config_cmd, sizeof(config_cmd), "$%s::_config", project_name);

  lcl_value *config_dict = NULL;

  if (lcl_eval_string(interp, config_cmd, &config_dict) != LCL_RC_OK ||
      !config_dict) {
    return;
  }

  char *shell = extract_dict_string(config_dict, "shell");

  if (shell) {
    free((void *)project->shell);
    project->shell = shell;
  }

  char *shell_arg = extract_dict_string(config_dict, "shell_arg");

  if (shell_arg) {
    free((void *)project->shell_arg);
    project->shell_arg = shell_arg;
  }

  project->always_run = extract_dict_bool(config_dict, "always_run");

  char *desc = extract_dict_string(config_dict, "description");

  if (desc) {
    free((void *)project->description);
    project->description = desc;
  }

  lcl_ref_dec(config_dict);
}

static bool extract_all_projects(lcl_interp *interp, p_map *project_registry,
                                 t_map *task_registry,
                                 t_map *command_registry) {
  lcl_value *projects_list = NULL;
  int rc = lcl_eval_string(interp, "$dagwood::projects", &projects_list);

  if (rc != LCL_RC_OK || !projects_list) {
    return true;
  }

  size_t num_projects = lcl_list_len(projects_list);

  for (size_t i = 0; i < num_projects; i++) {
    lcl_value *project_name_val = NULL;

    if (lcl_list_get(projects_list, i, &project_name_val) != LCL_OK ||
        !project_name_val) {
      continue;
    }

    const char *project_name = lcl_value_to_string(project_name_val);

    dagwood_project *project = dagwood_project_new();

    if (!project) {
      lcl_ref_dec(project_name_val);
      continue;
    }

    free((void *)project->name);
    project->name = strdup_safe(project_name);
    free((void *)project->shell);
    project->shell = strdup_safe(dagwood_platform_shell());
    free((void *)project->shell_arg);
    project->shell_arg = strdup_safe(dagwood_platform_shell_arg());

    char cwd[4096];

    if (getcwd(cwd, sizeof(cwd))) {
      free((void *)project->cwd);
      project->cwd = strdup_safe(cwd);
    }

    extract_project_config(interp, project_name, project);
    p_map_set(project_registry, project_name, project);

    char tasks_cmd[512];
    snprintf(tasks_cmd, sizeof(tasks_cmd), "$%s::_tasks", project_name);

    lcl_value *tasks_dict = NULL;
    int rc = lcl_eval_string(interp, tasks_cmd, &tasks_dict);

    if (rc == LCL_RC_OK && tasks_dict) {
      char keys_cmd[1024];
      snprintf(keys_cmd, sizeof(keys_cmd), "Dict::keys $%s::_tasks",
               project_name);
      lcl_value *keys_result = NULL;

      if (lcl_eval_string(interp, keys_cmd, &keys_result) == LCL_RC_OK &&
          keys_result) {
        size_t num_tasks = lcl_list_len(keys_result);

        for (size_t j = 0; j < num_tasks; j++) {
          lcl_value *key_val = NULL;

          if (lcl_list_get(keys_result, j, &key_val) != LCL_OK || !key_val) {
            continue;
          }

          const char *task_name = lcl_value_to_string(key_val);
          lcl_value *task_dict = NULL;

          if (lcl_dict_get(tasks_dict, task_name, &task_dict) == LCL_OK &&
              task_dict) {
            dagwood_task *task =
                extract_task(interp, project_name, task_name, task_dict, project);

            if (!task) {
              lcl_ref_dec(task_dict);
              lcl_ref_dec(key_val);
              lcl_ref_dec(keys_result);
              lcl_ref_dec(tasks_dict);
              lcl_ref_dec(project_name_val);
              lcl_ref_dec(projects_list);
              return false;
            }

            t_map_set(task_registry, task->id, task);
            lcl_ref_dec(task_dict);
          }

          lcl_ref_dec(key_val);
        }

        lcl_ref_dec(keys_result);
      }

      lcl_ref_dec(tasks_dict);
    }

    char commands_cmd[512];
    snprintf(commands_cmd, sizeof(commands_cmd), "$%s::_commands",
             project_name);
    lcl_value *commands_dict = NULL;

    if (lcl_eval_string(interp, commands_cmd, &commands_dict) == LCL_RC_OK &&
        commands_dict) {
      char keys_cmd[1024];
      snprintf(keys_cmd, sizeof(keys_cmd), "Dict::keys $%s::_commands",
               project_name);
      lcl_value *keys_result = NULL;

      if (lcl_eval_string(interp, keys_cmd, &keys_result) == LCL_RC_OK &&
          keys_result) {
        size_t num_commands = lcl_list_len(keys_result);

        for (size_t j = 0; j < num_commands; j++) {
          lcl_value *key_val = NULL;

          if (lcl_list_get(keys_result, j, &key_val) != LCL_OK || !key_val) {
            continue;
          }

          const char *cmd_name = lcl_value_to_string(key_val);

          lcl_value *cmd_dict = NULL;

          if (lcl_dict_get(commands_dict, cmd_name, &cmd_dict) == LCL_OK &&
              cmd_dict) {
            dagwood_task *cmd =
                extract_task(interp, project_name, cmd_name, cmd_dict, project);

            if (!cmd) {
              lcl_ref_dec(cmd_dict);
              lcl_ref_dec(key_val);
              lcl_ref_dec(keys_result);
              lcl_ref_dec(commands_dict);
              lcl_ref_dec(project_name_val);
              lcl_ref_dec(projects_list);
              return false;
            }

            t_map_set(command_registry, cmd->id, cmd);
            lcl_ref_dec(cmd_dict);
          }

          lcl_ref_dec(key_val);
        }

        lcl_ref_dec(keys_result);
      }

      lcl_ref_dec(commands_dict);
    }

    lcl_ref_dec(project_name_val);
  }

  lcl_ref_dec(projects_list);
  return true;
}

static void register_dagwood_commands(lcl_interp *interp) {
  lcl_register_proc(interp, "platform-shell", c_platform_shell);
  lcl_register_proc(interp, "platform-shell-arg", c_platform_shell_arg);

  /* File utilities */
  lcl_register_proc(interp, "pwd", c_pwd);
  lcl_register_proc(interp, "cd", c_cd);
  lcl_register_proc(interp, "glob", c_glob);
  lcl_register_proc(interp, "file", c_file);

  /* DSL special forms - these call the LCL-based DSL and eval the result */
  lcl_register_spec(interp, "project", s_project);
}

interpreter *interpreter_new(void) {
  assert(!g_ctx.active &&
         "interpreter_new called while another interpreter is active; "
         "Dagwood embeds at most one Lcl interpreter per process");

  interpreter *w = calloc(1, sizeof(*w));
  if (!w) {
    return NULL;
  }

  w->project_registry = p_map_new();

  if (!w->project_registry) {
    free(w);
    return NULL;
  }

  w->task_registry = t_map_new();

  if (!w->task_registry) {
    p_map_delete(w->project_registry);
    free(w);
    return NULL;
  }

  w->command_registry = t_map_new();

  if (!w->command_registry) {
    t_map_delete(w->task_registry);
    p_map_delete(w->project_registry);
    free(w);
    return NULL;
  }

  w->interp = lcl_interp_new();

  if (!w->interp) {
    t_map_delete(w->command_registry);
    t_map_delete(w->task_registry);
    p_map_delete(w->project_registry);
    free(w);
    return NULL;
  }

  lcl_register_core(w->interp);
  lcl_register_io(w->interp);

  g_ctx.active = true;

  register_dagwood_commands(w->interp);

  if (!load_embedded_dsl(w->interp)) {
    t_map_delete(w->command_registry);
    t_map_delete(w->task_registry);
    p_map_delete(w->project_registry);
    lcl_interp_free(w->interp);
    free(w);
    g_ctx.active = false;
    return NULL;
  }

  return w;
}

void interpreter_set_cli_arg(interpreter *interp, const char *name,
                             const char *value) {
  if (!interp || !name || !value) {
    return;
  }

  char cli_key[256];
  snprintf(cli_key, sizeof(cli_key), CLI_ARG_PREFIX "%s", name);

  lcl_value *val = lcl_string_new(value);

  if (val) {
    lcl_define(interp->interp, cli_key, val);
    lcl_ref_dec(val);
  }
}

void interpreter_delete(interpreter *interp) {
  if (!interp) {
    return;
  }

  p_map *p = interp->project_registry;

  for (size_t i = p_map_begin(p); i < p_map_end(p); i++) {
    dagwood_project *project = p_map_value(p, i);

    if (project) {
      dagwood_project_delete(project);
    }
  }

  t_map *c = interp->command_registry;
  t_map *t = interp->task_registry;

  for (size_t j = t_map_begin(c); j < t_map_end(c); j++) {
    dagwood_task *task = t_map_value(c, j);

    if (task && !t_map_exists(t, task->id)) {
      dagwood_task_delete(task);
    }
  }

  for (size_t j = t_map_begin(t); j < t_map_end(t); j++) {
    dagwood_task *task = t_map_value(t, j);

    if (task) {
      dagwood_task_delete(task);
    }
  }

  t_map_delete(t);
  t_map_delete(c);
  p_map_delete(p);

  lcl_interp_free(interp->interp);
  free(interp);

  g_ctx.active = false;
}

const char *interpreter_get_error(interpreter *interp) {
  if (!interp || !interp->interp) {
    return NULL;
  }

  const char *file = lcl_interp_error_file(interp->interp);
  int line = lcl_interp_error_line(interp->interp);
  const char *msg = lcl_interp_error_msg(interp->interp);

  static char error_buf[512];

  if (file && msg) {
    snprintf(error_buf, sizeof(error_buf), "%s (at %s:%d)", msg, file, line);
  } else if (msg) {
    snprintf(error_buf, sizeof(error_buf), "%s (at line %d)", msg, line);
  } else if (file) {
    snprintf(error_buf, sizeof(error_buf), "Error at %s:%d", file, line);
  } else {
    snprintf(error_buf, sizeof(error_buf), "Error at line %d", line);
  }

  return error_buf;
}

static interp_result build(interpreter *interp, const char *file) {
  if (!interp || !file) {
    fprintf(stderr, "[dagwood] bad file or interpreter\n");
    return INTERP_ERROR;
  }

  lcl_value *result = NULL;
  int rc = lcl_eval_file(interp->interp, file, &result);

  if (rc != LCL_RC_OK) {
    fprintf(stderr, "[dagwood] LCL Error: %s\n", interpreter_get_error(interp));

    if (result) {
      lcl_ref_dec(result);
    }

    return INTERP_ERROR;
  }

  if (result) {
    lcl_ref_dec(result);
  }

  if (!extract_all_projects(interp->interp, interp->project_registry,
                            interp->task_registry, interp->command_registry)) {
    fprintf(stderr, "[dagwood] Failed to extract project data\n");
    return INTERP_ERROR;
  }

  return INTERP_OK;
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

t_map *interpreter_command_registry(interpreter *interp, const char *file) {
  if (!interp->built) {
    if (build(interp, file) != INTERP_OK) {
      return NULL;
    }

    interp->built = true;
  }

  return interp->command_registry;
}

interp_result interpreter_list_all(interpreter *interp) {
  printf("Projects:\n");
  p_map *p = interp->project_registry;

  for (size_t i = p_map_begin(p); i < p_map_end(p); i++) {
    dagwood_project *proj = p_map_value(p, i);

    if (proj) {
      printf("  %s\n", proj->name);

      if (proj->description) {
        printf("    description: %s\n", proj->description);
      }
    }
  }

  printf("Tasks:\n");
  t_map *t = interp->task_registry;

  for (size_t i = t_map_begin(t); i < t_map_end(t); i++) {
    dagwood_task *task = t_map_value(t, i);

    if (task) {
      printf("  %s\n", task->id);

      if (task->description) {
        printf("    description: %s\n", task->description);
      }
    }
  }

  printf("Commands:\n");
  t_map *c = interp->command_registry;

  for (size_t i = t_map_begin(c); i < t_map_end(c); i++) {
    dagwood_task *cmd = t_map_value(c, i);

    if (cmd) {
      printf("  %s\n", cmd->id);
    }
  }

  return INTERP_OK;
}

static void inspect_print_arr(const char *label, s_arr *arr) {
  if (!arr || s_arr_len(arr) == 0) {
    return;
  }

  printf("    %s:", label);

  for (size_t i = 0; i < s_arr_len(arr); i++) {
    printf(" %s", s_arr_get(arr, i));
  }

  printf("\n");
}

static void inspect_task(dagwood_task *task) {
  printf("  %s\n", task->id);

  if (task->description) {
    printf("    description: %s\n", task->description);
  }

  if (task->shell) {
    printf("    shell: %s\n", task->shell);
  }

  if (task->shell_arg) {
    printf("    shell_arg: %s\n", task->shell_arg);
  }

  if (task->wd) {
    printf("    wd: %s\n", task->wd);
  }

  if (task->run) {
    printf("    run: %.80s%s\n", task->run,
           strlen(task->run) > 80 ? "..." : "");
  }

  if (task->always_run) {
    printf("    always_run: true\n");
  }

  inspect_print_arr("inputs", task->inputs);
  inspect_print_arr("outputs", task->outputs);
  inspect_print_arr("depends_on", task->depends_on);
}

interp_result interpreter_inspect(interpreter *interp, const char *task_name) {
  if (task_name) {
    dagwood_task *task = t_map_get(interp->task_registry, task_name);

    if (!task) {
      task = t_map_get(interp->command_registry, task_name);
    }

    if (!task) {
      fprintf(stderr, "[dagwood] task not found: %s\n", task_name);
      return INTERP_ERROR;
    }

    inspect_task(task);
    return INTERP_OK;
  }

  t_map *t = interp->task_registry;
  printf("Tasks:\n");

  for (size_t i = t_map_begin(t); i < t_map_end(t); i++) {
    dagwood_task *task = t_map_value(t, i);

    if (task) {
      inspect_task(task);
    }
  }

  t_map *c = interp->command_registry;

  if (c && t_map_begin(c) < t_map_end(c)) {
    printf("Commands:\n");

    for (size_t i = t_map_begin(c); i < t_map_end(c); i++) {
      dagwood_task *cmd = t_map_value(c, i);

      if (cmd) {
        inspect_task(cmd);
      }
    }
  }

  return INTERP_OK;
}

interp_result interpreter_repl(interpreter *interp) {
  char line[1024];
  printf("Dagwood REPL (type 'exit' to quit)\n");

  while (1) {
    printf("dagwood> ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
      break;
    }

    if (strncmp(line, "exit", 4) == 0 &&
        (line[4] == '\0' || line[4] == '\n' || line[4] == '\r' ||
         line[4] == ' ' || line[4] == '\t')) {
      break;
    }

    lcl_value *result = NULL;
    int rc = lcl_eval_string(interp->interp, line, &result);

    if (rc == LCL_RC_OK) {
      if (result) {
        const char *str = lcl_value_to_string(result);

        if (str && str[0]) {
          printf("%s\n", str);
        }

        lcl_ref_dec(result);
      }
    } else {
      fprintf(stderr, "[dagwood] Error: %s\n", interpreter_get_error(interp));

      if (result) {
        lcl_ref_dec(result);
      }
    }
  }

  return INTERP_OK;
}
