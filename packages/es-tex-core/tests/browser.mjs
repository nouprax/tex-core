import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import path from "node:path";

const packageDirectory = path.resolve(fileURLToPath(new URL("..", import.meta.url)));
const chromeCandidates = [
    process.env.CHROME,
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser"
].filter(Boolean);
let chrome;
for (const candidate of chromeCandidates) {
    try {
        await (await import("node:fs/promises")).access(candidate);
        chrome = candidate;
        break;
    } catch {
        // Try the next platform-specific executable path.
    }
}
if (!chrome) throw new Error("browser suite requires Chrome/Chromium; set CHROME to its executable");

// The browser run covers the public runtime behaviors, not only a compile
// smoke: canonical dumps, structured errors, raw-byte UTF-8 rejection, the
// visitor over every node kind, and a conformance sample decoded from the
// shared fixtures bundle.
const html = `<!doctype html><meta charset="utf-8"><title>RUNNING</title><body id="result">RUNNING<script type="module">
  const failures = [];
  const check = (condition, label) => { if (!condition) failures.push(label); };
  try {
    const api = await import('/index.js');
    const tree = api.Document.compile('x', { mode: 'mathInline' });
    const glyph = tree.root.children[0];
    check(glyph.kind === 'glyph' && glyph.style === 'italic', 'glyph decode');
    check(tree.dump().startsWith('render-tree 5\\n'), 'dump header');
    check(tree.dump() === api.Document.compile('x', { mode: 'mathInline' }).dump(), 'determinism');
    check(!('memory' in api) && !('native' in api), 'no engine internals exported');

    // Structured fail-fast error with a byte range.
    let failure;
    try { api.Document.compile('\\\\frobnicate', { mode: 'mathInline' }); } catch (error) { failure = error; }
    check(failure instanceof api.CompileError, 'error type');
    check(failure?.status === 'unsupported', 'error status');
    check(failure?.range?.begin === 0, 'error range');
    check(Object.isFrozen(failure?.range), 'error range frozen');

    // Raw invalid bytes reach the engine's encoding validation.
    let encoding;
    try { api.Document.compile(new Uint8Array([0x61, 0xff])); } catch (error) { encoding = error; }
    check(encoding instanceof api.CompileError && encoding.status === 'invalidUtf8', 'raw-byte UTF-8 rejection');

    // The visitor dispatches over all four schema kinds.
    const counts = { hbox: 0, glyph: 0, kern: 0, rule: 0 };
    const visitor = {
      visitHBox(node) { counts.hbox += 1; for (const child of node.children) api.accept(child, visitor); },
      visitGlyph() { counts.glyph += 1; },
      visitKern() { counts.kern += 1; },
      visitRule() { counts.rule += 1; }
    };
    api.accept(api.Document.compile('\\\\frac{a}{2} \\\\sqrt{x} b', { mode: 'mathInline' }).root, visitor);
    check(counts.hbox > 0 && counts.glyph > 0 && counts.kern > 0 && counts.rule > 0, 'visitor coverage');

    // A conformance sample: the first tree cases must dump byte-identically
    // in the browser runtime too.
    const bundle = await (await fetch('/conformance.json')).json();
    const decode = (base64) => Uint8Array.from(atob(base64), (character) => character.codePointAt(0));
    const sample = bundle.cases.filter((entry) => entry.outcome === 'tree').slice(0, 8);
    check(sample.length > 0, 'conformance sample present');
    for (const entry of sample) {
      const compiled = api.Document.compile(decode(entry.sourceBase64), { mode: entry.mode });
      check(compiled.dump() === entry.expected, 'conformance: ' + entry.name);
    }

    document.title = failures.length === 0 ? 'PASS' : 'FAIL';
    document.body.textContent = failures.length === 0 ? 'PASS' : failures.join('; ');
  } catch (error) {
    document.title = 'FAIL'; document.body.textContent = String(error.stack || error);
  } finally {
    await fetch('/result?status=' + encodeURIComponent(document.title)
      + '&detail=' + encodeURIComponent(document.body.textContent.slice(0, 2000)));
  }
</script>`;

let reportBrowserResult;
const requests = [];
let serverErrors = 0;
let browserDetail = "";
const server = createServer(async (request, response) => {
    try {
        requests.push(request.url);
        if (request.url === "/") {
            response.setHeader("content-type", "text/html; charset=utf-8");
            response.end(html);
            return;
        }
        if (request.url.startsWith("/result?")) {
            const parameters = new URL(request.url, "http://127.0.0.1").searchParams;
            response.end("ok");
            browserDetail = parameters.get("detail") ?? "";
            reportBrowserResult?.(parameters.get("status"));
            return;
        }
        if (request.url === "/conformance.json") {
            response.setHeader("content-type", "application/json");
            response.end(
                await readFile(path.join(packageDirectory, "build/generated/conformance/render-tree-fixtures.json"))
            );
            return;
        }
        const name = request.url.slice(1);
        const resolved = path.resolve(packageDirectory, "dist", name);
        if (!resolved.startsWith(path.resolve(packageDirectory, "dist") + path.sep)) {
            response.statusCode = 404;
            response.end("not found");
            return;
        }
        let body;
        try {
            body = await readFile(resolved);
        } catch (error) {
            // A missing asset (Chrome's automatic favicon probe, for one) is
            // an ordinary 404, never a server error.
            if (error?.code === "ENOENT" || error?.code === "EISDIR") {
                response.statusCode = 404;
                response.end("not found");
                return;
            }
            throw error;
        }
        response.setHeader("content-type", name.endsWith(".wasm") ? "application/wasm" : "text/javascript");
        response.end(body);
    } catch (error) {
        // Diagnostics belong in the test process log, not in the response:
        // even on this loopback-only test server, echoing exception text as
        // the (default text/html) body trips stack-exposure and
        // HTML-reinterpretation scanning for good reason. An unexpected
        // server error also fails the suite after the run.
        serverErrors += 1;
        console.error(error);
        response.statusCode = 500;
        response.setHeader("content-type", "text/plain; charset=utf-8");
        response.end("internal test-server error");
    }
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
let child;
try {
    const { port } = server.address();
    const result = await new Promise((resolve, reject) => {
        let settled = false;
        let stderr = "";
        const finish = (callback, value) => {
            if (settled) return;
            settled = true;
            clearTimeout(timeout);
            callback(value);
        };
        reportBrowserResult = (status) => finish(resolve, status);
        child = spawn(chrome, [
            "--headless=new",
            "--disable-gpu",
            "--no-sandbox",
            "--disable-dev-shm-usage",
            "--disable-background-networking",
            "--disable-component-update",
            "--disable-default-apps",
            "--no-first-run",
            `http://127.0.0.1:${port}/`
        ]);
        child.stderr.setEncoding("utf8").on("data", (chunk) => {
            stderr += chunk;
        });
        const timeout = setTimeout(() => {
            finish(
                reject,
                new Error(
                    `Chrome browser test timed out\nexecutable: ${chrome}\nrequests: ${requests.join(", ")}\n${stderr}`
                )
            );
        }, 60_000);
        child.on("error", (error) => finish(reject, error));
        child.on("close", (status) => {
            finish(reject, new Error(stderr || `Chrome exited with ${status}`));
        });
    });
    assert.equal(result, "PASS", `browser reported ${result}: ${browserDetail}`);
    assert.equal(serverErrors, 0, "the test server answered a request with an unexpected 500");
    console.log("browser: Chrome/Chromium ESM + WASM compile, errors, visitor, and conformance sample passed");
} finally {
    reportBrowserResult = undefined;
    child?.kill();
    await new Promise((resolve) => server.close(resolve));
}
