## MODIFIED Requirements

### Requirement: Presentation And Display Scaling
The system SHALL provide a light theme and a dark theme, selectable by the user
and persisted across sessions, and SHALL honor the display scale reported by the
windowing system. Every text role in a shipped theme SHALL meet a stated
contrast ratio against the surfaces it is drawn on, checked by the build rather
than by inspection.

#### Scenario: Switch theme
- **WHEN** the user selects the other theme
- **THEN** the system re-renders the entire interface in that theme and restores the same theme on the next launch

#### Scenario: HiDPI display
- **WHEN** the application window reports a display scale greater than 1.0
- **THEN** text and interface elements are rendered at that scale without blurring or clipping

#### Scenario: A shipped palette is illegible
- **WHEN** a built-in theme pairs a text role with a surface below the ratio that role is held to
- **THEN** the test suite fails, naming the pairing and the ratio it fell short of
