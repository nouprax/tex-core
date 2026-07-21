/* Minimal native test harness (markdown-core's batch-runner shape): each
 * suite is one executable whose exit status reports the failure count. The
 * test tree is all C — no scripting-language dependency, no network. */

#ifndef TXC_TEST_HARNESS_H
#define TXC_TEST_HARNESS_H

#include <stddef.h>

typedef struct txc_test {
    int passed;
    int failed;
} txc_test;

void txc_check(txc_test *test, int condition, const char *format, ...);
void txc_check_int(txc_test *test, long long got, long long expected, const char *label);
void txc_check_size(txc_test *test, size_t got, size_t expected, const char *label);
void txc_check_text(txc_test *test, const char *got, const char *expected, const char *label);

/* Prints the summary and returns the process exit status. */
int txc_test_finish(txc_test *test, const char *suite);

#endif
