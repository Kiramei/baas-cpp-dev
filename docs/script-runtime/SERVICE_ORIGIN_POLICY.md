# HTTP Origin/CORS policy foundation

This document records the bounded HTTP browser-origin boundary implemented by
`BAAS_service_origin_policy` and used by `HttplibAdapter`/`HttpHost`. It is a
CORS policy foundation, not user authentication, transport authentication,
authorization, TLS, or permission to expose the service beyond loopback.
`HttpHost` still binds only IPv4 `127.0.0.1`.

## Audited evidence and deliberate compatibility boundary

The implementation was derived from read-only inspection of the adjacent
repositories and frozen protocol:

- `baas-dev/service/app.py` configures FastAPI `CORSMiddleware` with credentials,
  wildcard methods/headers, and a default regex covering localhost, Tauri,
  all ports, and broad private IPv4 ranges. The regex is replaceable through
  `BAAS_SERVICE_CORS_ORIGIN_REGEX`.
- `baas-dev/service/api/security.py::is_allowed_origin` accepts a comma-separated
  `BAAS_SERVICE_ALLOWED_ORIGINS` set, loopback/Tauri hosts, and any origin host
  equal to the request Host. It also permits a missing Origin. This function is
  used for WebSocket handshakes, not FastAPI HTTP CORS.
- `baas-tauri/src-tauri/tauri.conf.json` declares desktop development origin
  `http://localhost:8191`; `tauri.android.conf.json` declares
  `http://127.0.0.1:8191`; Android capability URLs repeat those exact origins.
- the checked-in generated Android backend falls back to
  `http://tauri.localhost`, and native Rust health probing does not add Origin.
- `SERVICE_PROTOCOL_V1.md` section 6.3 requires credentialed Tauri/WebView CORS,
  configured/Tauri/loopback/same-host development origins, and rejection of
  arbitrary public origins. Section 12 requires loopback defaults and explicit
  policy before LAN exposure, while recording the origin/CORS matrix and wider
  security review as missing.

The Python rules are broader and internally different between HTTP and
WebSocket. C++ therefore does not guess compatibility by copying either regex.
It uses an exact, configured allowlist and fails closed. The default set is only
the three Tauri origins evidenced above. Deployments may replace that set with
explicit serialized origins; no wildcard, host suffix, same-host inference,
private-range inference, or arbitrary-port rule exists.

## Configuration and normalization

`CorsPolicyConfig` contains exact allowed origins, methods, headers, and the
explicit `allow_requests_without_origin` switch. Defaults are:

- origins: `http://localhost:8191`, `http://127.0.0.1:8191`, and
  `http://tauri.localhost`;
- methods: `GET`, `HEAD`, and `POST`;
- non-simple/requested headers: `accept` and `content-type`;
- requests without Origin: allowed, preserving local native clients.

An Origin is a serialized origin, never a URL. Parsing permits only `http`,
`https`, and the exact `tauri://localhost` form; lowercases scheme/host and
removes HTTP 80/HTTPS 443. It rejects `null`, wildcards, CR/LF and other control
or non-ASCII bytes, userinfo, path, query, fragment, percent/backslash/comma,
empty/invalid ports, invalid/legacy IPv4 spelling, malformed DNS labels,
unbracketed IPv6, and non-loopback bracketed IPv6. A syntactically valid origin
is still rejected unless its canonical value is in the exact allowlist.

Fixed bounds are 512 Origin bytes, 32 configured origins, 8 KiB total bytes per
allowlist, 16 methods, 32 configured/requested headers, 32 method bytes, and
2 KiB requested-header bytes. Relevant HTTP header cardinality is classified
as zero/one/multiple with constant work. Policy evaluation views request-owned
header storage, so attacker-controlled values are length-checked before a
policy copy or parse. Constructing an invalid or oversized configuration throws
`std::invalid_argument` before a listener can start.

## Request and response matrix

| Request | Result |
| --- | --- |
| no Origin and native behavior enabled | Router/transport response unchanged; no CORS headers |
| allowed Origin and allowed actual method | Router/transport response plus exact `Access-Control-Allow-Origin`, credentials `true`, and `Vary: Origin` |
| valid preflight | empty 204 with exact origin, credentials, deterministic allowed methods, only normalized requested/allowed headers, and all three `Vary` inputs |
| malformed, duplicate, or denied Origin | stable JSON 403; `Vary` present; no allow/credential headers |
| denied/malformed preflight method or headers | stable JSON 403; all preflight `Vary` inputs; no allow/credential headers |
| cpp-httplib payload-limit rejection | same Origin decision in the error handler; allowed actual Origin keeps 413 plus actual CORS headers, denied Origin gets stable 403 |

Rejection codes are `origin_required`, `invalid_origin`,
`origin_not_allowed`, `cors_invalid_request`, `cors_invalid_method`,
`cors_method_not_allowed`, `cors_invalid_headers`, and
`cors_headers_not_allowed`. Rejections deliberately reveal no allowlist
contents. `Access-Control-Allow-Origin: *` is never emitted, especially with
credentials.

Origin is browser-supplied context and can be forged by native clients. A
missing Origin being accepted is not a bypass because this layer is not an
authentication control in the first place. Privileged routes still require
their own loopback, platform, session, authentication, and authorization checks.

## Build and tests

The pure target has no cpp-httplib/Conan dependency and is part of the standard
Windows/Linux/macOS Debug and Release foundation CI:

```powershell
cmake -S . -B build\origin-policy -DBUILD_TESTING=ON `
  -DBUILD_SERVICE_ORIGIN_POLICY_TESTS=ON -DBAAS_FETCH_RESOURCES=OFF
cmake --build build\origin-policy --target BAAS_service_origin_policy_tests
ctest --test-dir build\origin-policy --output-on-failure `
  -R BAAS_service_origin_policy_tests
```

With the repository Conan generators available, `BUILD_SERVICE_HTTP_TESTS=ON`
also tests direct adapter behavior, real loopback requests, payload rejection,
custom `HttpHostConfig`, repeated lifecycle, and concurrent deterministic
evaluation. No Python service, Tauri process, external address, device, or
emulator is started.

## Retained gaps

- no user/session authentication, authorization, cookie implementation, TLS,
  certificate policy, WebSocket Origin implementation, or LAN exposure policy;
- no shared generated configuration/schema with Python or Tauri;
- no browser/Tauri end-to-end test and no claim that Python's broad private-LAN
  regex is compatible;
- no complete HTTP header-section/connection/rate/global-memory bound;
- protocol-wide origin/CORS matrix, hostile local-process testing, and security
  review remain open as required by `SERVICE_PROTOCOL_V1.md` section 12.
