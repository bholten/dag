/**
 * Dagwood CLI - A task runner with DAG-based dependency resolution
 *
 * Usage:
 *   dag                      Run whole project (default: ./Dagwood)
 *   dag <task>               Run a specific task
 *   dag KEY=value <task>     Run task with build arguments
 *   dag run <task>           Run a specific task (alias for dag <task>)
 *   dag -l                   List all tasks
 *   dag -d                   Print DAG as Graphviz dot
 *   dag -y                   Dry run (show what would execute)
 *   dag -i [task]            Inspect task properties
 *   dag -r                   Start REPL
 */

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dagwood.h"
#include "data.h"
#include "interpreter.h"

#define DAGWOOD_VERSION "0.1.0"
#define DEFAULT_FILE "Dag"

typedef struct {
  char *name;
  char *value;
} cli_arg;

static dagwood_graph *g_graph = NULL;
static cli_arg *g_cli_args = NULL;
static int g_cli_argc = 0;

static void handle_signal(int signum) {
  if (g_graph && g_graph->pidsv_unsafe) {
    const char msg[] = "[dagwood] caught signal, terminating children\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);

    for (size_t i = 0; i < g_graph->pidsc; i++) {
      if (g_graph->pidsv_unsafe[i] > 0) {
        kill(-g_graph->pidsv_unsafe[i], SIGTERM);
      }
    }
  }
  _exit(128 + signum);
}

static void setup_signal_handlers(void) {
  struct sigaction sa;
  sa.sa_handler = handle_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
}

// clang-format off
static void show_help(void) {
  puts("Usage: dag [options] [KEY=value...] [target]");
  puts("");
  puts("Options:");
  puts("  -C, --directory DIR    Change to DIR before running");
  puts("  -f, --file FILE        Use FILE instead of ./Dagwood");
  puts("  -l, --list             List all tasks");
  puts("  -d, --dot              Print DAG as Graphviz dot format");
  puts("  -y, --dry-run          Show execution order without running");
  puts("  -i, --inspect          Inspect task properties (all or specific task)");
  puts("  -F, --force            Force rebuild (skip staleness checks)");
  puts("  -r, --repl             Start interactive REPL");
  puts("  -h, --help             Show this help message");
  puts("  -v, --version          Print version");
  puts("");
  puts("Build Arguments:");
  puts("  KEY=value              Override default value for 'arg KEY default'");
  puts("");
  puts("Examples:");
  puts("  dag                      Run all tasks in ./Dag");
  puts("  dag build                Run the 'build' task");
  puts("  dag BUILD_TYPE=release   Set BUILD_TYPE arg to 'release'");
  puts("  dag -f custom.dw         Use custom.dw instead of Dag");
  puts("  dag -C src build         Change to src/, then run 'build'");
}
// clang-format on

static void show_version(void) {
  puts(DAGWOOD_VERSION);
}

/* ============================================================================
 * Graph Setup and Execution
 * ============================================================================
 */

typedef struct {
  interpreter *interp;
  t_map *tasks;
  t_map *commands;
} dagwood_env;

static dagwood_env *env_new(const char *file) {
  dagwood_env *env = calloc(1, sizeof(*env));

  if (!env) {
    return NULL;
  }

  env->interp = interpreter_new();

  if (!env->interp) {
    free(env);
    return NULL;
  }

  for (int i = 0; i < g_cli_argc; i++) {
    interpreter_set_cli_arg(env->interp, g_cli_args[i].name,
                            g_cli_args[i].value);
  }

  env->tasks = interpreter_task_registry(env->interp, file);

  if (!env->tasks) {
    fprintf(stderr, "[dagwood] could not build project registry: %s\n",
            interpreter_get_error(env->interp));
    interpreter_delete(env->interp);
    free(env);
    return NULL;
  }

  env->commands = interpreter_command_registry(env->interp, file);
  return env;
}

static void env_delete(dagwood_env *env) {
  if (!env) {
    return;
  }

  if (g_graph) {
    dagwood_graph_delete(g_graph);
    g_graph = NULL;
  }

  interpreter_delete(env->interp);
  free(env);
}

static int env_build_graph(dagwood_env *env) {
  g_graph = dagwood_graph_new(env->tasks);

  if (!g_graph) {
    fprintf(stderr, "[dagwood] could not build graph\n");
    return -1;
  }

  setup_signal_handlers();
  return 0;
}

/* Look up task, falling back to command registry if needed */
static dagwood_task *env_find_task(dagwood_env *env, const char *name) {
  dagwood_task *task = t_map_get(env->tasks, name);

  if (task) {
    return task;
  }

  if (!env->commands) {
    return NULL;
  }

  task = t_map_get(env->commands, name);

  if (task) {
    /* Promote command to task registry for execution */
    t_map_set(env->tasks, name, task);
  }
  return task;
}

static int cmd_list(const char *file) {
  dagwood_env *env = env_new(file);

  if (!env) {
    return EXIT_FAILURE;
  }

  interpreter_list_all(env->interp);
  env_delete(env);
  return EXIT_SUCCESS;
}

static int cmd_dot(const char *file) {
  dagwood_env *env = env_new(file);

  if (!env) {
    return EXIT_FAILURE;
  }

  if (env_build_graph(env) < 0) {
    env_delete(env);
    return EXIT_FAILURE;
  }

  bool ok = dagwood_graph_to_dot(g_graph);
  env_delete(env);
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int cmd_dry_run(const char *file) {
  dagwood_env *env = env_new(file);

  if (!env) {
    return EXIT_FAILURE;
  }

  if (env_build_graph(env) < 0) {
    env_delete(env);
    return EXIT_FAILURE;
  }

  bool ok = dagwood_graph_dry_run(g_graph);
  env_delete(env);
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int cmd_inspect(const char *file, const char *task_name) {
  dagwood_env *env = env_new(file);

  if (!env) {
    return EXIT_FAILURE;
  }

  interp_result r = interpreter_inspect(env->interp, task_name);
  env_delete(env);
  return r == INTERP_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int cmd_repl(void) {
  interpreter *interp = interpreter_new();
  interpreter_repl(interp);
  interpreter_delete(interp);
  return EXIT_SUCCESS;
}

static int cmd_run_all(const char *file) {
  dagwood_env *env = env_new(file);

  if (!env) {
    return EXIT_FAILURE;
  }

  if (env_build_graph(env) < 0) {
    env_delete(env);
    return EXIT_FAILURE;
  }

  int ok = dagwood_graph_execute(g_graph);
  env_delete(env);
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int cmd_run_task(const char *file, const char *name, int force) {
  dagwood_env *env = env_new(file);

  if (!env) {
    return EXIT_FAILURE;
  }

  if (!env_find_task(env, name)) {
    fprintf(stderr, "[dagwood] task not found: %s\n", name);
    env_delete(env);
    return EXIT_FAILURE;
  }

  if (env_build_graph(env) < 0) {
    env_delete(env);
    return EXIT_FAILURE;
  }

  if (force) {
    g_graph->force_run = true;
  }

  int ok = dagwood_graph_execute_task(g_graph, name);
  env_delete(env);
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int cmd_run_subcommand(const char *file, int force, int argc,
                              char **argv) {
  /* argv[0] is "run", argv[1] should be the task name */
  if (argc < 2) {
    fprintf(stderr, "[dagwood] run: missing task name\n");
    return EXIT_FAILURE;
  }

  return cmd_run_task(file, argv[1], force);
}

static void parse_cli_args(int argc, char **argv, int start_idx,
                           const char **task_out) {
  *task_out = NULL;

  int arg_count = 0;

  for (int i = start_idx; i < argc; i++) {
    if (strchr(argv[i], '=') != NULL) {
      arg_count++;
    }
  }

  if (arg_count > 0) {
    g_cli_args = calloc((size_t)arg_count, sizeof(cli_arg));

    if (!g_cli_args) {
      return;
    }
  }

  for (int i = start_idx; i < argc; i++) {
    char *eq = strchr(argv[i], '=');

    if (eq != NULL) {
      size_t name_len = (size_t)(eq - argv[i]);
      g_cli_args[g_cli_argc].name = malloc(name_len + 1);

      if (g_cli_args[g_cli_argc].name) {
        memcpy(g_cli_args[g_cli_argc].name, argv[i], name_len);
        g_cli_args[g_cli_argc].name[name_len] = '\0';
      }
      g_cli_args[g_cli_argc].value = strdup(eq + 1);
      g_cli_argc++;
    } else if (*task_out == NULL) {
      *task_out = argv[i];
    }
  }
}

static void free_cli_args(void) {
  for (int i = 0; i < g_cli_argc; i++) {
    free(g_cli_args[i].name);
    free(g_cli_args[i].value);
  }

  free(g_cli_args);
  g_cli_args = NULL;
  g_cli_argc = 0;
}

int main(int argc, char **argv) {
  static struct option long_opts[] = {
      {"directory", required_argument, 0, 'C'},
      {"file",      required_argument, 0, 'f'},
      {"list",      no_argument,       0, 'l'},
      {"dot",       no_argument,       0, 'd'},
      {"dry-run",   no_argument,       0, 'y'},
      {"inspect",   no_argument,       0, 'i'},
      {"force",     no_argument,       0, 'F'},
      {"repl",      no_argument,       0, 'r'},
      {"help",      no_argument,       0, 'h'},
      {"version",   no_argument,       0, 'v'},
      {0,           0,                 0, 0  }
  };

  const char *directory = NULL;
  const char *file = DEFAULT_FILE;
  int force = 0;
  int opt;

  enum {
    MODE_RUN,
    MODE_LIST,
    MODE_DOT,
    MODE_DRY,
    MODE_INSPECT,
    MODE_REPL
  } mode = MODE_RUN;

  while ((opt = getopt_long(argc, argv, "C:f:ldyiFrhv", long_opts, NULL)) !=
         -1) {
    switch (opt) {
    case 'C': directory = optarg; break;
    case 'f': file = optarg; break;
    case 'l': mode = MODE_LIST; break;
    case 'd': mode = MODE_DOT; break;
    case 'y': mode = MODE_DRY; break;
    case 'i': mode = MODE_INSPECT; break;
    case 'F': force = 1; break;
    case 'r': mode = MODE_REPL; break;
    case 'h': show_help(); return EXIT_SUCCESS;
    case 'v': show_version(); return EXIT_SUCCESS;
    default: show_help(); return EXIT_FAILURE;
    }
  }

  if (directory && chdir(directory) != 0) {
    perror("[dagwood] chdir");
    return EXIT_FAILURE;
  }

  const char *target = NULL;
  parse_cli_args(argc, argv, optind, &target);

  int result;

  switch (mode) {
  case MODE_LIST:
    result = cmd_list(file);
    free_cli_args();
    return result;
  case MODE_DOT:
    result = cmd_dot(file);
    free_cli_args();
    return result;
  case MODE_DRY:
    result = cmd_dry_run(file);
    free_cli_args();
    return result;
  case MODE_INSPECT:
    result = cmd_inspect(file, target);
    free_cli_args();
    return result;
  case MODE_REPL: free_cli_args(); return cmd_repl();
  case MODE_RUN: break;
  }

  if (target == NULL) {
    result = cmd_run_all(file);
    free_cli_args();
    return result;
  }

  if (strcmp(target, "run") == 0) {
    result = cmd_run_subcommand(file, force, argc - optind, &argv[optind]);
    free_cli_args();
    return result;
  }

  result = cmd_run_task(file, target, force);
  free_cli_args();
  return result;
}
