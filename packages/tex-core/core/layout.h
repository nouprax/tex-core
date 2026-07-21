/* Layout: semantic list to geometry over the embedded metrics. The walking
 * skeleton sets one horizontal box; the full box-and-glue model, math style
 * resolution, and paragraph breaking arrive with the 1.0.0 milestones. */

#ifndef TXC_LAYOUT_H
#define TXC_LAYOUT_H

#include "memory.h"
#include "parse.h"
#include "tex_core.h"
#include "tree.h"

/* Builds the root hbox for `list`, allocating nodes from `arena`.
 * `source_length` bounds the root's source range. */
tex_core_status
txc_layout(txc_arena *arena, const txc_list *list, size_t source_length, const txc_node **root, tex_core_error *error);

#endif
