#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data.h"

#define INITIAL_CAP 16
#define LOAD_FACTOR 75

struct p_map {
  char **keys;
  dagwood_project **values;
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

static int p_map_resize(p_map *p, size_t new_cap);

p_map *p_map_new(void) {
  struct p_map *p = malloc(sizeof(*p));
  if (!p) {
    return NULL;
  }

  p->keys = calloc(INITIAL_CAP, sizeof(*p->keys));
  p->values = calloc(INITIAL_CAP, sizeof(*p->values));
  p->used = calloc(INITIAL_CAP, sizeof(*p->used));

  if (!p->keys || !p->values || !p->used) {
    free(p->keys);
    free(p->values);
    free(p->used);
    free(p);
    return NULL;
  }

  p->cap = INITIAL_CAP;
  p->len = 0;
  return p;
}

void p_map_delete(p_map *p) {
  size_t i;
  if (!p) {
    return;
  }

  for (i = 0; i < p->cap; i++) {
    if (p->used[i]) {
      free(p->keys[i]);
    }
  }

  free(p->keys);
  free(p->values);
  free(p->used);
  free(p);
}

dagwood_project *p_map_get(p_map *p, const char *name) {
  size_t h, idx, i;

  if (!name) {
    return NULL;
  }

  h = hash_string(name);
  idx = h % p->cap;

  for (i = 0; i < p->cap; i++) {
    size_t probe = (idx + i) % p->cap;

    if (!p->used[probe]) {
      return NULL;
    }

    if (strcmp(p->keys[probe], name) == 0) {
      return p->values[probe];
    }
  }

  return NULL;
}

static int p_map_resize(p_map *p, size_t new_cap) {
  char **old_keys = p->keys;
  dagwood_project **old_values = p->values;
  unsigned char *old_used = p->used;
  size_t old_cap = p->cap;
  size_t i;

  p->keys = calloc(new_cap, sizeof(*p->keys));
  p->values = calloc(new_cap, sizeof(*p->values));
  p->used = calloc(new_cap, sizeof(*p->used));

  if (!p->keys || !p->values || !p->used) {
    free(p->keys);
    free(p->values);
    free(p->used);
    p->keys = old_keys;
    p->values = old_values;
    p->used = old_used;
    return 0;
  }

  p->cap = new_cap;
  p->len = 0;

  for (i = 0; i < old_cap; i++) {
    if (old_used[i]) {
      size_t h = hash_string(old_keys[i]);
      size_t idx = h % new_cap;
      size_t j;

      for (j = 0; j < new_cap; j++) {
        size_t probe = (idx + j) % new_cap;
        if (!p->used[probe]) {
          p->keys[probe] = old_keys[i];
          p->values[probe] = old_values[i];
          p->used[probe] = 1;
          p->len++;
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

bool p_map_set(p_map *p, const char *name, const dagwood_project *project) {
  size_t h, idx, i;

  assert(project != NULL);

  if (p->len * 100 >= p->cap * LOAD_FACTOR) {
    if (!p_map_resize(p, p->cap * 2)) {
      fprintf(stderr, "[dagwood] out of memory inserting project\n");
      return false;
    }
  }

  h = hash_string(name);
  idx = h % p->cap;

  for (i = 0; i < p->cap; i++) {
    size_t probe = (idx + i) % p->cap;

    if (!p->used[probe]) {
      p->keys[probe] = strdup(name);
      if (!p->keys[probe]) {
        fprintf(stderr, "[dagwood] out of memory inserting project\n");
        return false;
      }
      p->values[probe] = (dagwood_project *)project;
      p->used[probe] = 1;
      p->len++;
      return true;
    }

    if (strcmp(p->keys[probe], name) == 0) {
      p->values[probe] = (dagwood_project *)project;
      return true;
    }
  }

  fprintf(stderr, "[dagwood] hash table full inserting project\n");
  return false;
}

size_t p_map_begin(p_map *p) {
  (void)p;
  return 0;
}

size_t p_map_end(p_map *p) {
  return p->cap;
}

dagwood_project *p_map_value(p_map *p, size_t index) {
  if (index >= p->cap) {
    return NULL;
  }
  if (!p->used[index]) {
    return NULL;
  }
  return p->values[index];
}
