/* TeX Core public C API.
 *
 * The single entry point is tex_core_document_compile: UTF-8 LaTeX source in,
 * an immutable render tree out. The tree carries both the semantic and the
 * geometric view of the compiled document; consumers walk it through borrowed
 * read-only node views and render with their own text stack. This library
 * never draws.
 *
 * Ownership model: the render tree is an opaque owned handle released with
 * tex_core_render_tree_free. Node pointers are borrowed views into the tree
 * and are valid until the tree is freed. No public read operation allocates.
 *
 * Thread safety: there is no process-global mutable state; every entry point
 * is thread-safe, and trees are immutable and safely shareable across
 * threads once compile returns.
 *
 * Errors are fail-fast (plan decision D4): unsupported or invalid input
 * yields a structured error naming the offending token and its source byte
 * range — never partial output. The error struct is caller-allocated plain
 * data so that allocation-failure reports themselves allocate nothing.
 *
 * The supported surface (milestone M1 in progress): document mode compiles
 * ordinary text atoms (ASCII letters, digits, period, comma) and explicit
 * spacing commands; the math modes add the classed math characters
 * (+ - * / = < > ( ) [ ] , ; : ! ? |), the M1 symbol commands (Greek
 * letters, binary operators, relations, arrows, letter-like and delimiter
 * symbols), braced groups, superscripts/subscripts set at TeX's script
 * sizes, and fractions (\frac, \dfrac, \tfrac), laid out with TeX's
 * inter-atom spacing. The full self-contained
 * LaTeX surface arrives with the 1.0.0 milestones; every extension of the
 * surface is a reviewed public-behavior change.
 */

#ifndef TEX_CORE_H
#define TEX_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tex-core-export.h"
#include "tex-core-version.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tex_core_status {
    TEX_CORE_STATUS_OK = 0,
    TEX_CORE_STATUS_INVALID_ARGUMENT = 1,
    TEX_CORE_STATUS_INVALID_UTF8 = 2,
    TEX_CORE_STATUS_UNSUPPORTED = 3,
    TEX_CORE_STATUS_ALLOCATION_FAILED = 4
} tex_core_status;

/* Input form of one compile call. Math modes receive a bare math fragment
 * with the delimiters already stripped by the caller; document mode receives
 * a self-contained LaTeX file or block. */
typedef enum tex_core_mode {
    TEX_CORE_MODE_DOCUMENT = 0,
    TEX_CORE_MODE_MATH_INLINE = 1,
    TEX_CORE_MODE_MATH_DISPLAY = 2
} tex_core_mode;

typedef struct tex_core_options {
    tex_core_mode mode;
} tex_core_options;

/* Initializes every field to the frozen defaults (document mode). */
TEX_CORE_API void tex_core_options_init(tex_core_options *options);

/* Half-open byte range [begin, end) into the compile call's source. */
typedef struct tex_core_range {
    size_t begin;
    size_t end;
} tex_core_range;

#define TEX_CORE_ERROR_MESSAGE_CAPACITY 224

typedef struct tex_core_error {
    tex_core_status status;
    /* When true, `range` locates the offending source bytes. */
    bool has_range;
    tex_core_range range;
    /* Deterministic NUL-terminated ASCII description. */
    char message[TEX_CORE_ERROR_MESSAGE_CAPACITY];
} tex_core_error;

typedef struct tex_core_render_tree tex_core_render_tree;
typedef struct tex_core_node tex_core_node;

/* Compiles exactly `length` UTF-8 bytes. `options == NULL` selects the
 * defaults. On success `*tree` receives the owned render tree and `error`
 * (when non-NULL) is reset to OK. On failure `*tree` is NULL, `error` is
 * filled when non-NULL, and nothing is leaked. `source` may be NULL only
 * when `length` is 0. */
TEX_CORE_API tex_core_status tex_core_document_compile(
    const uint8_t *source,
    size_t length,
    const tex_core_options *options,
    tex_core_render_tree **tree,
    tex_core_error *error
);
TEX_CORE_API void tex_core_render_tree_free(tex_core_render_tree *tree);
TEX_CORE_API const tex_core_node *tex_core_render_tree_root(const tex_core_render_tree *tree);

/* Allocates the canonical render-tree dump: NUL-terminated, newline-
 * terminated, byte-deterministic for a given tree across runs and
 * platforms. `length` (when non-NULL) receives the byte count excluding the
 * NUL. Free it with tex_core_dump_free. */
TEX_CORE_API tex_core_status tex_core_render_tree_dump(const tex_core_render_tree *tree, char **dump, size_t *length);
TEX_CORE_API void tex_core_dump_free(char *dump);

typedef enum tex_core_node_kind {
    TEX_CORE_NODE_NONE = 0,
    TEX_CORE_NODE_HBOX = 1,
    TEX_CORE_NODE_GLYPH = 2,
    TEX_CORE_NODE_KERN = 3,
    TEX_CORE_NODE_RULE = 4
} tex_core_node_kind;

typedef enum tex_core_style { TEX_CORE_STYLE_UPRIGHT = 0, TEX_CORE_STYLE_ITALIC = 1 } tex_core_style;

typedef enum tex_core_family {
    TEX_CORE_FAMILY_MAIN = 0,
    TEX_CORE_FAMILY_SIZE1 = 1,
    TEX_CORE_FAMILY_SIZE2 = 2,
    TEX_CORE_FAMILY_SIZE3 = 3,
    TEX_CORE_FAMILY_SIZE4 = 4
} tex_core_family;

/* Resolved geometry in points, relative to the parent's reference point on
 * the baseline. Kern nodes use `width` only; `italic` is the italic
 * correction of glyph nodes. The canonical schema is
 * docs/specs/render-tree.md; scaled points are internal. */
typedef struct tex_core_frame {
    double x;
    double y;
    double width;
    double ascent;
    double descent;
    double italic;
} tex_core_frame;

/* Glyphs are named by Unicode codepoint + style + family + size — never by
 * private glyph IDs. */
typedef struct tex_core_glyph {
    uint32_t codepoint;
    tex_core_style style;
    tex_core_family family;
    double size;
} tex_core_glyph;

/* Node views are NULL-tolerant: a NULL node reads as kind NONE with zeroed
 * fields and no children. */
TEX_CORE_API tex_core_node_kind tex_core_node_get_kind(const tex_core_node *node);
TEX_CORE_API const char *tex_core_node_kind_name(tex_core_node_kind kind);
TEX_CORE_API size_t tex_core_node_child_count(const tex_core_node *node);
TEX_CORE_API const tex_core_node *tex_core_node_child(const tex_core_node *node, size_t index);
TEX_CORE_API tex_core_range tex_core_node_range(const tex_core_node *node);
TEX_CORE_API tex_core_frame tex_core_node_frame(const tex_core_node *node);
/* Zeroed (codepoint 0) for non-glyph nodes. */
TEX_CORE_API tex_core_glyph tex_core_node_glyph(const tex_core_node *node);

/* Returns TEX_CORE_VERSION_STRING; always matches the repository VERSION. */
TEX_CORE_API const char *tex_core_version_string(void);

#ifdef __cplusplus
}
#endif

#endif
