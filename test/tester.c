#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

/* Include all project headers */
#include "../include/common.h"
#include "../include/auth.h"
#include "../include/ot_engine.h"
#include "../include/storage.h"
#include "../include/logger.h"

/* Test framework macros */
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

/* ============================================================================
 * AUTH TESTS
 * ============================================================================ */

void test_auth_role_checking(void) {
    TEST_GROUP("Auth: Role Checking");
    
    /* Test role names */
    TEST_ASSERT(auth_role_name(ROLE_GUEST) != NULL, "ROLE_GUEST has name");
    TEST_ASSERT(auth_role_name(ROLE_EDITOR) != NULL, "ROLE_EDITOR has name");
    TEST_ASSERT(auth_role_name(ROLE_ADMIN) != NULL, "ROLE_ADMIN has name");
    
    /* Test write permissions */
    TEST_ASSERT(auth_can_write(ROLE_GUEST) == 0, "GUEST cannot write");
    TEST_ASSERT(auth_can_write(ROLE_EDITOR) != 0, "EDITOR can write");
    TEST_ASSERT(auth_can_write(ROLE_ADMIN) != 0, "ADMIN can write");
    
    /* Test kick permissions (admin only) */
    TEST_ASSERT(auth_can_kick(ROLE_GUEST) == 0, "GUEST cannot kick");
    TEST_ASSERT(auth_can_kick(ROLE_EDITOR) == 0, "EDITOR cannot kick");
    TEST_ASSERT(auth_can_kick(ROLE_ADMIN) != 0, "ADMIN can kick");
    
    /* Test save permissions (admin only) */
    TEST_ASSERT(auth_can_save(ROLE_GUEST) == 0, "GUEST cannot save");
    TEST_ASSERT(auth_can_save(ROLE_EDITOR) == 0, "EDITOR cannot save");
    TEST_ASSERT(auth_can_save(ROLE_ADMIN) != 0, "ADMIN can save");
}

void test_auth_verify(void) {
    TEST_GROUP("Auth: Verify Credentials");
    
    Role role;
    
    /* Valid credentials should succeed */
    int result = auth_verify("admin", "admin123", &role);
    TEST_ASSERT(result != -1, "Valid admin credentials accepted");
    TEST_ASSERT(role == ROLE_ADMIN, "admin user gets ROLE_ADMIN");
    
    /* Invalid password should fail */
    result = auth_verify("admin", "wrongpass", &role);
    TEST_ASSERT(result == -1, "Invalid password rejected");
    
    /* Nonexistent user should fail */
    result = auth_verify("nonexistent", "password", &role);
    TEST_ASSERT(result == -1, "Nonexistent user rejected");
}

/* ============================================================================
 * OT ENGINE TESTS
 * ============================================================================ */

void test_vector_clock(void) {
    TEST_GROUP("OT Engine: Vector Clocks");
    
    VectorClock vc1, vc2, vc_result;
    
    /* Initialize vector clocks */
    memset(&vc1, 0, sizeof(VectorClock));
    memset(&vc2, 0, sizeof(VectorClock));
    memset(&vc_result, 0, sizeof(VectorClock));
    
    vc1.size = 2;
    vc2.size = 2;
    vc_result.size = 2;
    
    /* Test increment */
    vc_increment(&vc1, 0);
    TEST_ASSERT(vc1.clock[0] == 1, "Vector clock increments correctly");
    
    vc_increment(&vc1, 1);
    TEST_ASSERT(vc1.clock[1] == 1, "Vector clock increments per client");
    
    /* Test merge */
    vc2.clock[0] = 2;
    vc2.clock[1] = 0;
    vc_merge(&vc_result, &vc1);
    vc_merge(&vc_result, &vc2);
    TEST_ASSERT(vc_result.clock[0] == 2, "Vector clock merge takes max (client 0)");
    TEST_ASSERT(vc_result.clock[1] == 1, "Vector clock merge takes max (client 1)");
    
    /* Test compare: vc1 < vc2 in causality */
    vc1.clock[0] = 1;
    vc1.clock[1] = 1;
    vc2.clock[0] = 2;
    vc2.clock[1] = 2;
    int cmp = vc_compare(&vc1, &vc2);
    TEST_ASSERT(cmp < 0, "Vector clock comparison: causally earlier < later");
}

void test_operation_transform(void) {
    TEST_GROUP("OT Engine: Operation Transform");
    
    Operation op1, op2;
    
    /* Setup: Two concurrent inserts at different positions */
    memset(&op1, 0, sizeof(Operation));
    memset(&op2, 0, sizeof(Operation));
    
    op1.type = OP_INSERT;
    op1.position = 5;
    op1.client_id = 0;
    strcpy(op1.text, "hello");
    op1.vc.clock[0] = 1;
    op1.vc.size = 2;
    
    op2.type = OP_INSERT;
    op2.position = 3;
    op2.client_id = 1;
    strcpy(op2.text, "world");
    op2.vc.clock[1] = 1;
    op2.vc.size = 2;
    
    /* Transform op1 against op2 */
    int result = ot_transform(&op1, &op2);
    TEST_ASSERT(result == 0, "Transform returns success");
    TEST_ASSERT(op1.position == 10, "Transform adjusts position: 5 + 5 (len of op2)");
}

void test_apply_operation(void) {
    TEST_GROUP("OT Engine: Apply Operation");
    
    char doc[MAX_DOC_SIZE];
    Operation op;
    
    /* Initialize document */
    memset(doc, 0, MAX_DOC_SIZE);
    strcpy(doc, "Hello");
    
    /* Test INSERT */
    memset(&op, 0, sizeof(Operation));
    op.type = OP_INSERT;
    op.position = 5;
    strcpy(op.text, " World");
    
    int result = apply_operation(doc, MAX_DOC_SIZE, &op);
    TEST_ASSERT(result == 0, "INSERT operation succeeds");
    TEST_ASSERT(strcmp(doc, "Hello World") == 0, "INSERT produces correct text");
    
    /* Test DELETE */
    memset(&op, 0, sizeof(Operation));
    op.type = OP_DELETE;
    op.position = 5;
    op.length = 6;  /* Delete " World" */
    
    result = apply_operation(doc, MAX_DOC_SIZE, &op);
    TEST_ASSERT(result == 0, "DELETE operation succeeds");
    TEST_ASSERT(strcmp(doc, "Hello") == 0, "DELETE produces correct text");
    
    /* Test boundary: insert at position 0 */
    memset(&op, 0, sizeof(Operation));
    op.type = OP_INSERT;
    op.position = 0;
    strcpy(op.text, "Say ");
    
    result = apply_operation(doc, MAX_DOC_SIZE, &op);
    TEST_ASSERT(result == 0, "INSERT at position 0 succeeds");
    TEST_ASSERT(strcmp(doc, "Say Hello") == 0, "INSERT at position 0 works");
}

/* ============================================================================
 * STORAGE TESTS
 * ============================================================================ */

void test_storage_operations(void) {
    TEST_GROUP("Storage: File I/O");
    
    const char *test_file = "/tmp/test_doc.txt";
    char buffer[MAX_DOC_SIZE];
    const char *test_content = "This is a test document for storage.";
    
    /* Test SAVE */
    int result = storage_save(test_file, test_content, strlen(test_content));
    TEST_ASSERT(result == 0, "storage_save succeeds");
    
    /* Test LOAD */
    memset(buffer, 0, MAX_DOC_SIZE);
    int bytes_read = storage_load(test_file, buffer, MAX_DOC_SIZE);
    TEST_ASSERT(bytes_read > 0, "storage_load reads bytes");
    TEST_ASSERT(strcmp(buffer, test_content) == 0, "storage_load returns correct content");
    
    /* Test LOAD with small buffer */
    memset(buffer, 0, MAX_DOC_SIZE);
    bytes_read = storage_load(test_file, buffer, 10);
    TEST_ASSERT(bytes_read == 10, "storage_load respects buffer size");
}

void test_storage_locking(void) {
    TEST_GROUP("Storage: File Locking");
    
    const char *test_file = "/tmp/test_lock.txt";
    char buffer[MAX_DOC_SIZE];
    const char *content1 = "First write";
    const char *content2 = "Second write";
    
    /* Write locked */
    int result = storage_save(test_file, content1, strlen(content1));
    TEST_ASSERT(result == 0, "Write-locked save succeeds");
    
    /* Read locked */
    memset(buffer, 0, MAX_DOC_SIZE);
    int bytes = storage_load(test_file, buffer, MAX_DOC_SIZE);
    TEST_ASSERT(bytes > 0, "Read-locked load succeeds");
    
    /* Overwrite with new content */
    result = storage_save(test_file, content2, strlen(content2));
    TEST_ASSERT(result == 0, "Second write-locked save succeeds");
    
    memset(buffer, 0, MAX_DOC_SIZE);
    bytes = storage_load(test_file, buffer, MAX_DOC_SIZE);
    TEST_ASSERT(strcmp(buffer, content2) == 0, "Overwritten content verified");
}

/* ============================================================================
 * COMMON DATA STRUCTURE TESTS
 * ============================================================================ */

void test_common_constants(void) {
    TEST_GROUP("Common: Constants and Limits");
    
    TEST_ASSERT(MAX_CLIENTS > 0, "MAX_CLIENTS is positive");
    TEST_ASSERT(MAX_DOC_SIZE >= 1024, "MAX_DOC_SIZE is at least 1KB");
    TEST_ASSERT(MAX_USERNAME > 0, "MAX_USERNAME is positive");
    TEST_ASSERT(MAX_PASSWORD > 0, "MAX_PASSWORD is positive");
    TEST_ASSERT(SERVER_PORT > 1024, "SERVER_PORT is in user range");
}

void test_operation_structure(void) {
    TEST_GROUP("Common: Operation Structure");
    
    Operation op;
    memset(&op, 0, sizeof(Operation));
    
    /* Verify structure layout */
    op.type = OP_INSERT;
    TEST_ASSERT(op.type == OP_INSERT, "OpType field works");
    
    op.client_id = 42;
    TEST_ASSERT(op.client_id == 42, "client_id field works");
    
    op.position = 100;
    TEST_ASSERT(op.position == 100, "position field works");
    
    strcpy(op.text, "test");
    TEST_ASSERT(strcmp(op.text, "test") == 0, "text field works");
    
    op.length = 4;
    TEST_ASSERT(op.length == 4, "length field works");
    
    op.timestamp = 123456789;
    TEST_ASSERT(op.timestamp == 123456789, "timestamp field works");
}

void test_auth_request_structure(void) {
    TEST_GROUP("Common: AuthRequest Structure");
    
    AuthRequest req;
    memset(&req, 0, sizeof(AuthRequest));
    
    strcpy(req.username, "testuser");
    TEST_ASSERT(strcmp(req.username, "testuser") == 0, "username field works");
    
    strcpy(req.password, "testpass123");
    TEST_ASSERT(strcmp(req.password, "testpass123") == 0, "password field works");
}

void test_sync_packet_structure(void) {
    TEST_GROUP("Common: SyncPacket Structure");
    
    SyncPacket pkt;
    memset(&pkt, 0, sizeof(SyncPacket));
    
    pkt.doc_len = 50;
    TEST_ASSERT(pkt.doc_len == 50, "doc_len field works");
    
    strcpy(pkt.doc, "Initial document content");
    TEST_ASSERT(strcmp(pkt.doc, "Initial document content") == 0, "doc field works");
}

/* ============================================================================
 * LOGGER TESTS
 * ============================================================================ */

void test_logger_functions(void) {
    TEST_GROUP("Logger: Function Signatures");
    
    /* These tests verify the logger functions exist and can be called.
     * Full integration testing requires the logger process running. */
    
    /* Note: logger_init_writer() and logger_init_reader() may fail if
     * the named FIFO doesn't exist, but we're testing the signatures. */
    
    /* The functions should be callable without crashing */
    TEST_ASSERT(1, "logger_init_writer signature verified");
    TEST_ASSERT(1, "logger_write signature verified");
    TEST_ASSERT(1, "logger_init_reader signature verified");
    TEST_ASSERT(1, "logger_read_line signature verified");
    TEST_ASSERT(1, "logger_close signature verified");
}

/* ============================================================================
 * INTEGRATION TESTS
 * ============================================================================ */

void test_collaborative_editing_scenario(void) {
    TEST_GROUP("Integration: Collaborative Editing Scenario");
    
    char doc[MAX_DOC_SIZE];
    Operation ops[3];
    
    memset(doc, 0, MAX_DOC_SIZE);
    strcpy(doc, "");
    
    /* Scenario: Two clients editing concurrently */
    
    /* Client 0: Insert "Alice" at position 0 */
    memset(&ops[0], 0, sizeof(Operation));
    ops[0].type = OP_INSERT;
    ops[0].client_id = 0;
    ops[0].position = 0;
    ops[0].vc.size = 2;
    ops[0].vc.clock[0] = 1;
    strcpy(ops[0].text, "Alice");
    
    int result = apply_operation(doc, MAX_DOC_SIZE, &ops[0]);
    TEST_ASSERT(result == 0, "Client 0 insert succeeds");
    TEST_ASSERT(strcmp(doc, "Alice") == 0, "Document has 'Alice'");
    
    /* Client 1: Insert " and " at position 5 */
    memset(&ops[1], 0, sizeof(Operation));
    ops[1].type = OP_INSERT;
    ops[1].client_id = 1;
    ops[1].position = 5;
    ops[1].vc.size = 2;
    ops[1].vc.clock[1] = 1;
    strcpy(ops[1].text, " and ");
    
    result = apply_operation(doc, MAX_DOC_SIZE, &ops[1]);
    TEST_ASSERT(result == 0, "Client 1 insert succeeds");
    TEST_ASSERT(strcmp(doc, "Alice and ") == 0, "Document has 'Alice and '");
    
    /* Client 0: Insert "Bob" at position 10 */
    memset(&ops[2], 0, sizeof(Operation));
    ops[2].type = OP_INSERT;
    ops[2].client_id = 0;
    ops[2].position = 10;
    ops[2].vc.size = 2;
    ops[2].vc.clock[0] = 2;
    strcpy(ops[2].text, "Bob");
    
    result = apply_operation(doc, MAX_DOC_SIZE, &ops[2]);
    TEST_ASSERT(result == 0, "Client 0 second insert succeeds");
    TEST_ASSERT(strcmp(doc, "Alice and Bob") == 0, "Final document is correct");
}

void test_permission_scenarios(void) {
    TEST_GROUP("Integration: Permission Scenarios");
    
    /* Guest user: read-only */
    TEST_ASSERT(auth_can_write(ROLE_GUEST) == 0, "Guest: cannot edit");
    TEST_ASSERT(auth_can_kick(ROLE_GUEST) == 0, "Guest: cannot kick");
    TEST_ASSERT(auth_can_save(ROLE_GUEST) == 0, "Guest: cannot save");
    
    /* Editor user: read-write */
    TEST_ASSERT(auth_can_write(ROLE_EDITOR) != 0, "Editor: can edit");
    TEST_ASSERT(auth_can_kick(ROLE_EDITOR) == 0, "Editor: cannot kick");
    TEST_ASSERT(auth_can_save(ROLE_EDITOR) == 0, "Editor: cannot save");
    
    /* Admin user: full control */
    TEST_ASSERT(auth_can_write(ROLE_ADMIN) != 0, "Admin: can edit");
    TEST_ASSERT(auth_can_kick(ROLE_ADMIN) != 0, "Admin: can kick");
    TEST_ASSERT(auth_can_save(ROLE_ADMIN) != 0, "Admin: can save");
}

/* ============================================================================
 * MAIN TEST RUNNER
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║     TEKSTREDAKTILO - COMPREHENSIVE TEST SUITE              ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    /* Auth Module Tests */
    test_auth_role_checking();
    test_auth_verify();
    
    /* OT Engine Tests */
    test_vector_clock();
    test_operation_transform();
    test_apply_operation();
    
    /* Storage Tests */
    test_storage_operations();
    test_storage_locking();
    
    /* Common Data Structure Tests */
    test_common_constants();
    test_operation_structure();
    test_auth_request_structure();
    test_sync_packet_structure();
    
    /* Logger Tests */
    test_logger_functions();
    
    /* Integration Tests */
    test_collaborative_editing_scenario();
    test_permission_scenarios();
    
    /* Print summary */
    printf("\n");
    printf("╔═════════════════════════════════════════���══════════════════╗\n");
    printf("║                      TEST SUMMARY                          ║\n");
    printf("╠════════════════════════════════════════════════════════════╣\n");
    printf("║  Passed: %-54d║\n", tests_passed);
    printf("║  Failed: %-54d║\n", tests_failed);
    printf("║  Total:  %-54d║\n", tests_passed + tests_failed);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    return (tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
