#ifndef STR_MAP_H
#define STR_MAP_H

/*
 * str_map.h - Generic string-keyed hash map via macro expansion
 *
 * Generates a complete hash map implementation with string keys and a
 * configurable value type. Uses open addressing with linear probing.
 *
 * Usage:
 *   In a header:  STR_MAP_DECL(my_map, my_value_type)
 *   In a .c file: STR_MAP_IMPL(my_map, my_value_type, MISSING_VAL, "label")
 *
 * The .c file must include: <stdio.h>, <stdlib.h>, <string.h>
 * The header must include: <stdbool.h>, <stddef.h>
 */

#define STR_MAP_INITIAL_CAP 16
#define STR_MAP_LOAD_FACTOR 75

#define STR_MAP_DECL(prefix, value_type)                                       \
  typedef struct prefix prefix;                                                \
  prefix *prefix##_new(void);                                                  \
  void prefix##_delete(prefix *m);                                             \
  value_type prefix##_get(prefix *m, const char *key);                         \
  bool prefix##_set(prefix *m, const char *key, const value_type val);         \
  bool prefix##_exists(prefix *m, const char *key);                            \
  size_t prefix##_begin(prefix *m);                                            \
  size_t prefix##_end(prefix *m);                                              \
  value_type prefix##_value(prefix *m, size_t index);

#define STR_MAP_IMPL(prefix, value_type, missing_val, label)                   \
                                                                               \
  typedef struct prefix prefix;                                                \
  struct prefix {                                                              \
    char **keys;                                                               \
    value_type *values;                                                        \
    unsigned char *used;                                                       \
    size_t cap;                                                                \
    size_t len;                                                                \
  };                                                                           \
                                                                               \
  static size_t prefix##__hash(const char *s) {                                \
    size_t h = (size_t)*s;                                                     \
    if (h) {                                                                   \
      for (++s; *s; ++s) {                                                     \
        h = (h << 5) - h + (size_t)*s;                                         \
      }                                                                        \
    }                                                                          \
    return h;                                                                  \
  }                                                                            \
                                                                               \
  static int prefix##__resize(prefix *m, size_t new_cap) {                     \
    char **old_keys = m->keys;                                                 \
    value_type *old_values = m->values;                                        \
    unsigned char *old_used = m->used;                                         \
    size_t old_cap = m->cap;                                                   \
    size_t i;                                                                  \
                                                                               \
    m->keys = calloc(new_cap, sizeof(*m->keys));                               \
    m->values = calloc(new_cap, sizeof(*m->values));                           \
    m->used = calloc(new_cap, sizeof(*m->used));                               \
                                                                               \
    if (!m->keys || !m->values || !m->used) {                                  \
      free(m->keys);                                                           \
      free(m->values);                                                         \
      free(m->used);                                                           \
      m->keys = old_keys;                                                      \
      m->values = old_values;                                                  \
      m->used = old_used;                                                      \
      return 0;                                                                \
    }                                                                          \
                                                                               \
    m->cap = new_cap;                                                          \
    m->len = 0;                                                                \
                                                                               \
    for (i = 0; i < old_cap; i++) {                                            \
      if (old_used[i]) {                                                       \
        size_t h = prefix##__hash(old_keys[i]);                                \
        size_t idx = h % new_cap;                                              \
        size_t j;                                                              \
                                                                               \
        for (j = 0; j < new_cap; j++) {                                        \
          size_t probe = (idx + j) % new_cap;                                  \
          if (!m->used[probe]) {                                               \
            m->keys[probe] = old_keys[i];                                      \
            m->values[probe] = old_values[i];                                  \
            m->used[probe] = 1;                                                \
            m->len++;                                                          \
            break;                                                             \
          }                                                                    \
        }                                                                      \
      }                                                                        \
    }                                                                          \
                                                                               \
    free(old_keys);                                                            \
    free(old_values);                                                          \
    free(old_used);                                                            \
    return 1;                                                                  \
  }                                                                            \
                                                                               \
  prefix *prefix##_new(void) {                                                 \
    prefix *m = malloc(sizeof(*m));                                            \
    if (!m)                                                                    \
      return NULL;                                                             \
                                                                               \
    m->keys = calloc(STR_MAP_INITIAL_CAP, sizeof(*m->keys));                   \
    m->values = calloc(STR_MAP_INITIAL_CAP, sizeof(*m->values));               \
    m->used = calloc(STR_MAP_INITIAL_CAP, sizeof(*m->used));                   \
                                                                               \
    if (!m->keys || !m->values || !m->used) {                                  \
      free(m->keys);                                                           \
      free(m->values);                                                         \
      free(m->used);                                                           \
      free(m);                                                                 \
      return NULL;                                                             \
    }                                                                          \
                                                                               \
    m->cap = STR_MAP_INITIAL_CAP;                                              \
    m->len = 0;                                                                \
    return m;                                                                  \
  }                                                                            \
                                                                               \
  void prefix##_delete(prefix *m) {                                            \
    size_t i;                                                                  \
    if (!m)                                                                    \
      return;                                                                  \
                                                                               \
    for (i = 0; i < m->cap; i++) {                                             \
      if (m->used[i])                                                          \
        free(m->keys[i]);                                                      \
    }                                                                          \
                                                                               \
    free(m->keys);                                                             \
    free(m->values);                                                           \
    free(m->used);                                                             \
    free(m);                                                                   \
  }                                                                            \
                                                                               \
  value_type prefix##_get(prefix *m, const char *key) {                        \
    size_t h, idx, i;                                                          \
    if (!key)                                                                  \
      return (missing_val);                                                    \
                                                                               \
    h = prefix##__hash(key);                                                   \
    idx = h % m->cap;                                                          \
                                                                               \
    for (i = 0; i < m->cap; i++) {                                             \
      size_t probe = (idx + i) % m->cap;                                       \
      if (!m->used[probe])                                                     \
        return (missing_val);                                                  \
      if (strcmp(m->keys[probe], key) == 0)                                    \
        return m->values[probe];                                               \
    }                                                                          \
                                                                               \
    return (missing_val);                                                      \
  }                                                                            \
                                                                               \
  bool prefix##_set(prefix *m, const char *key, const value_type val) {        \
    size_t h, idx, i;                                                          \
                                                                               \
    if (m->len * 100 >= m->cap * STR_MAP_LOAD_FACTOR) {                        \
      if (!prefix##__resize(m, m->cap * 2)) {                                  \
        fprintf(stderr, "[dagwood] out of memory inserting " label "\n");      \
        return false;                                                          \
      }                                                                        \
    }                                                                          \
                                                                               \
    h = prefix##__hash(key);                                                   \
    idx = h % m->cap;                                                          \
                                                                               \
    for (i = 0; i < m->cap; i++) {                                             \
      size_t probe = (idx + i) % m->cap;                                       \
                                                                               \
      if (!m->used[probe]) {                                                   \
        m->keys[probe] = strdup(key);                                          \
        if (!m->keys[probe]) {                                                 \
          fprintf(stderr, "[dagwood] out of memory inserting " label "\n");    \
          return false;                                                        \
        }                                                                      \
        m->values[probe] = (value_type)(val);                                  \
        m->used[probe] = 1;                                                    \
        m->len++;                                                              \
        return true;                                                           \
      }                                                                        \
                                                                               \
      if (strcmp(m->keys[probe], key) == 0) {                                  \
        m->values[probe] = (value_type)(val);                                  \
        return true;                                                           \
      }                                                                        \
    }                                                                          \
                                                                               \
    fprintf(stderr, "[dagwood] hash table full inserting " label "\n");        \
    return false;                                                              \
  }                                                                            \
                                                                               \
  bool prefix##_exists(prefix *m, const char *key) {                           \
    size_t h, idx, i;                                                          \
    if (!key)                                                                  \
      return false;                                                            \
                                                                               \
    h = prefix##__hash(key);                                                   \
    idx = h % m->cap;                                                          \
                                                                               \
    for (i = 0; i < m->cap; i++) {                                             \
      size_t probe = (idx + i) % m->cap;                                       \
      if (!m->used[probe])                                                     \
        return false;                                                          \
      if (strcmp(m->keys[probe], key) == 0)                                    \
        return true;                                                           \
    }                                                                          \
                                                                               \
    return false;                                                              \
  }                                                                            \
                                                                               \
  size_t prefix##_begin(prefix *m) {                                           \
    (void)m;                                                                   \
    return 0;                                                                  \
  }                                                                            \
                                                                               \
  size_t prefix##_end(prefix *m) {                                             \
    return m->cap;                                                             \
  }                                                                            \
                                                                               \
  value_type prefix##_value(prefix *m, size_t index) {                         \
    if (index >= m->cap)                                                       \
      return (missing_val);                                                    \
    if (!m->used[index])                                                       \
      return (missing_val);                                                    \
    return m->values[index];                                                   \
  }

#endif /* STR_MAP_H */
