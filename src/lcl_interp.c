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

// clang-format off
// lcl.h must be included before lcl-io (or any libraries)
// Turn clang-format off for being aggressive
#include <lcl.h>
#include <lcl-io.h>
// clang-format on

#include "dagwood.h"
#include "data.h"
#include "generated/dagwood_dsl.h"
#include "interpreter.h"

static char *strdup_safe(const char *s);
static char *make_qualified_name(const char *project, const char *name);

static struct {
  const char *current_project;
  dagwood_project *project;
  p_map *project_registry;
  t_map *task_registry;
  t_map *command_registry;
  lcl_interp *interp;
  char *cwd;
  bool dsl_loaded;
} g_ctx;

static bool load_embedded_dsl(lcl_interp *interp) {
  if (g_ctx.dsl_loaded) {
    return true;
  }

  char *dsl_src = malloc(dagwood_dsl_lcl_len + 1);

  if (!dsl_src) {
    return false;
  }

  memcpy(dsl_src, dagwood_dsl_lcl, dagwood_dsl_lcl_len);
  dsl_src[dagwood_dsl_lcl_len] = '\0';

  lcl_value *result = NULL;
  int rc = lcl_eval_string(interp, dsl_src, &result);
  free(dsl_src);

  if (result) {
    lcl_ref_dec(result);
  }

  if (rc != LCL_RC_OK) {
    const char *msg = lcl_interp_error_msg(interp);
    int line = lcl_interp_error_line(interp);
    fprintf(stderr, "[dagwood] Failed to load embedded DSL: line %d msg: %s\n",
            line, msg);

    return false;
  }

  g_ctx.dsl_loaded = true;
  return true;
}

/*
 * Special form: project <name>
 *
 * Calls _project to get the initialization code, then evals it.
 * This sets up the project namespace with config/task/command procs.
 */
static int s_project(lcl_interp *interp, int argc, const lcl_word **args,
                     lcl_value **out) {
  if (argc != 1) {
    fprintf(stderr, "[dagwood] project requires exactly 1 argument\n");
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

  lcl_value *project_proc = NULL;

  if (lcl_get(interp, "_project", &project_proc) != LCL_OK || !project_proc) {
    fprintf(stderr, "[dagwood] _project proc not found (DSL not loaded?)\n");
    lcl_ref_dec(name_val);
    free(name);
    return LCL_RC_ERR;
  }

  lcl_value *init_code = NULL;
  lcl_value *call_args[1] = {name_val};
  rc = lcl_call_proc(interp, project_proc, 1, call_args, &init_code);
  lcl_ref_dec(project_proc);
  lcl_ref_dec(name_val);

  if (rc != LCL_RC_OK || !init_code) {
    fprintf(stderr, "[dagwood] _project failed\n");
    free(name);
    return LCL_RC_ERR;
  }

  const char *init_code_str = lcl_value_to_string(init_code);
  lcl_value *eval_result = NULL;
  rc = lcl_eval_string(interp, init_code_str, &eval_result);
  lcl_ref_dec(init_code);

  if (eval_result) {
    lcl_ref_dec(eval_result);
  }

  if (rc != LCL_RC_OK) {
    fprintf(stderr, "[dagwood] project initialization failed\n");
    free(name);
    return LCL_RC_ERR;
  }

  g_ctx.current_project = name;

  *out = lcl_string_new(name);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/*
 * Special form: import <path>
 *
 * Calls _import to get the import code, then evals it.
 * This loads the imported project and registers it.
 */
static int s_import(lcl_interp *interp, int argc, const lcl_word **args,
                    lcl_value **out) {
  if (argc != 1) {
    fprintf(stderr, "[dagwood] import requires exactly 1 argument\n");
    return LCL_RC_ERR;
  }

  lcl_value *path_val = NULL;
  int rc = lcl_eval_word(interp, args[0], &path_val);

  if (rc != LCL_RC_OK || !path_val) {
    return LCL_RC_ERR;
  }

  const char *path = lcl_value_to_string(path_val);

  if (!path || !path[0]) {
    fprintf(stderr, "[dagwood] import path cannot be empty\n");
    lcl_ref_dec(path_val);
    return LCL_RC_ERR;
  }

  lcl_value *import_proc = NULL;

  if (lcl_get(interp, "_import", &import_proc) != LCL_OK || !import_proc) {
    fprintf(stderr, "[dagwood] _import proc not found (DSL not loaded?)\n");
    lcl_ref_dec(path_val);
    return LCL_RC_ERR;
  }

  lcl_value *import_code = NULL;
  lcl_value *call_args[1] = {path_val};
  rc = lcl_call_proc(interp, import_proc, 1, call_args, &import_code);
  lcl_ref_dec(import_proc);
  lcl_ref_dec(path_val);

  if (rc != LCL_RC_OK || !import_code) {
    fprintf(stderr, "[dagwood] _import failed\n");
    return LCL_RC_ERR;
  }

  const char *import_code_str = lcl_value_to_string(import_code);
  lcl_value *eval_result = NULL;
  rc = lcl_eval_string(interp, import_code_str, &eval_result);
  lcl_ref_dec(import_code);

  if (eval_result) {
    lcl_ref_dec(eval_result);
  }

  if (rc != LCL_RC_OK) {
    fprintf(stderr, "[dagwood] import failed for: %s\n", path);
    return LCL_RC_ERR;
  }

  *out = lcl_string_new("");
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
            if (entry->d_name[0] == '.') {
              continue;
            }

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

static int c_let_var(lcl_interp *interp, int argc, lcl_value **argv,
                     lcl_value **out) {
  if (argc != 2) {
    fprintf(stderr, "[dagwood] def requires 2 arguments\n");
    return LCL_RC_ERR;
  }

  if (!g_ctx.current_project) {
    fprintf(stderr, "[dagwood] def: no project declared\n");
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
  if (!qname) {
    return LCL_RC_ERR;
  }

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
      if (cli_val) {
        lcl_ref_dec(cli_val);
      }
      return LCL_RC_ERR;
    }
    lcl_ns_def(proj_ns, name, val);
    lcl_ref_dec(proj_ns);
    if (cli_val) {
      lcl_ref_dec(cli_val);
    }
    *out = lcl_string_new(final_value);
    return *out ? LCL_RC_OK : LCL_RC_ERR;
  }

  /* Fallback to old behavior if namespace lookup fails */
  char *qname = make_qualified_name(g_ctx.current_project, name);
  if (!qname) {
    if (cli_val) {
      lcl_ref_dec(cli_val);
    }
    return LCL_RC_ERR;
  }

  lcl_value *val = lcl_string_new(final_value);
  if (!val) {
    free(qname);
    if (cli_val) {
      lcl_ref_dec(cli_val);
    }
    return LCL_RC_ERR;
  }

  lcl_result res = lcl_define(interp, qname, val);
  free(qname);

  if (cli_val) {
    lcl_ref_dec(cli_val);
  }

  if (res != LCL_OK) {
    lcl_ref_dec(val);
    return LCL_RC_ERR;
  }

  *out = lcl_string_new(final_value);
  return *out ? LCL_RC_OK : LCL_RC_ERR;
}

/* ============================================================================
 * Extraction Functions
 *
 * After LCL evaluation, extract data from namespaces into C structs.
 * ============================================================================
 */

/*
 * Extract a string from a dict by key.
 * Returns a strdup'd copy or NULL if not found.
 */
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

/*
 * Extract a bool from a dict by key.
 */
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

/*
 * Check if a character is valid in an LCL identifier.
 */
static bool is_ident_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_' || c == ':';
}

/*
 * Substitute namespace variables in a run script.
 * Only substitutes variables containing "::" (LCL namespace vars).
 * Preserves shell variables like $1, $$, $?, etc.
 *
 * Handles both $var::name and ${var::name} syntax.
 */
static char *substitute_namespace_vars(lcl_interp *interp, const char *script) {
  if (!script) {
    return NULL;
  }

  size_t script_len = strlen(script);
  /* Estimate output size - may grow if needed */
  size_t out_capacity = script_len * 2;
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

      /* Check for ${...} syntax */
      if (*var_start == '{') {
        braced = true;
        var_start++;
        var_end = var_start;
        while (*var_end && *var_end != '}') {
          var_end++;
        }
      } else {
        /* Regular $var syntax - scan identifier chars */
        while (is_ident_char(*var_end)) {
          var_end++;
        }
      }

      size_t var_len = var_end - var_start;

      /* Check if this is a namespace variable (contains ::) */
      bool is_namespace_var = false;
      for (const char *c = var_start; c < var_end - 1; c++) {
        if (c[0] == ':' && c[1] == ':') {
          is_namespace_var = true;
          break;
        }
      }

      if (is_namespace_var && var_len > 0) {
        /* Build the lookup expression "$var::name" */
        char *lookup = malloc(var_len + 2);
        if (lookup) {
          lookup[0] = '$';
          memcpy(lookup + 1, var_start, var_len);
          lookup[var_len + 1] = '\0';

          /* Look up in LCL */
          lcl_value *result = NULL;
          int rc = lcl_eval_string(interp, lookup, &result);
          free(lookup);

          const char *replacement = NULL;
          if (rc == LCL_RC_OK && result) {
            replacement = lcl_value_to_string(result);
          }

          if (replacement) {
            size_t repl_len = strlen(replacement);
            /* Ensure capacity */
            while (out_len + repl_len + 1 > out_capacity) {
              out_capacity *= 2;
              char *new_out = realloc(out, out_capacity);
              if (!new_out) {
                if (result) {
                  lcl_ref_dec(result);
                }
                free(out);
                return strdup_safe(script);
              }
              out = new_out;
            }
            memcpy(out + out_len, replacement, repl_len);
            out_len += repl_len;
          }
          if (result) {
            lcl_ref_dec(result);
          }

          /* Skip past the variable reference */
          p = braced ? (var_end + 1) : var_end;
          continue;
        }
      }
    }

    /* Copy character as-is */
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

/*
 * Extract a run script from a dict, with namespace variable substitution.
 */
static char *extract_run_script(lcl_interp *interp, lcl_value *dict) {
  char *raw = extract_dict_string(dict, "run");
  if (!raw) {
    return NULL;
  }

  char *substituted = substitute_namespace_vars(interp, raw);
  free(raw);
  return substituted;
}

/*
 * Extract a string list from a dict value.
 * Can handle both single strings and lists.
 */
static void extract_string_list(lcl_value *val, s_arr **arr) {
  if (!val) {
    return;
  }

  size_t len = lcl_list_len(val);
  if (len > 0) {
    /* It's a list */
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
    /* It's a single string */
    const char *str = lcl_value_to_string(val);
    if (str && str[0]) {
      if (!*arr) {
        *arr = s_arr_new();
      }
      s_arr_push(*arr, strdup_safe(str));
    }
  }
}

static dagwood_task *extract_task(const char *project_name,
                                  const char *task_name, lcl_value *task_dict,
                                  dagwood_project *project) {
  dagwood_task *task = dagwood_task_new();
  if (!task) {
    return NULL;
  }

  task->name = strdup_safe(task_name);
  task->id = make_qualified_name(project_name, task_name);
  task->description = extract_dict_string(task_dict, "description");
  task->run = extract_run_script(g_ctx.interp, task_dict);
  task->always_run = extract_dict_bool(task_dict, "always_run");

  task->shell = project->shell;
  task->shell_arg = project->shell_arg;
  task->wd = project->cwd;

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
    project->shell = shell;
  }

  char *shell_arg = extract_dict_string(config_dict, "shell_arg");

  if (shell_arg) {
    project->shell_arg = shell_arg;
  }

  project->always_run = extract_dict_bool(config_dict, "always_run");

  char *desc = extract_dict_string(config_dict, "description");

  if (desc) {
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

    project->name = strdup_safe(project_name);
    project->shell = dagwood_platform_shell();
    project->shell_arg = dagwood_platform_shell_arg();

    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd))) {
      project->cwd = strdup_safe(cwd);
    }

    extract_project_config(interp, project_name, project);
    p_map_set(project_registry, project_name, project);

    char tasks_cmd[512];
    snprintf(tasks_cmd, sizeof(tasks_cmd), "$%s::_tasks", project_name);

    lcl_value *tasks_dict = NULL;
    if (lcl_eval_string(interp, tasks_cmd, &tasks_dict) == LCL_RC_OK &&
        tasks_dict) {
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
                extract_task(project_name, task_name, task_dict, project);
            if (task) {
              t_map_set(task_registry, task->id, task);
            }

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
                extract_task(project_name, cmd_name, cmd_dict, project);

            if (cmd) {
              t_map_set(command_registry, cmd->id, cmd);
            }

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
  lcl_register_spec(interp, "import", s_import);

  /* Additional commands for project-scoped variables and CLI args */
  lcl_register_proc(interp, "def", c_let_var);
  lcl_register_proc(interp, "arg", c_arg);
}

interpreter *interpreter_new(void) {
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

  g_ctx.project_registry = w->project_registry;
  g_ctx.task_registry = w->task_registry;
  g_ctx.command_registry = w->command_registry;
  g_ctx.interp = w->interp;
  g_ctx.current_project = NULL;
  g_ctx.project = NULL;

  register_dagwood_commands(w->interp);

  if (!load_embedded_dsl(w->interp)) {
    t_map_delete(w->command_registry);
    t_map_delete(w->task_registry);
    p_map_delete(w->project_registry);
    lcl_interp_free(w->interp);
    free(w);
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

  g_ctx.project_registry = NULL;
  g_ctx.task_registry = NULL;
  g_ctx.command_registry = NULL;
  g_ctx.interp = NULL;
  g_ctx.current_project = NULL;
  g_ctx.project = NULL;
}

const char *interpreter_get_error(interpreter *interp) {
  if (!interp || !interp->interp) {
    return NULL;
  }

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
