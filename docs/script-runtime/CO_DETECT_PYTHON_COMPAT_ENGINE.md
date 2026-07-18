# Python-compatible `co_detect` procedure engine contract

Status: design contract only. The engine adapter, external resource bundles,
real resource digests, and production procedure definitions do not exist yet.
Nothing in this document authorizes a placeholder `baas.procedures.json` entry.

This contract freezes the required migration boundary for
`core.picture.co_detect`, initially for `navigation.to_main_page` and
`group.open`. It is based on the Python baseline in
`core/Baas_thread.py::to_main_page`, `module/group.py::to_group`,
`core/picture.py::co_detect`, `deal_with_pop_ups`, and
`GAME_ONE_TIME_POP_UPS`.

## Why the legacy engine is not parity

`legacy.appear_then_click/v1` can order end features and click reactions, set a
wall-clock timeout, suppress repeated clicks by an interval, and issue a
wall-clock tentative click. That is only a structural projection of
`co_detect`. It cannot claim Python parity because it does not encode all of the
following observable behavior:

- Android-only foreground-package checks governed by both time since the last
  feature and time since the last package check;
- `wait_loading`, whose Python predicate is the conjunction of
  `loadingNotWhite` and `loadingWhite`;
- the distinct RGB-end, image-end, RGB-reaction, image-reaction, common-popup,
  and server-popup priority classes;
- a tentative click triggered on the eleventh consecutive failed recognition
  cycle, repeated on every subsequent failed cycle, followed by a wait of five
  current screenshot intervals;
- the exact two-second same-feature, same-position duplicate-click rule;
- Python cancellation, which raises `RequestHumanTakeOver` when `flag_run`
  clears, and Python's `FunctionCallTimeout` classification;
- server/locale-specific resources and click positions, including the JP
  `main_page` click at x=565; and
- Python's missing-template-is-false behavior without inventing an empty image
  or a cross-locale fallback.

The legacy implementation also has a default 20-second stuck timeout that the
Python loop does not have, and its loading call is not active. Setting
`max_stuck_time` to 600, using a two-second possible interval, or writing
`tentative_click: [true, 1238, 45, 10]` changes the behavior; those values must
never be described as parity.

The engine ID for a faithful adapter is exactly:

```text
co_detect.python-compat/v1
```

## Strict procedure-definition schema

The external definition is strict JSON: duplicate object names, comments,
non-UTF-8 input, non-finite numbers, trailing data, and unknown fields are
rejected. Its root contains exactly `schema`, `engine`, and `payload`:

```json
{
  "schema": "baas.procedure-definition/v1",
  "engine": "co_detect.python-compat/v1",
  "payload": {}
}
```

`payload` contains exactly these fields, in this semantic order:

| Field | Type | Contract |
| --- | --- | --- |
| `profile_source` | string | Must equal `device.server-and-locale/v1`. |
| `ends` | object | Exactly `rgb` and `image`, each an ordered, duplicate-free array of feature IDs. |
| `reactions` | object | Exactly `rgb`, `rgb_profiled`, `image`, and `image_profiled`, each an ordered array of reaction objects. |
| `popups` | object | Exactly `rgb` and `profiled_image`, each an ordered array of reaction objects. |
| `loading` | object | Exactly `all_rgb`; an ordered, non-empty, duplicate-free feature array. |
| `foreground_check` | object | Exactly `android_only`, `interval_ms`, and `idle_feature_ms`. |
| `loop` | object | Exactly `skip_first_screenshot`, `timeout_ms`, `duplicate_click_window_ms`, and `tentative`. |

A non-profiled reaction contains exactly `feature` and `click`. A profiled
reaction contains exactly `profiles`, `feature`, and `click`. `feature` is a
non-empty logical feature string. `click` is exactly two integral coordinates
in the canonical 1280x720 landscape space. `profiles` is a non-empty,
duplicate-free array drawn from this closed set:

```text
CN, JP, Global_en-us, Global_zh-tw, Global_ko-kr
```

The adapter derives the profile from the frozen device connection. `CN` and
`JP` require the matching server. A `Global_*` profile requires server
`Global` and the matching frozen OCR locale. Caller options, scripts, browser
input, and procedure resources cannot override it. A device/profile mismatch
fails before capture as unavailable; it does not silently select another
locale.

`android_only` must be `true` for this v1 migration. `interval_ms` and
`idle_feature_ms` are positive integers. `timeout_ms` is a positive call
deadline in milliseconds and is capped by the earlier execution-context
deadline. `duplicate_click_window_ms` is a non-negative integer.

`tentative` is a discriminated strict object. Disabled form contains exactly:

```json
{"enabled": false}
```

Enabled form contains exactly:

```json
{
  "enabled": true,
  "after_failed_cycles": 10,
  "repeat_each_failed_cycle": true,
  "click": [1238, 45],
  "post_wait_screenshot_intervals": 5
}
```

For v1, `after_failed_cycles` and `post_wait_screenshot_intervals` are positive
integers and `repeat_each_failed_cycle` must be true. The first tentative click
therefore happens on failure 11, matching Python's `fail_cnt > max_fail_cnt`.

All arrays are semantically ordered. Reordering them changes the definition
digest and may change behavior. Feature IDs are resolved only from the pinned
support bundle; native paths and ambient global feature registries are
forbidden.

## Deterministic execution order

Each iteration performs exactly the following sequence:

1. Poll the earlier context/call deadline, then cancellation.
2. Unless `skip_first_screenshot` is still set, capture one screenshot; the flag
   is then cleared permanently.
3. Poll deadline, then cancellation again.
4. If the call timeout has expired, return deadline exceeded; never manufacture
   an `end`.
5. On Android, run the foreground check only when both `idle_feature_ms` has
   elapsed since the last reaction feature and `interval_ms` has elapsed since
   the last foreground check.
6. While every feature in `loading.all_rgb` matches, capture the next screenshot
   and cooperatively poll deadline/cancellation between captures.
7. Test `ends.rgb` in order. The first match succeeds.
8. Test `ends.image` in order. The first match succeeds.
9. Test `reactions.rgb` in order; at most one reaction is selected.
10. If none matched, test `reactions.rgb_profiled` in order, skipping entries
    whose profile does not contain the frozen profile.
11. If none matched, test `reactions.image` in order.
12. If none matched, test `reactions.image_profiled` in order, skipping entries
    whose profile does not contain the frozen profile.
13. If none matched, test `popups.rgb` in order.
14. If none matched, test `popups.profiled_image` in order.
15. Apply duplicate-click suppression to the selected feature/coordinate. A
    suppressed click still counts as a matched feature and resets the failed
    recognition count.
16. If no feature matched, increment the failed count and apply the tentative
    rule. A matched feature resets the failed count to zero.

An image feature uses its bundle crop, a threshold of 0.8, and per-channel mean
RGB tolerance 20 unless the bundle explicitly carries the same Python tuple
override. A missing or profile-inapplicable feature evaluates false. It is not
an adapter error and must not resolve to another locale.

## Deadline, cancellation, and effect semantics

The adapter is synchronous and cooperatively bounded. It observes the earlier
of the execution-context deadline and `loop.timeout_ms` before and after every
capture, vision batch, input, wait, and foreground check. If deadline and
cancellation are simultaneously observable, deadline wins. Context deadline
uses `HOST004_DEADLINE_EXCEEDED` with `details.deadline_scope=context`; the
payload timeout is a narrower call deadline and uses
`details.deadline_scope=call`. Cancellation uses `HOST003_CANCELLED`.

No cancellation or timeout path produces a successful `end`. Helper work is
joined before `execute()` returns, and the request, reporter, screenshot, or
bundle view is never retained afterward.

Both initial procedures declare all five effects, in this canonical order:

```json
["capture", "vision", "input", "wait", "foreground_check"]
```

The executor reports each effect at its real boundary. Aggregate
`effect_state` follows the ProcedureHost contract: no possibly committed input
is `not_started`, confirmed input is `committed`, and indeterminate input is
`unknown`. Capture, vision, wait, and foreground checks do not upgrade a
foreground mismatch to committed input. Foreground mismatch maps to
`HOST006_UNAVAILABLE`, `retryable=true`, with the sole public discriminator
`unavailable_reason=foreground_package_mismatch`; package names are not
published. A late success cannot outrank a deadline or cancellation.

## Locale support bundle contract

The activation-level resource IDs are exactly:

```text
procedure-support/navigation.to-main-page/v1
procedure-support/group.open/v1
```

Each ID resolves through the pinned `ResourceSnapshot` using the frozen locale.
The resolved value is one immutable, self-contained support bundle. A bundle
contains its strict feature graph, RGB ranges, crop metadata, and only the real
PNG bytes applicable to that locale. It has no native paths, symlinks, parent
segments, unmanifested files, or ambient fallback. Every member carries size
and lowercase SHA-256 metadata; archive and member limits are enforced before
publication. The adapter must validate the whole bundle before executing any
effect.

This bundle boundary is necessary because `RuntimeProcedureActivation` requires
every declared resource ID to resolve. A naive union of per-locale PNG IDs would
otherwise require fake images for intentionally absent Python templates. Such
placeholders are forbidden. The concrete archive encoding and media type must
be frozen with the adapter implementation; until then no bundle digest is
production-authoritative.

The bundle's internal IDs use only lowercase canonical resource characters.
Runtime feature names remain the exact case-sensitive Python names and are
mapped by the feature graph; lowercasing a resource ID does not rename a
terminal.

### `navigation.to_main_page` closure

The support bundle has 77 logical members: one feature graph, seven RGB
definitions, and 69 image-template members. Its non-image IDs are:

```text
feature/navigation.to-main-page
rgb/main-page
rgb/relationship-rank-up
rgb/area-rank-up
rgb/level-up
rgb/reward-acquired
rgb/loading-not-white
rgb/loading-white
```

The 69 image IDs are listed in the resource appendix below. A locale bundle
contains only the applicable real PNG members while its graph preserves
missing-template-is-false behavior.

Terminals and effects are:

```json
{
  "terminals": [{"source": "main_page", "id": "main_page"}],
  "effects": ["capture", "vision", "input", "wait", "foreground_check"],
  "resources": ["procedure-support/navigation.to-main-page/v1"]
}
```

Payload example, with the complete common image table supplied by the pinned
feature graph:

```json
{
  "schema": "baas.procedure-definition/v1",
  "engine": "co_detect.python-compat/v1",
  "payload": {
    "profile_source": "device.server-and-locale/v1",
    "ends": {"rgb": ["main_page"], "image": []},
    "reactions": {
      "rgb": [
        {"feature": "relationship_rank_up", "click": [640, 360]},
        {"feature": "area_rank_up", "click": [640, 100]},
        {"feature": "level_up", "click": [640, 200]},
        {"feature": "reward_acquired", "click": [640, 100]}
      ],
      "rgb_profiled": [],
      "image": [
        {"feature": "main_page_quick-home", "click": [1236, 31]},
        {"feature": "group_sign-up-reward", "click": [920, 159]}
      ],
      "image_profiled": [
        {"profiles": ["CN"], "feature": "main_page_news", "click": [1142, 104]},
        {"profiles": ["JP"], "feature": "main_page_news", "click": [1142, 104]},
        {"profiles": ["Global_en-us", "Global_zh-tw", "Global_ko-kr"], "feature": "main_page_news", "click": [1239, 50]}
      ]
    },
    "popups": {
      "rgb": [
        {"feature": "reward_acquired", "click": [640, 100]},
        {"feature": "relationship_rank_up", "click": [640, 100]},
        {"feature": "level_up", "click": [640, 200]}
      ],
      "profiled_image": []
    },
    "loading": {"all_rgb": ["loadingNotWhite", "loadingWhite"]},
    "foreground_check": {"android_only": true, "interval_ms": 20000, "idle_feature_ms": 20000},
    "loop": {
      "skip_first_screenshot": false,
      "timeout_ms": 600000,
      "duplicate_click_window_ms": 2000,
      "tentative": {
        "enabled": true,
        "after_failed_cycles": 10,
        "repeat_each_failed_cycle": true,
        "click": [1238, 45],
        "post_wait_screenshot_intervals": 5
      }
    }
  }
}
```

The abbreviated `reactions.image` array above demonstrates the strict shape; a
production definition must contain the complete ordered Python table and pass a
golden-trace comparison. An abbreviated example is not a production definition.

### `group.open` closure

The support bundle has 25 logical members: one feature graph, six RGB
definitions, and 18 image-template members. Its terminals and effects are:

```json
{
  "terminals": [
    {"source": "group_sign-up-reward", "id": "group_sign-up-reward"},
    {"source": "group_menu", "id": "group_menu"},
    {"source": "group_join-club", "id": "group_join-club"}
  ],
  "effects": ["capture", "vision", "input", "wait", "foreground_check"],
  "resources": ["procedure-support/group.open/v1"]
}
```

Its core payload is:

```json
{
  "schema": "baas.procedure-definition/v1",
  "engine": "co_detect.python-compat/v1",
  "payload": {
    "profile_source": "device.server-and-locale/v1",
    "ends": {"rgb": [], "image": ["group_sign-up-reward", "group_menu", "group_join-club"]},
    "reactions": {
      "rgb": [],
      "rgb_profiled": [
        {"profiles": ["CN", "Global_en-us", "Global_zh-tw", "Global_ko-kr"], "feature": "main_page", "click": [578, 648]},
        {"profiles": ["JP"], "feature": "main_page", "click": [565, 648]}
      ],
      "image": [{"feature": "group_enter-button", "click": [297, 380]}],
      "image_profiled": []
    },
    "popups": {
      "rgb": [
        {"feature": "reward_acquired", "click": [640, 100]},
        {"feature": "relationship_rank_up", "click": [640, 100]},
        {"feature": "level_up", "click": [640, 200]}
      ],
      "profiled_image": [
        {"profiles": ["CN"], "feature": "main_page_renewal-month-card", "click": [927, 109]},
        {"profiles": ["Global_en-us", "Global_zh-tw", "Global_ko-kr"], "feature": "main_page_item-expiring-notice", "click": [931, 132]}
      ]
    },
    "loading": {"all_rgb": ["loadingNotWhite", "loadingWhite"]},
    "foreground_check": {"android_only": true, "interval_ms": 20000, "idle_feature_ms": 20000},
    "loop": {
      "skip_first_screenshot": true,
      "timeout_ms": 600000,
      "duplicate_click_window_ms": 2000,
      "tentative": {"enabled": false}
    }
  }
}
```

The complete `GAME_ONE_TIME_POP_UPS` and implicit server popup arrays are part of
the production graph. The example is deliberately abbreviated and therefore is
not a production definition.

## Known Python baseline resource defects

Migration must preserve known absence until a reviewed resource correction and
golden trace prove the intended new behavior. It must not silently repair these
with copied, empty, or mismatched assets:

- `draw-card-point-exchange-to-stone-piece-notice` is referenced without the
  `main_page_` prefix. The JP crop/image pair loads under
  `main_page_draw-card-point-exchange-to-stone-piece-notice`, so the current
  reference does not match it.
- Global code references `main_page_Failed-to-convert-errorResponse`, while the
  en-us PNG is named `Fail-to-convert-errorResponse.png`; the loader cannot form
  the referenced feature.
- Global en-us has crop metadata for
  `Failed-to-receive-Platform-Steam-GetEntitlementsAsJsonArray` and
  `Failed-to-request-prices` but no corresponding PNG files.
- JP has `attendance-reward.png` but no matching crop entry, so
  `main_page_attendance-reward` is not loaded.
- Global en-us has the `group_join-club` crop entry commented out; it is not a
  loaded end feature even if a similarly named file is present.
- Many other base reactions are intentionally absent in particular locales.
  The Python outcome is false, not fallback to another locale.

## Production activation gate

The following are all mandatory before generating production definition files
or adding these procedures to a real `baas.procedures.json`:

1. Implement and review the `co_detect.python-compat/v1` adapter, including
   bounded archive parsing and ResourceSnapshot-only feature loading.
2. Publish real locale support bundles from the external resources repository.
3. Compute and verify every real bundle/member size and SHA-256 digest.
4. Run Python-versus-C++ golden traces for success terminals, popup priority,
   timeout, cancellation, tentative clicks, loading, and foreground mismatch.
5. Generate complete, non-abbreviated definitions and compute their real size
   and digest.

Until every item is complete, production definitions are forbidden. Empty
`resources`, fake digests, placeholder definitions, bundled repository images,
or relabeling `legacy.appear_then_click/v1` as parity must fail review.

## Resource appendix

The `navigation.to_main_page` image-template IDs are:

```text
image/main_page_game-download-resource-notice
image/main_page_game-download-resource-notice2
image/main_page_game-download-resource-notice3
image/main_page_privacy-policy
image/main_page_quick-home
image/main_page_daily-attendance
image/main_page_item-expire
image/main_page_skip-notice
image/draw-card-point-exchange-to-stone-piece-notice
image/normal_task_fight-end-back-to-main-page
image/main_page_enter-existing-fight
image/main_page_login-feature
image/main_page_relationship-rank-up
image/main_page_full-notice
image/normal_task_task-info
image/normal_task_fight-confirm
image/normal_task_task-finish
image/normal_task_prize-confirm
image/normal_task_fail-confirm
image/normal_task_fight-task-info
image/normal_task_sweep-complete
image/normal_task_start-sweep-notice
image/normal_task_unlock-notice
image/normal_task_skip-sweep-complete
image/normal_task_charge-challenge-counts
image/purchase_ap_notice
image/purchase_ap_notice-localized
image/normal_task_task-operating-feature
image/normal_task_mission-operating-task-info
image/normal_task_mission-operating-task-info-notice
image/normal_task_mission-pause
image/normal_task_task-begin-without-further-editing-notice
image/normal_task_task-operating-round-over-notice
image/momo_talk_momotalk-peach
image/cafe_students-arrived
image/cafe_quick-home
image/pass_menu
image/pass_mission-menu
image/group_sign-up-reward
image/cafe_invitation-ticket
image/lesson_lesson-information
image/lesson_all-locations
image/lesson_lesson-report
image/lesson_purchase-lesson-ticket-menu
image/rewarded_task_purchase-bounty-ticket-menu
image/scrimmage_purchase-scrimmage-ticket-menu
image/arena_battle-win
image/arena_battle-lost
image/arena_season-record
image/arena_best-record
image/arena_opponent-info
image/plot_menu
image/plot_skip-plot-button
image/plot_skip-plot-notice
image/activity_fight-success-confirm
image/total_assault_reach-season-highest-record
image/total_assault_total-assault-info
image/cafe_cafe-reward-status
image/main_page_news
image/main_page_news2
image/main_page_net-work-unstable
image/main_page_fail-to-load-game-resources
image/main_page_attendance-reward
image/main_page_download-additional-resources
image/main_page_login-store
image/main_page_insufficient-inventory-space
image/main_page_failed-to-convert-errorresponse
image/main_page_store-service-unavailable
image/main_page_request-failed-notice
```

The `group.open` image-template IDs are:

```text
image/group_sign-up-reward
image/group_menu
image/group_join-club
image/group_enter-button
image/main_page_renewal-month-card
image/main_page_item-expired-notice
image/main_page_item-expiring-notice
image/main_page_failed-to-receive-platform-steam-getentitlementsasjsonarray
image/main_page_failed-to-request-prices
image/main_page_news
image/main_page_news2
image/main_page_item-expire
image/draw-card-point-exchange-to-stone-piece-notice
image/main_page_failed-to-convert-errorresponse
image/main_page_login-store
image/main_page_net-work-unstable
image/main_page_store-service-unavailable
image/main_page_request-failed-notice
```
