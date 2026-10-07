import { z } from "zod";
import { isIP } from "node:net";

export function isLoopback(host: string): boolean {
    return host === "127.0.0.1" || host === "::1";
}

export function allowedHost(host: string): boolean {
    if (isLoopback(host)) return true;
    if (isIP(host) !== 4) return false;
    const [first, second] = host.split(".").map(Number);
    return (
        first === 10 ||
        (first === 192 && second === 168) ||
        (first === 172 && second !== undefined && second >= 16 && second <= 31)
    );
}

const configSchema = z
    .object({
        host: z.string().refine(allowedHost).default("127.0.0.1"),
        allowLan: z
            .enum(["true", "false"])
            .default("false")
            .transform((value) => value === "true"),
        tlsIdentity: z
            .string()
            .regex(/^[a-zA-Z0-9_-]{1,64}$/)
            .optional(),
        port: z.coerce.number().int().min(1024).max(65535).default(8787),
        token: z.string().regex(/^[A-Za-z0-9_-]{32,256}$/),
        logLevel: z
            .enum(["silent", "error", "warn", "info", "debug"])
            .default("info"),
    })
    .superRefine((value, ctx) => {
        if (
            !isLoopback(value.host) &&
            (!value.allowLan || !value.tlsIdentity)
        ) {
            ctx.addIssue({
                code: "custom",
                path: ["host"],
                message: "LAN requires opt-in and TLS",
            });
        }
    });

export type Config = z.infer<typeof configSchema>;

export function loadConfig(env: NodeJS.ProcessEnv): Config {
    const result = configSchema.safeParse({
        host: env.CLOUDPLAY_HOST,
        allowLan: env.CLOUDPLAY_ALLOW_LAN,
        tlsIdentity: env.CLOUDPLAY_TLS_IDENTITY,
        port: env.CLOUDPLAY_PORT,
        token: env.CLOUDPLAY_TOKEN,
        logLevel: env.CLOUDPLAY_LOG_LEVEL,
    });
    if (!result.success) {
        // Zod errors can contain supplied values. Emit only allowlisted field names.
        const fields = [
            ...new Set(result.error.issues.map((issue) => issue.path[0])),
        ];
        throw new Error(
            `Invalid signaling configuration fields: ${fields.join(", ")}`,
        );
    }
    return result.data;
}
