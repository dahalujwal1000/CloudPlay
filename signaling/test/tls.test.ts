import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import {
    generateKeyPairSync,
    randomBytes,
    randomUUID,
    X509Certificate,
} from "node:crypto";
import { once } from "node:events";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { request } from "node:https";
import test, { after, before } from "node:test";
import WebSocket from "ws";
import { loadConfig } from "../src/config.js";
import { buildServer } from "../src/server.js";
import { readSecretServiceIdentity, validateTlsIdentity } from "../src/tls.js";

let directory: string;
let certificate: string;
let privateKey: string;
let serialized: string;
const token = randomBytes(32).toString("base64url");

before(() => {
    directory = mkdtempSync(join(tmpdir(), "cloudplay-tls-test-"));
    execFileSync(
        "openssl",
        [
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-nodes",
            "-days",
            "1",
            "-subj",
            "/CN=CloudPlay Test",
            "-addext",
            "subjectAltName=IP:127.0.0.1,IP:::1,IP:10.0.0.1",
            "-keyout",
            join(directory, "key.pem"),
            "-out",
            join(directory, "cert.pem"),
        ],
        { stdio: "ignore" },
    );
    certificate = readFileSync(join(directory, "cert.pem"), "utf8");
    privateKey = readFileSync(join(directory, "key.pem"), "utf8");
    serialized = JSON.stringify({ certificate, privateKey });
});
after(() => {
    if (directory) rmSync(directory, { recursive: true, force: true });
});

test("LAN requires explicit opt-in, private address and keyring identity", () => {
    for (const host of [
        "0.0.0.0",
        "::",
        "8.8.8.8",
        "example.com",
        "172.32.0.1",
        "169.254.1.1",
    ]) {
        assert.throws(() =>
            loadConfig({
                CLOUDPLAY_TOKEN: token,
                CLOUDPLAY_HOST: host,
                CLOUDPLAY_ALLOW_LAN: "true",
                CLOUDPLAY_TLS_IDENTITY: "test",
            }),
        );
    }
    for (const host of [
        "10.0.0.1",
        "192.168.1.2",
        "172.16.0.1",
        "172.31.255.254",
    ]) {
        assert.throws(() =>
            loadConfig({ CLOUDPLAY_TOKEN: token, CLOUDPLAY_HOST: host }),
        );
        assert.throws(() =>
            loadConfig({
                CLOUDPLAY_TOKEN: token,
                CLOUDPLAY_HOST: host,
                CLOUDPLAY_ALLOW_LAN: "true",
            }),
        );
        assert.throws(() =>
            loadConfig({
                CLOUDPLAY_TOKEN: token,
                CLOUDPLAY_HOST: host,
                CLOUDPLAY_TLS_IDENTITY: "test",
            }),
        );
        assert.equal(
            loadConfig({
                CLOUDPLAY_TOKEN: token,
                CLOUDPLAY_HOST: host,
                CLOUDPLAY_ALLOW_LAN: "true",
                CLOUDPLAY_TLS_IDENTITY: "test",
            }).host,
            host,
        );
    }
    assert.throws(() =>
        loadConfig({
            CLOUDPLAY_TOKEN: token,
            CLOUDPLAY_TLS_IDENTITY: "../key",
        }),
    );
    assert.throws(() =>
        loadConfig({ CLOUDPLAY_TOKEN: token, CLOUDPLAY_ALLOW_LAN: "yes" }),
    );
});

test("TLS material validates dates, bind address and key pairing without leaking input", async () => {
    assert.equal(
        validateTlsIdentity(serialized, "127.0.0.1").minVersion,
        "TLSv1.2",
    );
    const cert = new X509Certificate(certificate);
    for (const now of [
        Date.parse(cert.validFrom) - 1,
        Date.parse(cert.validTo),
        NaN,
    ]) {
        assert.throws(
            () => validateTlsIdentity(serialized, "127.0.0.1", now),
            /TLS identity invalid/,
        );
    }
    assert.throws(() => validateTlsIdentity(serialized, "192.168.1.5"));
    const otherKey = generateKeyPairSync("rsa", {
        modulusLength: 2048,
    }).privateKey.export({ type: "pkcs8", format: "pem" });
    assert.throws(() =>
        validateTlsIdentity(
            JSON.stringify({ certificate, privateKey: otherKey }),
            "127.0.0.1",
        ),
    );
    for (const value of [
        "private-secret",
        "x".repeat(65537),
        JSON.stringify({ certificate, privateKey, extra: "secret" }),
    ]) {
        assert.throws(
            () => validateTlsIdentity(value, "127.0.0.1"),
            (error: unknown) =>
                error instanceof Error &&
                error.message ===
                    "TLS identity invalid, expired, or not valid for bind address",
        );
    }
    await assert.rejects(
        readSecretServiceIdentity("--invalid"),
        /^Error: TLS identity unavailable from Secret Service$/,
    );
});

test("TLS startup fails closed when identity retrieval fails", async () => {
    const config = loadConfig({
        CLOUDPLAY_TOKEN: token,
        CLOUDPLAY_TLS_IDENTITY: "test",
        CLOUDPLAY_LOG_LEVEL: "silent",
    });
    await assert.rejects(
        buildServer(config, undefined, async () => ""),
        /TLS identity invalid/,
    );
});

test("real HTTPS and WSS enforce certificate trust, hostname and authentication", async (t) => {
    const app = await buildServer(
        loadConfig({
            CLOUDPLAY_TOKEN: token,
            CLOUDPLAY_TLS_IDENTITY: "test",
            CLOUDPLAY_LOG_LEVEL: "silent",
        }),
        undefined,
        async () => serialized,
    );
    t.after(() => app.close());
    const address = await app.listen({ host: "127.0.0.1", port: 0 });
    assert.match(address, /^https:/);
    const get = (trusted: boolean, authorized: boolean, servername?: string) =>
        new Promise<number>((resolve, reject) => {
            const req = request(
                `${address}/health`,
                {
                    ...(trusted ? { ca: certificate } : {}),
                    ...(servername ? { servername } : {}),
                    headers: authorized
                        ? { authorization: `Bearer ${token}` }
                        : {},
                },
                (res) => {
                    res.resume();
                    resolve(res.statusCode!);
                },
            );
            req.setTimeout(2000, () =>
                req.destroy(new Error("TLS request timed out")),
            );
            req.on("error", reject);
            req.end();
        });
    assert.equal(await get(true, true), 200);
    assert.equal(await get(true, false), 401);
    await assert.rejects(get(false, true));
    await assert.rejects(get(true, true, "wrong.invalid"));
    const socket = new WebSocket(
        `${address.replace("https:", "wss:")}/v1/signaling`,
        { ca: certificate, headers: { authorization: `Bearer ${token}` } },
    );
    t.after(() => socket.terminate());
    await once(socket, "open");
    const received = once(socket, "message");
    socket.send(
        JSON.stringify({
            version: 1,
            type: "service.status",
            requestId: randomUUID(),
            payload: {},
        }),
    );
    assert.equal(
        JSON.parse(String((await received)[0])).payload.status,
        "ready",
    );
    const closed = once(socket, "close");
    socket.close();
    await closed;
});
