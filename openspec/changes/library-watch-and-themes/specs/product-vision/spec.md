## MODIFIED Requirements

### Requirement: Minimal Scope
The system SHALL exclude sync, cloud accounts, telemetry, plugin marketplaces, web clipper features, importers, and renderer extensions such as Mermaid. The system SHALL also exclude content constructs that cannot be expressed in portable plain Markdown, specifically per-block text and background colors, multi-column block layouts, and database/table view features. A user-supplied colour theme file is in scope: it is inert configuration data describing interface colours only, carries no executable content, and does not affect note content or its Markdown encoding.

#### Scenario: Excluded feature requested by runtime data
- **WHEN** a note contains Mermaid syntax, plugin directives, or app-specific importer metadata
- **THEN** the system renders or displays it as ordinary Markdown text where possible and does not execute extension behavior

#### Scenario: Formatting that would not survive a round trip
- **WHEN** a block-level formatting feature would require a non-portable encoding in the `.md` file to persist
- **THEN** the system does not offer that feature

#### Scenario: User theme file is not an extension
- **WHEN** the user installs a colour theme file
- **THEN** the system applies its interface colours, executes nothing from it, and leaves note content and its Markdown encoding unaffected

### Requirement: Presentation And Display Scaling
The system SHALL provide a light theme and a dark theme, selectable by the user and persisted across sessions, and SHALL honor the display scale reported by the windowing system. The light and dark themes SHALL be provided through the same theme mechanism available to user-supplied themes.

#### Scenario: Switch theme
- **WHEN** the user selects the other theme
- **THEN** the system re-renders the entire interface in that theme and restores the same theme on the next launch

#### Scenario: HiDPI display
- **WHEN** the application window reports a display scale greater than 1.0
- **THEN** text and interface elements are rendered at that scale without blurring or clipping

#### Scenario: Built-in themes always available
- **WHEN** no user theme files are installed
- **THEN** the light and dark themes remain selectable and persist across sessions
