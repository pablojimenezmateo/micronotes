## ADDED Requirements

### Requirement: Activity Ribbon
The system SHALL provide a fixed-width column of icon controls along the leading
edge of the window, holding the actions used most often to start work, and the
controls that show and hide the side panels.

#### Scenario: Run an action from the ribbon
- **WHEN** the user activates a ribbon control
- **THEN** the system runs the same command the palette and the keyboard binding for that action run, through the one command registry

#### Scenario: Ribbon is always reachable
- **WHEN** every side panel is hidden
- **THEN** the ribbon is still drawn, so the panels can be brought back without the palette

#### Scenario: Control names
- **WHEN** the pointer rests on a ribbon control
- **THEN** the system names the action and the keys that also run it
