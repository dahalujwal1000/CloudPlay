import {
    IdentityProvisionError,
    inspectIdentity,
    provisionIdentity,
} from "./identity.js";

const usage =
    "identity provision --host <IP> | identity inspect --identity <name> --host <IP>";
const [operation, ...args] = process.argv.slice(2);

async function main() {
    if (operation === "--help" && args.length === 0) {
        console.log(usage);
        return;
    }
    const options = new Map<string, string>();
    for (let i = 0; i < args.length; i += 2) {
        const key = args[i];
        const value = args[i + 1];
        if (
            !key ||
            !value ||
            !["--host", "--identity"].includes(key) ||
            options.has(key)
        )
            throw new Error(usage);
        options.set(key, value);
    }
    const host = options.get("--host");
    const identity = options.get("--identity");
    if (
        !host ||
        (operation !== "provision" && operation !== "inspect") ||
        (operation === "provision" && identity) ||
        (operation === "inspect" && !identity)
    )
        throw new Error(usage);
    const result =
        operation === "provision"
            ? await provisionIdentity(host)
            : await inspectIdentity(identity!, host);
    console.log(JSON.stringify(result));
}

void main().catch((error: unknown) => {
    // Do not print arbitrary child errors or supplied argument values.
    console.error(
        "Identity setup failed. Check arguments, OpenSSL, and unlocked Secret Service. No identity was activated.",
    );
    if (error instanceof IdentityProvisionError)
        console.error(
            `An unused Secret Service entry may remain: ${error.identity}`,
        );
    process.exitCode = 1;
});
