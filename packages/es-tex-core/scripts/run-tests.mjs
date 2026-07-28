import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import path from "node:path";

const packageDirectory = path.resolve(fileURLToPath(new URL("..", import.meta.url)));

const correctnessSuites = [
    "api",
    "errors",
    "visitor",
    "robustness",
    "unicode",
    "immutability",
    "wire",
    "consumer",
    "types",
    "packaging"
];
const targetIndex = process.argv.indexOf("--target");
const suiteIndex = process.argv.indexOf("--suite");
const target = targetIndex >= 0 ? process.argv[targetIndex + 1] : "node";
const requested = suiteIndex >= 0 ? process.argv[suiteIndex + 1] : null;
// node-minimum: the pure engine-behavior suites, runnable on the oldest
// Node the package's `engines` range claims (no packaging toolchain).
const targets = {
    node: correctnessSuites,
    "node-minimum": ["api", "errors", "visitor", "robustness", "unicode", "immutability", "wire"],
    browser: ["api"]
};
const suites = targets[target];
if (!suites) throw new Error(`unsupported correctness target: ${target}`);
if (process.argv.includes("--list")) {
    process.stdout.write(`${suites.join("\n")}\n`);
    process.exit(0);
}
if (requested && !suites.includes(requested)) throw new Error(`unknown suite: ${requested}`);

function run(command, args) {
    const result = spawnSync(command, args, { cwd: packageDirectory, stdio: "inherit" });
    if (result.status !== 0) process.exit(result.status ?? 1);
}

// A filtered run that matches nothing exits 0, so a rename or pattern typo
// would turn the suite into a green no-op. Capture the TAP plan and require
// at least one executed (non-skipped) test.
function runNodeTest(args) {
    const result = spawnSync("node", ["--test", "--test-reporter=tap", ...args], {
        cwd: packageDirectory,
        encoding: "utf8"
    });
    process.stdout.write(result.stdout ?? "");
    process.stderr.write(result.stderr ?? "");
    if (result.status !== 0) process.exit(result.status ?? 1);
    const executed = Number((result.stdout ?? "").match(/^# pass (\d+)$/m)?.[1] ?? 0);
    if (!Number.isInteger(executed) || executed < 1) {
        process.stderr.write("test filter matched no executed tests\n");
        process.exit(1);
    }
}

if (!process.argv.includes("--skip-build")) run("node", ["scripts/build.mjs"]);
const selected = requested ? [requested] : suites;
if (target === "browser") {
    // The browser page verifies a conformance sample, so the shared
    // fixtures bundle must exist before the server starts.
    run("node", ["scripts/bundle-conformance-fixtures.mjs"]);
    run("node", ["tests/browser.mjs"]);
    process.exit(0);
}
const nodeSuiteNames = ["api", "errors", "visitor", "robustness", "unicode", "immutability", "wire"];
const selectedNodeSuites = selected.filter((suite) => nodeSuiteNames.includes(suite));
if (selectedNodeSuites.length) {
    runNodeTest([`--test-name-pattern=^(${selectedNodeSuites.join("|")}):`, "tests/node.test.mjs"]);
}
const packageSuites = selected.filter((suite) => ["consumer", "types", "packaging"].includes(suite));
if (packageSuites.length === 3) {
    run("node", ["tests/packaging.mjs", "all"]);
} else {
    for (const suite of packageSuites) run("node", ["tests/packaging.mjs", suite]);
}
