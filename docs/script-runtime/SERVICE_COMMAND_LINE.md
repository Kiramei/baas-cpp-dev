# Bounded service command-line contract

`BAAS_service_command_line` is the dependency-free C++20 parsing boundary used
by the real `BAAS_service` executable. This service executable matches the
explicit Tauri C++ launch contract. `ServiceApplication` owns help/version
text, process exit codes, diagnostics, signal handling, host construction,
readiness, and shutdown.

## Dispositions and grammar

The parser consumes arguments after `argv[0]` and returns one structured
`ServiceCommandLineDisposition`: `run`, `help`, `version`, or `error`.
`--help` and `--version` are successful, independent dispositions only when
they are the sole argument. Values or any mixture with another argument fail.

A run requires each of these exactly once:

```text
--project-root <directory> --host 127.0.0.1 --port <1..65535> --runtime-repository-generation <64-lowercase-hex>
```

It may contain `--pipe-name <endpoint>` once. Every valued option accepts both
the separated spelling above and `--option=value`; order is irrelevant.
Duplicate options, unknown options, positional arguments, missing or empty
values, signs, whitespace, suffixes, zero, and out-of-range ports fail closed.
The host value is exactly `127.0.0.1`; this parser does not opt the service into
LAN exposure.

The runtime repository generation is mandatory. It is exactly 64 lowercase
hexadecimal characters and identifies the generation already published for
this process start. Uppercase, short, long, or non-hex spellings are rejected;
there is no absent-generation compatibility mode.

The project root is converted only after all string and aggregate budgets pass.
It must resolve to an existing directory. The implementation uses the
non-throwing filesystem query and treats an `error_code` as `filesystem_error`.
It also catches `filesystem_error`, allocation failure, and any unexpected
exception at the public `noexcept` boundary. User input therefore produces a
stable `ServiceCommandLineError` and `service_command_line_error_name()` rather
than escaping an exception.

## Input budgets

| Boundary | Limit |
|---|---:|
| arguments after `argv[0]` | 16 |
| one argument | 1,024 bytes |
| aggregate arguments plus separators | 4,096 bytes |
| project-root value | 1,024 bytes |
| Windows named-pipe endpoint | 512 bytes |
| Unix socket endpoint | 103 bytes |

Bounds and embedded-NUL rejection happen before option parsing, allocation, or
filesystem access. The `argc`/`argv` adapter bounds each C string while building
a fixed-size view array and rejects null vectors or entries. The result's
`error_argument` is a zero-based index excluding `argv[0]`, or `no_argument`
when an error is not attributable to one token.

## Pipe policy

- Windows requires the exact `\\.\pipe\` prefix and a non-empty suffix.
- Linux and macOS require an absolute endpoint beginning `/`, with the portable
  103-byte `sockaddr_un` payload bound.
- Android explicitly returns `pipe_not_supported`; it does not silently treat a
  filesystem endpoint as a supported transport.

`ServiceCommandLinePlatform` is injectable so the full policy matrix is tested
on every CI host, while the default is selected at compile time.

## Build and evidence

The library is opt-in through `BUILD_SERVICE_COMMAND_LINE`; tests use
`BUILD_SERVICE_COMMAND_LINE_TESTS`. `BAAS_service_command_line_tests` covers
separated/equal forms, option order, every required field, strict port parsing,
help/version isolation, duplicate/unknown/positional rejection, project-root
filesystem gating, exact generation syntax, Windows/Unix/Android Pipe policy,
input budgets, embedded NUL, invalid `argc`/`argv`, and stable error names.
Foundation CI builds and runs the target on Windows, Linux, and macOS in Debug
and Release.

The module and `ServiceApplication` are excluded from the legacy `BAAS_CORE`
glob. The executable, production composition, and loopback lifecycle tests are
documented in `SERVICE_APPLICATION.md`. Packaging beside Tauri remains pending.
