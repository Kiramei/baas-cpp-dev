# Optional libgit2 dependency boundary

`BUILD_RUNTIME_REPOSITORY_GIT2` builds the optional native fetch backend used by
the transport-independent runtime repository updater. The backend fetches one
trusted full `refs/heads/*` or `refs/tags/*` name, peels annotated tags within a
fixed depth, proves an exact lowercase SHA-1 commit, materializes only Git
`100644` blobs, and invokes the strict tree/manifest validator before returning.

The option defaults to `OFF`. Existing BAAS applications, `BAAS_service`, and
Tauri/WebUI consumer-only builds neither resolve nor link libgit2 unless their
dependency install explicitly enables `use_libgit2=True` and their CMake
configure explicitly enables `BUILD_RUNTIME_REPOSITORY_GIT2=ON`.

## Pinned package policy

The repository-owned `baas-libgit2/1.9.3` recipe verifies the upstream release
archive with SHA256
`d532172d7ab24d2a25944e2434212d63ee85f3650e97b5f7579e7f201a78ad64`.
It always builds a static, thread-safe library with HTTPS and collision-detecting
SHA-1 enabled. SSH, NTLM, iconv, the CLI, examples, fuzzers, and upstream tests
are disabled. The upstream bundled llhttp and zlib sources are selected; the
unrelated `baas-miniz` package is not reused as zlib.

The backend separately links the pinned `baas-miniz/3.1.2` target only for its
pre-libgit2 pack preflight. This is intentionally independent of libgit2's
private bundled zlib implementation: the preflight inflates each pack stream
under BAAS limits before replaying the validated response to libgit2.

TLS backends are platform-specific:

| Target | HTTPS backend | Additional Conan dependency |
| --- | --- | --- |
| Windows | WinHTTP | none |
| macOS | SecureTransport | none |
| Linux | OpenSSL | `openssl/3.5.7`, static |
| Android | OpenSSL | `openssl/3.5.7`, static |

## Runtime security boundary

Production accepts credential-free HTTPS URLs only. Userinfo, query strings,
fragments, proxy discovery, redirects, authentication callbacks, Git
`insteadOf`, implicit fetchspecs, and automatic tag downloads are disabled.
The HTTPS smart transport buffers and validates the complete advertised-ref
pkt-line response within independent byte/ref budgets before libgit2 parses it.

Fetches have configurable connect, no-progress, and absolute deadlines (the
absolute default is 15 minutes and is capped at 24 hours), cancellation, pack
byte/object budgets, ODB object/count/uncompressed-byte budgets, bounded
commit/tag/tree metadata, and independent tree/path/file/manifest limits.
Materialization uses exclusive no-follow file creation. The private transport
ODB is removed before strict validation and is never published with runtime
resources or scripts. External repository contents remain data files; they are
not added to the CMake source graph or compiled into BAAS.

Before any upload-pack bytes reach libgit2, the transport accepts both raw and
side-band responses, parses the PACK object stream, incrementally inflates each
zlib member under a hard ceiling, validates object-size varints, delta
base/result sizes and instructions, and enforces cumulative uncompressed
budgets. Post-fetch ODB scanning repeats count/size checks before bounded object
lookup and materialization. Cancellation and the absolute deadline are checked
inside both object and inflate loops.

## Verified and pending gates

The dedicated CI gate builds Debug and Release on Windows and macOS with
`--no-remote`. It runs the dependency link check, the real local-protocol
backend suite (exact branch/tag fetch, validation, cancellation, cleanup,
limits and mode rejection), and the updater suite. The test-only local
transport seam is thread-local, compiled only into that test build, and does
not alter public production type layout.

Linux and Android are declared source-build targets but are not offline-complete:
their HTTPS backend requires the Conan Center `openssl/3.5.7` recipe and its
build closure. Until BAAS owns and pins that closure, do not add `--no-remote`
Linux/Android claims and do not enable libgit2 in their production presets.
The default-OFF builds remain independent of this pending closure.

## Explicit development build

Export the private recipe and install only the optional dependency:

```text
python deploy/conan/scripts/manage_recipes.py export --only baas-libgit2
python deploy/conan/scripts/manage_recipes.py export --only baas-miniz
conan install --requires=baas-libgit2/1.9.3 --requires=baas-miniz/3.1.2 -of build/conan/runtime-repository-git2 -g CMakeDeps -g CMakeToolchain --build=missing --no-remote
cmake -S . -B build/runtime-repository-git2 -DCMAKE_TOOLCHAIN_FILE=build/conan/runtime-repository-git2/conan_toolchain.cmake -DBUILD_RUNTIME_REPOSITORY_GIT2_TESTS=ON -DBUILD_RUNTIME_REPOSITORY_UPDATER_TESTS=ON -DBUILD_TESTING=ON -DBAAS_FETCH_RESOURCES=OFF
cmake --build build/runtime-repository-git2 --target BAAS_runtime_repository_git2_backend_tests BAAS_runtime_repository_git2_link_tests BAAS_runtime_repository_updater_tests
ctest --test-dir build/runtime-repository-git2 --output-on-failure -R "^BAAS_runtime_repository_(git2_(backend|link)_tests|updater_tests)$"
```

The `--no-remote` example applies to Windows and macOS. Linux and Android need
an explicitly provisioned OpenSSL recipe until the private closure is added.
