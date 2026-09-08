#include "app/Fields.h"

#include "app/Notes.h"
#include "app/Prompts.h"
#include "app/Shell.h"

#include <algorithm>
#include <array>
#include <string>

namespace micronotes::app {
namespace {

// The five fields, once. Every question any surface asks about "the field the
// focus is in" is a lookup here.
constexpr std::array<FieldSpec, 5> kFields {{
  {FocusArea::Search, &TextFields::search, nullptr},
  {FocusArea::Find, &TextFields::find, nullptr},
  {FocusArea::TagEditor, &TextFields::tag, saveTags},
  {FocusArea::RenameNote, &TextFields::rename, saveRename},
  {FocusArea::RenameFolder, &TextFields::folderRename, saveFolderRename},
}};

const FieldSpec* specFor(FocusArea focus) {
  for(const auto& spec : kFields) {
    if(spec.focus == focus) return &spec;
  }
  return nullptr;
}

}

std::span<const FieldSpec> fieldSpecs() {
  return kFields;
}

editor::TextField* focusedField(UiRuntime& ui) {
  const FieldSpec* spec = specFor(ui.focus);
  return spec ? &(ui.fields.*(spec->member)) : nullptr;
}

const editor::TextField* focusedField(const UiRuntime& ui) {
  const FieldSpec* spec = specFor(ui.focus);
  return spec ? &(ui.fields.*(spec->member)) : nullptr;
}

void (*fieldCommit(FocusArea focus))(UiRuntime&) {
  const FieldSpec* spec = specFor(focus);
  return spec ? spec->commit : nullptr;
}

void syncFocusedInput(UiRuntime& ui) {
  if(ui.focus == FocusArea::Search) {
    ui.state.setSearch(ui.fields.search.text(), ui.fields.searchScope);
    selectNoteAt(ui, 0);
  } else if(ui.focus == FocusArea::Find) {
    updateFindStatus(ui);
  }
}

void updateFindStatus(UiRuntime& ui) {
  const std::string& needle = ui.fields.find.text();
  if(needle.empty()) {
    ui.status = "Find in note";
    return;
  }
  std::size_t count = 0;
  std::size_t pos = ui.editor.text().find(needle);
  while(pos != std::string::npos) {
    ++count;
    pos = ui.editor.text().find(needle, pos + std::max<std::size_t>(1, needle.size()));
  }
  ui.status = std::to_string(count) + " matches in note";
}

// Focusing a search field is an action like any other, so the key and the
// palette row run the same code rather than two copies that drift.
void focusFindInNote(UiRuntime& ui) {
  ui.focus = FocusArea::Find;
  ui.fields.find.editor.selectAll();
  updateFindStatus(ui);
}

void focusSearchAllNotes(UiRuntime& ui) {
  if(ui.editor.dirty() && !ui.state.selection().noteId.empty() && !saveCurrent(ui)) return;
  ui.focus = FocusArea::Search;
  ui.fields.search.editor.selectAll();
  ui.state.setSearch(ui.fields.search.text(), ui.fields.searchScope);
  ui.status = "Search all notes";
}

}
