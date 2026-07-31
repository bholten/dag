#define _GNU_SOURCE

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Dagwood embeds a single Lcl interpreter per process. Only one piece
 * of state genuinely needs to live as a file-scope global:
 *
 *   active — singleton guard, set in interpreter_new and cleared in
 *            interpreter_delete. The assert in interpreter_new makes
 *            the constraint loud.
 *
 * The whole DSL surface lives in lib/dagwood.lcl as pure Lcl —
 * including `project`, which is a macro that tracks the active
 * project via ${dagwood::current_project}.
 */

/* clang-format off */
/* lcl.h must be included before lcl-io (or any libraries)
 * Turn clang-format off for being aggressive */
#include <lcl.h>
#include <lcl-io.h>
#include <lcl-posix.h>
#include <lcl-process.h>
#include <lcl-time.h>
/* clang-format on */

#include "dagwood.h"
#include "dagwood_dsl.h"
#include "data.h"
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

#define CLI_ARG_NS "__dagwood_cli_args"

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

/* Find "::" within [s, s + len); NULL if absent. */
static const char *find_colons(const char *s, size_t len) {
  for (size_t i = 0; i + 1 < len; i++) {
    if (s[i] == ':' && s[i + 1] == ':') {
      return s + i;
    }
  }

  return NULL;
}

static bool out_append(char **out, size_t *len, size_t *cap, const char *s,
                       size_t n) {
  while (*len + n + 1 > *cap) {
    size_t new_cap = *cap * 2;
    char *new_out = realloc(*out, new_cap);

    if (!new_out) {
      return false;
    }

    *out = new_out;
    *cap = new_cap;
  }

  memcpy(*out + *len, s, n);
  *len += n;
  return true;
}

static bool is_registered_project(lcl_interp *interp, const char *name,
                                  size_t len) {
  lcl_value *projects = NULL;

  if (lcl_eval_string(interp, "${dagwood::projects}", &projects) != LCL_RC_OK ||
      !projects) {
    return false;
  }

  bool found = false;
  size_t n = lcl_list_len(projects);

  for (size_t i = 0; i < n && !found; i++) {
    lcl_value *item = NULL;

    if (lcl_list_get(projects, i, &item) == LCL_OK && item) {
      const char *s = lcl_value_to_string(item);

      if (s && strlen(s) == len && strncmp(s, name, len) == 0) {
        found = true;
      }

      lcl_ref_dec(item);
    }
  }

  lcl_ref_dec(projects);
  return found;
}

/* Splice explicit ${name::path} references into a run body.
 *
 * Run bodies are opaque foreign-language text (docs/splicing.md):
 *   - ${...::...} is a Dagwood splice, claimed unconditionally and
 *     resolved in the defining interpreter; failure to resolve aborts
 *     the load (NULL return).
 *   - \${ emits a literal "${"; \\${ emits a backslash then a live
 *     splice. A backslash is only special immediately before "${".
 *   - Everything else passes through verbatim, including ${HOME},
 *     $1, and bare $name::path. A bare qualified reference whose
 *     prefix is a registered project gets a forgot-the-braces
 *     warning (lint only; the registry never affects semantics).
 */
static char *substitute_namespace_vars(lcl_interp *interp, const char *script,
                                       const char *task_id) {
  if (!script) {
    return NULL;
  }

  size_t cap = strlen(script) * 2 + 16;
  size_t len = 0;
  char *out = malloc(cap);

  if (!out) {
    return NULL;
  }

  const char *p = script;

  while (*p) {
    if (p[0] == '\\' && p[1] == '\\' && p[2] == '$' && p[3] == '{') {
      /* \\${ -> literal backslash, then process ${ as a splice */
      if (!out_append(&out, &len, &cap, "\\", 1)) {
        goto oom;
      }

      p += 2;
      continue;
    }

    if (p[0] == '\\' && p[1] == '$' && p[2] == '{') {
      /* \${ -> literal "${", splice suppressed */
      if (!out_append(&out, &len, &cap, "${", 2)) {
        goto oom;
      }

      p += 3;
      continue;
    }

    if (p[0] == '$' && p[1] == '{') {
      const char *start = p + 2;
      const char *end = strchr(start, '}');

      if (!end) {
        /* Unterminated ${...: guest text, copy the rest verbatim */
        if (!out_append(&out, &len, &cap, p, strlen(p))) {
          goto oom;
        }

        break;
      }

      size_t var_len = (size_t)(end - start);
      const char *sep = find_colons(start, var_len);

      if (!sep) {
        /* ${HOME}, ${var:-default}, ...: guest syntax, verbatim */
        if (!out_append(&out, &len, &cap, p, (size_t)(end + 1 - p))) {
          goto oom;
        }

        p = end + 1;
        continue;
      }

      /* Dagwood splice. The splice spelling ${name::path} is exactly
       * Lcl's braced qualified substitution, so the run-body text
       * "${...}" is evaluated as-is: no rewriting, and Lcl applies its
       * own qualname grammar check to the contents. */
      /* ${self::path} names the task being extracted: rewrite the
       * prefix to <project>::<task> (task_id is already "proj::task")
       * and evaluate as an ordinary braced reference. Lcl's reference
       * grammar accepts '-'/'?'/'!' in segments (kebab-case task
       * names included) since d71b1a43. `self` is a reserved project
       * name in the DSL, so the two can never collide. */
      size_t lookup_len;
      char *lookup;
      size_t prefix_len = (size_t)(sep - start);

      if (prefix_len == 4 && memcmp(start, "self", 4) == 0) {
        const char *rest = sep + 2;
        size_t rest_len = (size_t)(end - rest);
        size_t id_len = strlen(task_id);

        /* "${" + id + "::" + rest + "}" */
        lookup_len = 2 + id_len + 2 + rest_len + 1;
        lookup = malloc(lookup_len + 1);

        if (!lookup) {
          goto oom;
        }

        snprintf(lookup, lookup_len + 1, "${%s::%.*s}", task_id, (int)rest_len,
                 rest);
      } else {
        lookup_len = (size_t)(end + 1 - p);
        lookup = malloc(lookup_len + 1);

        if (!lookup) {
          goto oom;
        }

        memcpy(lookup, p, lookup_len);
        lookup[lookup_len] = '\0';
      }

      lcl_value *result = NULL;
      int rc = lcl_eval_string(interp, lookup, &result);
      free(lookup);

      const char *replacement = NULL;

      if (rc == LCL_RC_OK && result) {
        replacement = lcl_value_to_string(result);
      }

      if (!replacement) {
        if (prefix_len == 4 && memcmp(start, "self", 4) == 0) {
          fprintf(stderr,
                  "[dagwood] %s: task has no attribute '%.*s' (in splice "
                  "${%.*s})\n",
                  task_id, (int)(end - (sep + 2)), sep + 2, (int)var_len,
                  start);
        } else if (!is_registered_project(interp, start, prefix_len)) {
          fprintf(stderr,
                  "[dagwood] %s: unknown project '%.*s' in splice ${%.*s}; "
                  "use \\${...} for literal text\n",
                  task_id, (int)(sep - start), start, (int)var_len, start);
        } else {
          fprintf(stderr,
                  "[dagwood] %s: undefined variable in splice ${%.*s}\n",
                  task_id, (int)var_len, start);
        }

        if (result) {
          lcl_ref_dec(result);
        }

        free(out);
        return NULL;
      }

      bool ok = out_append(&out, &len, &cap, replacement, strlen(replacement));
      lcl_ref_dec(result);

      if (!ok) {
        goto oom;
      }

      p = end + 1;
      continue;
    }

    if (p[0] == '$') {
      /* Bare reference: verbatim, but lint likely-missing braces */
      const char *var_start = p + 1;
      const char *var_end = var_start;

      while (is_ident_char(*var_end)) {
        var_end++;
      }

      size_t var_len = (size_t)(var_end - var_start);
      const char *sep = find_colons(var_start, var_len);

      if (sep && sep > var_start &&
          is_registered_project(interp, var_start, (size_t)(sep - var_start))) {
        fprintf(stderr,
                "[dagwood] warning: %s: bare $%.*s is not substituted; "
                "write ${%.*s} to splice it\n",
                task_id, (int)var_len, var_start, (int)var_len, var_start);
      }

      /* var_end >= p + 1 always, so this copies at least the "$";
       * for $?, $!, or a lone $ the next char is handled by the
       * ordinary copy path on the following iteration. */
      if (!out_append(&out, &len, &cap, p, (size_t)(var_end - p))) {
        goto oom;
      }

      p = var_end;
      continue;
    }

    if (!out_append(&out, &len, &cap, p, 1)) {
      goto oom;
    }

    p++;
  }

  out[len] = '\0';
  return out;

oom:
  free(out);
  return NULL;
}

static char *extract_run_script(lcl_interp *interp, lcl_value *dict,
                                const char *task_id, bool *error) {
  *error = false;
  char *raw = extract_dict_string(dict, "run");

  if (!raw) {
    return NULL;
  }

  char *substituted = substitute_namespace_vars(interp, raw, task_id);
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
  char *run_script =
      extract_run_script(interp, task_dict, task->id, &run_error);

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

  task->always_run = extract_dict_bool(task_dict, "always-run");
  task->wd = project->cwd ? strdup(project->cwd) : NULL;

  char *shell = extract_dict_string(task_dict, "shell");

  if (shell) {
    free((void *)task->shell);
    task->shell = shell;
  } else {
    task->shell = project->shell ? strdup(project->shell) : NULL;
  }

  char *shell_arg = extract_dict_string(task_dict, "shell-arg");

  if (shell_arg) {
    free((void *)task->shell_arg);
    task->shell_arg = shell_arg;
  } else {
    task->shell_arg = project->shell_arg ? strdup(project->shell_arg) : NULL;
  }

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
  snprintf(config_cmd, sizeof(config_cmd), "${%s::_config}", project_name);

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

  char *shell_arg = extract_dict_string(config_dict, "shell-arg");

  if (shell_arg) {
    free((void *)project->shell_arg);
    project->shell_arg = shell_arg;
  }

  project->always_run = extract_dict_bool(config_dict, "always-run");

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
  int rc = lcl_eval_string(interp, "${dagwood::projects}", &projects_list);

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
    snprintf(tasks_cmd, sizeof(tasks_cmd), "${%s::_tasks}", project_name);

    lcl_value *tasks_dict = NULL;
    int rc = lcl_eval_string(interp, tasks_cmd, &tasks_dict);

    if (rc == LCL_RC_OK && tasks_dict) {
      char keys_cmd[1024];
      snprintf(keys_cmd, sizeof(keys_cmd), "Dict::keys ${%s::_tasks}",
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
            dagwood_task *task = extract_task(interp, project_name, task_name,
                                              task_dict, project);

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
    snprintf(commands_cmd, sizeof(commands_cmd), "${%s::_commands}",
             project_name);
    lcl_value *commands_dict = NULL;

    if (lcl_eval_string(interp, commands_cmd, &commands_dict) == LCL_RC_OK &&
        commands_dict) {
      char keys_cmd[1024];
      snprintf(keys_cmd, sizeof(keys_cmd), "Dict::keys ${%s::_commands}",
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
  lcl_register_posix(w->interp);
  lcl_register_time(w->interp);
  lcl_register_process(w->interp);

  g_ctx.active = true;

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

  lcl_value *ns = NULL;

  if (lcl_get(interp->interp, CLI_ARG_NS, &ns) != LCL_OK || !ns) {
    ns = lcl_ns_new(CLI_ARG_NS);

    if (!ns) {
      return;
    }

    lcl_define(interp->interp, CLI_ARG_NS, ns);
  }

  lcl_value *val = lcl_string_new(value);

  if (val) {
    lcl_ns_def(ns, name, val);

    char cli_key[256];
    snprintf(cli_key, sizeof(cli_key), "::" CLI_ARG_NS "::%s", name);
    lcl_define(interp->interp, cli_key, val);

    lcl_ref_dec(val);
  }

  lcl_ref_dec(ns);
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
    printf("    shell-arg: %s\n", task->shell_arg);
  }

  if (task->wd) {
    printf("    wd: %s\n", task->wd);
  }

  if (task->run) {
    printf("    run: %.80s%s\n", task->run,
           strlen(task->run) > 80 ? "..." : "");
  }

  if (task->always_run) {
    printf("    always-run: true\n");
  }

  inspect_print_arr("inputs", task->inputs);
  inspect_print_arr("outputs", task->outputs);
  inspect_print_arr("depends-on", task->depends_on);
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
