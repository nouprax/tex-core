#include "harness.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void txc_result(txc_test *test, int condition, const char *format, va_list arguments) {
    if (condition) {
        test->passed += 1;
    } else {
        test->failed += 1;
        fputs("FAILED: ", stderr);
        vfprintf(stderr, format, arguments);
        fputs("\n", stderr);
    }
}

void txc_check(txc_test *test, int condition, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    txc_result(test, condition, format, arguments);
    va_end(arguments);
}

void txc_check_int(txc_test *test, long long got, long long expected, const char *label) {
    txc_check(test, got == expected, "%s: got %lld, expected %lld", label, got, expected);
}

void txc_check_size(txc_test *test, size_t got, size_t expected, const char *label) {
    txc_check(test, got == expected, "%s: got %zu, expected %zu", label, got, expected);
}

void txc_check_text(txc_test *test, const char *got, const char *expected, const char *label) {
    int condition = got != NULL && expected != NULL && strcmp(got, expected) == 0;
    txc_check(test, condition, "%s: got \"%s\", expected \"%s\"", label, got != NULL ? got : "(null)", expected);
}

int txc_test_finish(txc_test *test, const char *suite) {
    printf("%s: %d passed, %d failed\n", suite, test->passed, test->failed);
    return test->failed == 0 ? 0 : 1;
}
