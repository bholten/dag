#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data.h"

#define INITIAL_CAP 16
#define LOAD_FACTOR 75

struct ts_map {
  char **keys;
  task_state *values;
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

static int ts_map_resize(ts_map *ts, size_t new_cap);

struct ts_map *ts_map_new(void) {
  struct ts_map *ts = malloc(sizeof(*ts));
  if (!ts) {
    return NULL;
  }

  ts->keys = calloc(INITIAL_CAP, sizeof(*ts->keys));
  ts->values = calloc(INITIAL_CAP, sizeof(*ts->values));
  ts->used = calloc(INITIAL_CAP, sizeof(*ts->used));

  if (!ts->keys || !ts->values || !ts->used) {
    free(ts->keys);
    free(ts->values);
    free(ts->used);
    free(ts);
    return NULL;
  }

  ts->cap = INITIAL_CAP;
  ts->len = 0;
  return ts;
}

void ts_map_delete(struct ts_map *ts) {
  size_t i;
  if (!ts) {
    return;
  }

  for (i = 0; i < ts->cap; i++) {
    if (ts->used[i]) {
      free(ts->keys[i]);
    }
  }

  free(ts->keys);
  free(ts->values);
  free(ts->used);
  free(ts);
}

task_state ts_map_get(struct ts_map *ts, const char *name) {
  size_t h, idx, i;

  if (!name) {
    return -1;
  }

  h = hash_string(name);
  idx = h % ts->cap;

  for (i = 0; i < ts->cap; i++) {
    size_t probe = (idx + i) % ts->cap;

    if (!ts->used[probe]) {
      return -1;
    }

    if (strcmp(ts->keys[probe], name) == 0) {
      return ts->values[probe];
    }
  }

  return -1;
}

static int ts_map_resize(ts_map *ts, size_t new_cap) {
  char **old_keys = ts->keys;
  task_state *old_values = ts->values;
  unsigned char *old_used = ts->used;
  size_t old_cap = ts->cap;
  size_t i;

  ts->keys = calloc(new_cap, sizeof(*ts->keys));
  ts->values = calloc(new_cap, sizeof(*ts->values));
  ts->used = calloc(new_cap, sizeof(*ts->used));

  if (!ts->keys || !ts->values || !ts->used) {
    free(ts->keys);
    free(ts->values);
    free(ts->used);
    ts->keys = old_keys;
    ts->values = old_values;
    ts->used = old_used;
    return 0;
  }

  ts->cap = new_cap;
  ts->len = 0;

  for (i = 0; i < old_cap; i++) {
    if (old_used[i]) {
      size_t h = hash_string(old_keys[i]);
      size_t idx = h % new_cap;
      size_t j;

      for (j = 0; j < new_cap; j++) {
        size_t probe = (idx + j) % new_cap;
        if (!ts->used[probe]) {
          ts->keys[probe] = old_keys[i];
          ts->values[probe] = old_values[i];
          ts->used[probe] = 1;
          ts->len++;
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

bool ts_map_set(ts_map *ts, const char *name, const task_state state) {
  size_t h, idx, i;

  if (ts->len * 100 >= ts->cap * LOAD_FACTOR) {
    if (!ts_map_resize(ts, ts->cap * 2)) {
      fprintf(stderr,
              "[dagwood] out of memory inserting memoized task state\n");
      return false;
    }
  }

  h = hash_string(name);
  idx = h % ts->cap;

  for (i = 0; i < ts->cap; i++) {
    size_t probe = (idx + i) % ts->cap;

    if (!ts->used[probe]) {
      ts->keys[probe] = strdup(name);
      if (!ts->keys[probe]) {
        fprintf(stderr,
                "[dagwood] out of memory inserting memoized task state\n");
        return false;
      }
      ts->values[probe] = state;
      ts->used[probe] = 1;
      ts->len++;
      return true;
    }

    if (strcmp(ts->keys[probe], name) == 0) {
      ts->values[probe] = state;
      return true;
    }
  }

  fprintf(stderr, "[dagwood] hash table full inserting memoized task state\n");
  return false;
}
