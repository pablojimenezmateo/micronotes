#include "app/ExportPdf.h"

#include "app/Shell.h"
#include "app/WikiLinks.h"
#include "core/platform/PathUtils.h"
#include "export/NotePdf.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <ctime>
#include <memory>
#include <mutex>
#include <utility>

namespace micronotes::app {
namespace {

// The one filter the chooser offers. `SDL_ShowSaveFileDialog` requires the
// array to stay alive until the callback runs, which for a static is the life
// of the process -- the alternative is a heap allocation whose only job is to
// be freed by a callback that may or may not run.
const SDL_DialogFileFilter kFilters[] = {
  {"PDF document", "pdf"},
  {"All files", "*"},
};

// The application's window, for a chooser that should be modal to it.
//
// Asked of SDL rather than threaded down from `run()`: a context menu handler
// has no window in scope, and passing one through four call sites so that a
// dialog can be parented is a parameter every one of them would have to carry
// and none of them would use.
SDL_Window* mainWindow() {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  return count > 0 && windows != nullptr ? windows[0] : nullptr;
}

// Whether `folder` is `candidate` or contains it. Path components rather than
// a string prefix: `notes/api` must not swallow `notes/api-v2`.
bool folderContains(const std::filesystem::path& folder, const std::filesystem::path& candidate) {
  if(folder.empty()) return true;
  auto want = folder.begin();
  auto have = candidate.begin();
  for(; want != folder.end(); ++want, ++have) {
    if(have == candidate.end() || *have != *want) return false;
  }
  return true;
}

// A note as the exporter wants it: its title, its tags, and its Markdown with
// the front matter already off.
//
// The *open* note is taken from the buffer rather than from the disk, because
// the buffer is what the reader is looking at. Exporting the saved copy of a
// note with unsaved edits in it would quietly produce a PDF of something that
// is not on screen.
exporting::PdfNote noteFor(const UiRuntime& ui, const library::NoteListItem& item) {
  exporting::PdfNote note;
  note.title = item.title;
  note.tags = item.tags;
  if(item.id == ui.loadedNoteId) {
    note.body = ui.editor.text();
    return note;
  }
  note.body = ui.state.catalog().loadNote(item.path).body;
  return note;
}

// Called by SDL when the chooser closes, possibly on another thread. It does
// as little as it can: take the path, set the flag, wake the loop.
//
// `userdata` is a heap-allocated `shared_ptr` handed over by `ask`, and this
// owns it: the strong reference is what keeps the state alive if the window
// was closed while the chooser was up. Deleted on every path out, including
// the error and cancel ones -- SDL calls this exactly once.
void chosen(void* userdata, const char* const* files, int) {
  const std::unique_ptr<std::shared_ptr<PdfExportState>> owned(
    static_cast<std::shared_ptr<PdfExportState>*>(userdata));
  PdfExportState* state = owned ? owned->get() : nullptr;
  if(state == nullptr) return;
  // A null list is an error and a list whose first entry is null is a
  // cancellation. Neither is worth telling the reader about: they either know
  // they pressed Cancel, or the desktop has already said what went wrong.
  if(files != nullptr && files[0] != nullptr) {
    {
      const std::lock_guard<std::mutex> guard(state->lock);
      state->path = files[0];
    }
    state->phase.store(PdfExportState::Phase::Chosen, std::memory_order_release);
  } else {
    state->phase.store(PdfExportState::Phase::Cancelled, std::memory_order_release);
  }
  // The loop may be asleep in `SDL_WaitEvent`, and this is the one SDL call
  // documented as safe from any thread.
  SDL_Event wake {};
  wake.type = SDL_EVENT_USER;
  SDL_PushEvent(&wake);
}

// Puts the chooser up for a request that is ready to go.
void ask(UiRuntime& ui, exporting::PdfRequest request, const std::string& what,
         const std::filesystem::path& suggested) {
  PdfExportState& state = *ui.pdfExport;
  if(state.busy()) {
    ui.status = "A PDF export is already waiting for a file name";
    return;
  }
  if(request.notes.empty()) {
    ui.status = "Nothing to export";
    return;
  }
  {
    const std::lock_guard<std::mutex> guard(state.lock);
    state.request = std::move(request);
    state.what = what;
    state.path.clear();
  }
  state.phase.store(PdfExportState::Phase::Asking, std::memory_order_release);
  ui.status = "Exporting " + what + "...";
  const std::string location = suggested.string();
  // A strong reference for the callback to own, because the answer may come
  // back after this shell is gone. `chosen` deletes it.
  auto* owned = new std::shared_ptr<PdfExportState>(ui.pdfExport);
  SDL_ShowSaveFileDialog(&chosen, owned, mainWindow(), kFilters,
                         static_cast<int>(std::size(kFilters)), location.c_str());
}

// `<name>.pdf`, with anything a file name cannot hold taken out of `name`.
std::filesystem::path suggestedFile(const std::filesystem::path& folder, const std::string& name) {
  auto stem = platform::sanitizeFileStem(name);
  if(stem.empty()) stem = "export";
  return folder / (stem + ".pdf");
}

}

std::size_t notesUnderFolder(const UiRuntime& ui, const std::filesystem::path& folder) {
  std::size_t count = 0;
  for(const auto& item : ui.state.catalog().notes()) {
    if(folderContains(folder, item.folder)) ++count;
  }
  return count;
}

void exportNoteToPdf(UiRuntime& ui, std::string_view noteId) {
  const library::NoteListItem* item = ui.state.catalog().noteById(noteId);
  if(item == nullptr) {
    ui.status = "No note to export";
    return;
  }
  exporting::PdfRequest request;
  request.notes.push_back(noteFor(ui, *item));
  request.documentTitle = item->title;
  request.libraryRoot = ui.state.catalog().root();
  request.creationDate = exporting::pdfDate(std::time(nullptr));
  request.wikiLinkResolves = [&ui](std::string_view target) {
    return wikiLinkResolves(ui, target);
  };
  ask(ui, std::move(request), item->title, suggestedFile(item->path.parent_path(), item->title));
}

void exportFolderToPdf(UiRuntime& ui, const std::filesystem::path& folder) {
  const auto& root = ui.state.catalog().root();
  // Everything under the notebook, its sub-notebooks included. `notes()` is
  // already sorted by title, so the pages come out in the order the tree shows
  // them once they are grouped by folder.
  std::vector<const library::NoteListItem*> items;
  for(const auto& item : ui.state.catalog().notes()) {
    if(folderContains(folder, item.folder)) items.push_back(&item);
  }
  std::stable_sort(items.begin(), items.end(),
                   [](const library::NoteListItem* a, const library::NoteListItem* b) {
                     return a->folder < b->folder;
                   });

  exporting::PdfRequest request;
  request.notes.reserve(items.size());
  for(const auto* item : items) request.notes.push_back(noteFor(ui, *item));
  const std::string name =
    folder.empty() ? root.filename().string() : folder.filename().string();
  request.documentTitle = name;
  request.libraryRoot = root;
  request.creationDate = exporting::pdfDate(std::time(nullptr));
  request.wikiLinkResolves = [&ui](std::string_view target) {
    return wikiLinkResolves(ui, target);
  };
  const std::string what =
    std::to_string(items.size()) + (items.size() == 1 ? " note" : " notes");
  ask(ui, std::move(request), what, suggestedFile(root / folder, name));
}

bool applyPendingExport(UiRuntime& ui) {
  PdfExportState& state = *ui.pdfExport;
  const auto phase = state.phase.load(std::memory_order_acquire);
  if(phase == PdfExportState::Phase::Idle || phase == PdfExportState::Phase::Asking) return false;

  if(phase == PdfExportState::Phase::Cancelled) {
    const std::lock_guard<std::mutex> guard(state.lock);
    state.request = {};
    state.what.clear();
    state.phase.store(PdfExportState::Phase::Idle, std::memory_order_release);
    ui.status = "Export cancelled";
    return true;
  }

  // Taken out of the state before the write, so the chooser can be opened
  // again the moment this one is finished rather than after it -- and so the
  // write is not holding the lock the callback thread takes.
  exporting::PdfRequest request;
  std::filesystem::path file;
  std::string what;
  {
    const std::lock_guard<std::mutex> guard(state.lock);
    request = std::move(state.request);
    state.request = {};
    file = state.path;
    what = state.what;
  }
  // A chooser that does not enforce its own filter -- most of them -- hands
  // back whatever was typed, and a PDF called `notes` is a file the desktop
  // will not know how to open.
  if(file.extension() != ".pdf") file += ".pdf";
  state.phase.store(PdfExportState::Phase::Idle, std::memory_order_release);

  const exporting::PdfResult result = exporting::writePdf(request, file);
  if(!result.ok) {
    ui.status = "Export failed: " + result.error;
    return true;
  }
  ui.status = "Exported " + what + " to " + file.filename().string() + " (" +
              std::to_string(result.pages) +
              (result.pages == 1 ? " page)" : " pages)");
  return true;
}

}
