#include "app/CaretPolicy.h"

#include "core/util/Hash.h"

namespace micronotes::app {

std::uint64_t caretStateKey(const UiRuntime& ui) {
  std::uint64_t key = util::kFnvOffset;
  key = util::hashValue(key, ui.focus);
  // The page's caret: where it is, and which revision of the buffer it is in.
  key = util::hashValue(key, ui.editor.revision());
  key = util::hashValue(key, ui.editor.cursor());
  // Whichever single-line field has the keyboard. Its length stands in for its
  // text: this only has to *change* when the field does, and a field cannot
  // change its contents without changing its length or its caret.
  if(const editor::TextField* field = focusedField(ui)) {
    key = util::hashValue(key, field->text().size());
    key = util::hashValue(key, field->editor.cursor());
  }
  // And the overlay's own field, which is the one the reader is most likely to
  // be typing into and is not reachable through `ui.focus` at all.
  if(const ui::Overlay* overlay = ui.overlays.top()) {
    key = util::hashValue(key, overlay->value.text().size());
    key = util::hashValue(key, overlay->value.editor.cursor());
    key = util::hashValue(key, overlay->highlighted);
  }
  return key;
}

int settleCaret(UiRuntime& ui) {
  // A frozen caret is solid and asks for no wake-ups: a capture wants the same
  // pixels every time it is taken. See `CaretState::frozen`.
  if(ui.caret.frozen) {
    ui.caret.visible = true;
    return -1;
  }
  const Uint64 now = SDL_GetTicks();
  ui.caret.blink.observe(caretStateKey(ui), now);
  ui.caret.visible = ui.caret.blink.visible(now);
  return ui.caret.blink.waitMs(now);
}

// Whether the caret's blink has flipped since the last frame painted one.
//
// The run loop repaints on events, on a window action, on a watched change and
// on an autosave -- and the caret is behind none of those. So a blink wake
// arrived, found nothing to do, and counted a skipped repaint: the deadline was
// honoured and the frame it existed for was never drawn.
bool caretPhaseChanged(UiRuntime& ui) {
  (void)settleCaret(ui);
  return ui.caret.visible != ui.caret.painted;
}

}
