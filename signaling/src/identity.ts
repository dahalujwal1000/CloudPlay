import { execFile } from "node:child_process";
import { createPrivateKey, randomUUID, X509Certificate } from "node:crypto";
import { allowedHost } from "./config.js";
import { readSecretServiceIdentity, validateTlsIdentity } from "./tls.js";

type Command = (file: string, args: string[], input: string) => Promise<string>;

export class IdentityProvisionError extends Error {
    constructor(readonly identity: string) {
        super("TLS provisioning failed; a Secret Service entry may remain");
    }
}

// Secrets travel only over child stdin/stdout, never shell arguments or files.
export const privateCommand: Command = (file, args, input) =>
    new Promise((resolve, reject) => {
        const child = execFile(
            file,
            args,
            {
                encoding: "utf8",
                timeout: 15_000,
                killSignal: "SIGKILL",
                maxBuffer: 65536,
            },
            (error, stdout) => {
                if (error) reject(new Error("Identity operation failed"));
                else resolve(stdout);
            },
        );
        // A child that fails before reading can close the pipe; completion handles failure.
        child.stdin?.on("error", () => {});
        child.stdin?.end(input);
    });

function publicIdentity(name: string, host: string, serialized: string) {
    const options = validateTlsIdentity(serialized, host);
    const certificate = new X509Certificate(options.cert);
    return {
        identity: name,
        host,
        certificateSha256: certificate.fingerprint256,
        expiresAt: new Date(certificate.validTo).toISOString(),
    };
}

export async function inspectIdentity(
    name: string,
    host: string,
    readIdentity = readSecretServiceIdentity,
) {
    if (!/^[a-zA-Z0-9_-]{1,64}$/.test(name) || !allowedHost(host))
        throw new Error("Invalid identity name or host address");
    try {
        return publicIdentity(name, host, await readIdentity(name));
    } catch {
        throw new Error("Unable to inspect TLS identity");
    }
}

export async function provisionIdentity(
    host: string,
    command: Command = privateCommand,
    readIdentity = readSecretServiceIdentity,
) {
    if (!allowedHost(host)) throw new Error("Invalid host address");
    // A unique name avoids silently replacing a host already trusted by a client.
    const name = `host-${randomUUID()}`;
    try {
        const addresses = [...new Set([host, "127.0.0.1", "::1"])];
        const pem = await command(
            "openssl",
            [
                "req",
                "-new",
                "-x509",
                "-batch",
                "-sha256",
                "-newkey",
                "ec",
                "-pkeyopt",
                "ec_paramgen_curve:prime256v1",
                "-noenc",
                "-keyout",
                "-",
                "-days",
                "90",
                "-subj",
                "/CN=CloudPlay Host",
                "-addext",
                `subjectAltName=${addresses.map((ip) => `IP:${ip}`).join(",")}`,
                "-addext",
                "basicConstraints=critical,CA:FALSE",
                "-addext",
                "keyUsage=critical,digitalSignature",
                "-addext",
                "extendedKeyUsage=serverAuth",
            ],
            "",
        );
        const certificate = new X509Certificate(pem).toString();
        const privateKey = createPrivateKey(pem)
            .export({ type: "pkcs8", format: "pem" })
            .toString();
        const serialized = JSON.stringify({ certificate, privateKey });
        const expected = publicIdentity(name, host, serialized);
        await command(
            "secret-tool",
            [
                "store",
                "--label=CloudPlay signaling TLS",
                "application",
                "cloudplay",
                "purpose",
                "signaling-tls",
                "identity",
                name,
            ],
            serialized,
        );
        const stored = await inspectIdentity(name, host, readIdentity);
        if (stored.certificateSha256 !== expected.certificateSha256)
            throw new Error("Identity read-back mismatch");
        return stored;
    } catch {
        // Store may have succeeded before read-back failed. Do not delete blindly.
        throw new IdentityProvisionError(name);
    }
}
