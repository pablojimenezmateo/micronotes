#pragma once

#include <filesystem>
#include <string_view>

namespace micronotes::app {

struct UiRuntime;

// "Export as PDF" -- the tree's two context menus, and the write that follows
// the file chooser.
//
// Only the tree opens one. There is no palette action and no menu bar row: an
// export is about a *thing in the library* rather than about whatever happens
// to be showing, and the two rows below are on the two things it can be about.
//
// Neither of these writes anything by itself. Both put the desktop's own save
// dialog up and return; the loop finishes the job when an answer comes back,
// which is what `applyPendingExport` is for.
void exportNoteToPdf(UiRuntime& ui, std::string_view noteId);
void exportFolderToPdf(UiRuntime& ui, const std::filesystem::path& folder);

// Writes an export whose destination has come back from the chooser, and says
// whether anything happened -- which the loop reads as "repaint", because the
// status line has changed.
//
// Called once a pass, beside the other two things that change without an event
// of their own. It is a load of one atomic on every pass that has no export in
// flight, which is all of them.
bool applyPendingExport(UiRuntime& ui);

// The notes a folder export covers: everything under `folder`, its
// sub-notebooks included, in the order the tree shows them.
//
// Exposed for the test, which is the only way to check the *set* without a
// file chooser in the way.
std::size_t notesUnderFolder(const UiRuntime& ui, const std::filesystem::path& folder);

}
