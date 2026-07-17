# Pure WebUI service supervisor foundation

`BAAS_webui_service_process_owner` is the desktop-only child-process ownership
foundation for a future pure WebUI product. It does not host WebUI assets,
expose an update API, invoke `BAAS_runtime_repository_update`, or claim that the
standalone WebUI update/restart product is complete.

The target is opt-in through `BUILD_WEBUI_SERVICE_SUPERVISOR=ON` and defaults
to off. `BUILD_WEBUI_SERVICE_SUPERVISOR_TESTS=ON` enables the deterministic
helper and lifecycle suite. Android configuration fails when the foundation is
requested. The library has only platform and C++ standard-library dependencies;
it does not link libgit2, the runtime repository publisher, `BAAS_service`, or
legacy resource/configuration targets.

## Fixed launch contract

The caller supplies a strongly typed `ServiceProcessConfig` containing:

- one absolute, already selected `BAAS_service` executable path;
- one absolute existing project root;
- the exact loopback host `127.0.0.1`;
- a nonzero port; and
- one exact 64-character lowercase hexadecimal runtime repository generation.

The owner canonicalizes and validates the filesystem objects once, then starts
the selected executable directly. It never searches `PATH`, invokes a shell,
or accepts an extra argument/command string. The child argument vector is fixed
to `--project-root`, `--host`, `--port`, and
`--runtime-repository-generation`.

On Windows, launch uses `CreateProcessW` with an explicit application path,
UTF-16 arguments, disabled handle inheritance, and a suspended child that is
assigned to a kill-on-close Job Object before it runs. On Linux and macOS,
launch uses `posix_spawn` with an independent process group and closes unrelated
descriptors (`addclosefrom_np` on Linux and `POSIX_SPAWN_CLOEXEC_DEFAULT` on
macOS). Stop and destruction terminate the complete job/process group and reap
the child.

## Ownership state

One owner is either `stopped`, `running`, or `exited`. Start is allowed only
from `stopped`; a duplicate start is rejected. Bounded wait distinguishes
timeout from exit, retains exact normal exit/signal information, and reaps a
completed child. Stop is forceful, bounded, and idempotent. Destruction performs
the final force-terminate/reap boundary so a supervisor cannot leave its owned
service or a zombie behind.

This is deliberately not service self-replacement. A future supervisor must
validate a published generation, stop the old generation-bound service, start a
new child with that exact generation, and accept it only after exact `/health`
confirmation. That orchestration and the browser-facing asynchronous update job
API remain separate future work.

## Evidence

The test helper records its received argument vector below a project root with
spaces and Unicode characters. Tests cover exact arguments, invalid executable
and launch image, invalid host/port/generation, duplicate start, bounded timeout,
normal exit, force termination, idempotent stop, and owner destruction. CI runs
the suite in Debug and Release on Windows, Linux, and macOS with `/W4 /WX` or
`-Wall -Wextra -Wpedantic -Werror` for every new target.
