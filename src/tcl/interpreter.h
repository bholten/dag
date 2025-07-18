#ifndef INTERPRETER
#define INTERPRETER

typedef struct interpreter interpreter;

interpreter* interpreter_new(void);
int interpreter_eval(interpreter* interp, const char* file);
void interpreter_delete(interpreter* interp);
const char* interpreter_get_error(interpreter* interp);

#endif
