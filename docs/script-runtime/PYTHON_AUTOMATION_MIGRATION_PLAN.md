# Python automation migration plan

Status: implementation plan; only the first `group` script parity slice exists

This document is the conversion ledger for the Python automation control code
that remains after the first `group` task. It deliberately separates frequently
updated game control from stable native infrastructure. It is not evidence that
the listed tasks, Host adapters, resources, or device paths already work through
the C++ production service.

## Source authority and reproducible inventory

The immutable Python behavior baseline for this ledger is
`baas-dev@b8cc64705feb0067aba349892031a450d1bf8083` (2026-07-17,
`merge: add Python C++ parity trace`). All paths in the batch table are relative
to that tree. A later Python commit is a new input that must be diffed against
this ledger; it does not silently change the meaning of these counts.

The inventory excludes `gui/**`, `window.py`, `.venv/**`, the vendored Python
distribution under `toolkit/uv/**`, tests, deployment tools, and developer
tools. PyQt is replaced by Tauri and is not a migration target. The following
read-only Python 3 procedure produced the source counts:

```python
from pathlib import Path
import ast

root = Path("D:/WorkSpace/pro/BAAS/baas-dev")
assert (root / ".git").exists()

for name in ("module", "core", "service", "src"):
    files = sorted((root / name).rglob("*.py"))
    lines = sum(len(path.read_text("utf-8", errors="ignore").splitlines())
                for path in files)
    print(name, len(files), lines)

tree = ast.parse((root / "core/Baas_thread.py").read_text("utf-8"))
dispatch = next(
    node.value for node in tree.body
    if isinstance(node, ast.Assign)
    and any(isinstance(target, ast.Name) and target.id == "func_dict"
            for target in node.targets)
)
print("task routes", len(dispatch.keys))
print("distinct callables", len({ast.dump(value) for value in dispatch.values}))

activity_shapes = {}
for path in sorted((root / "module/activities").glob("*.py")):
    if path.name in {"__init__.py", "activity_utils.py"}:
        continue
    shape = ast.dump(ast.parse(path.read_text("utf-8")), include_attributes=False)
    activity_shapes.setdefault(shape, []).append(path.name)
print(sorted(len(paths) for paths in activity_shapes.values()))

coordinate_modules = sorted(
    (root / "src/images").glob("**/x_y_range/**/*.py")
)
coordinate_module_count = len(coordinate_modules)
assert coordinate_module_count == 253
print("x_y_range recursive data modules", coordinate_module_count)
```

The resource counts use recursive file-extension counts under `src`: `*.png`,
`*.json`, and `src/images/**/x_y_range/**/*.py`. At the pinned baseline the result
is:

| Scope | Files | Logical lines / detail |
| --- | ---: | ---: |
| `module/**/*.py` | 109 | 9,501 |
| `core/**/*.py` | 65 | 13,912 |
| `service/**/*.py` | 58 | 9,510 |
| `src/**/*.py` | 253 | 4,309; all are coordinate/resource data modules |
| `src/**/*.png` | 2,870 | external dynamic image payloads |
| `src/**/*.json` | 128 | external dynamic data payloads |
| compatibility task routes | 36 | 35 distinct Python callables |
| activity implementations | 64 | 62 structurally identical forwarding wrappers and 2 custom workflows |

The 36 compatibility routes are kept explicit so a batch edit cannot make a
legacy service command disappear accidentally:

```text
group, momo_talk, common_shop, cafe_reward, no1_cafe_invite,
no2_cafe_invite, lesson, rewarded_task, arena, create,
explore_normal_task, explore_hard_task, mail, main_story, group_story,
mini_story, scrimmage, collect_reward, normal_task, hard_task,
clear_special_task_power, de_clothes, tactical_challenge_shop,
collect_daily_power, total_assault, restart, refresh_uiautomator2,
activity_sweep, explore_activity_story, explore_activity_challenge,
explore_activity_mission, dailyGameActivity, friend, joint_firing_drill,
pass, collect_daily_free_power
```

`collect_daily_power` and `collect_reward` intentionally address the same
callable. `module/collect_daily_task_power.py` is reached indirectly by that
callable. `module/purchase_ap.py` is not registered and contains only an empty
`start`; it is tracked as compatibility/dead-code evidence rather than treated
as implemented automation.

## Migration ownership

### Stable Python core becomes C++

The following code is native runtime or service infrastructure, not BAAS Script:

- `core/Baas_thread.py`: session ownership, cancellation, task dispatch, device
  initialization, foreground recovery, and application lifecycle;
- `core/scheduler.py`: queue construction, reset/interval policy, and dispatch;
- `core/device/**`: ADB, scrcpy, Nemu, uiautomator2, screenshots, and input;
- `core/picture.py`, `core/image.py`, `core/color.py`, `core/position.py`, and
  `core/geometry/**`: bounded vision primitives and Procedure engines;
- `core/ocr/**`: OCR lifecycle and inference;
- `core/config/**`: user configuration persistence, snapshots, validation, and
  transactions;
- `core/ipc_manager.py`, notification/push/log utilities, and `service/**`:
  process/service infrastructure, authentication, transports, repository
  updates, and task ownership;
- `main.py` and `main.service.py`: native application/session entry points.

Some low-level algorithms currently live inside `module`, but still belong in
C++ Host adapters: OpenCV matching in cafe/create/minigames, NumPy masks in
total assault, lesson geometry, OCR/image region extraction, `co_detect`,
screenshots, clicks, swipes, and application lifecycle. Translating those
algorithms line-for-line into the scripting language would preserve the wrong
boundary.

### Frequently updated control becomes BAAS Script

Task ordering, branching on terminal identifiers, retries, item/team priority,
activity selection, story progression, sweep decisions, task-specific log
messages, and task-specific scheduling decisions belong in external BAAS Script
packages. Scripts access native operations only through versioned,
capability-checked Host modules or declared Procedures. They receive immutable
config/resource views and never receive a repository path, Git remote, commit
selector, signing key, or unrestricted filesystem/device handle.

### Coordinate Python becomes external structured data

The 253 `src/images/**/x_y_range/**/*.py` files only declare prefixes, paths,
coordinates, and image metadata. They must be converted to a strict external
JSON or equivalent structured-resource schema and validated together with the
corresponding images. They must not be executed as Python, rewritten as C++, or
linked into an executable.

Resources, BAAS Script packages, Procedure definitions, and user configuration
are runtime state. None is compiled into `baas-cpp-dev`. Tauri may own desktop
repository update orchestration; the WebUI may request an update but cannot
choose trust inputs. Native WebUI/service-only deployments use the libgit2
runtime repository owner. Both paths must publish pinned, immutable generations
and keep scripts/resources outside the C++ repository. User configuration is
created and edited at runtime and is never repository payload.

During migration, Tauri must retain the legacy Python backend and its existing
route behavior. A new native C++ entry is additive and explicitly selected; it
must not silently replace the Python default before the corresponding batches
have production parity evidence. Repository updating may be shared, but Python
and C++ execution remain independently selectable compatibility paths.

## Common acceptance contract

Every converted batch must compare the pinned Python behavior with the C++
evaluator-backed package at observable boundaries:

1. ordered Procedure/Host calls, options, returned terminal identifiers, and
   input effect states;
2. `co_detect` candidate priority, popup handling, tentative clicks,
   `skip_first_screenshot`, loading, timeout, and foreground mismatch behavior;
3. screenshot fixture hashes, OCR requests/results, clicks, swipes, waits, and
   resource identifiers without recording raw secrets or user data;
4. exact return/error/cancellation outcome and retryability;
5. ordered log level, message, and structured fields;
6. config snapshot reads, atomic writes, conflicts, and preservation of unknown
   user fields;
7. scheduler `next_time`/`systole`, daily reset, and compatibility aliases;
8. CN, JP, Global language variants and unsupported-server fallthrough;
9. deterministic time, RNG, and structured-concurrency fixtures;
10. Windows Debug/Release golden tests, Android production compilation, then
    an `emulator-5556` smoke for the affected device behavior.

A package is not complete merely because a mock terminal golden passes. Its
declared Procedure/resource closure must activate from trusted external
generations and the production task backend must execute it.

## Completed first slice

The external `baas-runtime-scripts/packages/tasks/group/main.baas` is the first
converted script slice for Python `module/group.py`, route `group`. It calls
`navigation.to_main_page` followed by `group.open` and preserves the Python
reward/menu/join/other-terminal log/result behavior. Its evaluator-backed
goldens also cover cancellation and foreground-package mismatch.

This proves the normalized script/ProcedureHost/LogHost contract for those
fixtures only. It does not prove real `group` image resources, legacy Procedure
definitions, production `group` activation, service task-backend execution, or emulator
parity. Integration baseline `e165c76` already contains the merged schema-2
package Procedure closure, retained resource-activation provenance, and pinned
Procedure-definition activation foundations. Those foundations do not provide
the external real `group` definitions/resources, native legacy execution
adapter, production RuntimeTaskBackend, or emulator parity; those remain
unfinished.

## Remaining conversion batches

The order below is dependency order. Separate branches may implement independent
work after their shared Host/Procedure prerequisite is merged, but each verified
batch must explicitly merge back and delete its branch/worktree.

| Batch | Python files and compatibility entries | Required Host capabilities and proposed Procedure seams | External resources | Python behavior acceptance and blockers |
| --- | --- | --- | --- | --- |
| M01 — simple claims | `module/mail.py` (`mail`); `collect_daily_task_power.py` plus `collect_reward.py` (`collect_reward`, `collect_daily_power`); `collect_daily_free_power.py` (`collect_daily_free_power`); `collect_pass_reward.py` (`pass`) | `baas/procedure`, `baas/log`, config snapshot/transaction, capture, vision/RGB, OCR, input, wait; `mail.open_and_claim`, `tasks.claim_daily`, `purchase.claim_daily_free_ap`, `pass.open`, `pass.claim` | `main_page`, `mail`, `work_task`, `purchase_ap`, pass images/coordinates | Cover already-claimed/claimable/full/server-unsupported states, pass OCR/statistics and config writes. Blocked by production activation/backend plus Device/Vision/OCR/Config adapters |
| M02 — light story and social | `group_story.py` (`group_story`); `mini_story.py` (`mini_story`); `momo_talk.py` (`momo_talk`); `friend.py` (`friend`) | procedure/log/config, capture, vision, OCR, input, wait; shared `story.open_region`, `story.clear_episode`, `plot.skip`, `list.swipe_find` | `group_story`, `mini_story`, `plot`, `momo_talk`, `friend` | Preserve page-end and cleared-episode detection, unread conversation position, whitelist matching, delete order, fallthrough and logs. Needs reusable story/list Procedures |
| M03 — shops | `shop/shop_utils.py`; `shop/common_shop.py` (`common_shop`); `shop/tactical_challenge_shop.py` (`tactical_challenge_shop`) | procedure/log/config transaction, capture, vision, OCR, input, wait; `shop.open`, `shop.select`, `shop.find_item`, `shop.buy`, `shop.refresh` | `shop`, `main_page`, localized item images, external static price data | Preserve configured item priority, insufficient assets, sold-out/selected states, refresh confirmation/count and balance reads. Ephemeral RGB queries must not mutate ResourceSnapshot |
| M04 — common sweep | `clear_special_task_power.py` (`clear_special_task_power`); `rewarded_task.py` (`rewarded_task`); `scrimmage.py` (`scrimmage`) | procedure/log/config, capture, vision/RGB, OCR, input, wait; `stage.select`, `sweep.check_available`, `sweep.start`, `ticket.purchase` | `special_task`, `rewarded_task`, `scrimmage`, common popup resources | Preserve AP/ticket shortage, purchase limits, stage selection, server variants and sweep terminal. Needs a real ordered legacy `co_detect`/sweep engine |
| M05 — normal/hard missions | `explore_tasks/task_utils.py`, `sweep_task.py`, `explore_task.py`; entries `normal_task`, `hard_task`, `explore_normal_task`, `explore_hard_task` | procedure/log/config transaction, capture, vision, OCR, input, wait/cancellation; `mission.navigate`, `mission.select`, `formation.configure`, `battle.auto`, `grid.execute_action`, `grid.wait_turn_end` | `normal_task`, RGB features, `explore_task_data/normal_task/*.json`, `explore_task_data/hard_task/*.json` | Preserve unfinished-list pop/persist behavior, simple/full modes, team attributes, retreat/teleport, turn order, retry and cancellation. Needs grid action schema and formation/battle seams |
| M06 — main story | `main_story.py` (`main_story`) | M05 capabilities plus chapter/episode selection Procedures | `main_story`, `plot`, `explore_task_data/main_story/main_story.json` | Preserve region order, completed-episode skips, acceleration/auto toggles, formation selection and recovery. Blocked on M05 grid/battle contracts |
| M07 — competitive battle | `arena.py` (`arena`); `joint_firing_drill.py` (`joint_firing_drill`); `total_assault.py` (`total_assault`) | procedure/log/config, capture, vision, OCR, input, wait, scheduler; task-specific seams built on formation/battle/sweep | `arena`, `joint_firing_drill`, `total_assault` | Preserve arena refresh/rank strategy, three-team drill order, unfinished total-assault recovery, difficulty/ticket/reward decisions. Needs bounded fight monitoring and NumPy work moved to VisionHost |
| M08 — generic activities | `activities/activity_utils.py`; the 62 structural forwarding wrappers other than the two bunny files; `sweep_activity.py`, `explore_activity_story.py`, `explore_activity_mission.py`, `explore_activity_challenge.py`; entries `activity_sweep`, `explore_activity_story`, `explore_activity_mission`, `explore_activity_challenge` | procedure/log/config, resource, capture, vision, OCR, input, wait; `activity.open`, `activity.select_tab`, `activity.select_stage`, `activity.sweep`, `activity.exchange`, `activity.start_story`, `activity.start_fight` | per-activity images/coordinates and `explore_task_data/activities/*.json` | Use one common script module plus an explicit external activity catalog/package closure, not 62 hand-copied scripts. Preserve dynamic selection, unsupported activity warning/`true`, four modes and manual-boss behavior. Needs catalog route/options design |
| M09 — custom activities/minigames | `activities/bunnyChaserOnTheShip.py`, `bunnyChaserOnTheShip2.py`; `dailyGameActivity.py`; `dailyGameActivities/HinaSummerVacationAudioGame.py`, `serikaSummerRamenStall.py`; entry `dailyGameActivity` plus the four activity routes | procedure/log/config, capture, VisionHost template/color primitives, input, wait/time; card/exchange and minigame-specific Procedures | bunny activity resources, `dailyGameActivity`, noodles/templates and activity data | Preserve card draw/reshuffle terminals, exchange counts, audio-game timing windows, ramen matching and daily reward. Requires deterministic clock fixtures and native OpenCV primitives |
| M10 — cafe | `cafe_reward.py` (`cafe_reward`); `no1_cafe_invite.py` (`no1_cafe_invite`); `no2_cafe_invite.py` (`no2_cafe_invite`) | procedure/log/config, capture, vision/OCR, input, wait, structured tasks, scheduler; cafe navigation/invite/gift/interaction seams | `cafe`, shared `lesson_affection` student resources | Preserve two-cafe routing, duplicate/exchange policy, invitation cooldown, affection ordering, gift swipes, `next_time` and `scheduler.systole("cafe_reward")`. Needs cancellable screenshot/swipe concurrency and scheduler Host adapter |
| M11 — lesson | `lesson.py` (`lesson`) | procedure/log/config, capture, vision/OCR, input, wait; region/location/student/invite/purchase seams | `lesson`, `lesson_affection`, localized region/student metadata | Preserve locale parsing, region pages, relationship counts, favor-student selection, ticket purchases and execution limit. Geometry/image search moves to C++ |
| M12 — crafting | `create.py` (`create`) | procedure/log/config transaction, capture, vision/OCR, input, wait; crafting list/filter/sort/item/phase seams | `create`, external item/material/order/weight data | Preserve unfinished collection, phase weights, filters/sort, holding/selected quantities, acceleration tickets and atomic `alreadyCreateTime`. Largest single task; needs complete Vision/OCR/Config adapters |
| M13 — lifecycle/privileged compatibility | `restart.py` (`restart`); `refresh_uiautomator2.py` (`refresh_uiautomator2`); `de_clothes.py` (`de_clothes`); `purchase_ap.py` unregistered empty stub | controlled app lifecycle/device-maintenance/resource-push bindings, procedure/log/config/wait; never expose arbitrary path/process/socket access | dynamic `LocalizeConfig.txt` by resource ID; no compiled payload | Preserve foreground checks and stop/start timing. uiautomator refresh belongs to C++ maintenance. `de_clothes` may push only a policy-approved resource to a fixed logical destination. Keep `purchase_ap` as explicit no-op/deprecation evidence unless a real Python behavior appears |

## Infrastructure gates before broad conversion

At integration baseline `e165c76`, the schema-2 closure, resource provenance,
and pinned Procedure activation foundations named above are merged. The list
below retains their invariants as production composition gates and identifies
the still-unfinished adapter/backend/parity work; it is not a claim that the
merged foundations already execute a real automation task.

The first production-capable batch must not bypass these shared gates:

1. retain scripts and resources repository generation/commit provenance in
   unforgeable activation objects;
2. require schema-2 packages to declare a closed, canonical Procedure set;
3. load actual Procedure definition bytes, engine identity, terminal mapping,
   declared effects, and resource dependencies from the pinned scripts view;
4. prove scripts, Procedures, and resources belong to the intended immutable
   generations and reject mixed/stale inputs before publication;
5. adapt legacy `co_detect`/navigation/sweep behavior with typed errors,
   cooperative cancellation, bounded work, and conservative input effect state;
6. compose the evaluator, ProcedureHost, LogHost, resource/config snapshots,
   cancellation, status, and device strand in the production RuntimeTaskBackend;
7. implement production Device/Vision/OCR/Config/Scheduler adapters—the
   capability catalog alone is not an implementation;
8. expand Python parity capture beyond click/screenshot/swipe to ordered
   detection, OCR, config, log, scheduler, time, and task outcomes;
9. validate the external structured coordinate/resource repository without
   configure-time downloads or executable embedding.

Script authoring and mock golden construction may proceed after a batch's API is
stable, but a batch remains pending until the relevant gates, targeted tests,
real task route, and platform evidence pass. After targeted failing workflows
are green, the repository still requires the full Actions batch before explicit
merge to `main`.
