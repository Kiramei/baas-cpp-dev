#include <git2.h>

extern "C" int baas_runtime_repository_git2_dependency_anchor() noexcept {
    int major = 0;
    int minor = 0;
    int revision = 0;
    git_libgit2_version(&major, &minor, &revision);
    return major * 10000 + minor * 100 + revision;
}
