#include <assert.h>
#include <stdlib.h>

#include "dagwood.h"
#include "data.h"

dagwood_project *dagwood_project_new(void) {
  dagwood_project *project = calloc(1, sizeof(*project));
  if (!project) {
    return NULL;
  }

  project->name = "default_project";
  project->shell = dagwood_platform_shell();
  project->shell_arg = dagwood_platform_shell_arg();
  project->description = "N/A";
  project->cwd = NULL;

  return project;
}

void dagwood_project_delete(dagwood_project *project) {
  free(project);
}
