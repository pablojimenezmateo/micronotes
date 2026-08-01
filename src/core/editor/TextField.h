#pragma once

#include "core/editor/SingleLineEditor.h"

#include <SDL3/SDL.h>

#include <string>

namespace microcore::editor {

// A single-line field plus the one piece of view state it has to remember
// between frames: how far its text is scrolled sideways. Recomputing that from
// scratch each frame would make the field snap back to the left every repaint.
struct TextField {
  SingleLineEditor editor;
  float scrollX = 0.0f;

  const std::string& text() const { return editor.text(); }
  bool empty() const { return editor.empty(); }

  // Focus a field on an existing value. Selecting it all is what every other
  // rename box does: the first thing typed replaces the old name, but the old
  // name is still there to edit if that is what you wanted.
  void beginWith(std::string value, bool selectAll = true);
  void reset();
};

// What a key did, so the caller knows whether to re-run the search behind the
// field, merely repaint, or hand the key on to whatever is behind it.
enum class FieldKeyResult {
  Ignored,   // not a field key -- the caller still owns it
  Moved,     // caret or selection changed
  Changed,   // the text changed
};

// Applies one key press to a field: arrows, word motion, Home/End, Backspace
// and Delete (with their Ctrl word-wise forms), Shift for selection, and
// Ctrl+A / Ctrl+Z / Ctrl+Y.
//
// One function rather than a branch per field, and in core rather than in
// either app, on purpose. Both apps previously spelled "Backspace" out
// separately for every input they had -- five copies in one, three in the
// other -- and every copy was wrong in the same way. There is now one
// implementation to keep right.
FieldKeyResult applyKeyToField(TextField& field, SDL_Keycode key, bool ctrl, bool shift);

}
