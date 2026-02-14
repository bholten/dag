#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data.h"

STR_MAP_IMPL(ts_map, task_state, (task_state)-1, "memoized task state")
