#include "ui/Actions.h"

#include "CoreAliases.h"

#include "core/util/StringUtil.h"

#include <array>

namespace micronotes::ui {
namespace {

using S = ActionSection;

// The registry. One row per ActionId, in the order the shortcut list prints
// them. ArchitectureTests checks that every id appears exactly once, so an id
// added to the enum and forgotten here fails the build rather than showing up
// as a blank row.
constexpr std::array<ActionSpec, static_cast<std::size_t>(ActionId::Count)> kSpecs {{
  {ActionId::GoToNote,        "jump",           "Go to note...",                   "Ctrl+P",       "",              S::Navigation, false, true, "Ctrl+O", true},
  {ActionId::CommandPalette,  "command-palette","Commands...",                     "Ctrl+Shift+P", "",              S::Navigation, false, false, "", true},
  {ActionId::FindInNote,      "find",           "Find in this note",               "Ctrl+F",       "",              S::Navigation, true,  true, "", true},
  // F3 and Shift+F3 step whatever the find bar is holding, from anywhere --
  // including from inside the field, which is why they are bindings rather than
  // branches in the bar's own key handler. With the bar shut they do nothing,
  // and they are listed rather than hidden for the reason `needsNote` gives.
  {ActionId::FindNext,        "find-next",      "Next match",                      "F3",           "",              S::Navigation, true,  true, "", true},
  {ActionId::FindPrevious,    "find-previous",  "Previous match",                  "Shift+F3",     "",              S::Navigation, true,  true, "", true},
  // The find bar's two toggles. They are in the registry so that the chord is
  // spelled once -- the bar's own tooltips and the shortcut list both read it
  // from here -- and out of the palette because a toggle whose state you cannot
  // see is a row that tells you nothing: they belong to the bar, beside the
  // button that shows whether they are on. `keyRunsIt` is false for the same
  // reason `Ctrl+B` is: Alt+C means this only while the bar has the keyboard.
  {ActionId::FindMatchCase,   "find-match-case", "Find: match case",               "Alt+C",        "",              S::Navigation, true,  false, "", false},
  {ActionId::FindWholeWord,   "find-whole-word", "Find: whole word",               "Alt+W",        "",              S::Navigation, true,  false, "", false},
  {ActionId::SearchAllNotes,  "search",         "Search every note",               "Ctrl+Shift+F", "",              S::Navigation, false, true, "", true},
  {ActionId::Shortcuts,       "shortcuts",      "Keyboard shortcuts...",           "F1",           "",              S::Navigation, false, true, "", true},
  {ActionId::Settings,        "settings",       "Settings...",                     "Ctrl+,",       "",              S::Navigation, false, true, "", true},
  {ActionId::About,           "about",          "About micronotes",                "",             "",              S::Navigation, false, true, "", true},
  {ActionId::Quit,            "quit",           "Quit micronotes",                 "Ctrl+Q",       "",              S::Navigation, false, true, "", true},

  {ActionId::NewNote,         "new-note",       "New note",                        "Ctrl+N",       "",              S::Notes, false, true, "", true},
  {ActionId::NewFolder,       "new-folder",     "New notebook",                    "",             "",              S::Notes, false, true, "", true},
  {ActionId::Save,            "save",           "Save note",                       "Ctrl+S",       "",              S::Notes, true,  true, "", true},
  {ActionId::RenameNote,      "rename",         "Rename note...",                  "F2",           "",              S::Notes, true,  true, "", true},
  {ActionId::SetNoteIcon,     "icon",           "Set note icon...",                "",             "",              S::Notes, true,  true, "", true},
  {ActionId::EditTags,        "tags",           "Edit tags...",                    "Ctrl+T",       "",              S::Notes, true,  true, "", true},
  {ActionId::PinNote,         "pin-note",       "Pin or unpin this note",          "",             "",              S::Notes, true,  true, "", true},
  {ActionId::MoveNote,        "move-note",      "Move note to notebook...",        "",             "",              S::Notes, true,  true, "", true},
  // The note is a file, and these three are the questions a reader asks about
  // one. Ported from the sibling microide, whose file tree and tab strip both
  // carry them; there is no chord because none of the three is something you
  // reach for mid-sentence, and inventing one would spend a key nobody wanted.
  {ActionId::ShowOnDisk,      "show-on-disk",   "Show on disk",                    "",             "",              S::Notes, true,  true, "", true},
  {ActionId::CopyRelativePath, "copy-relative-path", "Copy relative path",         "",             "",              S::Notes, true,  true, "", true},
  {ActionId::CopyAbsolutePath, "copy-absolute-path", "Copy absolute path",         "",             "",              S::Notes, true,  true, "", true},
  {ActionId::MoveBlocks,      "move-blocks",    "Move blocks to another note...",  "",             "",              S::Notes, true,  true, "", true},
  // Both exports. They were reachable from the tree's two context menus and
  // nowhere else, on the reasoning that an export is about a thing in the
  // library rather than about whatever is showing -- but the Note menu is
  // *entirely* about the note in the library that is selected, which is the
  // same note the context menu's row exports, and it already carries "Show on
  // disk" and the two copy-path rows on exactly that basis. So the rule was
  // not the one the bar follows, and the only thing it bought was a command
  // you had to already know to right-click for.
  //
  // The notebook one needs no note and no folder: with nothing selected the
  // root is the folder, which means the library, and that is what the tree's
  // own row does on the root.
  {ActionId::ExportNotePdf,   "export-note-pdf", "Export note as PDF...",          "",             "",              S::Notes, true,  true, "", true},
  {ActionId::ExportFolderPdf, "export-folder-pdf", "Export notebook as PDF...",    "",             "",              S::Notes, false, true, "", true},
  {ActionId::DeleteNote,      "delete-note",    "Delete note...",                  "",             "",              S::Notes, true,  true, "", true},
  {ActionId::RenameFolder,    "rename-folder",  "Rename notebook...",              "",             "",              S::Notes, false, true, "", true},
  {ActionId::DeleteFolder,    "delete-folder",  "Delete notebook...",              "",             "",              S::Notes, false, true, "", true},
  {ActionId::RestoreFromTrash,"restore",        "Restore from trash...",           "",             "",              S::Notes, false, true, "", true},
  {ActionId::RefreshLibrary,  "refresh",        "Refresh library",                 "Ctrl+R",       "",              S::Notes, false, true, "", true},

  // The editing verbs act on what is selected, and opening the palette takes
  // the selection's focus away, so they are shortcuts and help rows only.
  {ActionId::Bold,            "bold",           "Bold",                            "Ctrl+B",       "",              S::Writing, true, false, "", true},
  {ActionId::Italic,          "italic",         "Italic",                          "Ctrl+I",       "",              S::Writing, true, false, "", true},
  {ActionId::Code,            "code",           "Inline code",                     "Ctrl+E",       "",              S::Writing, true, false, "", true},
  {ActionId::Link,            "link",           "Link the selection",              "Ctrl+K",       "",              S::Writing, true, false, "", false},
  {ActionId::Undo,            "undo",           "Undo",                            "Ctrl+Z",       "",              S::Writing, true, false, "", false},
  {ActionId::Redo,            "redo",           "Redo",                            "Ctrl+Y",       "",              S::Writing, true, false, "", false},
  // The five verbs that mean "do this to whatever has the keyboard". They were
  // help rows and key-handler branches and nothing else, which made them the
  // one group of editing commands the Edit menu could not offer -- and an Edit
  // menu without Cut, Copy and Paste is the first place a reader looks and does
  // not find them. `app/FocusedEdits.h` had already been split out of the key
  // router for exactly this reason, one release earlier, for Undo and Redo.
  //
  // `keyRunsIt` false for the same reason theirs is: the chord means the note
  // buffer, a block selection in it, or whichever one-line field is focused,
  // and only the key handler can see which. `inPalette` false for the same
  // reason `Bold` is: opening the palette takes away the very selection they
  // act on.
  {ActionId::Cut,             "cut",            "Cut",                             "Ctrl+X",       "",              S::Writing, false, false, "", false},
  {ActionId::Copy,            "copy",           "Copy",                            "Ctrl+C",       "",              S::Writing, false, false, "", false},
  {ActionId::Paste,           "paste",          "Paste",                           "Ctrl+V",       "",              S::Writing, false, false, "", false},
  // Shift forces text even when the clipboard also carries an image, which is
  // the opposite of what the shortcut list used to say it did -- it was printed
  // as "paste an image as an attachment", and an ordinary Ctrl+V is what does
  // that. Reading the label off the registry is what stops the two disagreeing
  // again.
  {ActionId::PastePlain,      "paste-plain",    "Paste as plain text",             "Ctrl+Shift+V", "",              S::Writing, false, false, "", false},
  {ActionId::SelectAll,       "select-all",     "Select everything",               "Ctrl+A",       "",              S::Writing, false, false, "", false},
  {ActionId::ToggleTask,      "toggle-task",    "Tick or untick a task",           "Ctrl+Enter",   "",              S::Writing, true, false, "", false},

  {ActionId::DuplicateBlock,  "duplicate-block","Duplicate the block",             "Ctrl+D",       "",              S::Blocks, true, false, "", false},
  {ActionId::DeleteBlock,     "delete-block",   "Delete the block",                "Ctrl+Shift+D", "",              S::Blocks, true, false, "", false},
  {ActionId::MoveBlockUp,     "move-block-up",  "Move the block up",               "Alt+Up",       "",              S::Blocks, true, false, "", false},
  {ActionId::MoveBlockDown,   "move-block-down","Move the block down",             "Alt+Down",     "",              S::Blocks, true, false, "", false},
  {ActionId::InsertBlock,     "insert-block",   "Insert a block",                  "",             "/",             S::Blocks, true, false, "", false},
  {ActionId::TurnInto,        "turn-into",      "Turn the block into...",          "",             "",S::Blocks, true, false, "", false},

  {ActionId::PaneRaw,         "pane-raw",       "View: raw Markdown",              "Ctrl+1",       "",              S::View, false, true, "", true},
  {ActionId::PaneReading,     "pane-reading",   "View: reading",                   "Ctrl+2",       "",              S::View, false, true, "", true},
  {ActionId::PaneSplit,       "pane-split",     "View: split",                     "Ctrl+3",       "",              S::View, false, true, "", true},
  {ActionId::CyclePane,       "cycle-pane",     "Cycle the three views",           "Ctrl+L",       "",              S::View, false, true, "", true},
  {ActionId::ToggleTheme,     "theme",          "Toggle light and dark",           "Ctrl+Shift+L", "",              S::View, false, true, "", true},
  {ActionId::ToggleSidebar,   "toggle-sidebar", "Show or hide the sidebar",        "Ctrl+Alt+Left","",              S::View, false, true, "", true},
  {ActionId::ToggleRightPanel,"toggle-right",   "Show or hide the outline panel",  "Ctrl+Alt+Right","",             S::View, false, true, "", true},
  {ActionId::NextTab,         "next-tab",       "Next tab",                        "Ctrl+Tab",     "",              S::View, false, true, "", true},
  {ActionId::PreviousTab,     "previous-tab",   "Previous tab",                    "Ctrl+Shift+Tab","",             S::View, false, true, "", true},
  {ActionId::CloseTab,        "close-tab",      "Close this tab",                  "Ctrl+W",       "",              S::View, true,  true, "", true},
  // The four bulk closes. `needsNote` for all of them: with nothing open there
  // is no tab for "other", "right" or "left" to be measured from, and each is
  // listed and refused rather than hidden for the reason the flag's comment
  // gives. Only `close-others` gets a chord -- the strip's own menu is where
  // the other three are reached, and a key apiece would be three bindings
  // spent on gestures nobody reaches for mid-sentence.
  {ActionId::CloseOtherTabs,  "close-other-tabs", "Close other tabs",              "Ctrl+Alt+W",   "",              S::View, true,  true, "", true},
  {ActionId::CloseTabsToRight,"close-tabs-right", "Close tabs to the right",       "",             "",              S::View, true,  true, "", true},
  {ActionId::CloseTabsToLeft, "close-tabs-left",  "Close tabs to the left",        "",             "",              S::View, true,  true, "", true},
  {ActionId::CloseAllTabs,    "close-all-tabs",   "Close all tabs",                "",             "",              S::View, true,  true, "", true},
  {ActionId::OpenInNewTab,    "new-tab",        "Open a note in a new tab...",     "Ctrl+Shift+T", "",              S::View, false, true, "", true},
  {ActionId::PinTab,          "pin-tab",        "Pin or unpin this tab",           "",             "",              S::View, true,  true, "", true},
  {ActionId::CycleRightPanel, "cycle-right",    "Outline, links or tags",      "Ctrl+Alt+Up",  "",              S::View, false, true, "", true},
}};

// Named keys, so a chord can say "Enter" rather than a keycode. Only the keys
// the registry actually binds are listed; anything else is a single character.
struct NamedKey {
  std::string_view name;
  SDL_Keycode key;
};

constexpr NamedKey kNamedKeys[] = {
  {"Enter", SDLK_RETURN},
  {"Tab", SDLK_TAB},
  {"Space", SDLK_SPACE},
  {"Escape", SDLK_ESCAPE},
  {"Backspace", SDLK_BACKSPACE},
  {"Delete", SDLK_DELETE},
  {"Left", SDLK_LEFT},
  {"Right", SDLK_RIGHT},
  {"Up", SDLK_UP},
  {"Down", SDLK_DOWN},
  {"Home", SDLK_HOME},
  {"End", SDLK_END},
  {"PageUp", SDLK_PAGEUP},
  {"PageDown", SDLK_PAGEDOWN},
  {"F1", SDLK_F1},
  {"F2", SDLK_F2},
  {"F3", SDLK_F3},
};


}

std::string_view sectionLabel(ActionSection section) {
  switch(section) {
    case ActionSection::Navigation: return "Getting around";
    case ActionSection::Notes: return "Notes and notebooks";
    case ActionSection::Writing: return "Writing";
    case ActionSection::Blocks: return "Blocks";
    case ActionSection::View: return "View";
    case ActionSection::Count: break;
  }
  return "";
}

std::optional<KeyChord> parseKeyChord(std::string_view text) {
  if(text.empty()) return std::nullopt;
  KeyChord chord;
  std::string_view rest = text;
  // Modifiers come first and in any order; the last token is the key itself.
  // A trailing "+" is a chord on the plus key, not a missing token, so the
  // search stops before the final character.
  while(true) {
    const auto plus = rest.substr(0, rest.size() - 1).find('+');
    if(plus == std::string_view::npos) break;
    const std::string_view token = rest.substr(0, plus);
    if(util::equalsIgnoringAsciiCase(token, "Ctrl")) chord.ctrl = true;
    else if(util::equalsIgnoringAsciiCase(token, "Shift")) chord.shift = true;
    else if(util::equalsIgnoringAsciiCase(token, "Alt")) chord.alt = true;
    else return std::nullopt;
    rest = rest.substr(plus + 1);
  }
  if(rest.empty()) return std::nullopt;
  for(const auto& named : kNamedKeys) {
    if(util::equalsIgnoringAsciiCase(rest, named.name)) {
      chord.key = named.key;
      return chord;
    }
  }
  if(rest.size() != 1) return std::nullopt;
  // Keycodes are the unshifted character, so "Ctrl+P" and "Ctrl+p" are one
  // chord and both spell the keycode 'p'.
  chord.key = static_cast<SDL_Keycode>(util::toLowerAscii(rest[0]));
  return chord;
}

std::string formatKeyChord(const KeyChord& chord) {
  if(chord.key == SDLK_UNKNOWN) return "";
  std::string out;
  if(chord.ctrl) out += "Ctrl+";
  if(chord.shift) out += "Shift+";
  if(chord.alt) out += "Alt+";
  for(const auto& named : kNamedKeys) {
    if(named.key == chord.key) {
      out += named.name;
      return out;
    }
  }
  // Printed uppercase because that is how a keyboard is labelled, even though
  // the keycode is the lowercase character.
  out += util::toUpperAscii(static_cast<char>(chord.key));
  return out;
}

constexpr HelpRow kHelpRows[] = {
  {"Ctrl+O", "Go to note, the other way round", S::Navigation},
  {"Enter, Shift+Enter", "Step through find matches, in the find bar", S::Navigation},
  {"Up, Down", "Walk the sidebar", S::Navigation},
  {"Right, Left", "Open or close a notebook", S::Navigation},
  {"Esc", "Close a dialog, or clear the search", S::Navigation},

  {"Ctrl+Left, Ctrl+Right", "Move by word", S::Writing},
  {"Ctrl+Home, Ctrl+End", "Start and end of the note", S::Writing},
  {"PageUp, PageDown", "Move by a screenful", S::Writing},
  {"Tab, Shift+Tab", "Indent, outdent a list item", S::Writing},
  {"Enter", "Continue the list, or leave it when empty", S::Writing},

  {"Esc", "Select the block, again to go back", S::Blocks},
  {"Shift+Up, Shift+Down", "Extend the block selection", S::Blocks},
};

std::span<const HelpRow> helpRows() {
  return {kHelpRows, sizeof(kHelpRows) / sizeof(kHelpRows[0])};
}

std::span<const ActionSpec> actionSpecs() {
  return {kSpecs.data(), kSpecs.size()};
}

const ActionSpec* findAction(ActionId id) {
  for(const auto& spec : kSpecs) {
    if(spec.id == id) return &spec;
  }
  return nullptr;
}

const ActionSpec* findAction(std::string_view name) {
  if(name.empty()) return nullptr;
  for(const auto& spec : kSpecs) {
    if(spec.name == name) return &spec;
  }
  return nullptr;
}

const ActionSpec* findActionForChord(const KeyChord& chord) {
  for(const auto& spec : kSpecs) {
    const auto bound = parseKeyChord(spec.chord);
    if(bound && *bound == chord) return &spec;
    const auto alias = parseKeyChord(spec.altChord);
    if(alias && *alias == chord) return &spec;
  }
  return nullptr;
}

namespace {

// The scancode a chord's key sits at on the layout the chords were written for.
//
// A chord names a *character* -- "Ctrl+S" -- and on a layout where the key in
// that position produces something else, matching the keycode alone means the
// shortcut is simply gone. The letters and digits are contiguous in both
// enumerations, which is what makes this arithmetic rather than a table.
//
// Unknown for anything else, which is the right answer: `F1` and `Ctrl+Tab`
// already *are* physical keys, and there is nothing to fall back to.
SDL_Scancode writtenScancodeFor(SDL_Keycode key) {
  if(key >= SDLK_A && key <= SDLK_Z) {
    return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (key - SDLK_A));
  }
  if(key >= SDLK_1 && key <= SDLK_9) {
    return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (key - SDLK_1));
  }
  if(key == SDLK_0) return SDL_SCANCODE_0;
  if(key == SDLK_COMMA) return SDL_SCANCODE_COMMA;
  if(key == SDLK_PERIOD) return SDL_SCANCODE_PERIOD;
  return SDL_SCANCODE_UNKNOWN;
}

}

const ActionSpec* findActionForKey(SDL_Keycode key, SDL_Scancode scancode, bool ctrl, bool shift,
                                   bool alt) {
  if(const auto* spec = findActionForChord({key, ctrl, shift, alt})) return spec;
  if(scancode == SDL_SCANCODE_UNKNOWN) return nullptr;
  for(const auto& spec : kSpecs) {
    for(const auto& written : {spec.chord, spec.altChord}) {
      const auto bound = parseKeyChord(written);
      if(!bound) continue;
      if(bound->ctrl != ctrl || bound->shift != shift || bound->alt != alt) continue;
      if(writtenScancodeFor(bound->key) == scancode) return &spec;
    }
  }
  return nullptr;
}

std::string keysFor(ActionId id) {
  const auto* spec = findAction(id);
  return spec ? acceleratorText(*spec) : std::string();
}

std::string acceleratorText(const ActionSpec& spec) {
  if(!spec.keyHint.empty()) return std::string(spec.keyHint);
  const auto chord = parseKeyChord(spec.chord);
  return chord ? formatKeyChord(*chord) : std::string();
}

}
