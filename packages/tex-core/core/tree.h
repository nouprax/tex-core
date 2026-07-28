/* Internal render-tree representation. The public opaque types are completed
 * here; nodes live in the tree's arena and are immutable once compile
 * returns (plan decision D6). */

#ifndef TXC_TREE_H
#define TXC_TREE_H

#include <stddef.h>
#include <stdint.h>

#include "memory.h"
#include "scaled.h"
#include "tex_core.h"

struct tex_core_node {
    tex_core_node_kind kind;
    /* Offset within the parent, from its reference point on the baseline. */
    txc_scaled x;
    txc_scaled y;
    txc_scaled width;
    txc_scaled ascent;
    txc_scaled descent;
    txc_scaled italic;
    /* TEX_CORE_NODE_GLYPH only. `size` is the glyph's em in scaled
     * points: the text size or a script size. */
    uint32_t codepoint;
    tex_core_style style;
    tex_core_family family;
    txc_scaled size;
    tex_core_range range;
    const struct tex_core_node **children;
    size_t child_count;
};

typedef struct tex_core_node txc_node;

struct tex_core_render_tree {
    txc_arena arena;
    const txc_node *root;
};

#endif
