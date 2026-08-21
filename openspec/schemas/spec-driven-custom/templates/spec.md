## ADDED Requirements

### Requirement: <!-- requirement name -->
<!-- requirement text, use SHALL/MUST for normative statements -->

#### Scenario: <!-- scenario name (normal path) -->
- **GIVEN** <!-- precondition: system state before the operation -->
- **WHEN** <!-- trigger: event, call, or condition -->
- **THEN** <!-- observable outcome: state change, output, return value -->
- **TIMING** <!-- (optional) deadline: "within Xms" / "no later than Xms" / omit if not applicable -->

#### Scenario: <!-- scenario name (error/fault path) -->
- **GIVEN** <!-- precondition -->
- **WHEN** <!-- fault: comm loss / power fail / sensor anomaly / queue full / ... -->
- **THEN** <!-- safe behavior: degrade, stop, alarm, retry with max count -->
- **TIMING** <!-- (optional) -->

---

## Invariants
<!-- Conditions that MUST hold at ALL times during system lifetime, regardless of state.
     Format: INV-XX: one-sentence assertion using "MUST NOT" or "MUST always".
     Invariants have no preconditions — they are always true. -->

- **INV-01**: <!-- e.g., "Motor and heater MUST NOT run at full power simultaneously" -->
- **INV-02**: <!-- e.g., "All shared data access MUST be protected by explicit concurrency mechanism" -->

---

## State Machine
<!-- Only if this capability involves explicit state transitions.
     If not applicable, delete this entire section. -->

| Current State | Event | Next State | Guard Condition | Side Effect |
|---------------|-------|------------|-----------------|-------------|
| | | | | |

- **Illegal transitions**: <!-- handling: ignore / alarm / emergency stop -->
- **Power-on default state**: <!-- state name -->
- **Fail-safe state**: <!-- state name (entered on unrecoverable fault) -->

---

## Capacity & Resource Constraints
<!-- Only if this capability manages bounded resources.
     If not applicable, delete this section. -->

| Resource | Upper Limit | Overflow Behavior | Basis |
|----------|-------------|-------------------|-------|
| | | | |
