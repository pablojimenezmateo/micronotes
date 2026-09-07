#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Everything the shell asks the desktop to do, in one place.
//
// There were two halves of this and they lived in two files that had no reason
// to own them: the clipboard write was a `static` in `Application.cpp`, and the
// `fork`+`execvp` that hands a path to `xdg-open` was a `static` in
// `PageChrome.cpp` because following a link was the first thing that needed it.
// The second caller is what made them a seam -- "show this note on disk" wants
// the launcher, "copy its path" wants the clipboard, and neither is anything to
// do with a link in a note or with the shell's key handler.
namespace micronotes::app {

// Puts `value` on both the clipboard and the X11 primary selection, because on
// this platform a copy means both. Reports whether the clipboard took it.
//
// Traces under `MICRONOTES_DEBUG_INPUT`: a clipboard that silently does nothing
// is the failure mode here, and it depends on the compositor rather than on
// anything in this process, so the trace is the only way to tell "we did not
// ask" from "we asked and were refused".
bool setClipboardText(std::string_view value);

// A desktop program, launched and let go of.
//
// `fork` + `execvp` rather than `system`, so a target with a space or a quote
// in its name is an argument rather than shell input. Detached rather than
// waited on, which is the whole reason this is safe to call from the UI thread:
// `xdg-open` normally forks and returns at once, but a wedged file manager
// would otherwise park the frame for as long as it took to give up. The sibling
// microide runs the same command through a subprocess helper with a 10-second
// timeout and posts it to a background executor to get the same property.
bool spawnDetached(const std::vector<std::string>& command);

// Hands `target` -- a URL, or a path -- to whatever the desktop opens it with.
bool openWithDesktop(std::string_view target);

// Opens the file manager on the directory *containing* `path`.
//
// The containing directory, not the file: "show on disk" means "show me where
// this lives", and `xdg-open` on a `.md` would launch a text editor on it,
// which is the one thing the reader already has. A path with no parent -- which
// should not happen for a note in a library -- falls back to itself.
//
// Checks the path exists first, so a note deleted underneath the app reports a
// failure instead of spawning a process that will fail out of sight.
bool revealInFileManager(const std::filesystem::path& path);

}
