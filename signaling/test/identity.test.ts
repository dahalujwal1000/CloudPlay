import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { randomBytes, X509Certificate } from "node:crypto";
import { request } from "node:https";
import test from "node:test";
import {
    IdentityProvisionError,
    inspectIdentity,
    privateCommand,
    provisionIdentity,
} from "../src/identity.js";
import { loadConfig } from "../src/config.js";
import { buildServer } from "../src/server.js";

function fakeKeyring() {
    const entries = new Map<string, string>();
    const calls: { file: string; args: string[] }[] = [];
    const command = async (file: string, args: string[], input: string) => {
        calls.push({ file, args });
        assert.equal(args.join(" ").includes("PRIVATE KEY"), false);
        if (file === "openssl") {
            assert.equal(args[args.indexOf("-keyout") + 1], "-");
            assert.equal(args.includes("-out"), false);
            assert.equal(input, "");
            return privateCommand(file, args, input);
        }
        assert.equal(file, "secret-tool");
        assert.equal(args[0], "store");
        const name = args.at(-1)!;
        assert.equal(entries.has(name), false);
        entries.set(name, input);
        return "";
    };
    const read = async (name: string) => {
        const value = entries.get(name);
        if (!value) throw new Error("Missing identity");
        return value;
    };
    return { entries, command, read, calls };
}

test("provisioning creates distinct validated identities and returns only public metadata", async () => {
    const store = fakeKeyring();
    const first = await provisionIdentity(
        "192.168.1.20",
        store.command,
        store.read,
    );
    const second = await provisionIdentity(
        "192.168.1.20",
        store.command,
        store.read,
    );
    assert.notEqual(first.identity, second.identity);
    assert.notEqual(first.certificateSha256, second.certificateSha256);
    assert.equal(store.entries.size, 2);
    assert.deepEqual(Object.keys(first).sort(), [
        "certificateSha256",
        "expiresAt",
        "host",
        "identity",
    ]);
    assert.match(first.certificateSha256, /^(?:[A-F0-9]{2}:){31}[A-F0-9]{2}$/);
    assert.equal(JSON.stringify(first).includes("PRIVATE KEY"), false);
    assert.deepEqual(
        await inspectIdentity(first.identity, first.host, store.read),
        first,
    );
    const certificate = new X509Certificate(
        JSON.parse(await store.read(first.identity)).certificate,
    );
    assert.equal(certificate.ca, false);
    assert.equal(certificate.checkIP("127.0.0.1"), "127.0.0.1");
    assert.equal(certificate.checkIP("::1"), "::1");
    assert.equal(certificate.checkIP(first.host), first.host);
    assert.ok(Date.parse(first.expiresAt) - Date.now() <= 90 * 86400000);
});

test("invalid input never invokes key generation or Secret Service", async () => {
    let calls = 0;
    const unexpected = async () => {
        calls++;
        throw new Error("unexpected");
    };
    for (const host of [
        "0.0.0.0",
        "8.8.8.8",
        "1.2.3.4;command",
        "192.168.1.2\nsecret",
    ]) {
        await assert.rejects(provisionIdentity(host, unexpected, unexpected));
    }
    await assert.rejects(inspectIdentity("../secret", "127.0.0.1", unexpected));
    assert.equal(calls, 0);
});

test("command/store/read-back failures never disclose secrets or activate an identity", async () => {
    const secret = "sensitive-child-output";
    await assert.rejects(
        provisionIdentity("127.0.0.1", async () => {
            throw new Error(secret);
        }),
        (error: unknown) =>
            error instanceof IdentityProvisionError &&
            !error.message.includes(secret),
    );
    const store = fakeKeyring();
    await assert.rejects(
        provisionIdentity("127.0.0.1", store.command, async () => {
            throw new Error(secret);
        }),
        (error: unknown) =>
            error instanceof IdentityProvisionError &&
            store.entries.has(error.identity),
    );
    const other = fakeKeyring();
    const old = await provisionIdentity("127.0.0.1", other.command, other.read);
    await assert.rejects(
        provisionIdentity("127.0.0.1", store.command, async () =>
            other.read(old.identity),
        ),
        IdentityProvisionError,
    );
    await assert.rejects(
        inspectIdentity(old.identity, old.host, async () => {
            throw new Error(secret);
        }),
        /^Error: Unable to inspect TLS identity$/,
    );
    await assert.rejects(
        privateCommand(
            process.execPath,
            [
                "-e",
                "process.stdin.resume(); process.stdin.on('data', d => { process.stderr.write(d); process.stdout.write(d); process.exit(1); });",
            ],
            secret,
        ),
        /^Error: Identity operation failed$/,
    );
    await assert.rejects(
        privateCommand(
            process.execPath,
            ["-e", "process.stdout.write('x'.repeat(70000))"],
            "",
        ),
        /^Error: Identity operation failed$/,
    );
});

test("generated identity serves authenticated HTTPS with normal trust validation", async (t) => {
    const store = fakeKeyring();
    const identity = await provisionIdentity(
        "127.0.0.1",
        store.command,
        store.read,
    );
    const token = randomBytes(32).toString("base64url");
    const app = await buildServer(
        loadConfig({
            CLOUDPLAY_TOKEN: token,
            CLOUDPLAY_TLS_IDENTITY: identity.identity,
            CLOUDPLAY_LOG_LEVEL: "silent",
        }),
        undefined,
        store.read,
    );
    t.after(() => app.close());
    const address = await app.listen({ host: "127.0.0.1", port: 0 });
    const status = await new Promise<number>((resolve, reject) => {
        const req = request(
            `${address}/health`,
            {
                ca: JSON.parse(store.entries.get(identity.identity)!)
                    .certificate,
                headers: { authorization: `Bearer ${token}` },
            },
            (res) => {
                res.resume();
                resolve(res.statusCode!);
            },
        );
        req.setTimeout(2000, () => req.destroy(new Error("Timed out")));
        req.on("error", reject);
        req.end();
    });
    assert.equal(status, 200);
});

test("identity CLI help and malformed requests never contact a keyring", () => {
    const script = new URL("../src/identity-cli.js", import.meta.url);
    assert.match(
        execFileSync(process.execPath, [script.pathname, "--help"], {
            encoding: "utf8",
        }),
        /identity provision/,
    );
    for (const args of [
        [],
        ["provision"],
        ["inspect", "--host", "127.0.0.1"],
        ["provision", "--host", "127.0.0.1", "--host", "127.0.0.1"],
        ["provision", "--host", "public.example"],
        ["provision", "--host", "127.0.0.1", "--identity", "existing"],
    ]) {
        assert.throws(() =>
            execFileSync(process.execPath, [script.pathname, ...args], {
                stdio: "pipe",
                timeout: 5000,
            }),
        );
    }
});
