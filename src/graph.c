#define _GNU_SOURCE

#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "dagwood.h"
#include "data.h"

extern char **environ;

dagwood_graph *dagwood_graph_new(t_map *task_registry) {
  dagwood_graph *g = calloc(1, sizeof(*g));

  t_map *task_by_name = t_map_new();

  if (!task_by_name) {
    free(g);
    return NULL;
  }

  t_map *task_by_output = t_map_new();

  if (!task_by_output) {
    t_map_delete(task_by_name);
    free(g);
    return NULL;
  }

  ts_map *memo = ts_map_new();

  if (!memo) {
    t_map_delete(task_by_output);
    t_map_delete(task_by_name);
    free(g);
    return NULL;
  }

  t_arr *tasks = t_arr_new();

  if (!tasks) {
    ts_map_delete(memo);
    t_map_delete(task_by_output);
    t_map_delete(task_by_name);
    free(g);
    return NULL;
  }

  bool graph_built = false;

  pid_t *pidsv_unsafe = NULL;
  size_t pidsc = 0;

  for (size_t i = t_map_begin(task_registry); i < t_map_end(task_registry);
       i++) {
    dagwood_task *t = t_map_value(task_registry, i);

    if (!t) {
      continue;
    }

    t_arr_push(tasks, t);
    t_map_set(task_by_name, t->id, t);

    for (size_t k = 0; k < s_arr_len(t->outputs); k++) {
      const char *output = s_arr_get(t->outputs, k);

      if (!output) {
        continue;
      }

      t_map_set(task_by_output, output, t);
    }
  }

  g->tasks = tasks;
  g->task_by_name = task_by_name;
  g->task_by_output = task_by_output;
  g->memo = memo;
  g->pidsv_unsafe = pidsv_unsafe;
  g->pidsc = pidsc;
  g->graph_built = graph_built;

  return g;
}

void dagwood_graph_delete(dagwood_graph *g) {
  t_map_delete(g->task_by_name);
  t_map_delete(g->task_by_output);
  ts_map_delete(g->memo);
  t_arr_delete(g->tasks);
  free(g);
}

static bool build_graph(dagwood_graph *g) {
  for (size_t i = 0; i < t_arr_len(g->tasks); i++) {
    dagwood_task *t = t_arr_get(g->tasks, i);

    for (size_t j = 0; j < s_arr_len(t->depends_on); j++) {
      const char *dep_name = s_arr_get(t->depends_on, j);
      dagwood_task *dep = t_map_get(g->task_by_name, dep_name);

      if (!dep) {
        fprintf(stderr,
                "[dagwood] task '%s' depends on '%s', which does not exist\n",
                t->id, dep_name);
        return false;
      }

      dagwood_task_add_edge(t, dep);
    }

    for (size_t k = 0; k < s_arr_len(t->inputs); k++) {
      const char *input = s_arr_get(t->inputs, k);
      dagwood_task *dep = t_map_get(g->task_by_output, input);

      if (!dep) {
        continue;
      }

      dagwood_task_add_edge(t, dep);
    }
  }

  g->graph_built = true;
  return true;
}

static bool task_stale(dagwood_graph *g, dagwood_task *task) {
  if (g->force_run) {
    return true;
  }

  task_state memo_value = ts_map_get(g->memo, task->id);

  if (memo_value == TASK_CLEAN) {
    printf("[dagwood] [%s] cache hit -> task not stale\n", task->id);
    return false;
  }

  if (memo_value == TASK_STALE) {
    printf("[dagwood] [%s] cache hit -> task stale\n", task->id);
    return true;
  }

  if (task->always_run) {
    ts_map_set(g->memo, task->id, TASK_STALE);
    printf("[dagwood] [%s] stale - always_run = true\n", task->id);
    return true;
  }

  if (s_arr_len(task->outputs) == 0) {
    if (s_arr_len(task->depends_on) == 0) {
      printf("[dagwood] [%s] stale - no outputs (side-effect task)\n",
             task->id);
      return true;
    }

    for (size_t i = 0; i < s_arr_len(task->depends_on); i++) {
      const char *dep_name = s_arr_get(task->depends_on, i);
      if (!dep_name) {
        continue;
      }

      dagwood_task *dep = t_map_get(g->task_by_name, dep_name);

      if (!dep) {
        continue;
      }

      if (task_stale(g, dep)) {
        ts_map_set(g->memo, task->id, TASK_STALE);
        printf("[dagwood] [%s] stale - depends_on task %s stale\n", task->id,
               dep->id);
        return true;
      }
    }

    ts_map_set(g->memo, task->id, TASK_STALE);
    printf("[dagwood] [%s] stale - no outputs (side-effect task)\n", task->id);
    return true;
  }

  /* Tasks with outputs but no inputs: stale only if outputs don't exist */
  if (s_arr_len(task->inputs) == 0) {
    for (size_t k = 0; k < s_arr_len(task->outputs); k++) {
      const char *file_out = s_arr_get(task->outputs, k);
      struct stat file_out_info;

      if (stat(file_out, &file_out_info) != 0) {
        ts_map_set(g->memo, task->id, TASK_STALE);
        printf("[dagwood] [%s] stale - output missing: %s\n", task->id,
               file_out);
        return true;
      }
    }

    ts_map_set(g->memo, task->id, TASK_CLEAN);
    printf("[dagwood] [%s] clean - outputs exist, no inputs to compare\n",
           task->id);
    return false;
  }

  for (size_t j = 0; j < s_arr_len(task->inputs); j++) {
    const char *file_in = s_arr_get(task->inputs, j);
    struct stat file_in_info;

    if (stat(file_in, &file_in_info) == 0) {
      time_t m_in = file_in_info.st_mtime;

      for (size_t k = 0; k < s_arr_len(task->outputs); k++) {
        const char *file_out = s_arr_get(task->outputs, k);
        struct stat file_out_info;

        if (stat(file_out, &file_out_info) == 0) {
          time_t m_out = file_out_info.st_mtime;

          if (m_in > m_out) {
            ts_map_set(g->memo, task->id, TASK_STALE);
            printf("[dagwood] [%s] stale - mtime calc: %s > %s\n", task->id,
                   file_in, file_out);
            return true;
          }
        } else {
          ts_map_set(g->memo, task->id, TASK_STALE);
          printf("[dagwood] [%s] stale - output file missing: %s\n", task->id,
                 file_out);
          return true;
        }
      }
    } else {
      ts_map_set(g->memo, task->id, TASK_STALE);
      printf("[dagwood] [%s] stale - input file missing: %s\n", task->id,
             file_in);
      return true;
    }
  }

  ts_map_set(g->memo, task->id, TASK_CLEAN);

  return false;
}

static bool run_layer(dagwood_graph *g, t_arr *layer) {
  if (!g->graph_built) {
    return false;
  }

  size_t count = t_arr_len(layer);
  pid_t *pids = calloc(count, sizeof(pid_t));

  if (!pids) {
    fprintf(stderr, "[dagwood] failed to allocate pid array\n");
    return false;
  }

  g->pidsv_unsafe = pids;
  g->pidsc = count;

  bool spawn_ok = true;

  for (size_t i = 0; i < count; i++) {
    dagwood_task *t = t_arr_get(layer, i);

    if (!task_stale(g, t)) {
      fprintf(stdout, "[dagwood] [%s] task not stale\n", t->id);
      pids[i] = -1;
      continue;
    }

    fprintf(stdout, "[dagwood] [%s] spawning task number %zu\n", t->id, i);

    if (!t->shell || !t->shell_arg) {
      fprintf(stderr, "[dagwood] [%s] missing shell or shell_arg\n", t->id);
      pids[i] = -1;
      continue;
    }

    pid_t pid;
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, STDERR_FILENO, STDERR_FILENO);

    if (t->wd != NULL) {
      printf("[dagwood] [%s] working directory: %s\n", t->id, t->wd);
      dagwood_spawn_addchdir(&actions, t->wd);
    }

    char *argv[] = {(char *)t->shell, (char *)t->shell_arg, (char *)t->run,
                    NULL};

    int spawn_status =
        posix_spawn(&pid, t->shell, &actions, &attr, argv, environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    if (spawn_status == 0) {
      printf("[dagwood] [%s] spawned\n", t->id);
      pids[i] = pid;
    } else {
      fprintf(stderr, "[dagwood] [%s] spawn failed\n", t->id);

      /* Kill already-spawned processes in this layer */
      for (size_t k = 0; k < i; k++) {
        if (pids[k] > 0) {
          kill(-pids[k], SIGTERM);
        }
      }

      spawn_ok = false;
      break;
    }
  }

  bool layer_successful = spawn_ok;

  for (size_t j = 0; j < count; j++) {
    if (pids[j] <= 0) {
      continue;
    }

    int status = 0;

    if (waitpid(pids[j], &status, 0) == -1) {
      int saved_errno = errno;
      fprintf(stderr, "[dagwood] waitpid failed in layer %zu\n", j);
      errno = saved_errno;
      perror("waitpid failed");
      layer_successful = false;
      continue;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      fprintf(stderr, "[dagwood] task number %zu failed\n", j);
      layer_successful = false;
    }
  }

  g->pidsv_unsafe = NULL;
  g->pidsc = 0;
  free(pids);

  return layer_successful;
}

bool dagwood_graph_execute_task(dagwood_graph *g, const char *task_name) {
  dagwood_task *task = t_map_get(g->task_by_name, task_name);

  if (!task) {
    return false;
  }

  if (!g->graph_built) {
    if (!build_graph(g)) {
      return false;
    }
  }

  t_layers *layers = t_layers_new();

  if (!dag_build_from_task(task, layers)) {
    t_layers_delete(layers);
    return false;
  }

  bool success = true;

  for (size_t i = 0; i < t_layers_len(layers); i++) {
    t_arr *layer = t_layers_get(layers, i);
    printf("[dagwood] executing layer %zu\n", i);

    if (!run_layer(g, layer)) {
      fprintf(stderr, "[dagwood] failed on layer %zu\n", i);
      success = false;
      break;
    }
  }

  t_layers_delete(layers);
  return success;
}

bool dagwood_graph_execute(dagwood_graph *g) {
  if (!g->graph_built) {
    if (!build_graph(g)) {
      return false;
    }
  }

  t_layers *layers = t_layers_new();

  if (!dag_build(g->tasks, layers)) {
    t_layers_delete(layers);
    return false;
  }

  bool success = true;

  for (size_t i = 0; i < t_layers_len(layers); i++) {
    t_arr *layer = t_layers_get(layers, i);
    printf("[dagwood] executing layer %zu\n", i);

    if (!run_layer(g, layer)) {
      fprintf(stderr, "[dagwood] failed on layer %zu\n", i);
      success = false;
      break;
    }
  }

  t_layers_delete(layers);
  return success;
}

bool dagwood_graph_dry_run(dagwood_graph *g) {
  if (!g->graph_built) {
    if (!build_graph(g)) {
      return false;
    }
  }

  t_layers *layers = t_layers_new();
  dag_build(g->tasks, layers);

  printf("Dagwood DAG Plan:\n");

  for (size_t i = 0; i < t_layers_len(layers); i++) {
    printf("-  Layer %zu:\n", i);

    t_arr *layer = t_layers_get(layers, i);

    for (size_t j = 0; j < t_arr_len(layer); j++) {
      dagwood_task *t = t_arr_get(layer, j);

      printf("  --  Task Name: %s\n", t->id);

      s_arr *deps = t->depends_on;
      s_arr *inputs = t->inputs;
      s_arr *outputs = t->outputs;
      const char *run = t->run;

      if (deps != NULL && s_arr_len(deps) > 0) {
        printf("    --  Depends On:\n");

        for (size_t k = 0; k < s_arr_len(deps); k++) {
          const char *dep = s_arr_get(deps, k);
          printf("                    - %s\n", dep);
        }
      }

      if (inputs != NULL && s_arr_len(inputs) > 0) {
        printf("    --  Inputs:\n");

        for (size_t m = 0; m < s_arr_len(inputs); m++) {
          const char *in = s_arr_get(inputs, m);
          printf("                - %s\n", in);
        }
      }

      if (outputs != NULL && s_arr_len(outputs) > 0) {
        printf("    --  Outputs:\n");

        for (size_t n = 0; n < s_arr_len(outputs); n++) {
          const char *out = s_arr_get(outputs, n);
          printf("                 - %s\n", out);
        }
      }
    }
  }

  t_layers_delete(layers);
  return true;
}

bool dagwood_graph_to_dot(dagwood_graph *g) {
  if (!g->graph_built) {
    if (!build_graph(g)) {
      return false;
    }
  }

  printf("digraph \"dagwood\" {\n");
  printf("  rankdir=TB;\n");

  t_layers *layers = t_layers_new();
  dag_build(g->tasks, layers);

  for (size_t i = 0; i < t_layers_len(layers); i++) {
    t_arr *layer = t_layers_get(layers, i);

    printf("  { rank = same; ");

    for (size_t j = 0; j < t_arr_len(layer); j++) {
      dagwood_task *t = t_arr_get(layer, j);

      printf("\"%s\"; ", t->id);
    }

    printf(" }\n");
  }

  for (size_t i = 0; i < t_arr_len(g->tasks); i++) {
    dagwood_task *t = t_arr_get(g->tasks, i);

    if (!t) {
      continue;
    }

    for (size_t j = 0; j < t_arr_len(t->edges); j++) {
      dagwood_task *e = t_arr_get(t->edges, j);

      if (!e) {
        continue;
      }

      printf("  \"%s\" -> \"%s\";\n", t->id, e->id);
    }
  }

  printf("}\n");

  t_layers_delete(layers);
  return true;
}
