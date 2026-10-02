# AI Agent Workflow

## Roles

### Human
Owns product decisions, acceptance criteria, credentials, deployment, and final review.

### Codex
Primary implementation agent.

### DeepSeek
Architecture/code reviewer, debugging assistant, alternative implementation reviewer.

## Workflow

Human requirement
→ task file
→ Codex implementation
→ tests
→ DeepSeek review
→ Codex fixes
→ human review
→ merge

Do not have multiple agents independently rewrite the same subsystem.

## Review prompts

Ask reviewer to inspect:
- memory ownership
- concurrency
- deadlocks
- race conditions
- protocol validation
- authentication
- secret handling
- latency
- GPU/CPU copies
- failure recovery
- API correctness
- test coverage

## AI rules
Generated code is untrusted until reviewed and tested.
Do not invent APIs.
Check official documentation for external APIs.
Never report tests as passing unless they were actually executed.
