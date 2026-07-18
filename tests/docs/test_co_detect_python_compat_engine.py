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
            "engine adapter, externally published real",
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

    def test_real_support_bundle_loader_is_frozen_bounded_and_ci_gated(self) -> None:
        implementation_paths = (
            "include/runtime/procedure/CoDetectSupportBundle.h",
            "src/runtime/procedure/CoDetectSupportBundle.cpp",
            "tests/runtime/CoDetectSupportBundleTests.cpp",
            "cmake/RuntimeCoDetectSupportBundle.cmake",
        )
        for path in implementation_paths:
            self.assertTrue((ROOT / path).is_file(), path)
        for token in (
            "application/vnd.baas.co-detect-support-bundle.v1+zip",
            "bundle.magic",
            "m00000000",
            "baas.co-detect-support-bundle/v1",
            "actually decodes every PNG through OpenCV",
            "reserves each entry's declared uncompressed size before\ncalling miniz",
            "individual miniz extraction call is not\nmid-inflate interruptible",
            "A feature omitted from the profile graph is a normal\n`false` lookup",
            "externally published real\nlocale bundles",
        ):
            self.assertIn(token, self.doc)
        self.assertNotIn(
            "The concrete archive encoding and media type must\nbe frozen",
            self.doc,
        )
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        for token in (
            "BUILD_RUNTIME_CO_DETECT_SUPPORT_BUNDLE",
            "BUILD_RUNTIME_CO_DETECT_SUPPORT_BUNDLE_TESTS",
            "baas_request_dependencies(opencv)",
            "baas_request_dependencies(miniz)",
            "cmake/RuntimeCoDetectSupportBundle.cmake",
        ):
            self.assertIn(token, cmake)
        for token in (
            "co-detect-support-bundle-windows",
            "co-detect-support-bundle-unix",
            "-DBUILD_RUNTIME_CO_DETECT_SUPPORT_BUNDLE_TESTS=ON",
            "-DBUILD_RUNTIME_CO_DETECT_SUPPORT_BUNDLE=ON",
            "BAAS_runtime_co_detect_support_bundle_tests",
            "BAAS_runtime_co_detect_support_bundle",
            "conan create deploy/conan/recipes/baas-miniz",
            "conan create deploy/conan/recipes/baas-opencv",
            "--requires=baas-miniz/3.1.2",
            "--requires=baas-opencv/4.13.0",
            "Compile and link support-bundle test executable",
        ):
            self.assertIn(token, self.workflow)
        self.assertIn("conanrun.bat", self.workflow)
        self.assertIn("conanrun.sh", self.workflow)
        self.assertEqual(self.workflow.count("Run support-bundle tests"), 2)
        for dependency_path in (
            "deploy/conan/recipes/baas-miniz/**",
            "deploy/conan/recipes/baas-opencv/**",
        ):
            self.assertEqual(self.workflow.count(f"- '{dependency_path}'"), 2)
        implementation = "\n".join(
            (ROOT / path).read_text(encoding="utf-8") for path in implementation_paths
        )
        for forbidden in (
            "BAAS_FETCH_RESOURCES",
            "RESOURCE_DIR",
            "FetchContent",
            "ExternalProject",
            "configure_file(",
        ):
            self.assertNotIn(forbidden, implementation)

    def test_production_session_view_is_immutable_pathless_and_ci_gated(self) -> None:
        implementation_paths = (
            "include/runtime/procedure/CoDetectProductionAdapter.h",
            "src/runtime/procedure/CoDetectProductionAdapter.cpp",
            "tests/runtime/CoDetectProductionAdapterFixture.cpp",
            "tests/runtime/CoDetectProductionAdapterTests.cpp",
            "cmake/RuntimeCoDetectProductionAdapter.cmake",
        )
        for path in implementation_paths:
            self.assertTrue((ROOT / path).is_file(), path)
        for token in (
            "Immutable production session and feature view",
            "Reconnect, device switch, or profile change creates a new token",
            "copies\ncaptured/cache rows into packed immutable storage",
            "resizes the whole crop to the template with OpenCV `INTER_AREA`",
            "single `TM_CCOEFF_NORMED` result and requires it to be strictly greater",
            "Missing features return `false`",
            "concrete application `CoDetectProductionDevicePort`",
        ):
            self.assertIn(token, self.doc)
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        for token in (
            "BUILD_RUNTIME_CO_DETECT_PRODUCTION_ADAPTER",
            "BUILD_RUNTIME_CO_DETECT_PRODUCTION_ADAPTER_TESTS",
            "cmake/RuntimeCoDetectProductionAdapter.cmake",
        ):
            self.assertIn(token, cmake)
        for token in (
            "-DBUILD_RUNTIME_CO_DETECT_PRODUCTION_ADAPTER_TESTS=ON",
            "-DBUILD_RUNTIME_CO_DETECT_PRODUCTION_ADAPTER=ON",
            "BAAS_runtime_co_detect_production_adapter_tests",
            "BAAS_runtime_co_detect_production_adapter",
        ):
            self.assertIn(token, self.workflow)
        header = (ROOT / implementation_paths[0]).read_text(encoding="utf-8")
        source = (ROOT / implementation_paths[1]).read_text(encoding="utf-8")
        executor_header = (
            ROOT / "include/runtime/procedure/CoDetectPythonCompatExecutor.h"
        ).read_text(encoding="utf-8")
        executor_source = (
            ROOT / "src/runtime/procedure/CoDetectPythonCompatExecutor.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("class CoDetectProductionDevicePort", header)
        self.assertIn("make_activated_co_detect_production_executor", header)
        self.assertIn("identity_valid() const noexcept", executor_header)
        self.assertIn("!session_.identity_valid() || !features_.identity_valid()", executor_source)
        for token in (
            "cv::resize(cropped, resized, templ.size(), 0.0, 0.0, cv::INTER_AREA)",
            "result.at<float>(0, 0)",
            "similarity > threshold",
            "limits.max_screenshot_interval_ms > 60'000",
        ):
            self.assertIn(token, source)
        self.assertNotIn("cv::minMaxLoc", source)
        for forbidden in (
            "BAASConnection",
            "BAASConfig",
            "RESOURCE_DIR",
            "BAAS_FETCH_RESOURCES",
            "std::filesystem",
            "getenv(",
            "set_runtime_",
        ):
            self.assertNotIn(forbidden, header + source)

    def test_baas_connection_port_is_owned_bounded_and_ci_gated(self) -> None:
        implementation_paths = (
            "include/runtime/procedure/BAASConnectionCoDetectPort.h",
            "src/runtime/procedure/BAASConnectionCoDetectPort.cpp",
            "src/runtime/procedure/BAASApplicationCoDetectBackend.cpp",
            "tests/runtime/BAASConnectionCoDetectPortTests.cpp",
            "tests/runtime/BAASApplicationCoDetectBackendLinkClosure.cpp",
            "cmake/RuntimeBAASConnectionCoDetectPort.cmake",
        )
        for path in implementation_paths:
            self.assertTrue((ROOT / path).is_file(), path)
        for token in (
            "BAASConnection production port",
            "already-created, shared `BAAS`",
            "strictly greater non-zero epoch",
            "old port returns no current identity",
            "at most 50 ms",
            "exactly 1280x720 packed BGR8",
            "exactly 2,764,800 bytes",
            "does not expose a cross-consumer operation mutex",
            "BAAS_runtime_baas_connection_co_detect_link_closure",
            "complete dependency graph",
            "BAAS_FETCH_RESOURCES=OFF",
        ):
            self.assertIn(token, self.doc)
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        workflow = self.workflow
        for token in (
            "BUILD_RUNTIME_BAAS_CONNECTION_CO_DETECT_PORT",
            "BUILD_RUNTIME_BAAS_CONNECTION_CO_DETECT_PORT_TESTS",
            "cmake/RuntimeBAASConnectionCoDetectPort.cmake",
        ):
            self.assertIn(token, cmake)
        for token in (
            "-DBUILD_RUNTIME_BAAS_CONNECTION_CO_DETECT_PORT_TESTS=ON",
            "-DBUILD_RUNTIME_BAAS_CONNECTION_CO_DETECT_PORT=ON",
            "BAAS_runtime_baas_connection_co_detect_port_tests",
            "BAAS_runtime_baas_connection_co_detect_port",
            "deploy/conan/recipes/baas-spdlog/**",
            "deploy/conan/recipes/baas-simdutf/**",
            "include/device/BAASConnection.h",
            "src/device/BAASConnection.cpp",
        ):
            self.assertIn(token, workflow)
        header = (ROOT / implementation_paths[0]).read_text(encoding="utf-8")
        owner = (ROOT / implementation_paths[1]).read_text(encoding="utf-8")
        backend = (ROOT / implementation_paths[2]).read_text(encoding="utf-8")
        link_closure = (ROOT / implementation_paths[4]).read_text(encoding="utf-8")
        port_cmake = (ROOT / implementation_paths[5]).read_text(encoding="utf-8")
        self.assertIn("class BAASConnectionCoDetectOwner final", header)
        self.assertIn("make_baas_application_co_detect_backend", header)
        self.assertIn("session_epoch <= last_epoch_", owner)
        self.assertIn("previous->operation_mutex", owner)
        self.assertIn("maximum_wait_slice_ms = 50U", owner)
        self.assertIn("baas_connection_co_detect_frame_bytes", owner)
        for token in (
            "application_->update_screenshot_array_controlled",
            "application_->click",
            "connection_->current_app",
            "cv::INTER_AREA",
            "connection_->get_serial() == device_id_",
        ):
            self.assertIn(token, backend)
        self.assertIn("make_baas_application_co_detect_backend({})", link_closure)
        self.assertIn(
            "BAAS_runtime_baas_connection_co_detect_link_closure", port_cmake
        )
        for forbidden in (
            "BAASUserConfig",
            "BAASConfig",
            "RESOURCE_DIR",
            "BAAS_FETCH_RESOURCES",
            "std::filesystem",
            "getenv(",
        ):
            self.assertNotIn(forbidden, header + owner + backend)


if __name__ == "__main__":
    unittest.main()
