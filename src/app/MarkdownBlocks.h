#pragma once

#include "CoreAliases.h"

#include "doc/BlockScan.h"
#include "ui/Rect.h"
#include "ui/TextRenderer.h"

#include <SDL3/SDL.h>

#include <string_view>

// Drawing a block the live scanner does not model -- a table, a footnote
// definition, raw HTML -- on screen.
//
// The *shaping* is not here. It is in `doc/RenderLayout.h`, which parses the
// block with md4c and lays the result out through the same tokenizer and line
// breaker the note page's own blocks go through, and which the exporter reads
// as well. What is left in this file is one painter over that layout, plus the
// cache the note keeps its entries in.
//
// It used to be the other half of `app/InlineText.cpp`, which carried a second
// line breaker of its own; the exporter could not reach it from a lower layer
// and set every table cell as plain text instead. That was TD-39.
namespace micronotes::app {

// By reference only, in three signatures. A forward declaration rather than
// `app/Shell.h`: a drawing helper that takes the runtime to reach one cache on
// it does not need the whole runtime's definition to say so.
struct UiRuntime;

// Drops every cached entry the note no longer contains, and nothing else.
//
// The cache is keyed on a block's own source, so its live set is exactly the
// `Complex` blocks of the buffer on screen. That is the rule
// `doc::DocumentLayout` uses for the block layouts these mirror -- keep one
// generation of the document, sweep when the cache runs past it -- and unlike
// an LRU it cannot be defeated by a note bigger than the cap: the cap *is* the
// note. The 64-entry version this replaced made room by clearing itself, so a
// note with 65 tables re-parsed all of them on every relayout, for a hit rate
// of exactly zero.
//
// Called once a frame, and costs one comparison on the frames where the cache
// has not grown past what the last sweep found live -- which is all of them,
// apart from a note opening or an edit that adds a table.
void sweepComplexCache(UiRuntime& ui, doc::BlockSpan blocks, std::string_view source);

// A block the live scanner does not model, parsed on its own and laid out
// through the md4c path so that tables and raw HTML look the same everywhere.
// Both of these lay the block out at `width` if it is not already, so the
// height the page reserves and the pixels it then draws come from one shaping
// rather than two.
float measureComplexBlock(ui::TextRenderer& text, UiRuntime& ui, const doc::SourceBlock& block,
                          float width);
void drawComplexBlock(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui,
                      const doc::SourceBlock& block, ui::Rect rect);

}
