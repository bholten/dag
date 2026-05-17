#define _GNU_SOURCE

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "dagwood.h"
#include "data.h"

extern char **environ;

#define DAGWOOD_LINE_BUF_SIZE 8192

/* One per task in a layer. Holds the spawned child's pid, the parent's
 * read ends of its stdout/stderr pipes, and per-stream line buffers.
 * pid == -1 means the task was skipped (not stale or invalid). */
struct task_capture {
  dagwood_task *task;
  pid_t pid;
  int out_fd;
  int err_fd;
  size_t out_len;
  size_t err_len;
  char out_buf[DAGWOOD_LINE_BUF_SIZE];
  char err_buf[DAGWOOD_LINE_BUF_SIZE];
};

dagwood_graph *dagwood_graph_new(t_map *task_registry) {
  dagwood_graph *g = calloc(1, sizeof(*g));

  if (!g) {
    return NULL;
  }

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
    fprintf(stderr, "[dagwood] [%s] cache hit -> task not stale\n", task->id);
    return false;
  }

  if (memo_value == TASK_STALE) {
    fprintf(stderr, "[dagwood] [%s] cache hit -> task stale\n", task->id);
    return true;
  }

  if (task->always_run) {
    ts_map_set(g->memo, task->id, TASK_STALE);
    fprintf(stderr, "[dagwood] [%s] stale - always_run = true\n", task->id);
    return true;
  }

  /* Side-effect tasks (no outputs) always run. With no outputs there's
   * nothing to compare mtimes against, and the layer-order execution
   * guarantees deps have already run by the time we get here. */
  if (s_arr_len(task->outputs) == 0) {
    ts_map_set(g->memo, task->id, TASK_STALE);
    fprintf(stderr, "[dagwood] [%s] stale - no outputs (side-effect task)\n",
            task->id);
    return true;
  }

  /* Tasks with outputs but no inputs: stale only if outputs don't exist */
  if (s_arr_len(task->inputs) == 0) {
    for (size_t k = 0; k < s_arr_len(task->outputs); k++) {
      const char *file_out = s_arr_get(task->outputs, k);
      struct stat file_out_info;

      if (stat(file_out, &file_out_info) != 0) {
        ts_map_set(g->memo, task->id, TASK_STALE);
        fprintf(stderr, "[dagwood] [%s] stale - output missing: %s\n", task->id,
                file_out);
        return true;
      }
    }

    ts_map_set(g->memo, task->id, TASK_CLEAN);
    fprintf(stderr, "[dagwood] [%s] clean - outputs exist, no inputs to compare\n",
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
            fprintf(stderr, "[dagwood] [%s] stale - mtime calc: %s > %s\n",
                    task->id, file_in, file_out);
            return true;
          }
        } else {
          ts_map_set(g->memo, task->id, TASK_STALE);
          fprintf(stderr, "[dagwood] [%s] stale - output file missing: %s\n",
                  task->id, file_out);
          return true;
        }
      }
    } else {
      ts_map_set(g->memo, task->id, TASK_STALE);
      fprintf(stderr, "[dagwood] [%s] stale - input file missing: %s\n",
              task->id, file_in);
      return true;
    }
  }

  ts_map_set(g->memo, task->id, TASK_CLEAN);

  return false;
}

/* Emit `len` bytes from `data` as a single prefixed line on `out`.
 * Always appends a newline (synthesized if `data` didn't end with one). */
static void emit_line(const char *task_id, const char *data, size_t len,
                      FILE *out, bool quiet) {
  if (quiet) {
    return;
  }

  fprintf(out, "[%s] %.*s\n", task_id, (int)len, data);
  fflush(out);
}

/* Scan `*len` bytes in `buf` for newlines. For each complete line, emit
 * it (without the trailing \n) and shift the buffer. If the buffer is
 * completely full with no newline found, emit what we have with a
 * synthesized terminator and reset. */
static void drain_buffer(const char *task_id, char *buf, size_t *len,
                         FILE *out, bool quiet) {
  size_t start = 0;
  size_t i;

  for (i = 0; i < *len; i++) {
    if (buf[i] == '\n') {
      emit_line(task_id, buf + start, i - start, out, quiet);
      start = i + 1;
    }
  }

  if (start > 0) {
    if (start < *len) {
      memmove(buf, buf + start, *len - start);
    }
    *len -= start;
  }

  if (*len == DAGWOOD_LINE_BUF_SIZE) {
    /* Buffer full, no newline in sight: split silently per design. */
    emit_line(task_id, buf, *len, out, quiet);
    *len = 0;
  }
}

/* Flush any trailing partial line at EOF. */
static void flush_partial(const char *task_id, char *buf, size_t *len,
                          FILE *out, bool quiet) {
  if (*len > 0) {
    emit_line(task_id, buf, *len, out, quiet);
    *len = 0;
  }
}

/* Drain pipes via poll() until every capture's stdout and stderr have
 * hit EOF (i.e. every child has exited and its pipes have been read
 * dry). */
static void drain_captures(struct task_capture *caps, size_t count,
                           bool quiet) {
  struct pollfd *pfds = calloc(count * 2, sizeof(*pfds));
  /* Parallel array: pfd_owner[i] is the cap index, pfd_is_err[i] is the
   * stream selector for pfds[i]. */
  size_t *pfd_owner = calloc(count * 2, sizeof(*pfd_owner));
  bool *pfd_is_err = calloc(count * 2, sizeof(*pfd_is_err));

  if (!pfds || !pfd_owner || !pfd_is_err) {
    fprintf(stderr, "[dagwood] failed to allocate poll arrays\n");
    free(pfds);
    free(pfd_owner);
    free(pfd_is_err);
    return;
  }

  while (1) {
    size_t nfds = 0;

    for (size_t i = 0; i < count; i++) {
      if (caps[i].out_fd >= 0) {
        pfds[nfds].fd = caps[i].out_fd;
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        pfd_owner[nfds] = i;
        pfd_is_err[nfds] = false;
        nfds++;
      }

      if (caps[i].err_fd >= 0) {
        pfds[nfds].fd = caps[i].err_fd;
        pfds[nfds].events = POLLIN;
        pfds[nfds].revents = 0;
        pfd_owner[nfds] = i;
        pfd_is_err[nfds] = true;
        nfds++;
      }
    }

    if (nfds == 0) {
      break;
    }

    int pr = poll(pfds, nfds, -1);

    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }

      perror("[dagwood] poll");
      break;
    }

    for (size_t i = 0; i < nfds; i++) {
      if (pfds[i].revents == 0) {
        continue;
      }

      struct task_capture *cap = &caps[pfd_owner[i]];
      bool is_err = pfd_is_err[i];
      int *fdp = is_err ? &cap->err_fd : &cap->out_fd;
      char *buf = is_err ? cap->err_buf : cap->out_buf;
      size_t *blen = is_err ? &cap->err_len : &cap->out_len;
      FILE *target = is_err ? stderr : stdout;

      ssize_t n = read(*fdp, buf + *blen, DAGWOOD_LINE_BUF_SIZE - *blen);

      if (n > 0) {
        *blen += (size_t)n;
        drain_buffer(cap->task->id, buf, blen, target, quiet);
      } else if (n == 0 || (n < 0 && errno != EINTR)) {
        /* EOF or fatal read error: flush partial and close. */
        flush_partial(cap->task->id, buf, blen, target, quiet);
        close(*fdp);
        *fdp = -1;
      }
    }
  }

  free(pfds);
  free(pfd_owner);
  free(pfd_is_err);
}

static bool run_layer(dagwood_graph *g, t_arr *layer) {
  if (!g->graph_built) {
    return false;
  }

  size_t count = t_arr_len(layer);
  pid_t *pids = calloc(count, sizeof(pid_t));
  struct task_capture *caps = calloc(count, sizeof(*caps));

  if (!pids || !caps) {
    fprintf(stderr, "[dagwood] failed to allocate layer state\n");
    free(pids);
    free(caps);
    return false;
  }

  for (size_t i = 0; i < count; i++) {
    caps[i].pid = -1;
    caps[i].out_fd = -1;
    caps[i].err_fd = -1;
  }

  g->pidsv_unsafe = pids;
  g->pidsc = count;

  bool spawn_ok = true;

  for (size_t i = 0; i < count; i++) {
    dagwood_task *t = t_arr_get(layer, i);
    caps[i].task = t;

    if (!task_stale(g, t)) {
      fprintf(stderr, "[dagwood] [%s] task not stale\n", t->id);
      pids[i] = -1;
      continue;
    }

    fprintf(stderr, "[dagwood] [%s] spawning task number %zu\n", t->id, i);

    if (!t->shell || !t->shell_arg) {
      fprintf(stderr, "[dagwood] [%s] missing shell or shell_arg\n", t->id);
      pids[i] = -1;
      continue;
    }

    int out_pipe[2];
    int err_pipe[2];

    if (pipe(out_pipe) != 0) {
      perror("[dagwood] pipe");
      spawn_ok = false;
      break;
    }

    if (pipe(err_pipe) != 0) {
      perror("[dagwood] pipe");
      close(out_pipe[0]);
      close(out_pipe[1]);
      spawn_ok = false;
      break;
    }

    pid_t pid;
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);

    if (t->wd != NULL) {
      fprintf(stderr, "[dagwood] [%s] working directory: %s\n", t->id, t->wd);
      dagwood_spawn_addchdir(&actions, t->wd);
    }

    /* Wire the child's stdout/stderr to our pipe write ends, then close
     * the parent's read ends (which the child inherited) so EOF
     * propagates correctly when the child exits. */
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[1]);

    char *argv[] = {(char *)t->shell, (char *)t->shell_arg, (char *)t->run,
                    NULL};

    int spawn_status =
        posix_spawn(&pid, t->shell, &actions, &attr, argv, environ);

    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);

    /* Parent never writes to these. Close so child sees EOF on exit. */
    close(out_pipe[1]);
    close(err_pipe[1]);

    if (spawn_status == 0) {
      fprintf(stderr, "[dagwood] [%s] spawned\n", t->id);
      pids[i] = pid;
      caps[i].pid = pid;
      caps[i].out_fd = out_pipe[0];
      caps[i].err_fd = err_pipe[0];
    } else {
      fprintf(stderr, "[dagwood] [%s] spawn failed\n", t->id);
      close(out_pipe[0]);
      close(err_pipe[0]);

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

  /* Stream prefixed child output until every pipe hits EOF. */
  drain_captures(caps, count, g->quiet);

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

    if (WIFSIGNALED(status)) {
      fprintf(stderr, "[dagwood] [%s] terminated by signal %d\n",
              caps[j].task->id, WTERMSIG(status));
      layer_successful = false;
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      fprintf(stderr, "[dagwood] task number %zu failed\n", j);
      layer_successful = false;
    }
  }

  g->pidsv_unsafe = NULL;
  g->pidsc = 0;
  free(pids);
  free(caps);

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
    fprintf(stderr, "[dagwood] executing layer %zu\n", i);

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
    fprintf(stderr, "[dagwood] executing layer %zu\n", i);

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

  if (!dag_build(g->tasks, layers)) {
    t_layers_delete(layers);
    return false;
  }

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

  t_layers *layers = t_layers_new();

  if (!dag_build(g->tasks, layers)) {
    t_layers_delete(layers);
    return false;
  }

  printf("digraph \"dagwood\" {\n");
  printf("  rankdir=TB;\n");

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
