## ADDED Requirements

### Requirement: Companion Files
The system SHALL treat a directory named `files` directly inside a notebook or the library root as holding companion files. Everything under it, recursively, SHALL be listed in the navigation tree under that notebook as a file rather than a note, SHALL be searchable by file name only, and SHALL be opened through the platform default handler. The system MUST NOT read, render or index the contents of a companion file.

#### Scenario: Listed under its notebook
- **WHEN** a notebook contains a `files` directory with entries in it
- **THEN** the tree shows the directory between the notebook's sub-notebooks and its notes, with folders before files inside it, each sorted by name

#### Scenario: Opened with the default handler
- **WHEN** the user clicks a file row or presses Enter on it
- **THEN** the system launches the file through the platform default opener, keeps micronotes running, and leaves the open note and current notebook unchanged

#### Scenario: Passed over by the keyboard
- **WHEN** the keyboard cursor moves onto a file row without the user pressing Enter
- **THEN** nothing is launched

#### Scenario: Found by name, not content
- **WHEN** the user searches for text that appears in a companion file's name
- **THEN** the file is listed under a caption of its own, and a search scoped to note content lists no files

#### Scenario: A note-shaped file inside the directory
- **WHEN** a `.md` file sits under a `files` directory
- **THEN** it is listed as a file, is not indexed as a note, and opens through the default handler

#### Scenario: Managed from the sidebar
- **WHEN** the user renames, moves or deletes a companion file or a folder inside a files directory
- **THEN** the change is made on disk, a deletion goes to the library trash and can be restored, and the tree reflects the result

#### Scenario: Moved onto a notebook
- **WHEN** the user drags a companion file onto a notebook that has no `files` directory
- **THEN** the directory is created and the file lands inside it

#### Scenario: The rule is protected
- **WHEN** the user tries to create or rename a notebook to `files`, create a note inside a files directory, or move a note or notebook into one
- **THEN** the system refuses and reports why on the status line

#### Scenario: The anchor keeps its name
- **WHEN** the user tries to rename or move a `files` directory itself
- **THEN** the system refuses; deleting it remains allowed

#### Scenario: Changed from outside
- **WHEN** a file is added to, renamed in or removed from a `files` directory by another program
- **THEN** the tree reflects the change without user action and without re-indexing the library
