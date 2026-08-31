# note-organization Specification

## Purpose
TBD - created by archiving change bootstrap-micronotes. Update Purpose after archive.
## Requirements
### Requirement: Folder Organization
The system SHALL present library directories as folders/notebooks for organizing notes.

#### Scenario: Folder tree shown
- **WHEN** the user opens a library containing nested note folders
- **THEN** the sidebar displays the folder hierarchy and allows selecting a folder to filter notes

### Requirement: Tag Assignment
The system SHALL allow notes to have zero or more tags and SHALL persist those tags in the note library metadata.

#### Scenario: Add tag to note
- **WHEN** the user adds a tag to a note
- **THEN** the tag is stored durably and appears in tag filters after restarting the application

#### Scenario: Remove tag from note
- **WHEN** the user removes a tag from a note
- **THEN** the note no longer appears when filtering by that tag

### Requirement: Note Listing
The system SHALL show a note list for the current folder, tag filter, or search result.

#### Scenario: Select folder
- **WHEN** the user selects a folder in the sidebar
- **THEN** the note list shows notes in that folder according to the active sort order

### Requirement: Local Search
The system SHALL provide local note search over note titles, body text, tags, and folder paths using the SQLite index.

#### Scenario: Search indexed note
- **WHEN** the user enters a search query matching a note body
- **THEN** the note appears in the search results without scanning every note file synchronously on the UI thread

#### Scenario: Search with no network
- **WHEN** the user searches while offline
- **THEN** the system returns results from local library data only

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

