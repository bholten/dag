/**
 * Unit tests for Dagwood's C89 data structures
 *
 * Tests:
 *   - s_arr: string array
 *   - t_arr: task pointer array
 *   - t_map: string -> task* hash map
 *   - ts_map: string -> task_state hash map
 *   - p_map: string -> project* hash map
 *   - t_layers: array of t_arr*
 *
 * Build and run:
 *   cmake --build build --target test_data_structures
 *   ./build/test_data_structures
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/data.h"
#include "../src/dagwood.h"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;

/* Test macros */
#define TEST(name) static int test_##name(void)

#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  %-50s ", #name); \
    if (test_##name()) { \
        tests_passed++; \
        printf("PASS\n"); \
    } else { \
        printf("FAIL\n"); \
    } \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("\n    ASSERT failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("\n    ASSERT_EQ failed: %s != %s (%s:%d)\n", #a, #b, __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("\n    ASSERT_STR_EQ failed: \"%s\" != \"%s\" (%s:%d)\n", (a), (b), __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_NULL(a) do { \
    if ((a) != NULL) { \
        printf("\n    ASSERT_NULL failed: %s is not NULL (%s:%d)\n", #a, __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_NOT_NULL(a) do { \
    if ((a) == NULL) { \
        printf("\n    ASSERT_NOT_NULL failed: %s is NULL (%s:%d)\n", #a, __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

/* ============================================================================
 * s_arr tests (string array)
 * ============================================================================ */

TEST(s_arr_new_and_delete) {
    s_arr *arr = s_arr_new();
    ASSERT_NOT_NULL(arr);
    ASSERT_EQ(s_arr_len(arr), 0);
    s_arr_delete(arr);
    return 1;
}

TEST(s_arr_push_and_get) {
    s_arr *arr = s_arr_new();
    s_arr_push(arr, "hello");
    s_arr_push(arr, "world");
    s_arr_push(arr, "test");

    ASSERT_EQ(s_arr_len(arr), 3);
    ASSERT_STR_EQ(s_arr_get(arr, 0), "hello");
    ASSERT_STR_EQ(s_arr_get(arr, 1), "world");
    ASSERT_STR_EQ(s_arr_get(arr, 2), "test");

    s_arr_delete(arr);
    return 1;
}

TEST(s_arr_pop) {
    s_arr *arr = s_arr_new();
    s_arr_push(arr, "first");
    s_arr_push(arr, "second");
    s_arr_push(arr, "third");

    const char *popped = s_arr_pop(arr);
    ASSERT_STR_EQ(popped, "third");
    ASSERT_EQ(s_arr_len(arr), 2);

    popped = s_arr_pop(arr);
    ASSERT_STR_EQ(popped, "second");
    ASSERT_EQ(s_arr_len(arr), 1);

    s_arr_delete(arr);
    return 1;
}

TEST(s_arr_grow_beyond_initial_capacity) {
    s_arr *arr = s_arr_new();

    /* s_arr stores pointers, not copies - allocate strings on heap */
    char *strings[100];
    for (int i = 0; i < 100; i++) {
        strings[i] = malloc(16);
        snprintf(strings[i], 16, "item_%d", i);
        s_arr_push(arr, strings[i]);
    }

    ASSERT_EQ(s_arr_len(arr), 100);

    /* Verify all items are correct */
    for (int i = 0; i < 100; i++) {
        char expected[16];
        snprintf(expected, sizeof(expected), "item_%d", i);
        ASSERT_STR_EQ(s_arr_get(arr, i), expected);
    }

    s_arr_delete(arr);

    /* Free our allocated strings */
    for (int i = 0; i < 100; i++) {
        free(strings[i]);
    }

    return 1;
}

TEST(s_arr_copy) {
    s_arr *arr = s_arr_new();
    s_arr_push(arr, "alpha");
    s_arr_push(arr, "beta");

    s_arr *copy = s_arr_copy(arr);
    ASSERT_NOT_NULL(copy);
    ASSERT_EQ(s_arr_len(copy), 2);
    ASSERT_STR_EQ(s_arr_get(copy, 0), "alpha");
    ASSERT_STR_EQ(s_arr_get(copy, 1), "beta");

    /* Modify original, copy should be independent */
    s_arr_push(arr, "gamma");
    ASSERT_EQ(s_arr_len(arr), 3);
    ASSERT_EQ(s_arr_len(copy), 2);

    s_arr_delete(arr);
    s_arr_delete(copy);
    return 1;
}

/* ============================================================================
 * t_arr tests (task pointer array)
 * ============================================================================ */

TEST(t_arr_new_and_delete) {
    t_arr *arr = t_arr_new();
    ASSERT_NOT_NULL(arr);
    ASSERT_EQ(t_arr_len(arr), 0);
    t_arr_delete(arr);
    return 1;
}

TEST(t_arr_push_and_get) {
    t_arr *arr = t_arr_new();
    dagwood_task t1, t2, t3;

    t_arr_push(arr, &t1);
    t_arr_push(arr, &t2);
    t_arr_push(arr, &t3);

    ASSERT_EQ(t_arr_len(arr), 3);
    ASSERT_EQ(t_arr_get(arr, 0), &t1);
    ASSERT_EQ(t_arr_get(arr, 1), &t2);
    ASSERT_EQ(t_arr_get(arr, 2), &t3);

    t_arr_delete(arr);
    return 1;
}

TEST(t_arr_pop) {
    t_arr *arr = t_arr_new();
    dagwood_task t1, t2;

    t_arr_push(arr, &t1);
    t_arr_push(arr, &t2);

    dagwood_task *popped = t_arr_pop(arr);
    ASSERT_EQ(popped, &t2);
    ASSERT_EQ(t_arr_len(arr), 1);

    t_arr_delete(arr);
    return 1;
}

TEST(t_arr_bounds_check) {
    t_arr *arr = t_arr_new();
    dagwood_task t1;
    t_arr_push(arr, &t1);

    /* Out of bounds should return NULL */
    ASSERT_NULL(t_arr_get(arr, 1));
    ASSERT_NULL(t_arr_get(arr, 100));

    t_arr_delete(arr);
    return 1;
}

/* ============================================================================
 * t_map tests (string -> task* hash map)
 * ============================================================================ */

TEST(t_map_new_and_delete) {
    t_map *map = t_map_new();
    ASSERT_NOT_NULL(map);
    t_map_delete(map);
    return 1;
}

TEST(t_map_set_and_get) {
    t_map *map = t_map_new();
    dagwood_task t1, t2, t3;

    ASSERT(t_map_set(map, "task1", &t1));
    ASSERT(t_map_set(map, "task2", &t2));
    ASSERT(t_map_set(map, "task3", &t3));

    ASSERT_EQ(t_map_get(map, "task1"), &t1);
    ASSERT_EQ(t_map_get(map, "task2"), &t2);
    ASSERT_EQ(t_map_get(map, "task3"), &t3);

    t_map_delete(map);
    return 1;
}

TEST(t_map_get_nonexistent) {
    t_map *map = t_map_new();
    dagwood_task t1;
    t_map_set(map, "exists", &t1);

    ASSERT_NULL(t_map_get(map, "nonexistent"));
    ASSERT_NULL(t_map_get(map, ""));
    ASSERT_NULL(t_map_get(map, NULL));

    t_map_delete(map);
    return 1;
}

TEST(t_map_exists) {
    t_map *map = t_map_new();
    dagwood_task t1;
    t_map_set(map, "exists", &t1);

    ASSERT(t_map_exists(map, "exists"));
    ASSERT(!t_map_exists(map, "not_exists"));

    t_map_delete(map);
    return 1;
}

TEST(t_map_overwrite) {
    t_map *map = t_map_new();
    dagwood_task t1, t2;

    t_map_set(map, "key", &t1);
    ASSERT_EQ(t_map_get(map, "key"), &t1);

    t_map_set(map, "key", &t2);
    ASSERT_EQ(t_map_get(map, "key"), &t2);

    t_map_delete(map);
    return 1;
}

TEST(t_map_many_entries) {
    t_map *map = t_map_new();
    dagwood_task tasks[100];

    /* Insert 100 entries to test resize */
    for (int i = 0; i < 100; i++) {
        char key[32];
        snprintf(key, sizeof(key), "task_%d", i);
        ASSERT(t_map_set(map, key, &tasks[i]));
    }

    /* Verify all entries */
    for (int i = 0; i < 100; i++) {
        char key[32];
        snprintf(key, sizeof(key), "task_%d", i);
        ASSERT_EQ(t_map_get(map, key), &tasks[i]);
    }

    t_map_delete(map);
    return 1;
}

TEST(t_map_iteration) {
    t_map *map = t_map_new();
    dagwood_task t1, t2, t3;

    t_map_set(map, "a", &t1);
    t_map_set(map, "b", &t2);
    t_map_set(map, "c", &t3);

    int count = 0;
    for (size_t i = t_map_begin(map); i < t_map_end(map); i++) {
        dagwood_task *t = t_map_value(map, i);
        if (t != NULL) count++;
    }

    ASSERT_EQ(count, 3);

    t_map_delete(map);
    return 1;
}

/* ============================================================================
 * ts_map tests (string -> task_state hash map)
 * ============================================================================ */

TEST(ts_map_new_and_delete) {
    ts_map *map = ts_map_new();
    ASSERT_NOT_NULL(map);
    ts_map_delete(map);
    return 1;
}

TEST(ts_map_set_and_get) {
    ts_map *map = ts_map_new();

    ts_map_set(map, "task1", TASK_CLEAN);
    ts_map_set(map, "task2", TASK_STALE);

    ASSERT_EQ(ts_map_get(map, "task1"), TASK_CLEAN);
    ASSERT_EQ(ts_map_get(map, "task2"), TASK_STALE);

    /* Non-existent keys return -1 (cast to task_state) */
    ASSERT_EQ(ts_map_get(map, "nonexistent"), (task_state)-1);

    ts_map_delete(map);
    return 1;
}

TEST(ts_map_overwrite) {
    ts_map *map = ts_map_new();

    ts_map_set(map, "key", TASK_CLEAN);
    ASSERT_EQ(ts_map_get(map, "key"), TASK_CLEAN);

    ts_map_set(map, "key", TASK_STALE);
    ASSERT_EQ(ts_map_get(map, "key"), TASK_STALE);

    ts_map_delete(map);
    return 1;
}

/* ============================================================================
 * p_map tests (string -> project* hash map)
 * ============================================================================ */

TEST(p_map_new_and_delete) {
    p_map *map = p_map_new();
    ASSERT_NOT_NULL(map);
    p_map_delete(map);
    return 1;
}

TEST(p_map_set_and_get) {
    p_map *map = p_map_new();
    dagwood_project p1, p2;

    ASSERT(p_map_set(map, "proj1", &p1));
    ASSERT(p_map_set(map, "proj2", &p2));

    ASSERT_EQ(p_map_get(map, "proj1"), &p1);
    ASSERT_EQ(p_map_get(map, "proj2"), &p2);
    ASSERT_NULL(p_map_get(map, "nonexistent"));

    p_map_delete(map);
    return 1;
}

TEST(p_map_iteration) {
    p_map *map = p_map_new();
    dagwood_project p1, p2;

    p_map_set(map, "a", &p1);
    p_map_set(map, "b", &p2);

    int count = 0;
    for (size_t i = p_map_begin(map); i < p_map_end(map); i++) {
        dagwood_project *p = p_map_value(map, i);
        if (p != NULL) count++;
    }

    ASSERT_EQ(count, 2);

    p_map_delete(map);
    return 1;
}

/* ============================================================================
 * t_layers tests (array of t_arr*)
 * ============================================================================ */

TEST(t_layers_new_and_delete) {
    t_layers *layers = t_layers_new();
    ASSERT_NOT_NULL(layers);
    ASSERT_EQ(t_layers_len(layers), 0);
    t_layers_delete(layers);
    return 1;
}

TEST(t_layers_push_and_get) {
    t_layers *layers = t_layers_new();

    t_arr *layer1 = t_arr_new();
    t_arr *layer2 = t_arr_new();

    t_layers_push(layers, layer1);
    t_layers_push(layers, layer2);

    ASSERT_EQ(t_layers_len(layers), 2);
    ASSERT_EQ(t_layers_get(layers, 0), layer1);
    ASSERT_EQ(t_layers_get(layers, 1), layer2);

    /* t_layers_delete frees the contained t_arrs */
    t_layers_delete(layers);
    return 1;
}

/* ============================================================================
 * Push return value tests
 * ============================================================================ */

TEST(s_arr_push_returns_true) {
    s_arr *arr = s_arr_new();
    ASSERT(s_arr_push(arr, "hello"));
    ASSERT(s_arr_push(arr, "world"));
    ASSERT_EQ(s_arr_len(arr), 2);
    s_arr_delete(arr);
    return 1;
}

TEST(t_arr_push_returns_true) {
    t_arr *arr = t_arr_new();
    dagwood_task t1;
    ASSERT(t_arr_push(arr, &t1));
    ASSERT_EQ(t_arr_len(arr), 1);
    t_arr_delete(arr);
    return 1;
}

TEST(t_layers_push_returns_true) {
    t_layers *layers = t_layers_new();
    t_arr *layer1 = t_arr_new();
    ASSERT(t_layers_push(layers, layer1));
    ASSERT_EQ(t_layers_len(layers), 1);
    t_layers_delete(layers);
    return 1;
}

TEST(s_arr_push_10000_items) {
    s_arr *arr = s_arr_new();
    char *strings[10000];

    for (int i = 0; i < 10000; i++) {
        strings[i] = malloc(16);
        snprintf(strings[i], 16, "s_%d", i);
        ASSERT(s_arr_push(arr, strings[i]));
    }

    ASSERT_EQ(s_arr_len(arr), 10000);
    ASSERT_STR_EQ(s_arr_get(arr, 0), "s_0");
    ASSERT_STR_EQ(s_arr_get(arr, 9999), "s_9999");

    s_arr_delete(arr);
    for (int i = 0; i < 10000; i++) {
        free(strings[i]);
    }
    return 1;
}

/* ============================================================================
 * Task memory tests
 * ============================================================================ */

TEST(task_create_and_delete) {
    dagwood_task *t = dagwood_task_new();
    ASSERT_NOT_NULL(t);

    /* Verify defaults */
    ASSERT_NULL(t->id);
    ASSERT_NULL(t->name);
    ASSERT_NULL(t->description);
    ASSERT_NOT_NULL(t->run);
    ASSERT_STR_EQ(t->run, "exit 0");

    dagwood_task_delete(t);
    return 1;
}

TEST(task_memory_with_strdup_fields) {
    dagwood_task *t = dagwood_task_new();
    ASSERT_NOT_NULL(t);

    /* Set strdup'd fields like lcl_interp.c does */
    free((void *)t->id);
    t->id = strdup("myproject::build");
    free((void *)t->name);
    t->name = strdup("build");
    free((void *)t->description);
    t->description = strdup("Build the project");
    free((void *)t->run);
    t->run = strdup("make -j4");

    /* Add some string array contents */
    s_arr_push(t->inputs, strdup("src/main.c"));
    s_arr_push(t->inputs, strdup("src/util.c"));
    s_arr_push(t->outputs, strdup("build/app"));
    s_arr_push(t->depends_on, strdup("myproject::prepare"));

    /* Delete should free everything without leaks (verified by ASan) */
    dagwood_task_delete(t);
    return 1;
}

TEST(s_arr_delete_contents_frees_strings) {
    s_arr *arr = s_arr_new();
    s_arr_push(arr, strdup("hello"));
    s_arr_push(arr, strdup("world"));
    s_arr_push(arr, strdup("test"));

    ASSERT_EQ(s_arr_len(arr), 3);

    /* s_arr_delete_contents should free each string + the array (verified by ASan) */
    s_arr_delete_contents(arr);
    return 1;
}

/* ============================================================================
 * dag_build tests
 * ============================================================================ */

TEST(dag_build_twice_same_result) {
    /* Verify dag_build doesn't destructively mutate in_degree */
    dagwood_task t1 = {0};
    dagwood_task t2 = {0};
    t1.id = "t1";
    t1.name = "t1";
    t1.run = "echo t1";
    t1.edges = t_arr_new();
    t1.reverse_edges = t_arr_new();
    t1.depends_on = s_arr_new();
    t1.inputs = s_arr_new();
    t1.outputs = s_arr_new();

    t2.id = "t2";
    t2.name = "t2";
    t2.run = "echo t2";
    t2.edges = t_arr_new();
    t2.reverse_edges = t_arr_new();
    t2.depends_on = s_arr_new();
    t2.inputs = s_arr_new();
    t2.outputs = s_arr_new();

    /* t2 depends on t1 */
    t_arr_push(t2.edges, &t1);
    t_arr_push(t1.reverse_edges, &t2);
    t2.in_degree = 1;
    t1.in_degree = 0;

    t_arr *arr = t_arr_new();
    t_arr_push(arr, &t1);
    t_arr_push(arr, &t2);

    /* First build */
    t_layers *layers1 = t_layers_new();
    ASSERT(dag_build(arr, layers1));
    ASSERT_EQ(t_layers_len(layers1), 2);

    /* Second build on same graph - should produce same result */
    t_layers *layers2 = t_layers_new();
    ASSERT(dag_build(arr, layers2));
    ASSERT_EQ(t_layers_len(layers2), 2);

    t_layers_delete(layers1);
    t_layers_delete(layers2);
    t_arr_delete(arr);

    /* Clean up task internals (don't use dagwood_task_delete since fields aren't heap-allocated) */
    t_arr_delete(t1.edges);
    t_arr_delete(t1.reverse_edges);
    s_arr_delete(t1.depends_on);
    s_arr_delete(t1.inputs);
    s_arr_delete(t1.outputs);
    t_arr_delete(t2.edges);
    t_arr_delete(t2.reverse_edges);
    s_arr_delete(t2.depends_on);
    s_arr_delete(t2.inputs);
    s_arr_delete(t2.outputs);

    return 1;
}

TEST(dag_build_detects_cycle) {
    dagwood_task t1 = {0};
    dagwood_task t2 = {0};
    t1.id = "t1";
    t1.name = "t1";
    t1.run = "echo t1";
    t1.edges = t_arr_new();
    t1.reverse_edges = t_arr_new();
    t1.depends_on = s_arr_new();
    t1.inputs = s_arr_new();
    t1.outputs = s_arr_new();

    t2.id = "t2";
    t2.name = "t2";
    t2.run = "echo t2";
    t2.edges = t_arr_new();
    t2.reverse_edges = t_arr_new();
    t2.depends_on = s_arr_new();
    t2.inputs = s_arr_new();
    t2.outputs = s_arr_new();

    /* Create cycle: t1 -> t2 -> t1 */
    t_arr_push(t1.edges, &t2);
    t_arr_push(t2.reverse_edges, &t1);
    t_arr_push(t2.edges, &t1);
    t_arr_push(t1.reverse_edges, &t2);
    t1.in_degree = 1;
    t2.in_degree = 1;

    t_arr *arr = t_arr_new();
    t_arr_push(arr, &t1);
    t_arr_push(arr, &t2);

    t_layers *layers = t_layers_new();
    ASSERT(!dag_build(arr, layers));

    t_layers_delete(layers);
    t_arr_delete(arr);
    t_arr_delete(t1.edges);
    t_arr_delete(t1.reverse_edges);
    s_arr_delete(t1.depends_on);
    s_arr_delete(t1.inputs);
    s_arr_delete(t1.outputs);
    t_arr_delete(t2.edges);
    t_arr_delete(t2.reverse_edges);
    s_arr_delete(t2.depends_on);
    s_arr_delete(t2.inputs);
    s_arr_delete(t2.outputs);

    return 1;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Dagwood Data Structure Unit Tests\n");
    printf("==================================\n\n");

    printf("s_arr (string array):\n");
    RUN_TEST(s_arr_new_and_delete);
    RUN_TEST(s_arr_push_and_get);
    RUN_TEST(s_arr_pop);
    RUN_TEST(s_arr_grow_beyond_initial_capacity);
    RUN_TEST(s_arr_copy);

    printf("\nt_arr (task pointer array):\n");
    RUN_TEST(t_arr_new_and_delete);
    RUN_TEST(t_arr_push_and_get);
    RUN_TEST(t_arr_pop);
    RUN_TEST(t_arr_bounds_check);

    printf("\nt_map (string -> task* hash map):\n");
    RUN_TEST(t_map_new_and_delete);
    RUN_TEST(t_map_set_and_get);
    RUN_TEST(t_map_get_nonexistent);
    RUN_TEST(t_map_exists);
    RUN_TEST(t_map_overwrite);
    RUN_TEST(t_map_many_entries);
    RUN_TEST(t_map_iteration);

    printf("\nts_map (string -> task_state hash map):\n");
    RUN_TEST(ts_map_new_and_delete);
    RUN_TEST(ts_map_set_and_get);
    RUN_TEST(ts_map_overwrite);

    printf("\np_map (string -> project* hash map):\n");
    RUN_TEST(p_map_new_and_delete);
    RUN_TEST(p_map_set_and_get);
    RUN_TEST(p_map_iteration);

    printf("\nt_layers (array of t_arr*):\n");
    RUN_TEST(t_layers_new_and_delete);
    RUN_TEST(t_layers_push_and_get);

    printf("\nPush return values:\n");
    RUN_TEST(s_arr_push_returns_true);
    RUN_TEST(t_arr_push_returns_true);
    RUN_TEST(t_layers_push_returns_true);
    RUN_TEST(s_arr_push_10000_items);

    printf("\nTask memory:\n");
    RUN_TEST(task_create_and_delete);
    RUN_TEST(task_memory_with_strdup_fields);
    RUN_TEST(s_arr_delete_contents_frees_strings);

    printf("\nDAG build:\n");
    RUN_TEST(dag_build_twice_same_result);
    RUN_TEST(dag_build_detects_cycle);

    printf("\n==================================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
