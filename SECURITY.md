# Security Policy

## Scope
CloudPlay host, Android client, signaling service, and protocol implementation.

## Never expose
- private keys
- auth tokens
- pairing secrets
- game credentials

## Reporting
For a private personal project, record security issues in a private issue tracker or local SECURITY_NOTES.md. Do not publish exploit details before remediation.

## Security priorities
P0: unauthorized host control, credential compromise, remote code execution
P1: session hijacking, persistent unauthorized access, major data exposure
P2: denial of service, recoverable information leakage
