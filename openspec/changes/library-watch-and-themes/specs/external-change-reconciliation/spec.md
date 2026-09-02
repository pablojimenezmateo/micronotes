## ADDED Requirements

### Requirement: Automatic Detection Of External Library Changes
The system SHALL observe the library root for changes to note files and folders made by other programs, and MUST NOT require the user to invoke a refresh for those changes to be noticed.

#### Scenario: Note edited by another program
- **WHEN** a `.md` file inside the library is modified by another editor while micronotes is running
- **THEN** the system notices the change without user action and reconciles it

#### Scenario: Note added outside the application
- **WHEN** a `.md` file is created inside the library by another program
- **THEN** the system adds it to the tree and the search index without user action

#### Scenario: Folder added or removed outside the application
- **WHEN** a directory inside the library is created or removed by another program
- **THEN** the system rebuilds the affected portion of the navigation tree and index

#### Scenario: Idle cost of observing
- **WHEN** the library is being observed and nothing in it changes
- **THEN** the application remains blocked on its event wait and consumes no measurable CPU

### Requirement: Reconciling A Note That Is Not Open
The system SHALL apply an external change to a note that is not currently open silently, without interrupting the user.

#### Scenario: Closed note changes on disk
- **WHEN** a note that is not open in the editor changes on disk
- **THEN** the system updates the index entry and the sidebar row and displays no message

### Requirement: Reconciling An Open Note With No Unsaved Edits
The system SHALL reload an externally changed note into the editing surface when that buffer has no unsaved edits, and SHALL preserve the caret position across the reload.

#### Scenario: Open clean note changes on disk
- **WHEN** the open note has no unsaved edits and its file changes on disk
- **THEN** the system reloads the buffer from disk and displays no message

#### Scenario: Caret survives the reload
- **WHEN** a clean open note is reloaded after an external change
- **THEN** the caret is restored to its previous byte offset, clamped to the new document length

### Requirement: Protecting Unsaved Edits From External Changes
The system MUST NOT overwrite a buffer containing unsaved edits in response to an external change. The system SHALL report the conflict and SHALL offer an explicit, user-initiated command to discard the local edits and reload from disk.

#### Scenario: Open dirty note changes on disk
- **WHEN** the open note has unsaved edits and its file changes on disk
- **THEN** the buffer is left exactly as the user left it and the system reports that the note changed on disk

#### Scenario: User chooses to take the disk version
- **WHEN** the user runs the reload-from-disk command on a note reported as changed on disk
- **THEN** the system discards the unsaved edits and loads the file contents

#### Scenario: User keeps editing and saves
- **WHEN** the user ignores the conflict report and saves the note
- **THEN** the buffer contents are written to the file and the conflict report is cleared

### Requirement: Surviving An External Delete Of An Open Note
The system MUST NOT close or clear an open note because its file was deleted outside the application, and SHALL allow the user to write the buffer back.

#### Scenario: Open note deleted on disk
- **WHEN** the file backing the open note is deleted by another program
- **THEN** the buffer is retained, the note is reported as deleted on disk, and the note remains editable

#### Scenario: Saving a note deleted on disk
- **WHEN** the user saves a note whose file was deleted externally
- **THEN** the system recreates the file at its recorded path with the buffer contents

### Requirement: Bounded Observation Of Oversized Trees
The system SHALL bound the cost of observing a library and SHALL degrade rather than stall when a library root is too large to watch.

#### Scenario: Library root exceeds the traversal budget
- **WHEN** the selected library root contains more entries than the traversal budget allows
- **THEN** the system stops observing that root, reports that automatic detection is unavailable, and continues to operate with manual refresh

#### Scenario: Startup does not walk the tree twice
- **WHEN** the application starts and performs its initial library scan
- **THEN** the observer adopts that scan as its baseline rather than performing a second traversal
