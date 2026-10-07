import assert from "node:assert/strict";
import { randomBytes, randomUUID } from "node:crypto";
import { once } from "node:events";
import test from "node:test";
import { PairingService } from "../src/pairing.js";
import { buildServer } from "../src/server.js";
import { loadConfig } from "../src/config.js";

test("pairing consumes one code, expires credentials, and revokes without revealing tokens", () => {
    let now = 1000;
    const service = new PairingService(() => now);
    const challenge = service.start()!;
    assert.match(challenge.code, /^\d{8}$/);
    const device = service.complete(challenge.challengeId, challenge.code)!;
    assert.ok(device);
    assert.equal(service.complete(challenge.challengeId, challenge.code), null);
    assert.equal(service.authenticate(device.token), device.deviceId);
    assert.equal(service.authenticate("wrong"), null);
    now = device.expiresAt;
    assert.equal(service.authenticate(device.token), null);
    const next = service.start()!;
    const second = service.complete(next.challengeId, next.code)!;
    service.revoke(second.deviceId);
    assert.equal(service.authenticate(second.token), null);
});

test("expiry, rotation and five guesses invalidate challenges", () => {
    let now = 0;
    const service = new PairingService(() => now);
    const old = service.start()!;
    const current = service.start()!;
    assert.equal(service.complete(old.challengeId, old.code), null);
    const wrong = current.code === "00000000" ? "11111111" : "00000000";
    for (let i = 0; i < 5; i++)
        assert.equal(service.complete(current.challengeId, wrong), null);
    assert.equal(service.complete(current.challengeId, current.code), null);
    now += 60_000;
    const expired = service.start()!;
    now = expired.expiresAt;
    assert.equal(service.complete(expired.challengeId, expired.code), null);
});

test("global budget survives code rotation and unknown challenge guesses", () => {
    let now = 0;
    const service = new PairingService(() => now);
    for (let i = 0; i < 10; i++)
        assert.equal(service.complete(randomUUID(), "12345678"), null);
    const challenge = service.start()!;
    assert.equal(service.complete(challenge.challengeId, challenge.code), null);
    now = 60_000;
    assert.ok(service.complete(challenge.challengeId, challenge.code));
});

test("device capacity is bounded and clear/restart invalidate credentials", () => {
    let now = 0;
    const service = new PairingService(() => now);
    let last;
    for (let i = 0; i < 16; i++) {
        now = i * 60_000;
        const challenge = service.start()!;
        last = service.complete(challenge.challengeId, challenge.code)!;
        assert.ok(last);
    }
    // The first token has expired at this point; refill its slot.
    const refill = service.start()!;
    assert.ok(service.complete(refill.challengeId, refill.code));
    assert.equal(service.start(), null);
    assert.equal(new PairingService().authenticate(last!.token), null);
    service.clear();
    assert.equal(service.authenticate(last!.token), null);
    assert.ok(service.start());
});

test("HTTP pairing grants diagnostics only; replay/revocation fail and secrets stay out of logs", async (t) => {
    const token = randomBytes(32).toString("base64url");
    const headers = { authorization: `Bearer ${token}` };
    let records = "";
    const app = await buildServer(loadConfig({ CLOUDPLAY_TOKEN: token }), {
        write: (line) => {
            records += line;
        },
    });
    t.after(() => app.close());
    assert.equal(
        (
            await app.inject({
                method: "POST",
                url: "/v1/pairing/start",
                payload: {},
            })
        ).statusCode,
        401,
    );
    const started = await app.inject({
        method: "POST",
        url: "/v1/pairing/start",
        headers,
        payload: {},
    });
    assert.equal(started.statusCode, 201);
    assert.equal(started.headers["cache-control"], "no-store");
    const { challengeId, code } = started.json();
    const payload = { challengeId, code };
    assert.equal(
        (
            await app.inject({
                method: "POST",
                url: "/v1/pairing/complete",
                payload: { ...payload, admin: true },
            })
        ).statusCode,
        400,
    );
    const paired = await app.inject({
        method: "POST",
        url: "/v1/pairing/complete",
        payload,
    });
    assert.equal(paired.statusCode, 201);
    const device = paired.json();
    const deviceHeaders = { authorization: `Bearer ${device.token}` };
    assert.equal(
        (
            await app.inject({
                method: "POST",
                url: "/v1/pairing/complete",
                payload,
            })
        ).statusCode,
        401,
    );
    assert.equal(
        (await app.inject({ url: "/health", headers: deviceHeaders }))
            .statusCode,
        200,
    );
    for (const url of ["/v1/pairing/start", "/v1/pairing/revoke"]) {
        assert.equal(
            (
                await app.inject({
                    method: "POST",
                    url,
                    headers: deviceHeaders,
                    payload: {},
                })
            ).statusCode,
            401,
        );
    }
    const socket = await app.injectWS("/v1/signaling", {
        headers: deviceHeaders,
    });
    const response = once(socket, "message");
    socket.send(
        JSON.stringify({
            version: 1,
            type: "service.status",
            requestId: randomUUID(),
            payload: {},
        }),
    );
    assert.equal(
        JSON.parse(String((await response)[0])).payload.status,
        "ready",
    );
    const closed = once(socket, "close");
    assert.equal(
        (
            await app.inject({
                method: "POST",
                url: "/v1/pairing/revoke",
                headers,
                payload: { deviceId: device.deviceId },
            })
        ).statusCode,
        204,
    );
    await closed;
    assert.equal(
        (await app.inject({ url: "/health", headers: deviceHeaders }))
            .statusCode,
        401,
    );
    for (const secret of [token, code, device.token])
        assert.equal(records.includes(secret), false);
});

test("concurrent redemption creates only one device credential", async (t) => {
    const token = randomBytes(32).toString("base64url");
    const app = await buildServer(
        loadConfig({ CLOUDPLAY_TOKEN: token, CLOUDPLAY_LOG_LEVEL: "silent" }),
    );
    t.after(() => app.close());
    const started = await app.inject({
        method: "POST",
        url: "/v1/pairing/start",
        headers: { authorization: `Bearer ${token}` },
        payload: {},
    });
    const { challengeId, code } = started.json();
    const results = await Promise.all(
        [0, 1].map(() =>
            app.inject({
                method: "POST",
                url: "/v1/pairing/complete",
                payload: { challengeId, code },
            }),
        ),
    );
    assert.deepEqual(
        results.map((result) => result.statusCode).sort(),
        [201, 401],
    );
});

test("redemption is rate-limited without an administrator credential", async (t) => {
    const app = await buildServer(
        loadConfig({
            CLOUDPLAY_TOKEN: randomBytes(32).toString("base64url"),
            CLOUDPLAY_LOG_LEVEL: "silent",
        }),
    );
    t.after(() => app.close());
    for (let i = 0; i < 10; i++) {
        assert.equal(
            (
                await app.inject({
                    method: "POST",
                    url: "/v1/pairing/complete",
                    payload: { challengeId: randomUUID(), code: "12345678" },
                })
            ).statusCode,
            401,
        );
    }
    assert.equal(
        (
            await app.inject({
                method: "POST",
                url: "/v1/pairing/complete",
                payload: {},
            })
        ).statusCode,
        429,
    );
});
