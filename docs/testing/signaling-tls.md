# Signaling TLS Foundation

The signaling service supports HTTPS/WSS using a TLS identity retrieved from
Fedora Secret Service through `secret-tool`. It never writes the identity to a
repository file or logs it. Device bearer credentials remain ephemeral; this
does not implement persistent device pairing. Android's manual trust UI and
pinned HTTPS client are described in [Android pairing validation](android-pairing.md).

## Configuration

Default behavior is unchanged: HTTP on 127.0.0.1:8787, with authenticated
diagnostics and code-authenticated pairing completion. To enable TLS, set
`CLOUDPLAY_TLS_IDENTITY` to a provisioned Secret Service identity name (1-64
letters, digits, underscores or hyphens).

The lookup attributes are:

```text
application cloudplay
purpose signaling-tls
identity <name>
```

The stored secret is a JSON object containing exactly `certificate` and
`privateKey`, both PEM strings. The setup CLI creates this entry without writing
the private key to a temporary file. From signaling/, with OpenSSL 3 and an
unlocked Secret Service available:

```sh
npm run build
npm run identity:provision -- --host 192.168.1.20
```

Use the PC's actual private LAN address, or 127.0.0.1 for a local-only identity.
The command creates an EC P-256 self-signed server certificate valid for 90 days,
with the requested IP and both loopback addresses in subjectAltName. It stores
the key/certificate using secret-tool stdin, under a fresh `host-<UUID>` name,
then verifies read-back. OpenSSL generation outputs are captured privately, not
inherited by the terminal. Child operations are bounded to 15 seconds/64 KiB.
Only identity name, host IP, SHA-256 certificate fingerprint, and expiry are
printed. Nothing is activated and no existing identity is intentionally replaced.

Set CLOUDPLAY_TLS_IDENTITY to the returned name when starting the service. To
inspect the public metadata again (replace the example name with the returned one):

```sh
npm run identity:inspect -- --identity host-REPLACE_WITH_RETURNED_UUID --host 192.168.1.20
```

The fingerprint hashes the full DER certificate, not only its public key. Verify
it through a trusted PC display before enrolling a phone. Do not disable normal
TLS checks. Android supports manual fingerprint confirmation; QR onboarding remains pending.
Renewal generates a different identity/fingerprint and requires explicit client
re-enrollment; automatic renewal, key deletion and a provisioning GUI are not
implemented. A failed read-back can leave an unused Secret Service entry; the
CLI reports its public name for manual inspection instead of deleting blindly.

The leaf certificate must be within its validity period, match the private key,
and contain the bind IP in its subjectAltName. Identity loading is bounded to
five seconds and 64 KiB. Missing/locked Secret Service, malformed data, mismatched
keys, expired certificates, and wrong bind addresses fail startup without HTTP
fallback. TLS minimum version is 1.2; library defaults choose secure ciphers.

LAN binding additionally requires `CLOUDPLAY_ALLOW_LAN=true` and an explicit
RFC1918 IPv4 address in `CLOUDPLAY_HOST`. Public, wildcard, DNS-name, link-local,
and non-loopback IPv6 binds are rejected. Example non-secret settings:

```sh
export CLOUDPLAY_HOST=192.168.1.20
export CLOUDPLAY_ALLOW_LAN=true
export CLOUDPLAY_TLS_IDENTITY=host-REPLACE_WITH_RETURNED_UUID
```

The address must belong to this machine and appear in the certificate. Preserve
the existing randomly generated administrator token in CLOUDPLAY_TOKEN. Never
give that token to a phone. All pairing management still requires administrator
authentication; paired devices retain diagnostics-only access.

Clients must validate certificate trust and the destination IP before sending a
code/token. A self-signed identity needs an independently authenticated trust
exchange; never disable certificate verification or accept a certificate merely
because an unauthenticated server supplied it. Android pins the manually confirmed
certificate while retaining platform hostname verification. This is not Internet deployment support: no automatic firewall,
NAT, reverse proxy or router changes are performed.

## Verification

Run `npm run check` in signaling/. The suite requires OpenSSL to generate a
short-lived test identity inside a private temporary directory, removed afterward.
Tests use an injected identity reader and real loopback HTTPS/WSS sockets, with
normal certificate verification enabled. They cover trust failure, hostname
mismatch, authenticated status, malformed/expired/mismatched identities, and LAN
configuration rejection. Existing pairing/revocation tests remain enabled.

Provisioning tests additionally run real OpenSSL generation with an in-memory
fake keyring and verify generated certificates over real HTTPS. They cover
distinct identities, validation before storage, read-back mismatch/failure,
secret-safe subprocess failures, oversized outputs and invalid CLI arguments.

2026-10-06: all 23 signaling tests, TypeScript build, ESLint and Prettier passed.
At that test checkpoint, actual Secret Service storage/lookup and a physical
Android connection were not exercised; no credentials or LAN listener were created.

2026-10-07 user-assisted validation: identity provisioning completed against the
real GNOME Secret Service, and the service reported listening on the PC's private
LAN IPv4 address using HTTPS port 8787. This verifies provisioning and startup,
not successful phone enrollment. The phone reported authorization rejection;
its exact request/status and cause are not yet established. Do not publish live
pairing codes, administrator/device tokens, or private keys in diagnostic reports.
