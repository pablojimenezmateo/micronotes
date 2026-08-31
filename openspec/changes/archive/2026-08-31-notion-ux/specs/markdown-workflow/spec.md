## MODIFIED Requirements

### Requirement: Raw Markdown Editor
The system SHALL provide a raw Markdown text editing mode for note bodies, selectable by the user, in addition to the live editing surface.

#### Scenario: Edit note text as raw Markdown
- **WHEN** the user switches to raw mode and types in the editor pane
- **THEN** the system updates the note buffer as raw Markdown text and can save it to the note's `.md` file

#### Scenario: Escape hatch for unmodelled Markdown
- **WHEN** a note contains Markdown that the live editing surface does not model
- **THEN** the user can switch to raw mode and edit the source directly

## ADDED Requirements

### Requirement: Live Markdown Editing Surface
The system SHALL provide a live editing surface, used by default, in which Markdown formatting is rendered in place while the note remains editable.

#### Scenario: Formatting renders while editing
- **WHEN** the user views a note in the live surface with the caret outside a given block
- **THEN** that block's headings, emphasis, strong text, code spans, links, list markers, and task checkboxes are displayed as rendered formatting rather than as syntax characters

#### Scenario: Markers reveal at the caret
- **WHEN** the caret is inside a block
- **THEN** that block's Markdown syntax markers are shown so the user can edit them directly

#### Scenario: Unmodelled block types
- **WHEN** a block is a table, an HTML block, or a footnote definition
- **THEN** the live surface renders it through the `md4c` render model as read-only content and allows the user to edit it as raw text on demand

### Requirement: Source Fidelity
The live editing surface MUST NOT modify any part of the note buffer that the user did not edit. Saving a note SHALL NOT reformat, normalize, or reserialize unedited Markdown.

#### Scenario: Open and save without editing
- **WHEN** the user opens a note in the live surface and saves it without typing
- **THEN** the `.md` file on disk is byte-for-byte unchanged

#### Scenario: Edit one block
- **WHEN** the user edits a single block and the note is saved
- **THEN** only the bytes belonging to that block differ from the previous file contents

### Requirement: Block Interactions
The system SHALL let the user act on individual blocks through direct manipulation.

#### Scenario: Reorder a block
- **WHEN** the user drags a block's gutter handle to another position
- **THEN** the system moves that block's source lines to the new position as a single undoable edit

#### Scenario: Insert a block from the slash menu
- **WHEN** the user types `/` in an empty block and chooses a block type
- **THEN** the system replaces the block with the chosen block type's Markdown

#### Scenario: Change a block's type
- **WHEN** the user chooses "turn into" for a block and selects another type
- **THEN** the system rewrites that block's leading markers and preserves its content

#### Scenario: Format a selection
- **WHEN** the user selects text and applies bold, italic, code, strikethrough, or a link
- **THEN** the system wraps the selected source range in the corresponding Markdown markers

### Requirement: Typing Shortcuts And List Behavior
The system SHALL convert Markdown shorthand as it is typed and SHALL continue list structure automatically.

#### Scenario: Shorthand converts a block
- **WHEN** the user types `# `, `- `, `1. `, `[] `, `> `, or a code fence at the start of an empty block
- **THEN** the system converts the block to the corresponding block type

#### Scenario: Continue a list
- **WHEN** the user presses Enter at the end of a non-empty list item
- **THEN** the system starts a new list item at the same depth

#### Scenario: Exit a list
- **WHEN** the user presses Enter in an empty list item
- **THEN** the system removes the list marker and leaves a paragraph

#### Scenario: Change list depth
- **WHEN** the user presses Tab or Shift+Tab in a list item
- **THEN** the system indents or outdents that item and its children

### Requirement: Editor Keyboard Coverage
The editing surface SHALL support word-wise caret movement, keyboard selection, and page navigation.

#### Scenario: Move and select by word
- **WHEN** the user presses Ctrl+Left or Ctrl+Right, with or without Shift
- **THEN** the caret moves by one word and the selection extends when Shift is held

#### Scenario: Page and document navigation
- **WHEN** the user presses PageUp, PageDown, Ctrl+Home, or Ctrl+End
- **THEN** the caret and viewport move accordingly

### Requirement: Undo Granularity
The system SHALL group a continuous run of typing into a single undo step, and SHALL start a new undo step at any structural block edit.

#### Scenario: Undo a typing run
- **WHEN** the user types a word and presses Ctrl+Z once
- **THEN** the whole typing run is undone rather than a single character
