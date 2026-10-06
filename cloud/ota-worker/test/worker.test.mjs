// node test/worker.test.mjs: the worker against an in-memory R2.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const src = readFileSync(new URL("../src/index.js", import.meta.url), "utf8");
const worker = (await import("data:text/javascript," + encodeURIComponent(src))).default;

const files = { "manifest.json": '{"version":"1.0.0"}', "files/app-1.0.0.bin": "0123456789" };
const env = {
  OTA: {
    async get(key, opts) {
      if (!(key in files)) return null;
      const body = files[key];
      let range;
      const h = opts?.range?.get?.("range");
      if (h) {
        const [a, b] = h.replace("bytes=", "").split("-").map((x) => (x === "" ? undefined : Number(x)));
        const offset = a ?? 0, end = b ?? body.length - 1;
        range = { offset, length: end - offset + 1 };
      }
      const part = range ? body.slice(range.offset, range.offset + range.length) : body;
      return { size: body.length, range, httpEtag: '"e"', body: part, writeHttpMetadata() {} };
    },
  },
};
const get = (path, headers = {}) => worker.fetch(new Request("https://x.workers.dev" + path, { headers }), env);

let r = await get("/v1/manifest.json");
assert.equal(r.status, 200);
assert.equal(await r.text(), '{"version":"1.0.0"}');
assert.equal(r.headers.get("cache-control"), "no-cache");

r = await get("/v1/files/app-1.0.0.bin", { range: "bytes=4-" });
assert.equal(r.status, 206);
assert.equal(await r.text(), "456789");
assert.equal(r.headers.get("content-range"), "bytes 4-9/10");

for (const bad of ["/v1/files/..%2Fmanifest.json", "/v1/files/..", "/v1/files/a%2Fb", "/v1/other", "/v1/manifest.sig"]) {
  assert.equal((await get(bad)).status, 404, bad);
}
r = await worker.fetch(new Request("https://x.workers.dev/v1/manifest.json", { method: "POST" }), env);
assert.equal(r.status, 405);
console.log("worker ok");
