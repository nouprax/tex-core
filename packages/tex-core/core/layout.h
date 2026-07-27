/* Layout: semantic list to geometry over the embedded metrics. The engine
 * sets one horizontal box with TeX's inter-atom spacing in math modes; the
 * full box-and-glue model, math style resolution, and paragraph breaking
 * arrive with the 1.0.0 milestones. */

#ifndef TXC_LAYOUT_H
#define TXC_LAYOUT_H

#include "memory.h"
#include "parse.h"
#include "tex_core.h"
#include "tree.h"

/* Builds the root hbox for `list`, allocating nodes from `arena`. In the
 * math modes this resolves contextual Bin atoms and inserts the inter-atom
 * spacing kerns; `source_length` bounds the root's source range. */
tex_core_status txc_layout(
    txc_arena *arena,
    const txc_list *list,
    tex_core_mode mode,
    size_t source_length,
    const txc_node **root,
    tex_core_error *error
);

#endif
