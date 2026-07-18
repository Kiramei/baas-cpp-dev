import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class PythonAutomationMigrationPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.plan_path = ROOT / "docs/script-runtime/PYTHON_AUTOMATION_MIGRATION_PLAN.md"
        cls.plan = cls.plan_path.read_text(encoding="utf-8")
        cls.workflow = (
            ROOT / ".github/workflows/foundation-runtime.yml"
        ).read_text(encoding="utf-8")

    def test_baseline_inventory_is_pinned_and_reproducible(self) -> None:
        for anchor in (
            "baas-dev@b8cc64705feb0067aba349892031a450d1bf8083",
            'for name in ("module", "core", "service", "src"):',
            'tree = ast.parse((root / "core/Baas_thread.py")',
            "| `module/**/*.py` | 109 | 9,501 |",
            "| `core/**/*.py` | 65 | 13,912 |",
            "| `service/**/*.py` | 58 | 9,510 |",
            "| `src/**/*.py` | 253 | 4,309; all are coordinate/resource data modules |",
            "| `src/**/*.png` | 2,870 |",
            "| `src/**/*.json` | 128 |",
            "| compatibility task routes | 36 | 35 distinct Python callables |",
            "62 structurally identical forwarding wrappers and 2 custom workflows",
        ):
            self.assertIn(anchor, self.plan)

    def test_pyqt_and_nonproduction_inputs_are_excluded(self) -> None:
        for anchor in (
            "The inventory excludes `gui/**`, `window.py`",
            "PyQt is replaced by Tauri and is not a migration target",
            "`toolkit/uv/**`",
            "tests, deployment tools, and developer\ntools",
        ):
            self.assertIn(anchor, self.plan)

    def test_three_migration_ownership_classes_are_explicit(self) -> None:
        for heading in (
            "### Stable Python core becomes C++",
            "### Frequently updated control becomes BAAS Script",
            "### Coordinate Python becomes external structured data",
        ):
            self.assertIn(heading, self.plan)
        for anchor in (
            "253 `src/images/**/x_y_range/*.py` files",
            "must not be executed as Python, rewritten as C++, or\nlinked into an executable",
            "Resources, BAAS Script packages, Procedure definitions, and user configuration\nare runtime state",
            "None is compiled into `baas-cpp-dev`",
            "Tauri may own desktop\nrepository update orchestration",
            "Native WebUI/service-only deployments use the libgit2\nruntime repository owner",
            "User configuration is\ncreated and edited at runtime",
            "Tauri must retain the legacy Python backend",
            "A new native C++ entry is additive and explicitly selected",
            "must not silently replace the Python default",
        ):
            self.assertIn(anchor, self.plan)

    def test_all_compatibility_routes_are_accounted_for(self) -> None:
        routes = {
            "group",
            "momo_talk",
            "common_shop",
            "cafe_reward",
            "no1_cafe_invite",
            "no2_cafe_invite",
            "lesson",
            "rewarded_task",
            "arena",
            "create",
            "explore_normal_task",
            "explore_hard_task",
            "mail",
            "main_story",
            "group_story",
            "mini_story",
            "scrimmage",
            "collect_reward",
            "normal_task",
            "hard_task",
            "clear_special_task_power",
            "de_clothes",
            "tactical_challenge_shop",
            "collect_daily_power",
            "total_assault",
            "restart",
            "refresh_uiautomator2",
            "activity_sweep",
            "explore_activity_story",
            "explore_activity_challenge",
            "explore_activity_mission",
            "dailyGameActivity",
            "friend",
            "joint_firing_drill",
            "pass",
            "collect_daily_free_power",
        }
        block = re.search(
            r"The 36 compatibility routes.*?```text\n(.*?)\n```",
            self.plan,
            re.DOTALL,
        )
        self.assertIsNotNone(block)
        observed = {
            route.strip()
            for route in block.group(1).replace("\n", " ").split(",")
            if route.strip()
        }
        self.assertEqual(observed, routes)

    def test_group_scope_is_honest_and_thirteen_batches_are_complete(self) -> None:
        for anchor in (
            "The external `baas-runtime-scripts/packages/tasks/group/main.baas`",
            "This proves the normalized script/ProcedureHost/LogHost contract for those\nfixtures only",
            "does not prove real `group` image resources",
            "service task-backend execution",
        ):
            self.assertIn(anchor, self.plan)
        batches = re.findall(r"^\| (M\d{2}) —", self.plan, re.MULTILINE)
        self.assertEqual(batches, [f"M{number:02d}" for number in range(1, 14)])
        for column_anchor in (
            "Required Host capabilities and proposed Procedure seams",
            "External resources",
            "Python behavior acceptance and blockers",
        ):
            self.assertIn(column_anchor, self.plan)

    def test_acceptance_and_infrastructure_gates_cannot_be_omitted(self) -> None:
        for anchor in (
            "ordered Procedure/Host calls",
            "`co_detect` candidate priority",
            "config snapshot reads, atomic writes, conflicts",
            "scheduler `next_time`/`systole`",
            "Windows Debug/Release golden tests",
            "`emulator-5556` smoke",
            "require schema-2 packages to declare a closed, canonical Procedure set",
            "load actual Procedure definition bytes",
            "production RuntimeTaskBackend",
            "capability catalog alone is not an implementation",
            "full Actions batch before explicit\nmerge to `main`",
        ):
            self.assertIn(anchor, self.plan)

    def test_foundation_workflow_covers_plan_changes_on_push_and_pr(self) -> None:
        path = "docs/script-runtime/PYTHON_AUTOMATION_MIGRATION_PLAN.md"
        self.assertEqual(self.workflow.count(f"- '{path}'"), 2)
        self.assertIn("- 'tests/docs/**'", self.workflow)


if __name__ == "__main__":
    unittest.main()
