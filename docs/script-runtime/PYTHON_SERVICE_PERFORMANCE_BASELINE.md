# Python Service Performance Baseline

This evidence extends the Windows Python host baseline with the safe, offline
portion of the production service HTTP path. It is an observation for future
comparison, not a regression threshold or proof that the complete service can
start without Python.

## Safety audit and measured boundary

The production lifespan was audited before measurement:

- `service/app.py::lifespan` calls `ServiceContext.startup()` and later
  `ServiceContext.shutdown()`; a Uvicorn listener enters the same lifespan.
- `ServiceContext.startup()` calls `ServiceRuntime.ensure_ready()`, whose
  production path initializes the lazy core and OCR server.
- Startup also creates a configuration filesystem watcher, may create a
  periodic remote update task, and launches `init_all_data()` in a daemon
  thread. Disabling the OCR *update check* does not disable OCR/core readiness.
- Materializing `ServiceContext` creates service authentication state under the
  project `config` directory.

Those effects are outside a no-device/no-OCR/no-network measurement. Therefore
the full production lifespan was not run, no Uvicorn socket was opened, and
real service shutdown latency was not fabricated. Both paths are recorded as
structured `skipped` probes in the evidence.

The measured path is the production FastAPI application and its real `/health`
route through a direct in-process ASGI 3.0 call. Each fresh child process:

1. imports the production service app;
2. redirects the lazy service context to a new temporary project root;
3. guards lifespan startup/shutdown, OCR/core readiness, background data
   initialization, and outbound `requests.Session` HTTP;
4. performs one readiness request followed by 50 measured health requests;
5. records request-scope teardown, then deletes the temporary root.

The only temporary files observed were
`config/service_signing_key.bin` and `config/service_ticket.key`. No device or
emulator path, OCR initialization/inference, external network request, network
listener, named pipe, Tauri process, or user project configuration was used.

## Source and command

The checked-in evidence was captured on Windows x64 from clean `baas-dev`
revision `75bbacb545bc87e9510d85cbe8034f9180397004`, using its Python 3.11.9
virtual environment, five serial fresh processes, 50 measured health requests
per process, and a 60-second timeout per process:

```powershell
python scripts/migration/measure_python_service_baseline.py `
  --python-repo D:\WorkSpace\pro\BAAS\baas-dev `
  --python-executable D:\WorkSpace\pro\BAAS\baas-dev\.venv\Scripts\python.exe `
  --output docs\script-runtime\evidence\python-service-performance-baseline.json `
  --repetitions 5 `
  --request-iterations 50 `
  --timeout 60
```

Validate the committed schema and safety boundary without running performance
work:

```powershell
python scripts/migration/measure_python_service_baseline.py `
  --check-evidence docs\script-runtime\evidence\python-service-performance-baseline.json
```

CI runs only mocked controller and committed-schema tests. It does not execute
the benchmark and does not assert timing or memory values.

## Windows observation

| Metric | Observed value |
| --- | ---: |
| Fresh child process wall median | 583.489 ms |
| ASGI ready elapsed median | 388.523 ms |
| First `/health` call median | 10.812 ms |
| Ready RSS median | 63,303,680 bytes |
| Peak RSS median | 63,500,288 bytes |
| Measured `/health` call median | 0.118 ms |
| Measured `/health` nearest-rank p95 | 0.208 ms |
| Aggregate in-process throughput | 7,192.175 requests/s |
| ASGI request-scope teardown median | 0.019 ms |

The throughput is an in-process ASGI upper-bound observation. It excludes TCP,
HTTP parsing by a server, Uvicorn scheduling, production lifespan work, and
cross-process transport, so it must not be presented as localhost or remote
client throughput. ASGI request-scope teardown is not service shutdown.

These values have natural timing variation from scheduler activity, CPU power
state, allocator behavior, antivirus activity, and the operating-system file
cache. The file cache was not flushed. Future comparisons should use repeated
runs on a controlled host and should not fail CI from the committed numbers.

## Relationship to the original baseline

The original host/import/image-algorithm/tree-size evidence remains unchanged
at [`evidence/python-performance-baseline.json`](evidence/python-performance-baseline.json).
Its startup, import, RSS, image matching, throughput, and logical-size metrics
remain the authoritative first host baseline. The new
[`evidence/python-service-performance-baseline.json`](evidence/python-service-performance-baseline.json)
adds service-route observations without regenerating or replacing those old
metrics.

The broad Phase 0 performance item remains partial. Production lifespan and
shutdown, localhost server transport, device/emulator, OCR, Tauri end-to-end,
and packaged installer measurements remain future work that requires safe
fixtures or explicit test infrastructure.
