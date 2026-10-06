// Copyright (c) 2026 Boopie contributors
// SPDX-License-Identifier: Apache-2.0
//
// Boopie's OTA server: serves what the release workflow put in R2.
//
//   GET /v1/manifest.json   the latest release (version, files, sizes, SHA-256)
//   GET /v1/manifest.sig    its ECDSA P-256 signature (raw r || s), checked by the board
//   GET /v1/files/<name>    a release file; Range requests resume a download
//
// The board trusts nothing here by itself: the manifest is signed with a key
// only the release workflow holds, and each file must match its SHA-256 in it.

const MANIFEST = new Set(["manifest.json", "manifest.sig"]);

export default {
  async fetch(request, env) {
    if (request.method !== "GET" && request.method !== "HEAD") {
      return new Response("method not allowed", { status: 405, headers: { allow: "GET, HEAD" } });
    }
    const path = new URL(request.url).pathname;
    let key = null;
    if (path.startsWith("/v1/") && MANIFEST.has(path.slice(4))) {
      key = path.slice(4);
    } else if (path.startsWith("/v1/files/")) {
      const name = path.slice("/v1/files/".length);
      if (/^[A-Za-z0-9._-]{1,128}$/.test(name) && !name.includes("..")) {
        key = "files/" + name;
      }
    }
    if (!key) {
      return new Response("not found", { status: 404 });
    }

    const range = request.headers.get("range");
    const object = await env.OTA.get(key, range ? { range: request.headers } : undefined);
    if (!object) {
      return new Response("not found", { status: 404 });
    }

    const headers = new Headers();
    object.writeHttpMetadata(headers);
    headers.set("etag", object.httpEtag);
    headers.set("accept-ranges", "bytes");
    headers.set("content-type", key.endsWith(".json") ? "application/json" : "application/octet-stream");
    // The manifest changes with each release; a file's name carries its version.
    headers.set("cache-control", MANIFEST.has(key) ? "no-cache" : "public, max-age=31536000, immutable");

    let status = 200;
    if (range && object.range) {
      const offset = object.range.offset ?? 0;
      const length = object.range.length ?? object.size - offset;
      headers.set("content-range", `bytes ${offset}-${offset + length - 1}/${object.size}`);
      headers.set("content-length", String(length));
      status = 206;
    } else {
      headers.set("content-length", String(object.size));
    }
    return new Response(request.method === "HEAD" ? null : object.body, { status, headers });
  },
};
