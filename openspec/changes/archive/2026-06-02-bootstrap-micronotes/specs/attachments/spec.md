## ADDED Requirements

### Requirement: Library-Managed Attachments
The system SHALL copy attached files into a library-managed attachment location associated with the owning note.

#### Scenario: Attach file
- **WHEN** the user attaches a local file to a note
- **THEN** the system copies the file into the library attachment area and inserts or records a relative Markdown link to that managed file

### Requirement: Inline Image Rendering
The system SHALL render supported local image attachments inline in the Markdown viewer.

#### Scenario: View image attachment
- **WHEN** a note contains a Markdown image link to a managed image attachment
- **THEN** the viewer displays the image inline at a size constrained by the viewer pane

### Requirement: Non-Image Attachment Links
The system SHALL display non-image attachments as links in the Markdown viewer.

#### Scenario: View PDF attachment
- **WHEN** a note contains a link to a managed PDF attachment
- **THEN** the viewer shows a clickable link instead of attempting to render the PDF inline

### Requirement: Open Attachments With Default Handler
The system SHALL open non-image attachment links with the Linux default handler after an explicit user action.

#### Scenario: Open non-image attachment
- **WHEN** the user activates a non-image attachment link
- **THEN** the system launches the file through the platform default opener and keeps micronotes running

### Requirement: Attachment Path Safety
The system SHALL reject attachment links that resolve outside the library root for managed rendering or opening.

#### Scenario: Path traversal link
- **WHEN** a note contains an attachment link that resolves outside the library root
- **THEN** the system does not render or open the file as a managed attachment
