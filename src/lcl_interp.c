/*
 * lcl_interp.c - LCL interpreter integration for Dagwood
 *
 * This file implements the Dagwood DSL commands using LCL.
 * Commands directly populate C data structures instead of
 * going through an intermediate dictionary format.
 */

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

#include <lcl.h>
#include <lcl-io.h>

#include "dagwood.h"
#include "data.h"
#include "interpreter.h"

static struct {
  const char *current_project;
  dagwood_project *project;
  p_map *project_registry;
  t_map *task_registry;
  t_map *command_registry;
  lcl_interp *interp;
  char *cwd;
} g_ctx;

struct interpreter {
  lcl_interp *interp;
  p_map *project_registry;
  t_map *task_registry;
  t_map *command_registry;
  bool built;
};

static char *strdup_safe(const char *s) {
  if (!s) return NULL;
  size_t len = strlen(s);
  char *dup = malloc(len + 1);
  if (dup) {
    memcpy(dup, s, len + 1);
  }
  return dup;
}

/* Build a qualified name like "project::name" */
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

/* Bug fixed here:
 * Variable substitution using lcl_get for $var references
 * This is different that core Lcl.
 */
static char *substitute_variables(lcl_interp *interp, const char *script) {
  char *result = NULL;
  size_t result_len = 0;
  size_t result_cap = 0;
  size_t script_len = strlen(script);
  size_t i = 0;

  while (i < script_len) {
    if (script[i] == '$') {
      size_t start = i + 1;
      size_t end = start;

      /* Handle ${...} form */
      if (start < script_len && script[start] == '{') {
        start++;
        end = start;
        while (end < script_len && script[end] != '}') end++;
        if (end >= script_len) {
          /* Unterminated - copy literally */
          size_t need = result_len + (end - i) + 1;
          if (need > result_cap) {
            result_cap = need * 2;
            result = realloc(result, result_cap);
          }
          memcpy(result + result_len, script + i, end - i);
          result_len += end - i;
          i = end;
          continue;
        }
        size_t name_len = end - start;
        char *varname = malloc(name_len + 1);
        memcpy(varname, script + start, name_len);
        varname[name_len] = '\0';

        lcl_value *val = NULL;
        if (lcl_get(interp, varname, &val) == LCL_OK && val) {
          const char *val_str = lcl_value_to_string(val);
          size_t val_len = strlen(val_str);
          size_t need = result_len + val_len + 1;
          if (need > result_cap) {
            result_cap = need * 2;
            result = realloc(result, result_cap);
          }
          memcpy(result + result_len, val_str, val_len);
          result_len += val_len;
          lcl_ref_dec(val);
        }
        free(varname);
        i = end + 1;
        continue;
      }

      /* Handle $name form */
      while (end < script_len &&
             (script[end] == '_' || script[end] == ':' ||
              (script[end] >= 'a' && script[end] <= 'z') ||
              (script[end] >= 'A' && script[end] <= 'Z') ||
              (script[end] >= '0' && script[end] <= '9'))) {
        end++;
      }

      if (end > start) {
        size_t name_len = end - start;
        char *varname = malloc(name_len + 1);
        memcpy(varname, script + start, name_len);
        varname[name_len] = '\0';

        lcl_value *val = NULL;
        int found = 0;
        int rc = lcl_get(interp, varname, &val);
        if (rc == LCL_OK && val) {
          const char *val_str = lcl_value_to_string(val);
          size_t val_len = strlen(val_str);
          /* Only substitute if we got a non-empty value that doesn't
           * look like a partial namespace path (starts with ::) */
          if (val_len > 0 && !(val_len >= 2 && val_str[0] == ':' && val_str[1] == ':')) {
            found = 1;
            size_t need = result_len + val_len + 1;
            if (need > result_cap) {
              result_cap = need * 2;
              result = realloc(result, result_cap);
            }
            memcpy(result + result_len, val_str, val_len);
            result_len += val_len;
          }
          lcl_ref_dec(val);
        }
        if (!found) {
          /* Variable not found or invalid - keep original */
          size_t orig_len = end - i;
          size_t need = result_len + orig_len + 1;
          if (need > result_cap) {
            result_cap = need * 2;
            result = realloc(result, result_cap);
          }
          memcpy(result + result_len, script + i, orig_len);
          result_len += orig_len;
        }
        free(varname);
        i = end;
        continue;
      } else {
        /* Bare $ */
        size_t need = result_len + 2;
        if (need > result_cap) {
          result_cap = need * 2;
          result = realloc(result, result_cap);
        }
        result[result_len++] = '$';
        i++;
        continue;
      }
    }

    /* Regular character */
    size_t need = result_len + 2;
    if (need > result_cap) {
      result_cap = need * 2;
      result = realloc(result, result_cap);
    }
    result[result_len++] = script[i++];
  }

  if (result) {
    size_t need = result_len + 1;
    if (need > result_cap) {
      result = realloc(result, need);
    }
    result[result_len] = '\0';
    return result;
  }
  return strdup_safe(script);
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
  if (!dir) return;

  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.' &&
        (entry->d_name[1] == '\0' ||
         (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
      continue;
    }

    char path[4096];

    if (base[0]) {
      snprintf(path, sizeof(path), "%s/%s", base, entry->d_name);
    } else {
      snprintf(path, sizeof(path), "%s", entry->d_name);
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
  if (!*out) return LCL_RC_ERR;

  for (int i = 0; i < argc; i++) {
    const char *pattern = lcl_value_to_string(argv[i]);
    if (!pattern) continue;

    const char *slash = strchr(pattern, '/');

    if (slash == NULL) {
      DIR *dir = opendir(".");

      if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
          if (entry->d_name[0] == '.') continue;

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
      /* Recursive glob */
      glob_recursive("", pattern, out);
    } else {
      /* Path with directory component */
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
            if (entry->d_name[0] == '.') continue;

            if (fnmatch(file_pattern, entry->d_name, 0) == 0) {
              char path[4096];
              snprintf(path, sizeof(path), "%s/%s", base, entry->d_name);
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
    if (!dup) return LCL_RC_ERR;

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

/*
 * project <name>
 *
 * Declares the current project. Must be called before config/task/command.
 */
static int c_project(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  if (argc != 1) {
    fprintf(stderr, "[dagwood] project requires exactly 1 argument\n");
    return LCL_RC_ERR;
  }

  const char *name = lcl_value_to_string(argv[0]);

  if (!name || !name[0]) {
    fprintf(stderr, "[dagwood] project name cannot be empty\n");
    return LCL_RC_ERR;
  }

  dagwood_project *proj = dagwood_project_new();
  if (!proj) return LCL_RC_ERR;

  proj->name = strdup_safe(name);
  proj->shell = dagwood_platform_shell();
  proj->shell_arg = dagwood_platform_shell_arg();
  proj->always_run = false;

  char cwd[4096];

  if (getcwd(cwd, sizeof(cwd))) {
    proj->cwd = strdup_safe(cwd);
  }

  p_map_set(g_ctx.project_registry, name, proj);

  g_ctx.current_project = proj->name;
  g_ctx.project = proj;

  /* Create project namespace and sub-namespaces for tasks/commands */
  char ns_cmd[512];
  lcl_value *ns_result = NULL;

  snprintf(ns_cmd, sizeof(ns_cmd), "namespace eval %s {}", name);
  lcl_eval_string(interp, ns_cmd, &ns_result);
  if (ns_result) lcl_ref_dec(ns_result);

  snprintf(ns_cmd, sizeof(ns_cmd), "namespace eval %s::tasks {}", name);
  lcl_eval_string(interp, ns_cmd, &ns_result);
  if (ns_result) lcl_ref_dec(ns_result);

  snprintf(ns_cmd, sizeof(ns_cmd), "namespace eval %s::commands {}", name);
  lcl_eval_string(interp, ns_cmd, &ns_result);
  if (ns_result) lcl_ref_dec(ns_result);

  *out = lcl_string_new(name);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_config(lcl_interp *interp, int argc, lcl_value **argv,
                    lcl_value **out) {
  if (argc != 1) {
    fprintf(stderr, "[dagwood] config requires exactly 1 argument (block)\n");
    return LCL_RC_ERR;
  }

  if (!g_ctx.project) {
    fprintf(stderr, "[dagwood] config: no project declared\n");
    return LCL_RC_ERR;
  }

  const char *block = lcl_value_to_string(argv[0]);
  if (!block) return LCL_RC_ERR;

  lcl_value *result = NULL;
  int rc = lcl_eval_string(interp, block, &result);
  if (result) lcl_ref_dec(result);

  *out = lcl_string_new("");

  return (rc == LCL_RC_OK && *out) ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_always_run(lcl_interp *interp, int argc, lcl_value **argv,
                        lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    fprintf(stderr, "[dagwood] always_run requires 1 argument\n");
    return LCL_RC_ERR;
  }

  if (!g_ctx.project) {
    fprintf(stderr, "[dagwood] always_run: no project declared\n");
    return LCL_RC_ERR;
  }

  const char *val = lcl_value_to_string(argv[0]);
  g_ctx.project->always_run =
      (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);

  *out = lcl_string_new(val);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_description(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    fprintf(stderr, "[dagwood] description requires 1 argument\n");
    return LCL_RC_ERR;
  }

  if (!g_ctx.project) {
    fprintf(stderr, "[dagwood] description: no project declared\n");
    return LCL_RC_ERR;
  }

  const char *val = lcl_value_to_string(argv[0]);
  g_ctx.project->description = strdup_safe(val);

  *out = lcl_string_new(val);

  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_shell(lcl_interp *interp, int argc, lcl_value **argv,
                   lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    fprintf(stderr, "[dagwood] shell requires 1 argument\n");
    return LCL_RC_ERR;
  }

  if (!g_ctx.project) {
    fprintf(stderr, "[dagwood] shell: no project declared\n");
    return LCL_RC_ERR;
  }

  const char *val = lcl_value_to_string(argv[0]);
  g_ctx.project->shell = strdup_safe(val);

  *out = lcl_string_new(val);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_shell_arg(lcl_interp *interp, int argc, lcl_value **argv,
                       lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    fprintf(stderr, "[dagwood] shell_arg requires 1 argument\n");
    return LCL_RC_ERR;
  }

  if (!g_ctx.project) {
    fprintf(stderr, "[dagwood] shell_arg: no project declared\n");
    return LCL_RC_ERR;
  }

  const char *val = lcl_value_to_string(argv[0]);
  g_ctx.project->shell_arg = strdup_safe(val);

  *out = lcl_string_new(val);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_let_var(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  if (argc != 2) {
    fprintf(stderr, "[dagwood] let requires 2 arguments\n");
    return LCL_RC_ERR;
  }

  if (!g_ctx.current_project) {
    fprintf(stderr, "[dagwood] let: no project declared\n");
    return LCL_RC_ERR;
  }

  const char *name = lcl_value_to_string(argv[0]);
  lcl_value *value = argv[1];

  /* Define in project namespace to avoid frame-local scoping issues */
  lcl_value *proj_ns = NULL;
  if (lcl_get(interp, g_ctx.current_project, &proj_ns) == LCL_OK && proj_ns) {
    /* Store the actual value (not just its string representation) */
    lcl_ref_inc(value);
    lcl_ns_def(proj_ns, name, value);
    lcl_ref_dec(proj_ns);
    lcl_ref_inc(value);
    *out = value;
    return LCL_RC_OK;
  }

  /* Fallback to old behavior if namespace lookup fails */
  char *qname = make_qualified_name(g_ctx.current_project, name);
  if (!qname) return LCL_RC_ERR;

  lcl_ref_inc(value);
  lcl_result res = lcl_define(interp, qname, value);
  free(qname);

  if (res != LCL_OK) {
    lcl_ref_dec(value);
    return LCL_RC_ERR;
  }

  *out = value;
  return LCL_RC_OK;
}

#define CLI_ARG_PREFIX "::__dagwood_cli_args::"

static int c_arg(lcl_interp *interp, int argc, lcl_value **argv,
                 lcl_value **out) {
  if (argc != 2) {
    fprintf(stderr, "[dagwood] arg requires 2 arguments (name and default)\n");
    return LCL_RC_ERR;
  }

  if (!g_ctx.current_project) {
    fprintf(stderr, "[dagwood] arg: no project declared\n");
    return LCL_RC_ERR;
  }

  const char *name = lcl_value_to_string(argv[0]);
  const char *default_val = lcl_value_to_string(argv[1]);

  char cli_key[256];
  snprintf(cli_key, sizeof(cli_key), CLI_ARG_PREFIX "%s", name);

  lcl_value *cli_val = NULL;
  const char *final_value = default_val;

  if (lcl_get(interp, cli_key, &cli_val) == LCL_OK && cli_val) {
    final_value = lcl_value_to_string(cli_val);
  }

  /* Define in project namespace to avoid frame-local scoping issues */
  lcl_value *proj_ns = NULL;
  if (lcl_get(interp, g_ctx.current_project, &proj_ns) == LCL_OK && proj_ns) {
    lcl_value *val = lcl_string_new(final_value);
    if (!val) {
      lcl_ref_dec(proj_ns);
      if (cli_val) lcl_ref_dec(cli_val);
      return LCL_RC_ERR;
    }
    lcl_ns_def(proj_ns, name, val);
    lcl_ref_dec(proj_ns);
    if (cli_val) lcl_ref_dec(cli_val);
    *out = lcl_string_new(final_value);
    return *out ? LCL_RC_OK : LCL_RC_ERR;
  }

  /* Fallback to old behavior if namespace lookup fails */
  char *qname = make_qualified_name(g_ctx.current_project, name);
  if (!qname) {
    if (cli_val) lcl_ref_dec(cli_val);
    return LCL_RC_ERR;
  }

  lcl_value *val = lcl_string_new(final_value);
  if (!val) {
    free(qname);
    if (cli_val) lcl_ref_dec(cli_val);
    return LCL_RC_ERR;
  }

  lcl_result res = lcl_define(interp, qname, val);
  free(qname);

  if (cli_val) lcl_ref_dec(cli_val);

  if (res != LCL_OK) {
    lcl_ref_dec(val);
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(final_value);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static dagwood_task *g_current_task = NULL;

static int c_task_inputs(lcl_interp *interp, int argc, lcl_value **argv,
                         lcl_value **out) {
  (void)interp;

  if (!g_current_task) {
    fprintf(stderr, "[dagwood] inputs: not in task context\n");
    return LCL_RC_ERR;
  }

  for (int i = 0; i < argc; i++) {
    size_t len = lcl_list_len(argv[i]);
    if (len > 0) {
      for (size_t j = 0; j < len; j++) {
        lcl_value *item = NULL;
        if (lcl_list_get(argv[i], j, &item) == LCL_OK && item) {
          dagwood_task_add_input(g_current_task,
                                 strdup_safe(lcl_value_to_string(item)));
          lcl_ref_dec(item);
        }
      }
    } else {
      dagwood_task_add_input(g_current_task,
                             strdup_safe(lcl_value_to_string(argv[i])));
    }
  }

  *out = lcl_string_new("");
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_task_outputs(lcl_interp *interp, int argc, lcl_value **argv,
                          lcl_value **out) {
  (void)interp;

  if (!g_current_task) {
    fprintf(stderr, "[dagwood] outputs: not in task context\n");
    return LCL_RC_ERR;
  }

  for (int i = 0; i < argc; i++) {
    size_t len = lcl_list_len(argv[i]);
    if (len > 0) {
      for (size_t j = 0; j < len; j++) {
        lcl_value *item = NULL;
        if (lcl_list_get(argv[i], j, &item) == LCL_OK && item) {
          dagwood_task_add_output(g_current_task,
                                  strdup_safe(lcl_value_to_string(item)));
          lcl_ref_dec(item);
        }
      }
    } else {
      dagwood_task_add_output(g_current_task,
                              strdup_safe(lcl_value_to_string(argv[i])));
    }
  }

  *out = lcl_string_new("");
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_task_depends_on(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  (void)interp;

  if (!g_current_task) {
    fprintf(stderr, "[dagwood] depends_on: not in task context\n");
    return LCL_RC_ERR;
  }

  for (int i = 0; i < argc; i++) {
    size_t len = lcl_list_len(argv[i]);
    if (len > 0) {
      for (size_t j = 0; j < len; j++) {
        lcl_value *item = NULL;

        if (lcl_list_get(argv[i], j, &item) == LCL_OK && item) {
          dagwood_task_add_depends_on(g_current_task,
                                      strdup_safe(lcl_value_to_string(item)));
          lcl_ref_dec(item);
        }
      }
    } else {
      dagwood_task_add_depends_on(g_current_task,
                                  strdup_safe(lcl_value_to_string(argv[i])));
    }
  }

  *out = lcl_string_new("");
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_task_run(lcl_interp *interp, int argc, lcl_value **argv,
                      lcl_value **out) {
  if (argc != 1) {
    fprintf(stderr, "[dagwood] run requires 1 argument\n");
    return LCL_RC_ERR;
  }

  if (!g_current_task) {
    fprintf(stderr, "[dagwood] run: not in task context\n");
    return LCL_RC_ERR;
  }

  const char *script = lcl_value_to_string(argv[0]);

  /* Store raw script - substitution will happen in c_task after namespace is
   * set up */
  g_current_task->run = strdup_safe(script);

  *out = lcl_string_new(g_current_task->run);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_task_description(lcl_interp *interp, int argc, lcl_value **argv,
                              lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    fprintf(stderr, "[dagwood] description requires 1 argument\n");
    return LCL_RC_ERR;
  }

  if (!g_current_task) {
    return c_description(interp, argc, argv, out);
  }

  const char *desc = lcl_value_to_string(argv[0]);
  g_current_task->description = strdup_safe(desc);

  *out = lcl_string_new(desc);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_task_always_run(lcl_interp *interp, int argc, lcl_value **argv,
                             lcl_value **out) {
  (void)interp;

  if (argc != 1) {
    fprintf(stderr, "[dagwood] always_run requires 1 argument\n");
    return LCL_RC_ERR;
  }

  if (!g_current_task) {
    return c_always_run(interp, argc, argv, out);
  }

  const char *val = lcl_value_to_string(argv[0]);
  g_current_task->always_run =
      (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);

  *out = lcl_string_new(val);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_task(lcl_interp *interp, int argc, lcl_value **argv,
                  lcl_value **out) {
  if (argc != 2) {
    fprintf(stderr, "[dagwood] task requires 2 arguments (name and block)\n");
    return LCL_RC_ERR;
  }

  if (!g_ctx.current_project) {
    fprintf(stderr, "[dagwood] task: no project declared\n");
    return LCL_RC_ERR;
  }

  const char *name = lcl_value_to_string(argv[0]);
  const char *block = lcl_value_to_string(argv[1]);

  dagwood_task *task = dagwood_task_new();
  if (!task) return LCL_RC_ERR;

  task->name = strdup_safe(name);
  task->id = make_qualified_name(g_ctx.current_project, name);

  task->shell = g_ctx.project->shell;
  task->shell_arg = g_ctx.project->shell_arg;
  task->wd = g_ctx.project->cwd;
  task->always_run = g_ctx.project->always_run;

  g_current_task = task;

  lcl_value *result = NULL;
  int rc = lcl_eval_string(interp, block, &result);
  if (result) lcl_ref_dec(result);

  g_current_task = NULL;

  if (rc != LCL_RC_OK) {
    dagwood_task_delete(task);
    return LCL_RC_ERR;
  }

  t_map_set(g_ctx.task_registry, task->id, task);

  /* Create task namespace and define attributes inside it.
   * Using namespace eval + lcl_ns_def ensures variables are defined globally,
   * not in the current proc's local scope. */
  {
    char ns_cmd[512];
    char ns_path[512];
    lcl_value *ns_result = NULL;
    lcl_value *task_ns = NULL;

    /* Create the task namespace: project::tasks::taskname */
    snprintf(ns_cmd, sizeof(ns_cmd), "namespace eval %s::tasks::%s {}",
             g_ctx.current_project, name);
    lcl_eval_string(interp, ns_cmd, &ns_result);
    if (ns_result) lcl_ref_dec(ns_result);

    /* Get the task namespace and define attributes in it */
    snprintf(ns_path, sizeof(ns_path), "%s::tasks::%s",
             g_ctx.current_project, name);
    if (lcl_get(interp, ns_path, &task_ns) == LCL_OK && task_ns) {
      /* Define inputs if present - as a list for proper iteration */
      if (task->inputs && s_arr_len(task->inputs) > 0) {
        lcl_value *inputs_list = lcl_list_new();
        for (size_t i = 0; i < s_arr_len(task->inputs); i++) {
          lcl_value *item = lcl_string_new(s_arr_get(task->inputs, i));
          lcl_list_push(&inputs_list, item);
          lcl_ref_dec(item);
        }
        lcl_ns_def(task_ns, "inputs", inputs_list);
      }

      /* Define outputs if present - as a list for proper iteration */
      if (task->outputs && s_arr_len(task->outputs) > 0) {
        lcl_value *outputs_list = lcl_list_new();
        for (size_t i = 0; i < s_arr_len(task->outputs); i++) {
          lcl_value *item = lcl_string_new(s_arr_get(task->outputs, i));
          lcl_list_push(&outputs_list, item);
          lcl_ref_dec(item);
        }
        lcl_ns_def(task_ns, "outputs", outputs_list);
      }

      /* Define description if present */
      if (task->description) {
        lcl_ns_def(task_ns, "description", lcl_string_new(task->description));
      }

      lcl_ref_dec(task_ns);
    }
  }

  /* Now that task namespace is set up, substitute variables in run script */
  if (task->run) {
    char *substituted = substitute_variables(interp, task->run);
    free(task->run);
    task->run = substituted;

    /* Also update the run attribute in the namespace */
    char ns_path[512];
    lcl_value *task_ns = NULL;
    snprintf(ns_path, sizeof(ns_path), "%s::tasks::%s", g_ctx.current_project,
             name);
    if (lcl_get(interp, ns_path, &task_ns) == LCL_OK && task_ns) {
      lcl_ns_def(task_ns, "run", lcl_string_new(task->run));
      lcl_ref_dec(task_ns);
    }
  }

  *out = lcl_string_new(task->id);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static int c_command(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  if (argc != 2) {
    fprintf(stderr,
            "[dagwood] command requires 2 arguments (name and block)\n");
    return LCL_RC_ERR;
  }

  if (!g_ctx.current_project) {
    fprintf(stderr, "[dagwood] command: no project declared\n");
    return LCL_RC_ERR;
  }

  const char *name = lcl_value_to_string(argv[0]);
  const char *block = lcl_value_to_string(argv[1]);

  dagwood_task *task = dagwood_task_new();
  if (!task) return LCL_RC_ERR;

  task->name = strdup_safe(name);
  task->id = make_qualified_name(g_ctx.current_project, name);

  task->shell = g_ctx.project->shell;
  task->shell_arg = g_ctx.project->shell_arg;
  task->wd = g_ctx.project->cwd;

  g_current_task = task;

  lcl_value *result = NULL;
  int rc = lcl_eval_string(interp, block, &result);
  if (result) lcl_ref_dec(result);

  g_current_task = NULL;

  if (rc != LCL_RC_OK) {
    dagwood_task_delete(task);
    return LCL_RC_ERR;
  }

  t_map_set(g_ctx.command_registry, task->id, task);

  /* Create command namespace and define attributes inside it */
  {
    char ns_cmd[512];
    char ns_path[512];
    lcl_value *ns_result = NULL;
    lcl_value *cmd_ns = NULL;

    /* Create the command namespace: project::commands::cmdname */
    snprintf(ns_cmd, sizeof(ns_cmd), "namespace eval %s::commands::%s {}",
             g_ctx.current_project, name);
    lcl_eval_string(interp, ns_cmd, &ns_result);
    if (ns_result) lcl_ref_dec(ns_result);

    /* Get the command namespace and define attributes in it */
    snprintf(ns_path, sizeof(ns_path), "%s::commands::%s",
             g_ctx.current_project, name);
    if (lcl_get(interp, ns_path, &cmd_ns) == LCL_OK && cmd_ns) {
      /* Define description if present */
      if (task->description) {
        lcl_ns_def(cmd_ns, "description", lcl_string_new(task->description));
      }

      lcl_ref_dec(cmd_ns);
    }
  }

  /* Substitute variables in run script */
  if (task->run) {
    char *substituted = substitute_variables(interp, task->run);
    free(task->run);
    task->run = substituted;

    /* Update the run attribute in the namespace */
    char ns_path[512];
    lcl_value *cmd_ns = NULL;
    snprintf(ns_path, sizeof(ns_path), "%s::commands::%s", g_ctx.current_project,
             name);
    if (lcl_get(interp, ns_path, &cmd_ns) == LCL_OK && cmd_ns) {
      lcl_ns_def(cmd_ns, "run", lcl_string_new(task->run));
      lcl_ref_dec(cmd_ns);
    }
  }

  *out = lcl_string_new(task->id);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

static void register_dagwood_commands(lcl_interp *interp) {
  lcl_register_proc(interp, "platform-shell", c_platform_shell);
  lcl_register_proc(interp, "platform-shell-arg", c_platform_shell_arg);

  /* File utilities */
  lcl_register_proc(interp, "pwd", c_pwd);
  lcl_register_proc(interp, "cd", c_cd);
  lcl_register_proc(interp, "glob", c_glob);
  lcl_register_proc(interp, "file", c_file);

  /* DSL commands */
  lcl_register_proc(interp, "project", c_project);
  lcl_register_proc(interp, "config", c_config);
  lcl_register_proc(interp, "task", c_task);
  lcl_register_proc(interp, "command", c_command);

  /* Variable definition */
  lcl_register_proc(interp, "let", c_let_var);
  lcl_register_proc(interp, "arg", c_arg);

  /* Config sub-commands (can be used in config block or top-level) */
  lcl_register_proc(interp, "always_run", c_task_always_run);
  lcl_register_proc(interp, "description", c_task_description);
  lcl_register_proc(interp, "shell", c_shell);
  lcl_register_proc(interp, "shell_arg", c_shell_arg);

  /* Task sub-commands */
  lcl_register_proc(interp, "inputs", c_task_inputs);
  lcl_register_proc(interp, "outputs", c_task_outputs);
  lcl_register_proc(interp, "depends_on", c_task_depends_on);
  lcl_register_proc(interp, "run", c_task_run);
}

interpreter *interpreter_new(void) {
  interpreter *w = calloc(1, sizeof(*w));
  if (!w) return NULL;

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

  g_ctx.project_registry = w->project_registry;
  g_ctx.task_registry = w->task_registry;
  g_ctx.command_registry = w->command_registry;
  g_ctx.interp = w->interp;
  g_ctx.current_project = NULL;
  g_ctx.project = NULL;

  register_dagwood_commands(w->interp);

  return w;
}

void interpreter_set_cli_arg(interpreter *interp, const char *name,
                             const char *value) {
  if (!interp || !name || !value) return;

  char cli_key[256];
  snprintf(cli_key, sizeof(cli_key), CLI_ARG_PREFIX "%s", name);

  lcl_value *val = lcl_string_new(value);
  if (val) {
    lcl_define(interp->interp, cli_key, val);
  }
}

void interpreter_delete(interpreter *interp) {
  if (!interp) return;

  p_map *p = interp->project_registry;
  for (size_t i = p_map_begin(p); i < p_map_end(p); i++) {
    dagwood_project *project = p_map_value(p, i);
    if (project) dagwood_project_delete(project);
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
    if (task) dagwood_task_delete(task);
  }

  t_map_delete(t);
  t_map_delete(c);
  p_map_delete(p);

  lcl_interp_free(interp->interp);
  free(interp);

  g_ctx.project_registry = NULL;
  g_ctx.task_registry = NULL;
  g_ctx.command_registry = NULL;
  g_ctx.interp = NULL;
  g_ctx.current_project = NULL;
  g_ctx.project = NULL;
}

const char *interpreter_get_error(interpreter *interp) {
  if (!interp || !interp->interp) return NULL;

  const char *file = lcl_interp_error_file(interp->interp);
  int line = lcl_interp_error_line(interp->interp);

  static char error_buf[512];

  if (file) {
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

  if (result) lcl_ref_dec(result);
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

interp_result interpreter_repl(interpreter *interp) {
  char line[1024];
  printf("Dagwood REPL (type 'exit' to quit)\n");

  while (1) {
    printf("dagwood> ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
      break;
    }

    if (strncmp(line, "exit", 4) == 0) {
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
