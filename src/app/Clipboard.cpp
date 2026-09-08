#include "app/Clipboard.h"

#include "app/Fields.h"
#include "app/Notes.h"
#include "app/Shell.h"
#include "core/attachments/AttachmentService.h"
#include "ui/TextUtil.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

namespace micronotes::app {
namespace {

using micronotes::ui::fileNameForMime;

static bool ensureSelectedNote(UiRuntime& ui) {
  if(!ui.state.hasLibrary()) {
    ui.status = "Open a library before attaching files";
    return false;
  }
  if(ui.state.selection().noteId.empty()) createNote(ui);
  return !ui.state.selection().noteId.empty();
}

static void insertAttachmentMarkdown(UiRuntime& ui, const attachments::AttachmentLink& link) {
  if(ui.editor.text().empty() || ui.editor.text().back() == '\n') ui.editor.insert(link.markdown + "\n");
  else ui.editor.insert("\n" + link.markdown + "\n");
  (void)saveCurrent(ui);
}

}

bool publishEditorPrimarySelection(UiRuntime& ui) {
  if(ui.focus == FocusArea::Editor && ui.editor.hasSelection()) {
    const auto selected = ui.editor.selectedText();
    return SDL_SetPrimarySelectionText(selected.c_str());
  }
  return false;
}

bool attachPathToEditor(UiRuntime& ui, const std::filesystem::path& source) {
  if(!ensureSelectedNote(ui)) return false;
  const auto& selected = ui.state.openNote();
  if(selected.noteId.empty()) return false;
  attachments::AttachmentService service;
  try {
    const auto link = service.attachFile(ui.state.libraryRoot(), selected.metadata.id, source);
    insertAttachmentMarkdown(ui, link);
    ui.status = "Attached " + source.filename().string();
    return true;
  } catch(const std::exception& error) {
    ui.status = "Attach failed: " + std::string(error.what());
    return false;
  }
}

bool pasteClipboardImage(UiRuntime& ui) {
  static constexpr const char* kImageMimes[] = {"image/png", "image/jpeg", "image/jpg", "image/bmp", "image/webp"};
  const char* mime = nullptr;
  for(const char* candidate : kImageMimes) {
    if(SDL_HasClipboardData(candidate)) {
      mime = candidate;
      break;
    }
  }
  if(!mime) return false;
  // Only commit to image handling (which may create a note) once we know the
  // clipboard actually holds image data, so a plain-text paste is never hijacked.
  if(!ensureSelectedNote(ui)) return false;

  size_t size = 0;
  void* data = SDL_GetClipboardData(mime, &size);
  if(!data || size == 0) {
    if(data) SDL_free(data);
    ui.status = "Clipboard image data is empty";
    return true;
  }

  const auto& selected = ui.state.openNote();
  if(selected.noteId.empty()) {
    SDL_free(data);
    return false;
  }

  attachments::AttachmentService service;
  try {
    const auto link = service.attachBytes(ui.state.libraryRoot(), selected.metadata.id, fileNameForMime(mime), data, size);
    SDL_free(data);
    insertAttachmentMarkdown(ui, link);
    ui.status = "Pasted image attachment";
    return true;
  } catch(const std::exception& error) {
    SDL_free(data);
    ui.status = "Paste image failed: " + std::string(error.what());
    return true;
  }
}

namespace {

// Which of X11's two selections a paste is reading.
//
// X11 has two, and they are read by different SDL calls but pasted into the
// same two places -- so this was four functions that differed by exactly which
// pair of calls they made. Four copies of "does it have text, take it, insert
// it, free it" is four places to leak the pointer SDL hands over, and four
// places for the debug line to say something slightly different.
struct Selection {
  const char* what;
  bool (*has)();
  char* (*take)();
};

const Selection kClipboard {"clipboard", &SDL_HasClipboardText, &SDL_GetClipboardText};
const Selection kPrimary {"primary", &SDL_HasPrimarySelectionText, &SDL_GetPrimarySelectionText};

// The text on `from`, or nothing when it holds none.
//
// RAII around SDL's pointer, because the free is the part a fifth copy of this
// would forget: every early return between the take and the free is a leak, and
// there were sixteen of them.
class Pasted {
public:
  explicit Pasted(const Selection& from, const char* into) {
    const bool has = from.has();
    if(inputDebugEnabled()) {
      std::cerr << from.what << " paste " << into << " has_text=" << has << "\n";
    }
    if(!has) return;
    raw_ = from.take();
    if(inputDebugEnabled() && raw_) {
      std::cerr << from.what << " paste " << into << " bytes=" << std::strlen(raw_) << "\n";
    }
  }
  ~Pasted() {
    if(raw_) SDL_free(raw_);
  }
  Pasted(const Pasted&) = delete;
  Pasted& operator=(const Pasted&) = delete;

  explicit operator bool() const { return raw_ != nullptr; }
  const char* text() const { return raw_; }

private:
  char* raw_ = nullptr;
};

// Into the note's buffer.
bool pasteIntoNote(UiRuntime& ui, const Selection& from) {
  const Pasted pasted(from, "editor");
  if(!pasted) return false;
  ui.editor.insert(pasted.text());
  ui.markEdited();
  return true;
}

// Into whichever one-line field has the keyboard, at the caret and replacing
// the selection -- rather than appended to the end of the field regardless of
// where the reader was working.
bool pasteIntoField(UiRuntime& ui, const Selection& from) {
  auto* input = focusedField(ui);
  if(!input) {
    if(inputDebugEnabled()) std::cerr << from.what << " paste input: no field has focus\n";
    return false;
  }
  const Pasted pasted(from, "input");
  if(!pasted) return false;
  input->editor.insert(pasted.text());
  syncFocusedInput(ui);
  return true;
}

}

bool pasteClipboardText(UiRuntime& ui) {
  return pasteIntoNote(ui, kClipboard);
}

bool pastePrimarySelectionText(UiRuntime& ui) {
  return pasteIntoNote(ui, kPrimary);
}

bool pasteClipboardIntoInput(UiRuntime& ui) {
  return pasteIntoField(ui, kClipboard);
}

bool pastePrimarySelectionIntoInput(UiRuntime& ui) {
  return pasteIntoField(ui, kPrimary);
}

}
