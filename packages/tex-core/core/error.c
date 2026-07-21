#include "error.h"

#include <stdarg.h>
#include <stdio.h>

tex_core_status
txc_fail(tex_core_error *error, tex_core_status status, const tex_core_range *range, const char *format, ...) {
    if (error != NULL) {
        error->status = status;
        error->has_range = range != NULL;
        error->range.begin = range != NULL ? range->begin : 0;
        error->range.end = range != NULL ? range->end : 0;
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(error->message, sizeof(error->message), format, arguments);
        va_end(arguments);
    }
    return status;
}
