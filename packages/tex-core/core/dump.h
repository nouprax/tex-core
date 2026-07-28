/* Canonical render-tree dump. The output is byte-deterministic: integer
 * scaled-point geometry printed with txc_scaled_format, no floating point,
 * no locale, no pointers. The dump grammar is frozen by
 * docs/specs/render-tree-dump.md; the schema line is `render-tree 5`. */

#ifndef TXC_DUMP_H
#define TXC_DUMP_H

#include <stddef.h>

#include "tex_core.h"
#include "tree.h"

/* Allocates the dump with txc_malloc: NUL-terminated, `*length` excludes
 * the NUL. Frees nothing on failure paths except its own partial buffer. */
tex_core_status txc_dump(const txc_node *root, char **dump, size_t *length);

#endif
