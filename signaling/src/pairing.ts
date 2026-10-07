import {
    createHash,
    createHmac,
    randomBytes,
    randomInt,
    randomUUID,
    timingSafeEqual,
} from "node:crypto";

const CODE_TTL_MS = 120_000;
const DEVICE_TTL_MS = 900_000;
const MAX_DEVICES = 16;

type Challenge = {
    id: string;
    digest: Buffer;
    expiresAt: number;
    attempts: number;
};
type Device = { digest: Buffer; expiresAt: number };

// Single event-loop owner; redemption has no await between verification and consumption.
// Only digests are retained. All credentials are ephemeral and disappear on restart.
export class PairingService {
    private readonly key = randomBytes(32);
    private challenge: Challenge | undefined;
    private readonly devices = new Map<string, Device>();
    private windowStart = -Infinity;
    private attempts = 0;

    constructor(private readonly now: () => number = Date.now) {}

    private codeDigest(id: string, code: string) {
        return createHmac("sha256", this.key).update(`${id}:${code}`).digest();
    }

    private prune() {
        const now = this.now();
        if (this.challenge && now >= this.challenge.expiresAt)
            this.challenge = undefined;
        for (const [id, device] of this.devices) {
            if (now >= device.expiresAt) this.devices.delete(id);
        }
    }

    start() {
        this.prune();
        if (this.devices.size >= MAX_DEVICES) return null;
        const id = randomUUID();
        const code = randomInt(100_000_000).toString().padStart(8, "0");
        const expiresAt = this.now() + CODE_TTL_MS;
        this.challenge = {
            id,
            digest: this.codeDigest(id, code),
            expiresAt,
            attempts: 0,
        };
        return { challengeId: id, code, expiresAt };
    }

    complete(id: string, code: string) {
        this.prune();
        const now = this.now();
        if (now - this.windowStart >= 60_000) {
            this.windowStart = now;
            this.attempts = 0;
        }
        // Global budget also limits guesses spread across source addresses/unknown IDs.
        if (this.attempts >= 10) return null;
        ++this.attempts;
        const challenge = this.challenge;
        if (!challenge || challenge.id !== id) return null;
        ++challenge.attempts;
        const matches = timingSafeEqual(
            challenge.digest,
            this.codeDigest(id, code),
        );
        if (challenge.attempts >= 5 || matches) this.challenge = undefined;
        if (!matches || this.devices.size >= MAX_DEVICES) return null;
        const deviceId = randomUUID();
        const token = randomBytes(32).toString("base64url");
        const expiresAt = now + DEVICE_TTL_MS;
        this.devices.set(deviceId, {
            digest: this.tokenDigest(token),
            expiresAt,
        });
        return { deviceId, token, expiresAt };
    }

    private tokenDigest(token: string) {
        return createHash("sha256").update(token).digest();
    }

    authenticate(token: string): string | null {
        this.prune();
        const digest = this.tokenDigest(token);
        for (const [id, device] of this.devices) {
            if (timingSafeEqual(device.digest, digest)) return id;
        }
        return null;
    }

    revoke(id: string) {
        this.devices.delete(id);
    }

    clear() {
        this.challenge = undefined;
        this.devices.clear();
    }
}
