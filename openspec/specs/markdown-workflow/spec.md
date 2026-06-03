# markdown-workflow Specification

## Purpose
TBD - created by archiving change bootstrap-micronotes. Update Purpose after archive.
## Requirements
### Requirement: Raw Markdown Editor
The system SHALL provide a plain Markdown text editor for note bodies.

#### Scenario: Edit note text
- **WHEN** the user types in the editor pane
- **THEN** the system updates the note buffer as raw Markdown text and can save it to the note's `.md` file

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

