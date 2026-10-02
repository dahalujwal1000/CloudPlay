# ADR-005: Game Profile Abstraction

Status: Accepted

## Decision
Represent games as external GameProfile configuration.

## Reason
The streaming system must not contain Genshin-specific logic.

## Allowed
- launch
- process monitoring
- capture
- stream
- user input
- user-requested termination

## Forbidden
- memory modification
- DLL injection
- anti-cheat bypass
- executable patching
