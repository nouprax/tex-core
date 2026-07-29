# Incremental editing design

Status: planned contract for `2.0.0` / milestone M6. This document records
the intended architecture and acceptance criteria; it does not describe an
API available in the current `0.x` packages. Public names remain provisional
until the M6 specification is approved.

## Goal and boundary

Incremental editing will add a mutable session around the existing pure,
one-shot compiler:

1. a consumer queues byte-range edits against one stored UTF-8 source;
2. `commit` updates only the affected parse and layout regions;
3. the commit returns an immutable render-tree snapshot and an exact delta;
4. unchanged values are structurally shared with the preceding snapshot.

`Document.compile` remains the one-shot entry point. It must stay
self-contained and must not retain a session or a process-global cache.
Incremental sessions are an additional ownership and lifetime model, not a
semantic fork of the compiler.

The `2.0.0` scope remains one self-contained input. Project-wide includes,
packages, references, user-defined macros, and other programmable TeX state
belong to the `3.0.0` project model. The session design must leave room for
those dependency domains without prematurely exposing them in the v2 API.

## Non-negotiable invariants

- **Fresh-compile equivalence:** after every successful commit, the
  incremental snapshot's canonical dump is byte-identical to a fresh
  `Document.compile` of the same source and options. Session lineage, node
  identity, revisions, and delta metadata are excluded from dump equality.
- **Snapshot immutability:** a committed snapshot never changes when the
  session receives more edits, commits, or closes.
- **Honest identity:** unchanged nodes retain identity; a node whose kind
  changes is removed and replaced. IDs are never reused within a session.
- **Transactional failure:** a failed commit leaves the last committed tree
  usable. No cache may expose a mixture of old and new revisions.
- **No global state:** sessions are independent and can run concurrently.
  One session has one externally synchronized writer.
- **One semantic implementation:** C owns incremental parsing, layout
  invalidation, canonical scope materialization, and delta ordering.
  Bindings consume those results rather than independently reconstructing
  the rules.

## Session ownership

A session is the sole mutable owner of:

- the exact edited source bytes;
- pending edits and their coalesced stale ranges;
- the current native parse/layout state and revision;
- per-session dependency indexes used to find affected work;
- the current identity table and binding mirror;
- caches whose keys are meaningful only within that session lineage.

Edits update the source store and invalidation records but do not parse.
`commit` applies the pending invalidation transactionally and publishes a
new immutable snapshot. The session receives a random 64-bit lineage; public
node identity is `(lineage, id, revision)`, preventing collisions between
sessions.

The commit delta records disjoint `added`, `removed`, `changed`, and
`bubbled` ID sets. The C facade supplies one deterministic,
children-before-parents materialization order for every node that bindings
must rebuild. Binding work is proportional to the delta plus child slots
copied by rebuilt immutable containers, not to the complete tree.

## Source-range resolver

In this document, **scope** means a node's source byte range within one
snapshot. It is not a TeX grouping or macro scope.

Absolute byte ranges must not be stored as immutable node content. An edit
near the beginning can shift every later range without changing the later
nodes' syntax or layout. Treating those shifts as node changes would destroy
structural sharing and turn a small edit into a whole-tree delta.

The native layer therefore exposes a canonical batch scope table: one
preorder row per node containing `(id, revision, absolute byte range)`.
Materializing the table is O(n) once per snapshot; lookup is O(1)
afterwards. Rows are caller-owned and remain valid independently of the
session.

One-shot compilation materializes the table eagerly, so its returned tree is
immediately self-contained. A session snapshot resolves it lazily on the
first operation that needs ranges, such as:

- `scope(node)`;
- a range-aware tree walk;
- the canonical dump;
- an explicit `materialize` requested before retaining a snapshot.

Structural traversal has a range-free overload and never implicitly pays for
scope materialization.

### Resolver state machine

Each snapshot owns a resolver with these conceptual states:

```text
live(session, revision)
          │ first range request
          ▼
materializing ───────────────► materialized(table)
          │ failure
          └──────────────────► live(session, revision)

live(session, revision) ── commit/close ──► detached
materialized(table)      ── commit/close ──► materialized(table)
```

- `live` may read only the native tree at the snapshot's exact revision.
- `materializing` provides single-flight behavior: concurrent readers share
  one batch request. A commit or close waits until the request publishes or
  fails; native memory is never mutated or freed beneath a reader.
- `materialized` is permanent and independent of the session.
- `detached` means the snapshot was superseded before it materialized.
  Requesting a range from it is a programmer error because silently reading
  the new native revision would attach incorrect positions to old values.
- A failed transactional commit reattaches the previous snapshot because
  its native revision remains current.

Every lookup validates lineage, node ID, and node revision. This prevents a
shared ID from pairing a stale node value with another snapshot's range.
The table is cached by the snapshot, not by the mutable session, because
absolute positions are revision-specific.

Kotlin should implement this state transition atomically rather than with a
plain nullable map. Swift and ECMAScript must preserve the same observable
lifetime and single-flight semantics using their idiomatic synchronization
primitives.

## Cache model

Every cache must document its owner, complete key, and invalidation domain.
The initial implementation is expected to use these layers:

| Cache | Owner | Minimum key | Invalidation |
| --- | --- | --- | --- |
| Scope table | immutable snapshot | lineage + snapshot revision | never mutated; detached if not materialized before supersession |
| Binding node mirror | session | lineage + node ID + node revision | exact commit delta; evict `removed`, rebuild `changed`, relink `bubbled` |
| Parse reuse | native session | source identity + parser mode + affected grammar state | edited token/construct ownership domain and dependants |
| Layout reuse | native session | semantic node revision + style + font-metrics version + layout constraints | changed semantic nodes and their layout dependency closure |
| Query/index data | native session | committed revision + query-specific semantic key | exact dependency-index hits, never a process-global flush |

No cache may be keyed only by a source offset, object address, filename, or
host-global revision. Cache entries must not cross session lineages.
Options, schema version, embedded metrics, and any future TeX environment
state that can affect an answer are part of the key or invalidate the entire
owning domain.

The parse and layout dependency domains must be explicit. Examples:

- an edit inside a math list may require reparsing that complete list;
- an edit inside a paragraph may require line breaking for the complete
  paragraph;
- changed box dimensions may require relayout of affected ancestors;
- later nodes whose only change is an absolute source shift keep their node
  identity and are covered by the snapshot scope table;
- genuinely non-local constructs may degrade to one bounded full compile,
  but never to repeated whole-document work per changed node.

Memory is bounded by ownership: closing a session releases its mutable
caches; retained snapshots keep only structurally shared values and any
scope table they explicitly or implicitly materialized. The implementation
must expose no unbounded process-global memoization.

## Cost contract

Warm incremental commit cost should be proportional to:

- the touched parse/layout ownership domains;
- the exact delta;
- child slots copied into rebuilt immutable containers;
- dependency-index results whose answers actually changed.

Cold session creation and the first compile are measured separately from
warm commits. Scope materialization is a distinct O(n) operation and must not
be hidden inside a scope-free commit benchmark. Complexity gates use fresh
warmup, adaptive sampling, and both localized and adversarial edits.

The contract is damage-proportional, not universally constant-time. A single
edit can legitimately invalidate a complete paragraph, deeply nested layout
ancestor chain, or the whole input. Such a commit must remain within a small
constant multiple of one fresh compile and must not exhibit
changes-times-document-size behavior.

## Delivery and acceptance

M6 lands in this order:

1. freeze the language-neutral session, identity, delta, scope, failure, and
   cost contracts;
2. implement the source store, invalidation engine, scope table, and ordered
   delta materialization in C;
3. add platform sessions that consume the C tables and share unchanged
   immutable values;
4. add deterministic edit-replay tests on every platform;
5. fuzz random and coverage-guided edit sequences against fresh-compile dump
   equality;
6. test retained/materialized/superseded snapshots, concurrent scope
   readers, commit failure, close races, and cache eviction;
7. enforce cold, warm, localized-damage, worst-case fallback, and memory
   growth gates in Release CI.

The feature is not complete until all bindings produce equivalent snapshots
and deltas from the same edit trace and the tests demonstrate that disabling
each cache changes performance only, never output.
