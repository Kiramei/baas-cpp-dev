import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class ServiceRuntimeScriptTrustTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.owner_header = (
            ROOT / "include/service/app/ServiceRuntimeRepositoryOwner.h"
        ).read_text(encoding="utf-8")
        cls.owner_source = (
            ROOT / "src/service/app/ServiceRuntimeRepositoryOwner.cpp"
        ).read_text(encoding="utf-8")
        cls.state_header = (
            ROOT / "include/service/app/RuntimeRepositoryTrustedPlanState.h"
        ).read_text(encoding="utf-8")
        cls.state_source = (
            ROOT / "src/service/app/RuntimeRepositoryTrustedPlanState.cpp"
        ).read_text(encoding="utf-8")
        cls.documentation = (
            ROOT / "docs/script-runtime/SERVICE_RUNTIME_REPOSITORY_OWNER.md"
        ).read_text(encoding="utf-8")

    def test_native_owner_seals_exact_nonserializable_evidence(self) -> None:
        self.assertIn("script_trust_evidence() const noexcept", self.owner_header)
        self.assertIn(
            "ExactRuntimeScriptRepositoryTrustEvidence", self.owner_source
        )
        self.assertIn("generation == generation_", self.owner_source)
        self.assertIn("scripts_commit == scripts_commit_", self.owner_source)
        self.assertNotIn("url", self.owner_header.lower())
        self.assertIn("no JSON/HTTP/Tauri representation", self.documentation)

    def test_service_attestation_is_read_only_and_recovery_free(self) -> None:
        self.assertIn("attest_exact", self.state_header)
        self.assertIn("publication_journal_name", self.state_source)
        self.assertIn("RuntimeRepositoryTrustedPlanStateError::pending_recovery", self.state_source)
        method = self.state_source.split(
            "RuntimeRepositoryTrustedPlanStateStore::attest_exact", 1
        )[1].split("RuntimeRepositoryTrustedPlanStateStore::prepare", 1)[0]
        self.assertNotIn("replace_file", method)
        self.assertNotIn("remove_file", method)
        self.assertNotIn("ensure_owner", method)
        self.assertIn("strictly read-only", self.documentation)


if __name__ == "__main__":
    unittest.main()
