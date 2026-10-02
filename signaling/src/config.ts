import { z } from "zod";

const configSchema = z.object({
    host: z.enum(["127.0.0.1", "::1"]).default("127.0.0.1"),
    port: z.coerce.number().int().min(1024).max(65535).default(8787),
    token: z.string().regex(/^[A-Za-z0-9_-]{32,256}$/),
    logLevel: z
        .enum(["silent", "error", "warn", "info", "debug"])
        .default("info"),
});

export type Config = z.infer<typeof configSchema>;

export function loadConfig(env: NodeJS.ProcessEnv): Config {
    const result = configSchema.safeParse({
        host: env.CLOUDPLAY_HOST,
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
