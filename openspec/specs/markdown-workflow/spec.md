# markdown-workflow Specification

## Purpose
TBD - created by archiving change bootstrap-micronotes. Update Purpose after archive.
## Requirements
### Requirement: Raw Markdown Editor
The system SHALL provide a raw Markdown text editing mode for note bodies, which is where the user types.

#### Scenario: Edit note text as raw Markdown
- **WHEN** the user types in the editor pane
- **THEN** the system updates the note buffer as raw Markdown text and can save it to the note's `.md` file

#### Scenario: Every construct is editable
- **WHEN** a note contains Markdown the rendered view does not model
- **THEN** the user can still edit its source directly, because the source is the editing surface

### Requirement: Rendered Markdown Viewer
The system SHALL provide a rendered Markdown viewer based on vendored `md4c` parsing and native SDL rendering.

#### Scenario: View rendered note
- **WHEN** the user opens a Markdown note in viewer mode
- **THEN** headings, paragraphs, emphasis, code spans, fenced code blocks, blockquotes, lists, links, tables where supported by `md4c`, and images are displayed as rendered content

### Requirement: Split Pane Mode
The system SHALL support editor-only, viewer-only, and split editor/viewer modes.

#### Scenario: Enable split pane
- **WHEN** the user switches to split mode
- **THEN** the raw Markdown editor and rendered viewer are visible side by side for the same note

#### Scenario: Edit in split pane
- **WHEN** the user edits Markdown in split mode
- **THEN** the viewer refreshes to reflect saved or debounced buffer changes without blocking text input

### Requirement: Constrained Markdown Scope
The system SHALL NOT execute or render Mermaid, math engines, scripts, custom plugins, remote embeds, or webview-only Markdown extensions.

#### Scenario: Mermaid block present
- **WHEN** a note contains a Mermaid fenced code block
- **THEN** the system displays it as a fenced code block and does not render a diagram

### Requirement: No Remote Resource Loading
The Markdown viewer MUST NOT fetch remote images, scripts, stylesheets, or other network resources.

#### Scenario: Remote image link
- **WHEN** a note contains an image link with an `http` or `https` URL
- **THEN** the system does not download the image and instead presents the link as unavailable or ordinary link text

### Requirement: Note Area Arrangements
The system SHALL offer the note area in three arrangements -- raw source, a rendered read-only view, and the two side by side -- and SHALL default to the side-by-side arrangement.

#### Scenario: Switch arrangement
- **WHEN** the user chooses the raw, reading, or split arrangement
- **THEN** the note area shows that arrangement, and the choice belongs to the tab rather than to the window

#### Scenario: Formatting renders in the reading view
- **WHEN** the user views a note in the reading view
- **THEN** its headings, emphasis, strong text, code spans, links, list markers, and task checkboxes are displayed as rendered formatting rather than as syntax characters

#### Scenario: Markers keep their offsets
- **WHEN** a block's Markdown syntax markers are hidden in the reading view
- **THEN** they still occupy their source offsets, so a selection covers them and a click maps to a position between them and the text

#### Scenario: Unmodelled block types
- **WHEN** a block is a table, an HTML block, or a footnote definition
- **THEN** the reading view renders it through the `md4c` render model

### Requirement: Source Fidelity
Editing MUST NOT modify any part of the note buffer that the user did not edit. Saving a note SHALL NOT reformat, normalize, or reserialize unedited Markdown.

#### Scenario: Open and save without editing
- **WHEN** the user opens a note and saves it without typing
- **THEN** the `.md` file on disk is byte-for-byte unchanged

#### Scenario: Edit one block
- **WHEN** the user edits a single block and the note is saved
- **THEN** only the bytes belonging to that block differ from the previous file contents

### Requirement: Block Interactions
The system SHALL let the user act on individual blocks through direct manipulation.

#### Scenario: Reorder a block
- **WHEN** the user moves the block under the caret past its neighbour
- **THEN** the system moves that block's source lines to the new position as a single undoable edit

#### Scenario: A command covers the selection
- **WHEN** the user selects text spanning several blocks and runs a block command
- **THEN** the command applies to every block the selection covers rather than only the one holding the caret

#### Scenario: Insert a block from the slash menu
- **WHEN** the user types `/` at the start of a block, or runs the insert-block command, and chooses a block type
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

