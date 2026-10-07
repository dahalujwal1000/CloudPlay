import { execFile } from "node:child_process";
import { createPrivateKey, X509Certificate } from "node:crypto";
import { promisify } from "node:util";
import { createSecureContext } from "node:tls";
import { z } from "zod";

const execute = promisify(execFile);
const identitySchema = z
    .object({
        certificate: z.string().min(1).max(32768),
        privateKey: z.string().min(1).max(32768),
    })
    .strict();

export async function readSecretServiceIdentity(name: string): Promise<string> {
    try {
        if (!/^[a-zA-Z0-9_-]{1,64}$/.test(name)) throw new Error();
        const result = await execute(
            "secret-tool",
            [
                "lookup",
                "application",
                "cloudplay",
                "purpose",
                "signaling-tls",
                "identity",
                name,
            ],
            { encoding: "utf8", timeout: 5000, maxBuffer: 65536 },
        );
        return result.stdout;
    } catch {
        // Child errors may include captured secret stdout/stderr. Never propagate them.
        throw new Error("TLS identity unavailable from Secret Service");
    }
}

export function validateTlsIdentity(
    serialized: string,
    host: string,
    now = Date.now(),
) {
    try {
        if (serialized.length > 65536) throw new Error();
        const identity = identitySchema.parse(JSON.parse(serialized));
        const certificate = new X509Certificate(identity.certificate);
        const key = createPrivateKey(identity.privateKey);
        if (
            !Number.isFinite(now) ||
            now < Date.parse(certificate.validFrom) ||
            now >= Date.parse(certificate.validTo) ||
            !certificate.checkPrivateKey(key) ||
            !certificate.checkIP(host)
        )
            throw new Error();
        const options = {
            cert: identity.certificate,
            key: identity.privateKey,
            minVersion: "TLSv1.2" as const,
        };
        createSecureContext(options);
        return options;
    } catch {
        throw new Error(
            "TLS identity invalid, expired, or not valid for bind address",
        );
    }
}
