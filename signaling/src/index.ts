import { loadConfig } from "./config.js";
import { buildServer } from "./server.js";

async function main() {
    const config = loadConfig(process.env);
    const app = await buildServer(config);
    let closing = false;
    const shutdown = () => {
        if (closing) return;
        closing = true;
        void app.close().catch(() => {
            process.exitCode = 1;
        });
    };
    process.once("SIGINT", shutdown);
    process.once("SIGTERM", shutdown);
    await app.listen({ host: config.host, port: config.port });
}

main().catch(() => {
    console.error(
        "Signaling startup failed. Check configuration and port availability.",
    );
    process.exitCode = 1;
});
