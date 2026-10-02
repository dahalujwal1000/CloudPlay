import assert from "node:assert/strict";
import { randomBytes } from "node:crypto";
import { once } from "node:events";
import test from "node:test";
import { loadConfig } from "../src/config.js";
import { parseStatusRequest } from "../src/protocol.js";
import { buildServer } from "../src/server.js";

const token = randomBytes(32).toString("base64url");
const auth = { authorization: `Bearer ${token}` };
const message = {
    version: 1,
    type: "service.status",
    requestId: "cd3aab88-e5ed-4d9e-9e18-ae8f7098da81",
    payload: {},
};

test("configuration fails closed and does not disclose supplied secrets", () => {
    assert.throws(() => loadConfig({}), /token/);
    assert.throws(
        () => loadConfig({ CLOUDPLAY_TOKEN: token, CLOUDPLAY_HOST: "0.0.0.0" }),
        /host/,
    );
    for (const port of ["0", "8787x", "65536", "NaN"]) {
        assert.throws(
            () => loadConfig({ CLOUDPLAY_TOKEN: token, CLOUDPLAY_PORT: port }),
            /port/,
        );
    }
    const secret = "secret with spaces";
    assert.throws(
        () => loadConfig({ CLOUDPLAY_TOKEN: secret }),
        (error: unknown) =>
            error instanceof Error && !error.message.includes(secret),
    );
});

test("protocol rejects malformed, future and unexpected messages", () => {
    assert.equal(parseStatusRequest(JSON.stringify(message)).success, true);
    for (const raw of [
        "{",
        "{}",
        JSON.stringify({ ...message, version: 2 }),
        JSON.stringify({ ...message, payload: { token } }),
        JSON.stringify({ ...message, extra: true }),
        JSON.stringify({ ...message, type: "session.create" }),
    ]) {
        assert.equal(parseStatusRequest(raw).success, false);
    }
});

test("health and upgrades require bearer authentication", async (t) => {
    const app = await buildServer(
        loadConfig({ CLOUDPLAY_TOKEN: token, CLOUDPLAY_LOG_LEVEL: "silent" }),
    );
    t.after(() => app.close());
    for (const authorization of ["", "Bearer bad", token]) {
        const result = await app.inject({
            url: "/health",
            headers: { authorization },
        });
        assert.equal(result.statusCode, 401);
        assert.equal(result.body.includes(token), false);
    }
    assert.equal(
        (await app.inject({ url: "/health", headers: auth })).statusCode,
        200,
    );
    assert.equal((await app.inject({ url: "/v1/signaling" })).statusCode, 401);
    assert.equal(
        (
            await app.inject({
                url: "/v1/signaling",
                headers: {
                    connection: "upgrade",
                    upgrade: "websocket",
                    "sec-websocket-version": "13",
                    "sec-websocket-key": randomBytes(16).toString("base64"),
                },
            })
        ).statusCode,
        401,
    );
});

test("authenticated WebSocket diagnostics correlate responses and reject invalid input", async (t) => {
    const app = await buildServer(
        loadConfig({ CLOUDPLAY_TOKEN: token, CLOUDPLAY_LOG_LEVEL: "silent" }),
    );
    t.after(() => app.close());
    await app.ready();
    const socket = await app.injectWS("/v1/signaling", { headers: auth });
    const response = once(socket, "message");
    socket.send(JSON.stringify(message));
    const [data] = await response;
    assert.deepEqual(JSON.parse(String(data)), {
        ...message,
        payload: { status: "ready", capabilities: [] },
    });
    const closed = once(socket, "close");
    socket.send(JSON.stringify({ ...message, payload: { unexpected: true } }));
    const [code] = await closed;
    assert.equal(code, 1008);
});

test("request floods are bounded", async (t) => {
    const app = await buildServer(
        loadConfig({ CLOUDPLAY_TOKEN: token, CLOUDPLAY_LOG_LEVEL: "silent" }),
    );
    t.after(() => app.close());
    for (let i = 0; i < 60; i++)
        await app.inject({ url: "/health", headers: auth });
    assert.equal(
        (await app.inject({ url: "/health", headers: auth })).statusCode,
        429,
    );
});

test("WebSocket binary, oversize, and message floods are bounded", async (t) => {
    const app = await buildServer(
        loadConfig({ CLOUDPLAY_TOKEN: token, CLOUDPLAY_LOG_LEVEL: "silent" }),
    );
    t.after(() => app.close());
    await app.ready();
    for (const [payload, expected] of [
        [Buffer.from([1]), 1008],
        ["x".repeat(4097), 1009],
    ] as const) {
        const socket = await app.injectWS("/v1/signaling", { headers: auth });
        const closed = once(socket, "close");
        socket.send(payload);
        assert.equal((await closed)[0], expected);
    }
    const socket = await app.injectWS("/v1/signaling", { headers: auth });
    for (let i = 0; i < 60; i++) {
        const response = once(socket, "message");
        socket.send(JSON.stringify(message));
        await response;
    }
    const closed = once(socket, "close");
    socket.send(JSON.stringify(message));
    assert.equal((await closed)[0], 1008);
});

test("shutdown closes active sockets and logs omit request secrets", async () => {
    let records = "";
    const app = await buildServer(loadConfig({ CLOUDPLAY_TOKEN: token }), {
        write: (message) => {
            records += message;
        },
    });
    const secret = randomBytes(32).toString("hex");
    try {
        await app.inject({ url: `/unknown?token=${secret}`, headers: auth });
        app.log.error(
            {
                req: { headers: auth, url: secret },
                err: new Error(secret),
                token,
            },
            "Test event",
        );
        assert.equal(records.includes(token), false);
        assert.equal(records.includes(secret), false);
        const socket = await app.injectWS("/v1/signaling", { headers: auth });
        const closed = once(socket, "close");
        await app.close();
        await closed;
    } finally {
        await app.close();
    }
});
