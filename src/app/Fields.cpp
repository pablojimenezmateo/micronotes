#include "app/Fields.h"

#include "app/FindBar.h"
#include "app/Notes.h"
#include "app/Prompts.h"
#include "app/Shell.h"

#include "core/editor/SingleLineView.h"

#include "ui/Fonts.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <string>

namespace micronotes::app {
namespace {

// The five fields, once. Every question any surface asks about "the field the
// focus is in" is a lookup here.
constexpr std::array<FieldSpec, 5> kFields {{
  {FocusArea::Search, &TextFields::search, nullptr},
  {FocusArea::Find, &TextFields::find, nullptr},
  {FocusArea::TagEditor, &TextFields::tag, saveTags},
  {FocusArea::RenameNote, &TextFields::rename, saveRename},
  {FocusArea::RenameFolder, &TextFields::folderRename, saveFolderRename},
}};

const FieldSpec* specFor(FocusArea focus) {
  for(const auto& spec : kFields) {
    if(spec.focus == focus) return &spec;
  }
  return nullptr;
}

// Core measures text through a callback so it stays free of any font
// dependency; this binds it to the renderer actually drawing the field.
editor::TextWidthFn fieldMeasure(const ui::TextRenderer& text) {
  const ui::TextStyle style = ui::chromeStyle();
  return [&text, style](std::string_view run) { return text.width(run, style); };
}

}

std::span<const FieldSpec> fieldSpecs() {
  return kFields;
}

editor::TextField* focusedField(UiRuntime& ui) {
  const FieldSpec* spec = specFor(ui.focus);
  return spec ? &(ui.fields.*(spec->member)) : nullptr;
}

const editor::TextField* focusedField(const UiRuntime& ui) {
  const FieldSpec* spec = specFor(ui.focus);
  return spec ? &(ui.fields.*(spec->member)) : nullptr;
}

void (*fieldCommit(FocusArea focus))(UiRuntime&) {
  const FieldSpec* spec = specFor(focus);
  return spec ? spec->commit : nullptr;
}

void syncFocusedInput(UiRuntime& ui) {
  if(ui.focus == FocusArea::Search) {
    ui.state.setSearch(ui.fields.search.text(), ui.fields.searchScope);
    selectNoteAt(ui, 0);
  } else if(ui.focus == FocusArea::Find) {
    // A needle that has changed is a different search, so the reader is put
    // back on the first match at or after where they were rather than left
    // pointing at an index into a list that no longer exists -- and taken to
    // it, because find-as-you-type that finds without showing you is a match
    // count with extra steps.
    refreshFindMatches(ui);
    revealFindMatch(ui);
  }
}

void focusSearchAllNotes(UiRuntime& ui) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.focus = FocusArea::Search;
  ui.fields.search.editor.selectAll();
  ui.state.setSearch(ui.fields.search.text(), ui.fields.searchScope);
  ui.status = "Search all notes";
}

// Paints a single-line field: selection band, then the text scrolled so the
// caret is visible, then the caret. The fields this replaces drew only the
// string, which is why they had no visible insertion point, no selection, and
// no way to reach text past the right edge.
void drawTextField(SDL_Renderer* renderer, ui::TextRenderer& text, UiRuntime& ui,
                   editor::TextField& field, ui::Rect box, bool focused,
                   std::string_view placeholder) {
  ui::TextFieldPaint paint;
  paint.box = box;
  paint.textY = box.y;
  // Negative: this box *is* the text line, so the caret and the band have to
  // grow past it rather than sit inside it.
  paint.insetY = -2.0f;
  paint.placeholder = placeholder;
  paint.focused = focused;
  // Painted only on the blink's on-phase. See `ui::CaretBlink`: a solid bar in
  // a mono face reads as a pipe character rather than as an insertion point, so
  // the caret used to say "your typing goes here" in exactly the same voice as
  // the text beside it.
  paint.caretVisible = ui.caret.visible;
  // Recorded for the drag: see `TextFields::drawnRect`. On the draw rather than
  // at each press site, so the rect a motion extends against is the rect the
  // text was actually laid out in.
  if(focused) ui.fields.drawnRect = box;
  const ui::Rect caret = ui::drawTextFieldText(renderer, text, ui::chromeStyle(), field, paint);
  if(ui::empty(caret)) return;
  // Give the IME a candidate rectangle here too. Without it a dead-key or
  // composition popup opened while typing in a field lands at the window origin
  // instead of next to the text being composed.
  ui.caret.reported = true;
  ui.caret.rect = SDL_Rect {static_cast<int>(caret.x), static_cast<int>(caret.y), 2,
                            static_cast<int>(caret.h)};
}

// Byte offset in `field` under a pointer at window x, for a field drawn in
// `box`. Always a code point boundary.
std::size_t fieldOffsetAtX(const ui::TextRenderer& text, const editor::TextField& field,
                           ui::Rect box, float x) {
  return editor::offsetAtX(field.text(), x - box.x + field.scrollX, fieldMeasure(text));
}

}
