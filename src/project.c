#include <stdlib.h>
#include <string.h>

#include "dagwood.h"
#include "data.h"

dagwood_project *dagwood_project_new(void) {
  dagwood_project *project = calloc(1, sizeof(*project));
  if (!project) {
    return NULL;
  }

  project->name = strdup("default_project");
  project->shell = strdup(dagwood_platform_shell());
  project->shell_arg = strdup(dagwood_platform_shell_arg());
  project->description = strdup("N/A");
  project->cwd = NULL;

  return project;
}

void dagwood_project_delete(dagwood_project *project) {
  if (!project) {
    return;
  }

  free((void *)project->name);
  free((void *)project->shell);
  free((void *)project->shell_arg);
  free((void *)project->description);
  free((void *)project->cwd);
  free(project);
}
