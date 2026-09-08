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

bool pasteClipboardText(UiRuntime& ui) {
  const bool hasText = SDL_HasClipboardText();
  if(inputDebugEnabled()) {
    std::cerr << "clipboard paste editor"
              << " has_text=" << hasText
              << " has_primary=" << SDL_HasPrimarySelectionText()
              << "\n";
  }
  if(!hasText) return false;
  char* raw = SDL_GetClipboardText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "clipboard paste editor bytes=" << std::strlen(raw) << "\n";
  ui.editor.insert(raw);
  ui.markEdited();
  SDL_free(raw);
  return true;
}

bool pasteClipboardIntoInput(UiRuntime& ui) {
  auto* input = focusedField(ui);
  const bool hasText = SDL_HasClipboardText();
  if(inputDebugEnabled()) {
    std::cerr << "clipboard paste input"
              << " input=" << (input != nullptr)
              << " has_text=" << hasText
              << " has_primary=" << SDL_HasPrimarySelectionText()
              << "\n";
  }
  if(!input || !hasText) return false;
  char* raw = SDL_GetClipboardText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "clipboard paste input bytes=" << std::strlen(raw) << "\n";
  // Lands at the caret and replaces the selection, rather than being appended
  // to the end of the field regardless of where the user was working.
  input->editor.insert(raw);
  SDL_free(raw);
  syncFocusedInput(ui);
  return true;
}

bool pastePrimarySelectionText(UiRuntime& ui) {
  const bool hasPrimary = SDL_HasPrimarySelectionText();
  if(inputDebugEnabled()) {
    std::cerr << "primary paste editor"
              << " has_primary=" << hasPrimary
              << " has_clipboard=" << SDL_HasClipboardText()
              << "\n";
  }
  if(!hasPrimary) return false;
  char* raw = SDL_GetPrimarySelectionText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "primary paste editor bytes=" << std::strlen(raw) << "\n";
  ui.editor.insert(raw);
  ui.markEdited();
  SDL_free(raw);
  return true;
}

bool pastePrimarySelectionIntoInput(UiRuntime& ui) {
  auto* input = focusedField(ui);
  const bool hasPrimary = SDL_HasPrimarySelectionText();
  if(inputDebugEnabled()) {
    std::cerr << "primary paste input"
              << " input=" << (input != nullptr)
              << " has_primary=" << hasPrimary
              << " has_clipboard=" << SDL_HasClipboardText()
              << "\n";
  }
  if(!input || !hasPrimary) return false;
  char* raw = SDL_GetPrimarySelectionText();
  if(!raw) return false;
  if(inputDebugEnabled()) std::cerr << "primary paste input bytes=" << std::strlen(raw) << "\n";
  input->editor.insert(raw);
  SDL_free(raw);
  syncFocusedInput(ui);
  return true;
}

}
