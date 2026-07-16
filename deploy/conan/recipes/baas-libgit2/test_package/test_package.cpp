#include <git2.h>

int main() {
    int major = 0;
    int minor = 0;
    int revision = 0;
    git_libgit2_version(&major, &minor, &revision);
    const int features = git_libgit2_features();

    if (major != 1 || minor != 9 || revision != 3) {
        return 1;
    }
    if ((features & GIT_FEATURE_THREADS) == 0) {
        return 2;
    }
    if ((features & GIT_FEATURE_HTTPS) == 0) {
        return 3;
    }
    return 0;
}
