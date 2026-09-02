## MODIFIED Requirements

### Requirement: SQLite Index Cache
The system SHALL maintain a SQLite database as a rebuildable cache/index for note metadata, folder mapping, tags, attachment references, and search. The system SHALL detect changes made to library files outside the application automatically, without requiring the user to invoke a refresh.

#### Scenario: Cache deleted
- **WHEN** the SQLite index file is missing but the note library files remain present
- **THEN** the system rebuilds the index from the library files and metadata

#### Scenario: Cache stale after external edit
- **WHEN** a note file changes outside micronotes
- **THEN** the system detects the changed file and updates the SQLite index before presenting stale metadata as current

#### Scenario: Detection without user action
- **WHEN** a note file changes outside micronotes and the user takes no action in the application
- **THEN** the system still detects the change and updates the index

#### Scenario: Manual refresh remains available
- **WHEN** automatic detection is unavailable because the library root exceeds the traversal budget
- **THEN** the user can still refresh the library explicitly and the index is rebuilt
