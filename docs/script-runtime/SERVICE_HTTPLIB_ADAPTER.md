# Optional cpp-httplib service adapter

`BAAS_service_http` is the first real HTTP transport adapter over the
dependency-free `BAAS_service_router`. It is optional: the normal foundation
configuration does not find or include cpp-httplib, and only
`BUILD_SERVICE_HTTP` or `BUILD_SERVICE_HTTP_TESTS` requests the Conan
`BAAS::httplib` target.

This target is an adapter, not the production service host. The only listener
created by this change is inside the test executable, bound explicitly to
`127.0.0.1` on an operating-system-selected ephemeral port.

## Mapping contract

`HttplibAdapter::handle` maps these fields without reinterpretation:

| cpp-httplib request | Router request |
| --- | --- |
| `Request::method` | `Request::method` |
| `Request::path` | normalized route `Request::path` |
| `Request::body` | `Request::body` |

The owning Router response status, headers, and body are copied back to
cpp-httplib. cpp-httplib may add wire-level headers such as `Content-Length` or
connection metadata when it serializes the response; those are transport
framing, not Router headers.

The mapped paths are intentionally unversioned. `/health` matches the frozen
required v1 path name, but the current minimal Router health body is not the
complete required readiness/status/auth/signing-key payload. `/version` and
`/shutdown` are foundation extensions rather than frozen required v1 routes.
The adapter performs no version negotiation, and `/api/v1/*` is not accepted.

`install()` registers one shared catch-all handler for cpp-httplib's GET/HEAD,
POST, PUT, PATCH, DELETE, and OPTIONS dispatch. This happens after httplib has
read and bounded the request body. A pre-routing handler is intentionally not
used because cpp-httplib invokes it before content is read.

The adapter and its Router are non-owning dependencies captured by installed
handlers. Both objects must outlive the `httplib::Server` and all requests.
Installing the adapter replaces the server error handler and is therefore an
exclusive server-configuration operation.

## Layered input limits

`InputBudget` independently limits method, path, and body before Router
dispatch. Its limits must be positive and no larger than the corresponding
Router limits. Defaults are 16 method bytes, 2,048 path bytes, and 1 MiB body.

The body limit is also installed with `Server::set_payload_max_length`, so the
transport rejects oversized declared or streamed content before route effects.
cpp-httplib's resulting 413 is converted to the same JSON-safe error shape used
by direct adapter rejections. The Router still applies its own input and output
budgets as a second boundary.

HTTP header count/size, request-target parsing, connection count, aggregate
memory, rate, and worker-queue budgets are not implemented here. They remain
requirements for the production host.

## Build and tests

The dependency must first be available through the repository's Conan
generators. For the configured Windows release profile:

```powershell
cmake -S . -B build\service-http-release -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=build\conan\windows-msvc-release-baas\generators\conan_toolchain.cmake `
  -DCMAKE_BUILD_TYPE=Release `
  -DBUILD_TESTING=ON `
  -DBUILD_SERVICE_HTTP_TESTS=ON `
  -DBAAS_FETCH_RESOURCES=OFF
cmake --build build\service-http-release --parallel 4 `
  --target BAAS_service_httplib_adapter_tests
ctest --test-dir build\service-http-release --output-on-failure `
  -R BAAS_service_httplib_adapter_tests
```

The test has two layers:

- direct cpp-httplib request/response objects verify exact field mapping,
  headers, errors, and transport-before-Router budgets;
- a real server binds only `127.0.0.1:0`, waits at most two seconds for ready,
  uses two-second client/server I/O timeouts, exercises health/method/body-limit
  responses, then calls `stop()` and joins the listener thread. CTest has a
  15-second outer timeout.

No hostname resolution, external address, remote network, device, emulator,
OCR, Python service, or Tauri process is used by this test.

## Still incomplete

- authentication, origin/CORS, cookie, listen-address, and exposure policy;
- a production-owned Server lifecycle and real shutdown/drain sequence;
- bounded worker/connection concurrency, backpressure, and load behavior;
- TLS, logging, metrics, task/config/resource routes, and BPIP integration;
- baas-tauri contract sharing and end-to-end testing.

The existing injected Router shutdown intent is mapped like any other route;
the adapter does not stop its Server or the process. Therefore this is not a
claim of graceful shutdown, a production HTTP service, high-concurrency
readiness, or Tauri integration.
