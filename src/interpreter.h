#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "data.h"

typedef struct interpreter interpreter;

typedef enum interp_result { INTERP_OK, INTERP_ERROR } interp_result;

interpreter *interpreter_new(void);
void interpreter_delete(interpreter *interp);

void interpreter_set_cli_arg(interpreter *interp, const char *name,
                             const char *value);

t_map *interpreter_task_registry(interpreter *interp, const char *file);
t_map *interpreter_command_registry(interpreter *interp, const char *file);

const char *interpreter_get_error(interpreter *interp);

interp_result interpreter_list_all(interpreter *interp);
interp_result interpreter_repl(interpreter *interp);

#endif
