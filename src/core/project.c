#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "t_arr.h"
#include "t_layers.h"
#include "t_map.h"
#include "ts_map.h"
#include "task.h"

#include "project.h"
#include "shell.h"

dagwood_project* dagwood_project_new(void) {
  dagwood_project* project = malloc(sizeof(*project));
  project->name = strdup("project-name");
  project->default_shell = strdup(DEFAULT_SHELL);
  project->description = strdup("description");
  
  project->tasks = t_arr_new();
  
  project->task_by_name = t_map_new();
  project->task_by_output = t_map_new();
  project->memo = ts_map_new();
  
  project->always_run = false;
  project->graph_built = false;
  
  return project;
}

void dagwood_project_delete(dagwood_project* project) {
  t_map_delete(project->task_by_name);
  t_map_delete(project->task_by_output);
  ts_map_delete(project->memo);

  t_arr_delete(project->tasks);

  free((void*) project->name);
  free((void*) project->default_shell);
  free((void*) project->description);
  free(project);
}

dagwood_task* dagwood_project_get_task(dagwood_project* project, const char* name) {
  return t_map_get(project->task_by_name, name);
}

void dagwood_project_add_task(dagwood_project* project, dagwood_task* task) {
  t_map_set(project->task_by_name, task->name, task);
  t_arr_push(project->tasks, task);
}

void dagwood_project_build_graph(dagwood_project* project) {
  if (!project) return;
  if (project->graph_built) return;

  for (size_t i = 0; i < t_arr_len(project->tasks); i++) {
    dagwood_task* t = t_arr_get(project->tasks, i);

    for (size_t j = 0; j < s_arr_len(t->outputs); j++) {
      const char* output = s_arr_get(t->outputs, j);
      t_map_set(project->task_by_output, output, t);
    }
  }

  for (size_t i = 0; i < t_arr_len(project->tasks); i++) {
    dagwood_task* t = t_arr_get(project->tasks, i);

    for (size_t j = 0; j < s_arr_len(t->depends_on); j++) {
      const char* dep_name = s_arr_get(t->depends_on, j);
      dagwood_task* dep = t_map_get(project->task_by_name, dep_name);

      if (dep) {
	dagwood_task_add_edge(t, dep);
      }
    }

    for (size_t k = 0; k < s_arr_len(t->inputs); k++) {
      const char* input_name = s_arr_get(t->inputs, k);
      dagwood_task* dep = t_map_get(project->task_by_output, input_name);
      
      if (dep) {
	dagwood_task_add_edge(t, dep);
      }
    }
  }

  project->graph_built = true;
}

bool dagwood_project_validate(dagwood_project* project) {
  for (size_t e = 0; e < t_arr_len(project->tasks); e++) {
    dagwood_task* t = t_arr_get(project->tasks, e);

    if (t->in_degree > 0) {
      fprintf(stderr, "Cycle detected involving task: %s\n", t->name);
      return false;
    }
  }

  return true;
}

t_layers* dagwood_project_build_dag(dagwood_project* project) {
  if (!project) return NULL;
  if (!project->graph_built) return NULL;
  
  t_arr* queue = t_arr_new();
  t_layers* layers = t_layers_new();

  for (size_t i = 0; i < t_arr_len(project->tasks); i++) {
    dagwood_task* task = t_arr_get(project->tasks, i);

    if (task->in_degree == 0) {
      t_arr_push(queue, task);
    }
  }

  while (t_arr_len(queue) > 0) {
    t_arr* layer = t_arr_new();
    t_arr* next_queue = t_arr_new();

    for (size_t i = 0; i < t_arr_len(queue); i++) {
      dagwood_task* task = t_arr_get(queue, i);
      t_arr_push(layer, task);

      printf("Visiting: %s\n", task->name);
      for (size_t j = 0; j < t_arr_len(task->reverse_edges); j++) {
	dagwood_task* dependent = t_arr_get(task->reverse_edges, j);
	dependent->in_degree--;
	
	if (dependent->in_degree == 0) {
	  t_arr_push(next_queue, dependent);
	}
      }
    }

    t_layers_push(layers, layer);
    t_arr_delete(queue);
    queue = next_queue;
  }

  if (!dagwood_project_validate(project)) return NULL;

  return layers;
}

static bool task_stale(dagwood_project* project, dagwood_task* task) {  
  if (!task) return true;
  if (project->always_run) return true;

  task_state memo_value = ts_map_get(project->memo, task->name);
  if (memo_value == TASK_CLEAN) return false;
  if (memo_value == TASK_STALE) return true;
  
  if (task->always_run) {
    ts_map_set(project->memo, task->name, TASK_STALE);
    printf("[%s] stale - always_run = true\n", task->name);
    return true;
  }
  
  if (s_arr_len(task->outputs) == 0) {
    for (size_t i = 0; i < s_arr_len(task->depends_on); i++) {
      const char* dep_name = s_arr_get(task->depends_on, i);
      if (!dep_name) continue;

      dagwood_task* dep = t_map_get(project->task_by_name, dep_name);

      if (!dep) continue;
      if (task_stale(project, dep)) {
	ts_map_set(project->memo, task->name, TASK_STALE);
	printf("[%s] stale - depends_on task %s stale\n", task->name, dep->name);
	return true;
      }
    }
  }
  
  for (size_t j = 0; j < s_arr_len(task->inputs); j++) {
    const char* file_in = s_arr_get(task->inputs, j);
    struct stat file_in_info;
    
    if (stat(file_in, &file_in_info) == 0) {
      time_t m_in = file_in_info.st_mtim.tv_sec;
      
      for (size_t k = 0; k < s_arr_len(task->outputs); k++) {
	const char* file_out = s_arr_get(task->outputs, k);	
	struct stat file_out_info;
	
	if (stat(file_out, &file_out_info) == 0) {
	  time_t m_out = file_out_info.st_mtim.tv_sec;

	  if (m_in > m_out) {
	    ts_map_set(project->memo, task->name, TASK_STALE);
	    printf("[%s] stale - mtime calc\n", task->name);
	    return true;
	  } 
	} else {
	  ts_map_set(project->memo, task->name, TASK_STALE);
	  printf("[%s] stale - output file missing\n", task->name);
	  return true;
	}
      }
    } else {
      ts_map_set(project->memo, task->name, TASK_STALE);
      printf("[%s] stale - input file missing\n", task->name);
      return true;
    }
  }
  
  for (size_t m = 0; m < s_arr_len(task->depends_on); m++) {
    const char* name = s_arr_get(task->depends_on, m);

    if (!name) continue;
    dagwood_task* dep = t_map_get(project->task_by_name, name);

    if (!dep) continue;
    if (task_stale(project, dep)) {
      ts_map_set(project->memo, dep->name, TASK_STALE);
      printf("[%s] stale - dep %s is stale\n", task->name, dep->name);
      return true; 
    }
  }

  ts_map_set(project->memo, task->name, TASK_CLEAN);

  return false;
}

int dagwood_project_execute(dagwood_project* project) {
  if (!project) return 1;
  
  if (!project->graph_built) {
    printf("Building graph\n");
    dagwood_project_build_graph(project);
  }

  printf("Building DAG\n");
  t_layers* layers = dagwood_project_build_dag(project);

  for (size_t i = 0; i < t_arr_len(project->tasks); i++) {
    dagwood_task* t = t_arr_get(project->tasks, i);
    printf("Task %s has edges to:\n", t->name);
    bool task_is_stale = task_stale(project, t);
    printf("[%s] stale? %s\n", t->name, task_is_stale ? "true" : "false");

    if (task_is_stale) {
      t->executor(t);
    }
    
    for (size_t j = 0; j < t_arr_len(t->edges); j++) {
      dagwood_task* dep = t_arr_get(t->edges, j);
      printf("  - %s\n", dep->name);
    }
    printf("Task %s has reverse_edges to:\n", t->name);
    for (size_t k = 0; k < t_arr_len(t->reverse_edges); k++) {
      dagwood_task* dep = t_arr_get(t->reverse_edges, k);
      printf("  - %s\n", dep->name);      
    }
  }
  
  if (!layers) return 1;

  for (size_t i = 0; i < t_layers_len(layers); i++) {
    t_arr* layer = t_layers_get(layers, i);
    printf("==> Layer %u\n", i);
    for (size_t j = 0; j < t_arr_len(layer); j++) {
      dagwood_task* task = t_arr_get(layer, j);
      printf("---- Task: %s\n", task->name);
    }
  }
  
  return 0;
}
