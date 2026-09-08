#pragma once

// Which surface has the keyboard.
//
// Its own header rather than a line inside `app/Shell.h`, because it is the
// question the widest range of code asks -- 88 mentions across 22 files -- and
// because `app/Fields.h` has to name it to say which of the five one-line
// fields each value stands for. A file that only routes a keystroke should not
// have to include the whole runtime to find out where the keystroke goes.
namespace micronotes::app {

// Eight values carrying two things at once: which surface has the keyboard, and
// which of the five prompts is open. The second is what `app/Fields.h`'s table
// is about; see the note there.
enum class FocusArea {
  Folders,
  Editor,
  Search,
  Find,
  Viewer,
  TagEditor,
  RenameNote,
  RenameFolder
};

}
