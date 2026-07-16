extern "C" int baas_runtime_repository_git2_dependency_anchor() noexcept;

int main() {
    return baas_runtime_repository_git2_dependency_anchor() == 10903 ? 0 : 1;
}
