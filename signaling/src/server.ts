import { createHash, timingSafeEqual } from "node:crypto";
import Fastify, { LogController } from "fastify";
import websocket from "@fastify/websocket";
import rateLimit from "@fastify/rate-limit";
import type { Config } from "./config.js";
import { parseStatusRequest } from "./protocol.js";

export async function buildServer(
    config: Config,
    logStream?: { write(message: string): void },
) {
    const expectedToken = createHash("sha256").update(config.token).digest();
    const app = Fastify({
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
        const header = request.headers.authorization;
        const token =
            typeof header === "string" && header.startsWith("Bearer ")
                ? header.slice(7)
                : "";
        const actual = createHash("sha256").update(token).digest();
        if (!timingSafeEqual(actual, expectedToken)) {
            await reply.code(401).send({ error: "UNAUTHORIZED" });
        }
    });

    app.get("/health", async () => ({ status: "ok", protocolVersion: 1 }));
    app.get("/v1/signaling", { websocket: true }, (socket) => {
        let messages = 0;
        // A fixed per-connection lifetime budget bounds validation work, with no input queue.
        socket.on("message", (data, isBinary) => {
            if (socket.readyState !== socket.OPEN) return;
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
        for (const socket of app.websocketServer.clients) socket.terminate();
    });
    return app;
}
