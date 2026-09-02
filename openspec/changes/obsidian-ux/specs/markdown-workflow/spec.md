## ADDED Requirements

### Requirement: Inline Note Title
The system SHALL draw the open note's title at the top of the page, above its
content, and SHALL allow the note to be renamed by editing it there. The title
is the note's name as the library holds it and MUST NOT be written into the
note's body.

#### Scenario: Rename from the page
- **WHEN** the user edits the inline title and commits it
- **THEN** the system renames the note through the same path the rename command uses, and the note's body is unchanged

#### Scenario: Title is not body text
- **WHEN** a note whose body has no `#` heading is saved after the inline title was edited
- **THEN** the saved `.md` file gains no heading line, and the title round-trips through the library's own metadata

#### Scenario: Abandoning an edit
- **WHEN** the user is editing the inline title and cancels
- **THEN** the previous title is restored and no rename is performed

### Requirement: Note Properties Display
The system SHALL present a note's front matter keys at the top of the page as
named properties, rather than parsing them and showing nothing. Keys the system
does not recognize SHALL be displayed alongside the ones it does.

#### Scenario: Note carries front matter
- **WHEN** a note whose front matter sets tags and other keys is opened
- **THEN** the page shows each key with its value above the note's content

#### Scenario: Note carries no front matter
- **WHEN** a note with no front matter is opened
- **THEN** no properties area is drawn and the content starts where it would have

#### Scenario: Display does not rewrite the file
- **WHEN** a note with front matter is opened, displayed, and saved with an unrelated body edit
- **THEN** the front matter block in the saved file is byte-identical to the one that was read
