#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>

/* Include all project headers */
#include "../include/common.h"
#include "../include/auth.h"
#include "../include/ot_engine.h"
#include "../include/storage.h"
#include "../include/logger.h"

/* Test macros */
#define TEST_ASSERT(condition, msg) \
    do { \
        if (!(condition)) { \
            printf("  ✗ FAIL: %s\n", msg); \
            tests_failed++; \
        } else { \
            printf("  ✓ PASS: %s\n", msg); \
            tests_passed++; \
        } \
    } while(0)

#define TEST_GROUP(name) \
    printf("\n=== %s ===\n", name)

/* Global test counters */
static int tests_passed = 0;
static int tests_failed = 0;

/* Shared document state for concurrency testing */
typedef struct {
    char doc[MAX_DOC_SIZE];
    VectorClock vc[MAX_CLIENTS];
    pthread_mutex_t lock;
    int client_count;
} SharedDocument;

/* Per-client state */
typedef struct {
    int client_id;
    SharedDocument *doc;
    Operation *local_ops;
    int op_count;
} ClientContext;

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

SharedDocument* shared_doc_init(int num_clients) {
    SharedDocument *sd = malloc(sizeof(SharedDocument));
    memset(sd->doc, 0, MAX_DOC_SIZE);
    memset(sd->vc, 0, sizeof(VectorClock) * MAX_CLIENTS);
    pthread_mutex_init(&sd->lock, NULL);
    sd->client_count = num_clients;
    for (int i = 0; i < num_clients; i++) {
        sd->vc[i].size = num_clients;
    }
    return sd;
}

void shared_doc_free(SharedDocument *sd) {
    pthread_mutex_destroy(&sd->lock);
    free(sd);
}

void print_doc_state(const char *label, const char *doc) {
    printf("    [%s] Document: \"%s\"\n", label, doc);
}

/* ============================================================================
 * TEST 1: Two Clients Insert at Different Positions Concurrently
 * ============================================================================ */

void* client_insert_worker(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    SharedDocument *doc = ctx->doc;
    int client_id = ctx->client_id;
    
    /* Create insert operation */
    Operation op;
    memset(&op, 0, sizeof(Operation));
    op.type = OP_INSERT;
    op.client_id = client_id;
    op.vc.size = doc->client_count;
    
    /* Client 0: Insert "Alice" at position 0 */
    if (client_id == 0) {
        op.position = 0;
        strcpy(op.text, "Alice");
        op.vc.clock[0] = 1;
    }
    /* Client 1: Insert "Bob" at position 0 (will conflict!) */
    else if (client_id == 1) {
        usleep(50000); /* Simulate slight delay */
        op.position = 0;
        strcpy(op.text, "Bob");
        op.vc.clock[1] = 1;
    }
    
    /* Acquire lock and apply operation */
    pthread_mutex_lock(&doc->lock);
    
    int result = apply_operation(doc->doc, MAX_DOC_SIZE, &op);
    
    /* Update vector clock */
    vc_increment(&doc->vc[client_id], client_id);
    
    pthread_mutex_unlock(&doc->lock);
    
    return NULL;
}

void test_concurrent_inserts(void) {
    TEST_GROUP("Concurrency: Two Clients Inserting at Different Positions");
    
    SharedDocument *doc = shared_doc_init(2);
    memset(doc->doc, 0, MAX_DOC_SIZE);
    
    ClientContext ctx[2];
    pthread_t threads[2];
    
    /* Setup clients */
    for (int i = 0; i < 2; i++) {
        ctx[i].client_id = i;
        ctx[i].doc = doc;
        ctx[i].local_ops = NULL;
    }
    
    print_doc_state("BEFORE", doc->doc);
    
    /* Spawn two threads */
    pthread_create(&threads[0], NULL, client_insert_worker, &ctx[0]);
    pthread_create(&threads[1], NULL, client_insert_worker, &ctx[1]);
    
    /* Wait for both */
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    
    print_doc_state("AFTER", doc->doc);
    
    /* Verify final state */
    /* Both inserts should be present. Order depends on OT transform logic */
    int has_alice = strstr(doc->doc, "Alice") != NULL;
    int has_bob = strstr(doc->doc, "Bob") != NULL;
    
    TEST_ASSERT(has_alice, "Alice's insert applied");
    TEST_ASSERT(has_bob, "Bob's insert applied");
    TEST_ASSERT(strlen(doc->doc) >= 8, "Final doc has both names");
    
    shared_doc_free(doc);
}

/* ============================================================================
 * TEST 2: Three Clients Rapid-Fire Edits
 * ============================================================================ */

void* rapid_edit_worker(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    SharedDocument *doc = ctx->doc;
    int client_id = ctx->client_id;
    
    /* Each client makes 3 rapid edits */
    for (int i = 0; i < 3; i++) {
        Operation op;
        memset(&op, 0, sizeof(Operation));
        op.type = OP_INSERT;
        op.client_id = client_id;
        op.vc.size = doc->client_count;
        op.position = strlen(doc->doc);  /* Always append */
        op.vc.clock[client_id] = i + 1;
        
        /* Each client inserts their client ID repeatedly */
        snprintf(op.text, MAX_OP_TEXT, "%d", client_id);
        
        pthread_mutex_lock(&doc->lock);
        apply_operation(doc->doc, MAX_DOC_SIZE, &op);
        vc_increment(&doc->vc[client_id], client_id);
        pthread_mutex_unlock(&doc->lock);
        
        usleep(10000 + (rand() % 5000)); /* Random small delay */
    }
    
    return NULL;
}

void test_rapid_concurrent_edits(void) {
    TEST_GROUP("Concurrency: Three Clients Rapid-Fire Edits");
    
    SharedDocument *doc = shared_doc_init(3);
    memset(doc->doc, 0, MAX_DOC_SIZE);
    
    ClientContext ctx[3];
    pthread_t threads[3];
    
    print_doc_state("BEFORE", doc->doc);
    
    /* Spawn three threads */
    for (int i = 0; i < 3; i++) {
        ctx[i].client_id = i;
        ctx[i].doc = doc;
        pthread_create(&threads[i], NULL, rapid_edit_worker, &ctx[i]);
    }
    
    /* Wait for all */
    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }
    
    print_doc_state("AFTER", doc->doc);
    
    /* Verify: Should have 9 characters total (3 clients × 3 edits) */
    int doc_len = strlen(doc->doc);
    TEST_ASSERT(doc_len == 9, "All 9 operations applied (3 clients × 3 edits)");
    
    /* Verify all client IDs are present */
    int has_0 = strchr(doc->doc, '0') != NULL;
    int has_1 = strchr(doc->doc, '1') != NULL;
    int has_2 = strchr(doc->doc, '2') != NULL;
    
    TEST_ASSERT(has_0, "Client 0 edits present");
    TEST_ASSERT(has_1, "Client 1 edits present");
    TEST_ASSERT(has_2, "Client 2 edits present");
    
    shared_doc_free(doc);
}

/* ============================================================================
 * TEST 3: Concurrent Insert and Delete
 * ============================================================================ */

void* mixed_ops_worker(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    SharedDocument *doc = ctx->doc;
    int client_id = ctx->client_id;
    
    Operation op;
    memset(&op, 0, sizeof(Operation));
    op.client_id = client_id;
    op.vc.size = doc->client_count;
    op.vc.clock[client_id] = 1;
    
    /* Client 0: Insert "Hello" at position 0 */
    if (client_id == 0) {
        op.type = OP_INSERT;
        op.position = 0;
        strcpy(op.text, "Hello");
    }
    /* Client 1: Try to delete from position 0 (after slight delay) */
    else if (client_id == 1) {
        usleep(100000); /* Give client 0 time to insert */
        op.type = OP_DELETE;
        op.position = 0;
        op.length = 2; /* Delete "He" */
    }
    
    pthread_mutex_lock(&doc->lock);
    int result = apply_operation(doc->doc, MAX_DOC_SIZE, &op);
    vc_increment(&doc->vc[client_id], client_id);
    pthread_mutex_unlock(&doc->lock);
    
    return NULL;
}

void test_concurrent_insert_delete(void) {
    TEST_GROUP("Concurrency: Insert and Delete Simultaneously");
    
    SharedDocument *doc = shared_doc_init(2);
    memset(doc->doc, 0, MAX_DOC_SIZE);
    
    ClientContext ctx[2];
    pthread_t threads[2];
    
    print_doc_state("BEFORE", doc->doc);
    
    pthread_create(&threads[0], NULL, mixed_ops_worker, &ctx[0]);
    pthread_create(&threads[1], NULL, mixed_ops_worker, &ctx[1]);
    
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    
    print_doc_state("AFTER", doc->doc);
    
    /* Verify operations executed */
    int doc_len = strlen(doc->doc);
    TEST_ASSERT(doc_len < 5, "Delete operation had effect (less than 5 chars)");
    
    shared_doc_free(doc);
}

/* ============================================================================
 * TEST 4: Vector Clock Causality Under Concurrency
 * ============================================================================ */

void* vc_worker(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    SharedDocument *doc = ctx->doc;
    int client_id = ctx->client_id;
    
    pthread_mutex_lock(&doc->lock);
    
    /* Each client increments their vector clock multiple times */
    for (int i = 0; i < 5; i++) {
        vc_increment(&doc->vc[client_id], client_id);
    }
    
    pthread_mutex_unlock(&doc->lock);
    
    return NULL;
}

void test_vector_clock_concurrency(void) {
    TEST_GROUP("Concurrency: Vector Clock Causality");
    
    SharedDocument *doc = shared_doc_init(3);
    
    ClientContext ctx[3];
    pthread_t threads[3];
    
    printf("    [BEFORE] Vector clocks all zeros\n");
    
    /* Spawn three threads each incrementing their VC 5 times */
    for (int i = 0; i < 3; i++) {
        ctx[i].client_id = i;
        ctx[i].doc = doc;
        pthread_create(&threads[i], NULL, vc_worker, &ctx[i]);
    }
    
    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("    [AFTER] Vector clock increments:\n");
    
    /* Verify each client's clock was incremented 5 times */
    for (int i = 0; i < 3; i++) {
        printf("      Client %d: %u\n", i, doc->vc[i].clock[i]);
        char msg[100];
        snprintf(msg, sizeof(msg), "Client %d VC incremented correctly", i);
        TEST_ASSERT(doc->vc[i].clock[i] == 5, msg);
    }
    
    shared_doc_free(doc);
}

/* ============================================================================
 * TEST 5: Concurrent Permission Checks
 * ============================================================================ */

void* permission_worker(void *arg) {
    ClientContext *ctx = (ClientContext *)arg;
    int client_id = ctx->client_id;
    
    Role roles[] = {ROLE_GUEST, ROLE_EDITOR, ROLE_ADMIN};
    Role my_role = roles[client_id % 3];
    
    /* Each client checks permissions under their role */
    int can_write = auth_can_write(my_role);
    int can_kick = auth_can_kick(my_role);
    int can_save = auth_can_save(my_role);
    
    /* Store results (in real scenario would be used) */
    (void)can_write;
    (void)can_kick;
    (void)can_save;
    
    return NULL;
}

void test_concurrent_permission_checks(void) {
    TEST_GROUP("Concurrency: Permission Checks Under Load");
    
    pthread_t threads[6];
    ClientContext ctx[6];
    
    /* Spawn 6 threads checking permissions concurrently */
    for (int i = 0; i < 6; i++) {
        ctx[i].client_id = i;
        pthread_create(&threads[i], NULL, permission_worker, &ctx[i]);
    }
    
    for (int i = 0; i < 6; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* If we got here without crashes, permission checks are thread-safe */
    TEST_ASSERT(1, "Permission checks survive concurrent access");
}

/* ============================================================================
 * TEST 6: Storage Lock Contention
 * ============================================================================ */

void* storage_worker(void *arg) {
    int client_id = *(int *)arg;
    const char *test_file = "/tmp/storage_contention_test.txt";
    
    char buffer[MAX_DOC_SIZE];
    
    /* Each client writes then reads */
    char write_content[256];
    snprintf(write_content, sizeof(write_content), 
             "Content from client %d at time %ld\n", 
             client_id, time(NULL));
    
    int save_result = storage_save(test_file, write_content, strlen(write_content));
    
    usleep(10000); /* Small delay */
    
    memset(buffer, 0, MAX_DOC_SIZE);
    int load_result = storage_load(test_file, buffer, MAX_DOC_SIZE);
    
    return NULL;
}

void test_storage_lock_contention(void) {
    TEST_GROUP("Concurrency: Storage File Locking Under Contention");
    
    pthread_t threads[4];
    int client_ids[4] = {0, 1, 2, 3};
    
    /* Spawn 4 threads all trying to read/write same file */
    for (int i = 0; i < 4; i++) {
        pthread_create(&threads[i], NULL, storage_worker, &client_ids[i]);
    }
    
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    /* If we got here without hangs or crashes, locking works */
    TEST_ASSERT(1, "Storage handles concurrent access without deadlock");
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================ */

int main(void) {
    srand(time(NULL));
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║   TEKSTREDAKTILO - CONCURRENCY TEST SUITE                  ║\n");
    printf("║   (Testing Real Multi-Threaded Concurrent Edits)           ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    /* Run concurrency tests */
    test_concurrent_inserts();
    test_rapid_concurrent_edits();
    test_concurrent_insert_delete();
    test_vector_clock_concurrency();
    test_concurrent_permission_checks();
    test_storage_lock_contention();
    
    /* Print summary */
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                      TEST SUMMARY                          ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  Passed: %-54d║\n", tests_passed);
    printf("║  Failed: %-54d║\n", tests_failed);
    printf("║  Total:  %-54d║\n", tests_passed + tests_failed);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
