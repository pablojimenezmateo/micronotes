## Context

The live editing surface must map a caret — a byte offset into the note buffer —
to a pixel position and back, for every character in the document. It must also
know the exact source range of every block and every syntax marker, so markers
can be hidden without altering the buffer and blocks can be reordered as text
edits.

## Goals / Non-Goals

**Goals**

- `.md` files stay byte-for-byte authoritative; nothing is reserialized.
- One undo stack for typing and for block operations.
- Typing latency stays flat as note size grows.

**Non-Goals**

- Full CommonMark fidelity in the editing surface. The reading view keeps that.
- An in-memory block tree as the model. The text is the model.

## Decisions

### Decision: A separate offset-preserving scanner for editing, `md4c` for reading

`md4c`'s callback API delivers decoded text chunks with no source offsets, and
never reports block-level markers at all — entity decoding and newline
normalization mean the callback text is frequently a copy rather than a view
into the source. It cannot answer "which bytes produced this glyph", which is
the only question the editing surface needs answered.

So editing gets `src/doc/BlockScan` and `src/doc/InlineScan`: line-based,
single-pass, offsets preserved throughout. `md4c` keeps the reading view and
renders the block types the scanner deliberately does not model.

**Alternatives considered**

- *Patch vendored `md4c` to emit offsets.* Rejected: diverges from upstream on a
  security-sensitive parser, and the block-marker gap is structural, not a
  missing field.
- *An in-memory block tree serialized to Markdown on save.* Rejected: any
  construct the serializer does not round-trip exactly gets silently rewritten
  in the user's file on every save. That breaks the product's central promise.

**Consequence**: two Markdown code paths must agree visually. Bounded by giving
the scanner only simple, unambiguous block types and routing everything else to
`md4c`, and by diffing both against a shared fixture.

### Decision: Blocks partition the buffer exactly

`scanBlocks` returns ranges that cover the buffer with no gaps and no overlaps.
This is what makes incremental layout sound: an edit is contained in exactly one
block, so exactly one block re-lays out and the rest shift by a constant `dy`.
It is the first property to test.

### Decision: Block operations are text transforms

`turnInto`, `moveBlock`, `indent`, `toggleTodo` and the rest all emit
`(offset, eraseCount, insertText)` and apply through `MarkdownEditor`. No
parallel state to keep in sync, and undo works on block operations for free.

### Decision: Per-block layout memoization

`BlockLayout` is cached per block, keyed by content fingerprint, width, font
scale, and whether markers are revealed. Moving the caret between blocks
re-lays out at most two blocks. Budget, enforced in `micronotes_perf`: one
keystroke in a 200 KB note re-lays out in under ~2 ms.

## Risks / Trade-offs

- **Inline scanner mishandles exotic Markdown.** The scanner never mutates the
  buffer, and raw mode is one keystroke away, so the failure mode is cosmetic.
- **Scanner and `md4c` disagree.** Golden fixture (`docs/markdown-elements.md`)
  diffed through both paths.
- **Color emoji support varies by `SDL3_ttf` build.** Probed at startup with a
  monochrome fallback.

## Migration Plan

Phased, each phase shippable on its own. Foundations and restyle first
(including splitting `Application.cpp` before feature work lands), then the live
surface with raw and reading modes retained as escape hatches, then editing
behavior, block affordances, block types, and the navigation shell.

## Open Questions

- Whether raw mode stays a permanent user-facing mode or becomes a debug
  affordance once the live surface is proven.
