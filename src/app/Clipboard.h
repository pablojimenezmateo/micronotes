#pragma once

#include <filesystem>

// Everything that moves text or an image between the shell and the rest of the
// desktop: the clipboard, X11's primary selection, and a file dropped on the
// window.
//
// Six of these paths differ only in *which* of the two selections they read
// and *what* they paste into, and they were six `static`s in Application.cpp
// sitting between the key handler and the mouse handler that call them. The
// pair is the reason they belong together: X11 has two selections, every
// surface that takes text has to answer for both, and a paste path that
// handles one and forgets the other is the bug this grouping makes visible.
//
// `app/Desktop.h` is the other half -- writing to the clipboard, and handing a
// path to the desktop. It has no notion of focus, which is why the reads live
// here and the writes live there.
namespace micronotes::app {

struct UiRuntime;

// The editor's selection, published as the primary selection so a middle click
// in another window pastes it. False when there was nothing to publish.
bool publishEditorPrimarySelection(UiRuntime& ui);

// Pastes into the note buffer. `Image` looks for image data first and creates
// an attachment; `Text` takes text. Both report whether they found anything of
// their own kind, so a caller can try one and fall back to the other.
bool pasteClipboardImage(UiRuntime& ui);
bool pasteClipboardText(UiRuntime& ui);
bool pastePrimarySelectionText(UiRuntime& ui);

// The same two, into whichever one-line field has focus. Nothing happens when
// the focus is not in a field.
bool pasteClipboardIntoInput(UiRuntime& ui);
bool pastePrimarySelectionIntoInput(UiRuntime& ui);

// Copies a file into the library's attachments and writes the link into the
// note. The drop target, and the `--attach` path.
bool attachPathToEditor(UiRuntime& ui, const std::filesystem::path& source);

}
