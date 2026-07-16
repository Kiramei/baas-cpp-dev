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

BAAS owns the per-operation Git smart HTTPS transport rather than mutating
libgit2 process-global timeout state. It uses the repository-owned
`baas-cpp-httplib/0.50.1` package with certificate verification enabled and a
pinned static `openssl/3.5.7` closure on Windows, Linux, macOS, and Android.
OpenSSL applications and zlib integration are disabled in that closure. The
same package target publishes one process-wide cpp-httplib configuration so
existing HTTP and WebSocket consumers cannot observe a conflicting header-only
ABI.

Windows loads the OS certificate stores and macOS loads the Keychain; the
package publishes their required system link dependencies. Android does not
assume that a static OpenSSL `OPENSSLDIR` can see platform roots: callers must
provide an absolute external PEM bundle through `trusted_ca_bundle`. The bundle
is runtime data, is never embedded in BAAS, disables system-CA fallback for that
operation, and an absent or unreadable Android bundle fails before networking.

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
budgets. OFS delta bases must name an earlier object boundary, delta dependency
depth is capped, and REF deltas are conservatively rejected because a fresh ODB
cannot prove their base graph before libgit2 consumes it. Post-fetch ODB scanning repeats count/size checks before bounded object
lookup and materialization. Cancellation and the absolute deadline are checked
inside both object and inflate loops.

## Build and ownership boundary

The pure C++/WebUI product can enable this backend directly from
`baas-cpp-dev`; no Tauri process is required. The backend is still optional and
defaults to `OFF`. Runtime resources and BAAS scripts remain external repository
data selected by a trusted ref and exact commit. They are validated and copied
to the updater-owned staging directory at runtime; they are never listed as
CMake sources, embedded as binary resources, or compiled into an executable.

## Verified gates

The dedicated CI gate builds Debug and Release on Windows, Linux, and macOS.
It runs the dependency link check, the real local-protocol
backend suite (exact branch/tag fetch, validation, cancellation, cleanup,
limits and mode rejection), and the updater suite. The test-only local
transport seam is thread-local, compiled only into that test build, and does
not alter public production type layout.

Android arm64-v8a and x86_64 jobs cross-build the same optional backend against
NDK 29.0.13846066. CI permits Conan Center only while resolving the exact
`openssl/3.5.7` recipe and its pinned build tools; all BAAS recipes and source
archives remain repository-owned and hash-verified. The default-OFF build
remains independent of the complete optional dependency closure.

## Explicit development build

Export the private recipe and install only the optional dependency:

```text
python deploy/conan/scripts/manage_recipes.py export --only baas-libgit2
python deploy/conan/scripts/manage_recipes.py export --only baas-miniz
python deploy/conan/scripts/manage_recipes.py export --only baas-cpp-httplib
conan install --requires=baas-libgit2/1.9.3 --requires=baas-miniz/3.1.2 --requires=baas-cpp-httplib/0.50.1 -of build/conan/runtime-repository-git2 -g CMakeDeps -g CMakeToolchain --build=missing
cmake -S . -B build/runtime-repository-git2 -DCMAKE_TOOLCHAIN_FILE=build/conan/runtime-repository-git2/conan_toolchain.cmake -DBUILD_RUNTIME_REPOSITORY_GIT2_TESTS=ON -DBUILD_RUNTIME_REPOSITORY_UPDATER_TESTS=ON -DBUILD_TESTING=ON -DBAAS_FETCH_RESOURCES=OFF
cmake --build build/runtime-repository-git2 --target BAAS_runtime_repository_git2_backend_tests BAAS_runtime_repository_git2_link_tests BAAS_runtime_repository_updater_tests
ctest --test-dir build/runtime-repository-git2 --output-on-failure -R "^BAAS_runtime_repository_(git2_(backend|link)_tests|updater_tests)$"
```
