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
- one native-host-owned application root containing that executable;
- one safe working directory contained by the application root;
- one absolute existing project root;
- the exact loopback host `127.0.0.1`;
- a nonzero port; and
- one exact 64-character lowercase hexadecimal runtime repository generation.

The owner canonicalizes and validates the filesystem objects once, then starts
the selected executable directly. It never searches `PATH`, invokes a shell,
inherits the parent environment, or accepts an extra argument/command string.
The application root, executable, and working directory are trusted native-host
inputs and are not accepted from browser messages. The child argument vector is fixed
to `--project-root`, `--host`, `--port`, and
`--runtime-repository-generation`.

On Windows, launch uses `CreateProcessW` with an explicit application path,
UTF-16 arguments, disabled handle inheritance, and a suspended child that is
assigned to a kill-on-close Job Object before it runs. On Linux and macOS,
launch uses `posix_spawn` with an independent process group and closes unrelated
descriptors (`addclosefrom_np` on Linux and `POSIX_SPAWN_CLOEXEC_DEFAULT` on
macOS). Both launch paths use the validated safe working directory and an empty,
controlled environment. Stop and destruction terminate the complete job/process
group and reap the child.

## Ownership state

One owner is either `stopped`, `running`, or `exited`. Start is allowed only
from `stopped`; a duplicate start is rejected. Bounded wait distinguishes
timeout from exit, retains exact normal exit/signal information, and reaps a
completed child. Wait does not retain the state lock while it is blocked, so a
concurrent stop remains responsive. On POSIX, natural exit is observed with
`waitid(WNOWAIT)`; the process group is killed while the leader PID is still
reserved, then the leader is reaped with `waitpid`. Stop is forceful, bounded,
restartable, and idempotent. Destruction performs
the final force-terminate/reap boundary so a supervisor cannot leave its owned
service or a zombie behind. POSIX constructs an allocation-independent emergency
reaper before any start is permitted; if bounded synchronous cleanup ever fails,
the already-running reaper receives the killed leader PID without creating a
thread or allocating during destruction.

This is deliberately not service self-replacement. A future supervisor must
validate a published generation, stop the old generation-bound service, start a
new child with that exact generation, and accept it only after exact `/health`
confirmation. That orchestration and the browser-facing asynchronous update job
API remain separate future work.

## Evidence

The test helper atomically records its received argument vector below paths with
spaces, Unicode, and command-shell metacharacters. It can create a real
grandchild to prove Job/process-group cleanup. Tests cover exact argv including
argv0, controlled environment, invalid executable
and launch image, invalid host/port/generation, duplicate start, bounded timeout,
negative and saturating maximum timeouts, concurrent long wait plus stop,
restart, normal leader exit with a live grandchild, force termination,
idempotent stop, owner destruction, and a deliberately inheritable HANDLE/fd
sentinel that must be absent in the child. CI runs
the suite in Debug and Release on Windows, Linux, and macOS with `/W4 /WX` or
`-Wall -Wextra -Wpedantic -Werror` for every new target.
