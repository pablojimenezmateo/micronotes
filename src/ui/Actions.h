#pragma once

#include <SDL3/SDL_keycode.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace micronotes::ui {

// Everything the shell can be asked to do, named once.
//
// The palette, the shortcut list and the key handler used to be three separate
// tables. Nothing held them together, so an action could be bound to a key and
// missing from the palette, or listed under a key it no longer answered to, and
// the only way to notice was to try it. They are three views of this enum now.
enum class ActionId {
  GoToNote,
  CommandPalette,
  FindInNote,
  SearchAllNotes,
  Shortcuts,
  Settings,

  NewNote,
  NewFolder,
  Save,
  RenameNote,
  SetNoteIcon,
  EditTags,
  ToggleFavorite,
  MoveNote,
  MoveBlocks,
  DeleteNote,
  RenameFolder,
  DeleteFolder,
  RestoreFromTrash,
  RefreshLibrary,

  Bold,
  Italic,
  Code,
  Link,
  Undo,
  Redo,
  ToggleTask,
  DuplicateBlock,
  DeleteBlock,
  MoveBlockUp,
  MoveBlockDown,
  InsertBlock,
  TurnInto,
  Fold,

  PaneLive,
  PaneRaw,
  PaneReading,
  PaneSplit,
  CyclePane,
  ToggleTheme,

  Count
};

// How the shortcut list groups its rows. Ordering here is the order the list
// prints, so a new action lands under a heading rather than at the bottom.
enum class ActionSection {
  Navigation,
  Notes,
  Writing,
  Blocks,
  View,
  Count
};

std::string_view sectionLabel(ActionSection section);

// One key, with its modifiers, as a key handler compares them.
struct KeyChord {
  SDL_Keycode key = SDLK_UNKNOWN;
  // Only Ctrl, Shift and Alt are modelled; Gui is left to the desktop.
  bool ctrl = false;
  bool shift = false;
  bool alt = false;

  friend bool operator==(const KeyChord&, const KeyChord&) = default;
};

// "Ctrl+Shift+P" both ways. One spelling, parsed once, so the palette, the
// shortcut list and any future rebinding UI all print the same string for the
// same keys instead of each writing it out by hand.
std::optional<KeyChord> parseKeyChord(std::string_view chord);
std::string formatKeyChord(const KeyChord& chord);

struct ActionSpec {
  ActionId id = ActionId::Count;
  // Stable name. It is what an overlay result carries back, so it outlives any
  // change to the label.
  std::string_view name;
  // What the user reads. Ends in "..." when choosing it opens something that
  // asks another question.
  std::string_view label;
  // The keys that run it, in the one spelling parseKeyChord understands. Empty
  // for an action reachable only from the palette.
  std::string_view chord;
  // What the shortcut list prints when the action answers to a family of keys
  // rather than a single chord -- "Ctrl+Shift+0..3". Empty means: print `chord`.
  std::string_view keyHint;
  ActionSection section = ActionSection::Navigation;
  // Actions that only make sense with a note open are listed and refused rather
  // than hidden: a palette that changes shape is a palette you cannot learn.
  bool needsNote = false;
  // A few actions are pure editing verbs with no useful palette row -- they act
  // on a selection the palette has just taken the focus away from.
  bool inPalette = true;
};

// Not everything with a key is a command. Walking the sidebar, continuing a
// list, moving by word: these are behaviours of a surface rather than things
// the palette could run, and the shortcut list would be lying by omission
// without them. They are named here so that list has one source too.
struct HelpRow {
  std::string_view keys;
  std::string_view what;
  ActionSection section = ActionSection::Navigation;
};

std::span<const HelpRow> helpRows();

std::span<const ActionSpec> actionSpecs();
const ActionSpec* findAction(ActionId id);
const ActionSpec* findAction(std::string_view name);
// The action a key press runs, or nullptr. Bindings live in one table, so two
// actions cannot quietly claim the same keys.
const ActionSpec* findActionForChord(const KeyChord& chord);

// What the shortcut list and the palette print beside a row.
std::string acceleratorText(const ActionSpec& spec);

// The keys an action answers to, spelled the one way this header spells them.
// Every hint the user reads goes through here rather than repeating the chord,
// so rebinding a key cannot leave a status line or an empty state behind.
std::string keysFor(ActionId id);

}
