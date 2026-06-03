# note-library Specification

## Purpose
TBD - created by archiving change bootstrap-micronotes. Update Purpose after archive.
## Requirements
### Requirement: Folder-Backed Library
The system SHALL store each notes library in a local filesystem folder where Markdown note files are the authoritative note bodies.

#### Scenario: Create note
- **WHEN** the user creates a note in a folder
- **THEN** the system writes a `.md` file under the library root and records index metadata for that note

#### Scenario: External readable files
- **WHEN** the user opens the library folder with another editor or file manager
- **THEN** note bodies are visible as ordinary Markdown files

### Requirement: SQLite Index Cache
The system SHALL maintain a SQLite database as a rebuildable cache/index for note metadata, folder mapping, tags, attachment references, and search.

#### Scenario: Cache deleted
- **WHEN** the SQLite index file is missing but the note library files remain present
- **THEN** the system rebuilds the index from the library files and metadata

#### Scenario: Cache stale after external edit
- **WHEN** a note file changes outside micronotes
- **THEN** the system detects the changed file and updates the SQLite index before presenting stale metadata as current

### Requirement: Stable Note Identity
The system SHALL assign each note a stable id that survives note renames and folder moves.

#### Scenario: Rename note file
- **WHEN** the user renames a note
- **THEN** the system preserves the note id and retains tag, attachment, and search index associations

### Requirement: Safe Library Boundaries
The system SHALL keep note metadata, index files, and managed attachments within the selected library root.

#### Scenario: Resolve managed path
- **WHEN** the system resolves a note or attachment path
- **THEN** the resolved path remains inside the library root or the operation is rejected

