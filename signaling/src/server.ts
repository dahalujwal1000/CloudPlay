import { createHash, timingSafeEqual } from "node:crypto";
import Fastify, { LogController } from "fastify";
import websocket from "@fastify/websocket";
import rateLimit from "@fastify/rate-limit";
import type { Config } from "./config.js";
import { parseStatusRequest } from "./protocol.js";
import { PairingService } from "./pairing.js";
import { z } from "zod";
import type { WebSocket } from "ws";
import { isLoopback } from "./config.js";
import { readSecretServiceIdentity, validateTlsIdentity } from "./tls.js";

export async function buildServer(
    config: Config,
    logStream?: { write(message: string): void },
    readIdentity: (name: string) => Promise<string> = readSecretServiceIdentity,
) {
    if (!isLoopback(config.host) && (!config.allowLan || !config.tlsIdentity))
        throw new Error("LAN requires opt-in and TLS");
    const https = config.tlsIdentity
        ? validateTlsIdentity(
              await readIdentity(config.tlsIdentity),
              config.host,
          )
        : null;
    const expectedToken = createHash("sha256").update(config.token).digest();
    const pairing = new PairingService();
    const deviceSockets = new WeakMap<WebSocket, string>();
    const completionSchema = z
        .object({
            challengeId: z.uuid(),
            code: z.string().regex(/^[0-9]{8}$/),
        })
        .strict();
    const revokeSchema = z.object({ deviceId: z.uuid() }).strict();
    const app = Fastify({
        https,
        bodyLimit: 4096,
        logController: new LogController({ disableRequestLogging: true }),
        logger: {
            ...(logStream ? { stream: logStream } : {}),
            level: config.logLevel,
            // Never serialize incoming URLs, headers, bodies or raw errors.
            serializers: {
                req: () => ({}),
                res: () => ({}),
                err: () => ({
                    type: "Error",
                    message: "Operation failed",
                    stack: "",
                }),
            },
            redact: ["token", "authorization", "req.headers.authorization"],
        },
    });
    await app.register(rateLimit, { max: 60, timeWindow: "1 minute" });
    await app.register(websocket, {
        options: { maxPayload: 4096, perMessageDeflate: false },
    });

    app.setErrorHandler((_error, _request, reply) => {
        const status =
            typeof _error === "object" &&
            _error !== null &&
            "statusCode" in _error &&
            typeof _error.statusCode === "number" &&
            _error.statusCode >= 400 &&
            _error.statusCode < 500
                ? _error.statusCode
                : 500;
        return reply.code(status).send({
            error: status < 500 ? "REQUEST_REJECTED" : "INTERNAL_ERROR",
        });
    });
    app.setNotFoundHandler((_request, reply) =>
        reply.code(404).send({ error: "NOT_FOUND" }),
    );

    // Authenticate HTTP and WebSocket upgrades before accepting any connection.
    app.addHook("onRequest", async (request, reply) => {
        reply.header("Cache-Control", "no-store");
        // This bootstrap endpoint authenticates using the one-use pairing secret.
        // No host-control operation is available through it.
        if (
            request.method === "POST" &&
            request.routeOptions.url === "/v1/pairing/complete"
        )
            return;
        const header = request.headers.authorization;
        const token =
            typeof header === "string" && header.startsWith("Bearer ")
                ? header.slice(7)
                : "";
        const actual = createHash("sha256").update(token).digest();
        if (!timingSafeEqual(actual, expectedToken)) {
            if (
                request.method === "GET" &&
                (request.routeOptions.url === "/health" ||
                    request.routeOptions.url === "/v1/signaling") &&
                pairing.authenticate(token)
            )
                return;
            await reply.code(401).send({ error: "UNAUTHORIZED" });
        }
    });

    app.post(
        "/v1/pairing/start",
        { config: { rateLimit: { max: 5, timeWindow: "1 minute" } } },
        async (request, reply) => {
            if (!z.object({}).strict().safeParse(request.body).success)
                return reply.code(400).send({ error: "REQUEST_REJECTED" });
            const challenge = pairing.start();
            if (!challenge)
                return reply.code(409).send({ error: "PAIRING_UNAVAILABLE" });
            return reply.code(201).send(challenge);
        },
    );
    app.post(
        "/v1/pairing/complete",
        { config: { rateLimit: { max: 10, timeWindow: "1 minute" } } },
        async (request, reply) => {
            const parsed = completionSchema.safeParse(request.body);
            if (!parsed.success)
                return reply.code(400).send({ error: "REQUEST_REJECTED" });
            const device = pairing.complete(
                parsed.data.challengeId,
                parsed.data.code,
            );
            if (!device)
                return reply.code(401).send({ error: "PAIRING_REJECTED" });
            return reply.code(201).send(device);
        },
    );
    app.post("/v1/pairing/revoke", async (request, reply) => {
        const parsed = revokeSchema.safeParse(request.body);
        if (!parsed.success)
            return reply.code(400).send({ error: "REQUEST_REJECTED" });
        pairing.revoke(parsed.data.deviceId);
        for (const socket of app.websocketServer.clients) {
            if (deviceSockets.get(socket) === parsed.data.deviceId)
                socket.terminate();
        }
        return reply.code(204).send();
    });

    app.get("/health", async () => ({ status: "ok", protocolVersion: 1 }));
    app.get("/v1/signaling", { websocket: true }, (socket, request) => {
        const token = request.headers.authorization?.slice(7) ?? "";
        const deviceId = pairing.authenticate(token);
        const admin = timingSafeEqual(
            createHash("sha256").update(token).digest(),
            expectedToken,
        );
        if (!admin && !deviceId) {
            socket.close(1008, "Authentication expired");
            return;
        }
        if (deviceId) deviceSockets.set(socket, deviceId);
        let messages = 0;
        // A fixed per-connection lifetime budget bounds validation work, with no input queue.
        socket.on("message", (data, isBinary) => {
            if (socket.readyState !== socket.OPEN) return;
            if (deviceId && pairing.authenticate(token) !== deviceId) {
                socket.close(1008, "Authentication expired");
                return;
            }
            if (isBinary || ++messages > 60 || socket.bufferedAmount > 16_384) {
                socket.close(1008, "Connection budget exceeded");
                return;
            }
            const parsed = parseStatusRequest(data.toString());
            if (!parsed.success) {
                socket.close(1008, "Invalid message");
                return;
            }
            socket.send(
                JSON.stringify({
                    version: 1,
                    type: "service.status",
                    requestId: parsed.data.requestId,
                    payload: { status: "ready", capabilities: [] },
                }),
            );
        });
        const timeout = setTimeout(
            () => socket.close(1000, "Connection expired"),
            60_000,
        );
        timeout.unref();
        socket.on("close", () => clearTimeout(timeout));
        socket.on("error", () => socket.close());
    });

    // Terminate diagnostics sockets before server shutdown; no session resources exist yet.
    app.addHook("preClose", async () => {
        pairing.clear();
        for (const socket of app.websocketServer.clients) socket.terminate();
    });
    return app;
}
