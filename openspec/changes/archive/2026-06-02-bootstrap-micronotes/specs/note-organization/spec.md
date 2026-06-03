## ADDED Requirements

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
