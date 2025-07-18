#include <stdio.h>
#include <stdlib.h>

#include "tcl/interpreter.h"

int main(int argc, const char **argv) {
  interpreter* interp = interpreter_new();
  int result = -1;
  
  if (argc > 1) {
    result = interpreter_eval(interp, argv[1]);
    
    if (result != 0) {
      fprintf(stderr, "[dagwood] error: %s\n", interpreter_get_error(interp));
    } else {
      puts("[dagwood] file evaluated successfully\n");
    }

  } else {
    printf("Usage: %s dagfile.tcl\n", argv[0]);
  }
  
  interpreter_delete(interp);
  
  return result;
}
