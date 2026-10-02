# Security Architecture

## Pairing
1. Host generates short-lived pairing code.
2. Android enters code or scans QR.
3. Pairing is single-use.
4. Device identity/credential is established.
5. Future sessions authenticate using stored credentials.

## Storage
Android: Android Keystore.
Windows: DPAPI/Credential Manager or equivalent protected storage.

## Threats
Consider:
- unauthorized host access
- replayed pairing codes
- stolen credentials
- malicious input packets
- signaling impersonation
- TURN credential leakage
- log leakage
- local network attackers

## Rules
Never log secrets.
Never trust client input.
Expire pairing codes.
Rate-limit authentication.
Require authenticated control operations.
