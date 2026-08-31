## MODIFIED Requirements

### Requirement: Minimal Scope
The system SHALL exclude sync, cloud accounts, telemetry, plugin marketplaces, web clipper features, importers, and renderer extensions such as Mermaid. The system SHALL also exclude content constructs that cannot be expressed in portable plain Markdown, specifically per-block text and background colors, multi-column block layouts, and database/table view features.

#### Scenario: Excluded feature requested by runtime data
- **WHEN** a note contains Mermaid syntax, plugin directives, or app-specific importer metadata
- **THEN** the system renders or displays it as ordinary Markdown text where possible and does not execute extension behavior

#### Scenario: Formatting that would not survive a round trip
- **WHEN** a block-level formatting feature would require a non-portable encoding in the `.md` file to persist
- **THEN** the system does not offer that feature

## ADDED Requirements

### Requirement: Presentation And Display Scaling
The system SHALL provide a light theme and a dark theme, selectable by the user and persisted across sessions, and SHALL honor the display scale reported by the windowing system.

#### Scenario: Switch theme
- **WHEN** the user selects the other theme
- **THEN** the system re-renders the entire interface in that theme and restores the same theme on the next launch

#### Scenario: HiDPI display
- **WHEN** the application window reports a display scale greater than 1.0
- **THEN** text and interface elements are rendered at that scale without blurring or clipping

### Requirement: Bundled Typography
The system SHALL render its interface using font files vendored in the repository, and MUST NOT download font files during configure, build, or run.

#### Scenario: Vendored fonts missing at runtime
- **WHEN** a vendored font file cannot be opened
- **THEN** the system falls back to an available system font and continues to run
