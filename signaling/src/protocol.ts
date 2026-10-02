import { z } from "zod";

// Bootstrap diagnostics only. SDP, ICE, pairing and session routing need their own schemas.
export const statusRequestSchema = z
    .object({
        version: z.literal(1),
        type: z.literal("service.status"),
        requestId: z.string().uuid(),
        payload: z.object({}).strict(),
    })
    .strict();

export function parseStatusRequest(raw: string) {
    try {
        return statusRequestSchema.safeParse(JSON.parse(raw));
    } catch {
        return { success: false } as const;
    }
}
