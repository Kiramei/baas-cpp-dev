# Optional libgit2 dependency boundary

`BUILD_RUNTIME_REPOSITORY_GIT2` is an opt-in build boundary for future native
repository work. It only builds a placeholder static target and does not
implement clone, fetch, reset, credential, or update behavior.

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

TLS backends are platform-specific:

| Target | HTTPS backend | Additional Conan dependency |
| --- | --- | --- |
| Windows | WinHTTP | none |
| macOS | SecureTransport | none |
| Linux | OpenSSL | `openssl/3.5.7`, static |
| Android | OpenSSL | `openssl/3.5.7`, static |

## Verified and pending gates

The dedicated CI gate builds and links the optional target on Windows and
macOS with `--no-remote`. It also runs a real executable that checks libgit2
1.9.3 and its thread/HTTPS feature bits.

Linux and Android are declared source-build targets but are not offline-complete:
their HTTPS backend requires the Conan Center `openssl/3.5.7` recipe and its
build closure. Until BAAS owns and pins that closure, do not add `--no-remote`
Linux/Android claims and do not enable libgit2 in their production presets.
The default-OFF builds remain independent of this pending closure.

## Explicit development build

Export the private recipe and install only the optional dependency:

```text
python deploy/conan/scripts/manage_recipes.py export --only baas-libgit2
conan install --requires=baas-libgit2/1.9.3 -of build/conan/runtime-repository-git2 -g CMakeDeps -g CMakeToolchain --build=missing --no-remote
cmake -S . -B build/runtime-repository-git2 -DCMAKE_TOOLCHAIN_FILE=build/conan/runtime-repository-git2/conan_toolchain.cmake -DBUILD_RUNTIME_REPOSITORY_GIT2=ON -DBUILD_TESTING=ON -DBAAS_FETCH_RESOURCES=OFF
cmake --build build/runtime-repository-git2 --target BAAS_runtime_repository_git2 BAAS_runtime_repository_git2_link_tests
```

The `--no-remote` example applies to Windows and macOS. Linux and Android need
an explicitly provisioned OpenSSL recipe until the private closure is added.
