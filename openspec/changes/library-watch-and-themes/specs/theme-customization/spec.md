## ADDED Requirements

### Requirement: User-Supplied Theme Files
The system SHALL load colour themes from plain text files in a user themes directory, and SHALL allow the user to select a discovered theme from the settings surface.

#### Scenario: Discover an installed theme
- **WHEN** a theme file is present in the user themes directory and the user opens the theme picker
- **THEN** that theme is listed by name alongside the built-in themes

#### Scenario: Select a user theme
- **WHEN** the user selects a discovered theme
- **THEN** the system re-renders the entire interface in that theme and restores it on the next launch

#### Scenario: Theme file is inert data
- **WHEN** a theme file is loaded
- **THEN** the system interprets only colour assignments and include directives, and MUST NOT execute any content from the file

### Requirement: Built-In Themes Use The Theme File Mechanism
The system SHALL express its built-in light and dark themes through the same derivation used for user themes, so that a built-in theme is not a special case.

#### Scenario: Built-in theme derivation
- **WHEN** the system loads a built-in theme
- **THEN** it derives the theme through the same role mapping applied to a user theme file

### Requirement: Colour Roles And Partial Themes
The system SHALL derive every drawn colour from a named semantic role, and SHALL fall back to the built-in value for the active light or dark mode for any role a theme file does not set.

#### Scenario: Theme file setting a subset of roles
- **WHEN** a theme file assigns three colour roles and omits the rest
- **THEN** the assigned roles take effect and every other role uses its built-in value

#### Scenario: Unparseable colour token
- **WHEN** a theme file assigns a role a value that cannot be parsed as a colour
- **THEN** that role falls back to its built-in value and the system reports the offending role

### Requirement: Theme File Composition
The system SHALL allow a theme file to include another theme file, and SHALL terminate rather than recurse when includes form a cycle.

#### Scenario: Theme extends another theme
- **WHEN** a theme file includes another theme file and overrides some of its roles
- **THEN** the resulting theme is the included theme with those overrides applied

#### Scenario: Include cycle
- **WHEN** theme files include one another in a cycle
- **THEN** the system stops loading, reports the cycle, and falls back to a built-in theme

#### Scenario: Missing include target
- **WHEN** a theme file includes a file that does not exist
- **THEN** the system reports the missing file and falls back to a built-in theme

### Requirement: Legibility Floors For Themes
The system SHALL enforce contrast floors on its built-in themes, and SHALL warn about rather than reject a user theme that fails a floor.

#### Scenario: Built-in theme below the contrast floor
- **WHEN** a built-in theme would render a text role below the stated contrast floor against its surface
- **THEN** the build fails its verification checks

#### Scenario: User theme below the contrast floor
- **WHEN** a user theme renders a text role below the stated contrast floor against its surface
- **THEN** the system loads the theme and reports which role failed
