/* CLI contract: the tex-core binary is a thin adapter over the public API,
 * so its stdout must equal the API dump byte for byte, and its exit codes
 * must be stable (0 success, 1 compile error, 2 usage). POSIX popen; the
 * suite is only added on POSIX hosts. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "harness.h"
#include "tex_core.h"

static const char *txc_cli_path;

/* Runs `<cli> <arguments>` with `input` on stdin; captures stdout into a
 * malloc'd string and the exit status. */
static char *txc_cli_run(const char *arguments, const char *input, int *exit_status) {
    char command[1024];
    snprintf(
        command,
        sizeof(command),
        "printf '%%s' '%s' | '%s' %s 2>/dev/null",
        input != NULL ? input : "",
        txc_cli_path,
        arguments
    );
    FILE *stream = popen(command, "r");
    if (stream == NULL) {
        return NULL;
    }
    char *output = NULL;
    size_t used = 0;
    size_t capacity = 0;
    for (;;) {
        if (capacity - used < 512) {
            capacity = capacity > 0 ? capacity * 2 : 1024;
            char *grown = realloc(output, capacity);
            if (grown == NULL) {
                free(output);
                pclose(stream);
                return NULL;
            }
            output = grown;
        }
        size_t read = fread(output + used, 1, capacity - used - 1, stream);
        used += read;
        if (read == 0) {
            break;
        }
    }
    output[used] = '\0';
    int raw = pclose(stream);
    *exit_status = raw >= 0 ? WEXITSTATUS(raw) : -1;
    return output;
}

static char *txc_api_dump(const char *source, tex_core_mode mode) {
    tex_core_options options;
    tex_core_options_init(&options);
    options.mode = mode;
    tex_core_render_tree *tree = NULL;
    if (tex_core_document_compile((const uint8_t *)source, strlen(source), &options, &tree, NULL) !=
        TEX_CORE_STATUS_OK) {
        return NULL;
    }
    char *dump = NULL;
    if (tex_core_render_tree_dump(tree, &dump, NULL) != TEX_CORE_STATUS_OK) {
        dump = NULL;
    }
    tex_core_render_tree_free(tree);
    return dump;
}

static void txc_check_cli_matches_api(
    txc_test *test,
    const char *arguments,
    const char *input,
    const char *source,
    tex_core_mode mode
) {
    int exit_status = -1;
    char *cli_output = txc_cli_run(arguments, input, &exit_status);
    char *api_output = txc_api_dump(source, mode);
    txc_check(test, exit_status == 0, "%s: exit status %d", arguments, exit_status);
    txc_check(
        test,
        cli_output != NULL && api_output != NULL && strcmp(cli_output, api_output) == 0,
        "%s: CLI output equals the API dump",
        arguments
    );
    free(cli_output);
    tex_core_dump_free(api_output);
}

int main(int argc, char *argv[]) {
    txc_test test = {0, 0};
    if (argc < 2) {
        fputs("usage: tex-core-test-cli <path-to-tex-core-binary>\n", stderr);
        return 1;
    }
    txc_cli_path = argv[1];

    int exit_status = -1;
    char *output = txc_cli_run("--version", NULL, &exit_status);
    txc_check(&test, exit_status == 0, "--version exits 0");
    txc_check(
        &test,
        output != NULL && strncmp(output, "tex-core ", 9) == 0 && strstr(output, tex_core_version_string()) != NULL,
        "--version prints the version"
    );
    free(output);

    txc_check_cli_matches_api(&test, "", "hello world", "hello world", TEX_CORE_MODE_DOCUMENT);
    txc_check_cli_matches_api(&test, "--mode document", "a b", "a b", TEX_CORE_MODE_DOCUMENT);
    txc_check_cli_matches_api(&test, "--mode math-inline", "fx", "fx", TEX_CORE_MODE_MATH_INLINE);
    txc_check_cli_matches_api(&test, "--mode math-display", "fx", "fx", TEX_CORE_MODE_MATH_DISPLAY);

    /* Two runs are byte-identical. */
    int first_status = -1;
    int second_status = -1;
    char *first = txc_cli_run("--mode math-inline", "abc", &first_status);
    char *second = txc_cli_run("--mode math-inline", "abc", &second_status);
    txc_check(
        &test,
        first != NULL && second != NULL && strcmp(first, second) == 0,
        "repeated CLI runs are byte-identical"
    );
    free(first);
    free(second);

    /* File input equals stdin input. */
    char path[] = "/tmp/tex-core-cli-XXXXXX";
    int descriptor = mkstemp(path);
    txc_check(&test, descriptor >= 0, "temp file for file-input check");
    if (descriptor >= 0) {
        FILE *file = fdopen(descriptor, "w");
        fputs("a b", file);
        fclose(file);
        char arguments[256];
        snprintf(arguments, sizeof(arguments), "--mode document %s", path);
        txc_check_cli_matches_api(&test, arguments, NULL, "a b", TEX_CORE_MODE_DOCUMENT);
        remove(path);
    }

    /* Compile errors exit 1 with no stdout. */
    output = txc_cli_run("--mode math-inline", "\\frac", &exit_status);
    txc_check(&test, exit_status == 1, "compile errors exit 1 (got %d)", exit_status);
    txc_check(&test, output != NULL && output[0] == '\0', "compile errors write nothing to stdout");
    free(output);

    /* Usage errors exit 2. */
    output = txc_cli_run("--mode nonsense", NULL, &exit_status);
    txc_check(&test, exit_status == 2, "unknown mode exits 2 (got %d)", exit_status);
    free(output);
    output = txc_cli_run("--frobnicate", NULL, &exit_status);
    txc_check(&test, exit_status == 2, "unknown option exits 2 (got %d)", exit_status);
    free(output);

    /* A failed or truncated stdout write must exit nonzero: scripts read
     * the exit status as the compile verdict. /dev/full makes every write
     * fail with ENOSPC; the device only exists on Linux, so the check is
     * skipped elsewhere. */
    if (access("/dev/full", W_OK) == 0) {
        char command[1024];
        snprintf(command, sizeof(command), "printf ab | '%s' --mode document >/dev/full 2>/dev/null", txc_cli_path);
        int raw = system(command);
        txc_check(&test, raw >= 0 && WIFEXITED(raw) && WEXITSTATUS(raw) == 1, "full output device exits 1");
    }

    return txc_test_finish(&test, "cli");
}
