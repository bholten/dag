#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data.h"

#define INITIAL_CAP 16
#define LOAD_FACTOR 75

struct t_map {
  char **keys;
  dagwood_task **values;
  unsigned char *used;
  size_t cap;
  size_t len;
};

static size_t hash_string(const char *s) {
  size_t h = (size_t)*s;
  if (h) {
    for (++s; *s; ++s) {
      h = (h << 5) - h + (size_t)*s;
    }
  }
  return h;
}

static int t_map_resize(t_map *table, size_t new_cap);

t_map *t_map_new(void) {
  struct t_map *table = malloc(sizeof(*table));
  if (!table) {
    return NULL;
  }

  table->keys = calloc(INITIAL_CAP, sizeof(*table->keys));
  table->values = calloc(INITIAL_CAP, sizeof(*table->values));
  table->used = calloc(INITIAL_CAP, sizeof(*table->used));

  if (!table->keys || !table->values || !table->used) {
    free(table->keys);
    free(table->values);
    free(table->used);
    free(table);
    return NULL;
  }

  table->cap = INITIAL_CAP;
  table->len = 0;
  return table;
}

void t_map_delete(t_map *table) {
  size_t i;
  if (!table) {
    return;
  }

  for (i = 0; i < table->cap; i++) {
    if (table->used[i]) {
      free(table->keys[i]);
    }
  }

  free(table->keys);
  free(table->values);
  free(table->used);
  free(table);
}

dagwood_task *t_map_get(t_map *table, const char *name) {
  size_t h, idx, i;

  if (!name) {
    return NULL;
  }

  h = hash_string(name);
  idx = h % table->cap;

  for (i = 0; i < table->cap; i++) {
    size_t probe = (idx + i) % table->cap;

    if (!table->used[probe]) {
      return NULL;
    }

    if (strcmp(table->keys[probe], name) == 0) {
      return table->values[probe];
    }
  }

  return NULL;
}

static int t_map_resize(t_map *table, size_t new_cap) {
  char **old_keys = table->keys;
  dagwood_task **old_values = table->values;
  unsigned char *old_used = table->used;
  size_t old_cap = table->cap;
  size_t i;

  table->keys = calloc(new_cap, sizeof(*table->keys));
  table->values = calloc(new_cap, sizeof(*table->values));
  table->used = calloc(new_cap, sizeof(*table->used));

  if (!table->keys || !table->values || !table->used) {
    free(table->keys);
    free(table->values);
    free(table->used);
    table->keys = old_keys;
    table->values = old_values;
    table->used = old_used;
    return 0;
  }

  table->cap = new_cap;
  table->len = 0;

  for (i = 0; i < old_cap; i++) {
    if (old_used[i]) {
      size_t h = hash_string(old_keys[i]);
      size_t idx = h % new_cap;
      size_t j;

      for (j = 0; j < new_cap; j++) {
        size_t probe = (idx + j) % new_cap;

        if (!table->used[probe]) {
          table->keys[probe] = old_keys[i];
          table->values[probe] = old_values[i];
          table->used[probe] = 1;
          table->len++;
          break;
        }
      }
    }
  }

  free(old_keys);
  free(old_values);
  free(old_used);
  return 1;
}

bool t_map_set(t_map *table, const char *name, const dagwood_task *task) {
  size_t h, idx, i;

  assert(task != NULL);

  if (table->len * 100 >= table->cap * LOAD_FACTOR) {
    if (!t_map_resize(table, table->cap * 2)) {
      fprintf(stderr, "[dagwood] out of memory inserting task\n");
      return false;
    }
  }

  h = hash_string(name);
  idx = h % table->cap;

  for (i = 0; i < table->cap; i++) {
    size_t probe = (idx + i) % table->cap;

    if (!table->used[probe]) {
      table->keys[probe] = strdup(name);
      if (!table->keys[probe]) {
        fprintf(stderr, "[dagwood] out of memory inserting task\n");
        return false;
      }
      table->values[probe] = (dagwood_task *)task;
      table->used[probe] = 1;
      table->len++;
      return true;
    }

    if (strcmp(table->keys[probe], name) == 0) {
      table->values[probe] = (dagwood_task *)task;
      return true;
    }
  }

  fprintf(stderr, "[dagwood] hash table full inserting task\n");
  return false;
}

bool t_map_exists(t_map *table, const char *name) {
  return t_map_get(table, name) != NULL;
}

size_t t_map_begin(t_map *table) {
  (void)table;
  return 0;
}

size_t t_map_end(t_map *table) {
  return table->cap;
}

dagwood_task *t_map_value(t_map *table, size_t index) {
  if (index >= table->cap) {
    return NULL;
  }

  if (!table->used[index]) {
    return NULL;
  }

  return table->values[index];
}
