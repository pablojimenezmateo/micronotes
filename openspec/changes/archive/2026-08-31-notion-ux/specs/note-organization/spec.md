## ADDED Requirements

### Requirement: Navigation Tree
The system SHALL present folders and their notes as a single collapsible tree, and SHALL persist which folders are expanded.

#### Scenario: Expand and collapse
- **WHEN** the user activates a folder's disclosure control
- **THEN** the folder's child folders and notes are shown or hidden, and the state is restored on the next launch

#### Scenario: Reparent by dragging
- **WHEN** the user drags a note or a folder onto another folder
- **THEN** the system moves it into that folder on disk and preserves note identity

### Requirement: Note Icons
The system SHALL allow an optional icon per note, stored in the note's front matter.

#### Scenario: Assign an icon
- **WHEN** the user sets an icon for a note
- **THEN** the system writes an `icon` key to that note's front matter and shows the icon in the tree and on the page

#### Scenario: Icon glyphs unavailable
- **WHEN** the text backend cannot render a color emoji glyph
- **THEN** the system renders a monochrome fallback rather than failing

### Requirement: Front Matter Preservation
The system MUST preserve front matter keys it does not recognize when saving a note.

#### Scenario: Unknown key survives a save
- **WHEN** a note's front matter contains a key micronotes does not use, and the user edits and saves the note
- **THEN** that key and its value are still present in the saved file

### Requirement: Command Palette
The system SHALL provide a keyboard-driven palette for opening notes by name and for running application commands.

#### Scenario: Jump to a note
- **WHEN** the user opens the palette and types part of a note title
- **THEN** matching notes are ranked and the chosen note is opened

#### Scenario: Run a command
- **WHEN** the user opens the command palette and types part of a command name
- **THEN** matching commands are listed with their keyboard shortcuts and the chosen command runs

### Requirement: Recoverable Deletion
The system SHALL confirm note and folder deletion and SHALL move deleted items to a trash area inside the library rather than removing them immediately.

#### Scenario: Delete a note
- **WHEN** the user deletes a note and confirms
- **THEN** the note file is moved into the library's trash area and no longer appears in the tree

#### Scenario: Restore a deleted note
- **WHEN** the user restores a note from the trash
- **THEN** the note returns to its previous folder with its identity intact
