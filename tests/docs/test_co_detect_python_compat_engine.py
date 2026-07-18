from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class CoDetectPythonCompatEngineContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.doc = (
            ROOT / "docs/script-runtime/CO_DETECT_PYTHON_COMPAT_ENGINE.md"
        ).read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")

    def test_legacy_projection_is_explicitly_not_parity(self) -> None:
        for token in (
            "Why the legacy engine is not parity",
            "legacy.appear_then_click/v1",
            "cannot claim Python parity",
            "default 20-second stuck timeout",
            "loading call is not active",
            "never be described as parity",
            "co_detect.python-compat/v1",
        ):
            self.assertIn(token, self.doc)

    def test_strict_payload_schema_and_profile_source_are_closed(self) -> None:
        for token in (
            "duplicate object names",
            "unknown fields are\nrejected",
            "device.server-and-locale/v1",
            "profile_source",
            "foreground_check",
            "duplicate_click_window_ms",
            "after_failed_cycles",
            "post_wait_screenshot_intervals",
            "match-only",
            "threshold",
            "rgb_diff",
            "x=1280",
            "y=720",
            "CN, JP, Global_en-us, Global_zh-tw, Global_ko-kr",
            "Caller options, scripts, browser",
            "Feature IDs are resolved only from the pinned\nsupport bundle",
        ):
            self.assertIn(token, self.doc)

    def test_execution_priority_is_total_and_deterministic(self) -> None:
        ordered = (
            "Poll the earlier context/call deadline",
            "capture one screenshot",
            "run the foreground check",
            "loading.all_rgb",
            "Test `ends.rgb`",
            "Test `ends.image`",
            "Test `reactions.rgb`",
            "test `reactions.rgb_profiled`",
            "test `reactions.image`",
            "test `reactions.image_profiled`",
            "test `popups.rgb`",
            "test `popups.profiled_image`",
            "apply duplicate-click\n    suppression",
            "A popup selected",
            "increment the failed\n    count",
        )
        positions = [self.doc.index(token) for token in ordered]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("first tentative click\ntherefore happens on failure 11", self.doc)

    def test_python_popup_state_and_group_game_priority_are_exact(self) -> None:
        for token in (
            "does not perform ordinary-reaction duplicate suppression",
            "does not update\n    `feature_last_appear_time`",
            "does not reset the tentative failure count",
            "Only an ordinary reaction resets the\n    failed count to zero",
            "`GAME_ONE_TIME_POP_UPS` table belongs to\n`reactions.image_profiled`",
            "after `group_enter-button` and before every common\nor server popup",
            "Only the implicit server popup table belongs to\n`popups.profiled_image`",
        ):
            self.assertIn(token, self.doc)
        game = self.doc.index('"feature": "main_page_renewal-month-card"')
        common_popup = self.doc.index('"feature": "reward_acquired"', game)
        server_popup = self.doc.index('"feature": "main_page_net-work-unstable"', game)
        self.assertLess(game, common_popup)
        self.assertLess(common_popup, server_popup)

    def test_latest_frame_session_and_safety_divergences_are_owned(self) -> None:
        for token in (
            "Device-session frame ownership",
            "latest-frame\ncache across serialized procedure calls",
            "`navigation.to_main_page` leaves its terminal frame",
            "`group.open`, whose definition sets `skip_first_screenshot=true`",
            "unavailable_reason=recent_frame_unavailable",
            "Per-call duplicate-click state is reset",
            "intentional C++ safety hardening, not bit-for-bit\nPython behavior",
            "samples time before capture",
            "does not poll timeout in\nits loading loop",
            "schedules click helper work asynchronously",
        ):
            self.assertIn(token, self.doc)

    def test_deadline_cancel_effect_and_foreground_semantics_are_bound(self) -> None:
        for token in (
            "deadline wins",
            "HOST004_DEADLINE_EXCEEDED",
            "details.deadline_scope=context",
            "details.deadline_scope=call",
            "HOST003_CANCELLED",
            "No cancellation or timeout path produces a successful `end`",
            '["capture", "vision", "input", "wait", "foreground_check"]',
            "not_started",
            "committed",
            "unknown",
            "HOST006_UNAVAILABLE",
            "unavailable_reason=foreground_package_mismatch",
            "fail-closed mismatch handling is an intentional C++ safety\nhardening",
        ):
            self.assertIn(token, self.doc)

    def test_support_bundle_ids_counts_and_external_ownership_are_exact(self) -> None:
        self.assertIn("procedure-support/navigation.to-main-page/v1", self.doc)
        self.assertIn("procedure-support/group.open/v1", self.doc)
        self.assertIn("77 logical members", self.doc)
        self.assertIn("25 logical members", self.doc)
        self.assertIn("external resources repository", self.doc)
        self.assertIn("fake images", self.doc)
        for token in (
            "definition owns these per-call tuple overrides",
            "Popup objects are exact feature/click forms",
            "support bundle exclusively owns\nthe feature graph, RGB ranges, crop metadata, and PNG bytes",
            "screenshot\ninterval timing, deterministic/injectable click jitter RNG",
        ):
            self.assertIn(token, self.doc)

        navigation = re.search(
            r"The `navigation\.to_main_page` image-template IDs are:\s+"
            r"```text\s+(.*?)\s+```",
            self.doc,
            re.DOTALL,
        )
        group = re.search(
            r"The `group\.open` image-template IDs are:\s+"
            r"```text\s+(.*?)\s+```",
            self.doc,
            re.DOTALL,
        )
        self.assertIsNotNone(navigation)
        self.assertIsNotNone(group)
        navigation_ids = navigation.group(1).splitlines()
        group_ids = group.group(1).splitlines()
        self.assertEqual(len(navigation_ids), 69)
        self.assertEqual(len(group_ids), 18)
        self.assertEqual(len(set(navigation_ids)), 69)
        self.assertEqual(len(set(group_ids)), 18)
        for resource_id in (*navigation_ids, *group_ids):
            self.assertRegex(resource_id, r"^[a-z0-9._/-]+$")

    def test_terminals_and_known_baseline_defects_are_recorded(self) -> None:
        for token in (
            '"source": "main_page", "id": "main_page"',
            '"source": "group_sign-up-reward", "id": "group_sign-up-reward"',
            '"source": "group_menu", "id": "group_menu"',
            '"source": "group_join-club", "id": "group_join-club"',
            "draw-card-point-exchange-to-stone-piece-notice",
            "Fail-to-convert-errorResponse.png",
            "Failed-to-receive-Platform-Steam-GetEntitlementsAsJsonArray",
            "attendance-reward.png",
            "`group_join-club` crop entry commented out",
        ):
            self.assertIn(token, self.doc)

    def test_production_definitions_are_gated_on_real_adapter_resources_and_digests(self) -> None:
        for token in (
            "engine adapter, external resource bundles",
            "real resource digests",
            "Nothing in this document authorizes a placeholder",
            "production definitions are forbidden",
            "Empty\n`resources`, fake digests, placeholder definitions",
            "golden traces",
        ):
            self.assertIn(token, self.doc)

    def test_foundation_path_filter_covers_doc_and_test(self) -> None:
        doc_path = "docs/script-runtime/CO_DETECT_PYTHON_COMPAT_ENGINE.md"
        self.assertEqual(self.workflow.count(doc_path), 2)
        self.assertGreaterEqual(self.workflow.count("tests/docs/**"), 2)

    def test_implemented_definition_model_is_bounded_and_ci_gated(self) -> None:
        for path in (
            "include/runtime/json/StrictJson.h",
            "src/runtime/json/StrictJson.cpp",
            "include/runtime/procedure/CoDetectPythonCompatDefinition.h",
            "src/runtime/procedure/CoDetectPythonCompatDefinition.cpp",
            "tests/runtime/CoDetectPythonCompatDefinitionTests.cpp",
            "cmake/RuntimeStrictJson.cmake",
            "cmake/RuntimeCoDetectDefinitionModel.cmake",
        ):
            self.assertTrue((ROOT / path).is_file(), path)
        for token in (
            "strict immutable definition parsing/model implemented",
            "BAAS_runtime_co_detect_definition_model",
            "owns the verified source bytes",
            "deterministic semantic\nidentity SHA-256",
            "has no image/resource payloads",
            "does not open the production\nactivation gate",
        ):
            self.assertIn(token, self.doc)
        self.assertIn("-DBUILD_RUNTIME_CO_DETECT_DEFINITION_MODEL_TESTS=ON",
                      self.workflow)
        self.assertIn("-DBUILD_RUNTIME_CO_DETECT_DEFINITION_MODEL=ON",
                      self.workflow)
        self.assertGreaterEqual(
            self.workflow.count("BAAS_runtime_co_detect_definition_model"), 2
        )
        for token in (
            "android-clang-arm64-v8a-release",
            "android-clang-x86_64-release",
            "conan create deploy/conan/recipes/baas-nlohmann-json",
            "--requires=baas-nlohmann-json/3.11.3",
            "tools.android:ndk_path=${ANDROID_NDK_HOME}",
            "build/conan/foundation-android-${{ matrix.abi }}/conan_toolchain.cmake",
        ):
            self.assertIn(token, self.workflow)
        for dependency_path in (
            "deploy/conan/recipes/baas-nlohmann-json/**",
            "deploy/conan/profiles/android-clang-arm64-v8a-release",
            "deploy/conan/profiles/android-clang-x86_64-release",
        ):
            self.assertEqual(
                self.workflow.count(f"- '{dependency_path}'"),
                2,
                dependency_path,
            )


if __name__ == "__main__":
    unittest.main()
