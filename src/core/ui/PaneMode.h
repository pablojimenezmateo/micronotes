#pragma once

namespace microcore::ui {

// How the note area is arranged. This is the one piece of the window's
// arrangement that is not notes-app furniture -- an editor, a rendering, or
// both, is a choice any Markdown surface has -- which is why it lives in the
// core and `ui::WorkspaceModel` (which holds the rest: the panels, their
// widths, the tabs) imports it rather than restating it.
enum class PaneMode {
  Editor,   // raw Markdown source
  Viewer,   // read-only md4c rendering
  Split,    // source beside rendering
  Live      // formatting rendered in place, and editable
};

}
