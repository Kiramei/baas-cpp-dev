# Python-compatible `co_detect` procedure engine contract

Status: strict immutable definition parsing/model implemented; the pure C++
execution state machine and bounded generation-bound support-bundle loader are
also implemented. The immutable production device-session/feature adapter and
BAASConnection-backed application port are implemented behind narrow injected
boundaries. The remaining production gate is the live-composed engine adapter, externally published real
locale bundles, real resource digests, and production procedure definitions; none exists yet.
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
| `ends` | object | Exactly `rgb` and `image`; RGB entries are feature IDs and image entries are the strict image-feature form below. Both arrays are ordered and duplicate-free by feature ID. |
| `reactions` | object | Exactly `rgb`, `rgb_profiled`, `image`, and `image_profiled`, each an ordered array of reaction objects. |
| `popups` | object | Exactly `rgb` and `profiled_image`, each an ordered array of reaction objects. |
| `loading` | object | Exactly `all_rgb`; an ordered, non-empty, duplicate-free feature array. |
| `foreground_check` | object | Exactly `android_only`, `interval_ms`, and `idle_feature_ms`. |
| `loop` | object | Exactly `skip_first_screenshot`, `timeout_ms`, `duplicate_click_window_ms`, and `tentative`. |

A non-profiled RGB reaction contains exactly `feature` and `click`. A profiled
RGB reaction contains exactly `profiles`, `feature`, and `click`. Image
reactions under `reactions.image` and `reactions.image_profiled` have the same
required fields and may additionally contain `threshold`, or `threshold`
followed by `rgb_diff`, matching Python's optional tuple positions. `rgb_diff`
without `threshold` is invalid. Image ends are either a feature string, or a
strict object containing `feature` plus the same optional image-match fields.
The definition owns these per-call tuple overrides; the support bundle does
not. Popup objects are exact feature/click forms and cannot carry image-match
overrides because Python's popup path does not read tuple positions 2 and 3.

`feature` is a non-empty logical feature string. `click` is exactly two
integral coordinates in the canonical 1280x720 landscape space. Non-negative
coordinates include the Python clamp boundary `x=1280`, `y=720`. If either
reaction coordinate is negative, the reaction is match-only: it counts as a
normal reaction match but issues no input. Tentative clicks must have two
non-negative coordinates. `threshold` is finite and in `[0,1]`; `rgb_diff` is
an integer in `[0,255]`. Omitted values mean Python's `0.8` and `20` defaults.
`profiles` is a non-empty, duplicate-free array drawn from this closed set:

```text
CN, JP, Global_en-us, Global_zh-tw, Global_ko-kr
```

The adapter derives the profile from the frozen device connection. `CN` and
`JP` require the matching server. A `Global_*` profile requires server
`Global` and the matching frozen OCR locale. Caller options, scripts, browser
input, and procedure resources cannot override it. A device/profile mismatch
fails before capture as unavailable; it does not silently select another
locale. This fail-closed mismatch handling is an intentional C++ safety
hardening, not a claim that Python raises the same error at the same point.

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

### Implemented definition and execution boundaries

`BAAS_runtime_co_detect_definition_model` implements the pure C++
definition boundary described above. It strictly parses the wrapper and
payload, publishes an immutable snapshot that owns the verified source bytes,
and exposes both the exact-source SHA-256 and a deterministic semantic
identity SHA-256. The semantic identity uses fixed field order while preserving
every array order, so insignificant JSON whitespace does not change it and an
array reorder does. Identity domain v2 includes effective image threshold/RGB
tolerance and signed match-only coordinates. Spelling the defaults explicitly
has the same semantic identity as omitting them.

The implementation shares the bounded strict-JSON parser used by runtime
procedure activation. Stable typed errors cover malformed UTF-8/JSON,
duplicate object names, field closure, profiles, duplicates, coordinate and
integer ranges, tentative forms, and caller-supplied byte/node/string/item/work
limits. It accepts and retains logical feature strings with their exact case;
it does not resolve those strings to filesystem paths or ambient registries.
The definition-model target has no image/resource payloads and performs no
capture, vision, input, wait, or foreground effects. By itself, the target
does not open the production
activation gate.

`BAAS_runtime_co_detect_executor` implements the synchronous
`ProcedureExecutor` state machine. Its `CoDetectPinnedDeviceSession` owns the frozen
device/profile, clock, latest frame, capture, input, wait, and foreground
boundary. `CoDetectPinnedFeatureView` is the narrow pathless RGB/image matching seam
for the pinned support-bundle adapter. The executor never opens native paths,
consults a global feature registry, or embeds resource/configuration data.

The executor enforces the ordered loop below, shared-frame fail-closed behavior,
deadline-before-cancellation precedence, synchronous effect boundaries, typed
device/resource failures, and exception/allocation fail-closed behavior.
`BAAS_runtime_co_detect_production_adapter` composes this executor with the verified
support-bundle loader; the pure execution target does not manufacture either dependency.

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
15. For an ordinary reaction selected in steps 9–12, apply duplicate-click
    suppression to the feature/coordinate. A suppressed or match-only reaction
    still updates `feature_last_appear_time` and resets the failed recognition
    count. Only a non-negative, non-suppressed reaction issues input.
16. A popup selected in steps 13–14 follows Python's separate popup path: it
    does not perform ordinary-reaction duplicate suppression, does not update
    `feature_last_appear_time`, and does not reset the tentative failure count.
    It does prevent a tentative click in that iteration. A negative popup click
    is likewise match-only.
17. If neither an ordinary reaction nor a popup matched, increment the failed
    count and apply the tentative rule. Only an ordinary reaction resets the
    failed count to zero.

An image feature uses its bundle template and crop. The definition supplies an
optional per-call threshold/RGB tuple override; otherwise threshold 0.8 and
per-channel mean RGB tolerance 20 apply. The support bundle exclusively owns
the feature graph, RGB ranges, crop metadata, and PNG bytes and cannot alter a
definition's call parameters. A missing or profile-inapplicable feature
evaluates false. It is not an adapter error and must not resolve to another
locale.

### Device-session frame ownership

The physical-device session owns one normalized, fully owned latest-frame
cache across serialized procedure calls. A successful
`navigation.to_main_page` leaves its terminal frame in that cache;
`group.open`, whose definition sets `skip_first_screenshot=true`, consumes that
same device/profile frame for its first iteration without capturing again. The
cache is not stored in a procedure definition, script option, resource bundle,
or execution request, and it never crosses a physical device or frozen profile.
Per-call duplicate-click state is reset at procedure entry and is not shared.

If `skip_first_screenshot` is requested without a valid cached frame, the
adapter fails closed before vision or input with `HOST006_UNAVAILABLE` and
`unavailable_reason=recent_frame_unavailable`; it must not silently capture a
replacement and change Python ordering. The session also owns screenshot
interval timing, deterministic/injectable click jitter RNG, normalized-device
coordinate conversion, and cancellable waits. Those values are runtime device
state, never definition or support-bundle data.

The executor accepts only a pinned device-session object whose device ID,
profile, and session epoch are immutable. Every cached or captured frame carries
the same identity and is checked before vision. The feature view is bound to the
same profile and exact activation generation. Identity drift, a cross-device
frame, a cross-profile view, or a descriptor not owned by the exact activation
snapshot fails closed before further device work.

### Immutable production session and feature view

`CoDetectProductionDevicePort` is the only embedding seam. Its owner publishes an
immutable identity token containing device ID, frozen profile, session epoch, and
platform. Reconnect, device switch, or profile change creates a new token and a new
adapter object; it cannot retarget an existing pin. The executor polls both the
session-owner pin and bundle generation/profile before and after every vision,
capture, input, wait, and foreground operation.

The port supplies only owned BGR capture/cache buffers, input, bounded wait,
foreground, monotonic clock, and screenshot interval operations. The adapter copies
captured/cache rows into packed immutable storage before publication and rejects
wrong identity, epoch, profile, dimensions, stride, or byte count. It contains no
resource/config path, script definition, test hook, or ambient feature registry.

The feature view reads RGB samples and decoded PNG templates only from its retained
`CoDetectSupportBundle`. RGB samples are conjunctive and use exact BGR pixels at the
canonical coordinates. Image matching validates the frozen crop, applies per-channel
mean RGB tolerance, resizes the whole crop to the template with OpenCV `INTER_AREA`,
then reads the single `TM_CCOEFF_NORMED` result and requires it to be strictly greater
than the threshold; definition call threshold/RGB values override the bundle defaults. Missing features return `false`.
OpenCV work is not mid-call interruptible, but both sides are identity/control-polled
and frame/crop/template dimensions are hard bounded.

### BAASConnection production port

`BAASConnectionCoDetectOwner` is the concrete lifecycle owner for the legacy C++
device stack. The embedding application injects an already-created, shared `BAAS`
instance; the adapter binds only its public `BAASConnection`, `BAASScreenshot`, and
`BAASControl` capabilities. It never constructs an application from a config name,
reads a config/resource/script path, or consults an ambient current repository.
This is the concrete application `CoDetectProductionDevicePort` implementation;
live composition remains gated separately below.

The production factory is not validated by a compile-only archive. The
`BAAS_runtime_baas_connection_co_detect_link_closure` executable links the real
resource-free `BAAS_CORE_SOURCES` legacy core and its complete dependency graph. Its
`main` calls the production factory, so the final link must resolve every `BAAS`,
`BAASConnection`, `BAASScreenshot`, and `BAASControl` method used by the backend.
The null application is rejected before any device, resource, or config operation.
This target neither includes the legacy application resource CMake nor copies or
embeds a resource/config payload. The BAAS application workflow configures it in a
separate `BUILD_APP_BAAS=OFF`, `BAAS_FETCH_RESOURCES=OFF` build tree against both
existing BAAS App Conan package profiles (CPU-provisioned and CUDA-provisioned).
The probe itself is backend-agnostic and does not claim to execute a CUDA path.

The production backend freezes the exact connection, screenshot, and control object
addresses plus serial, package, server, and language. The server/language pair must
map exactly to the requested `CN`, `JP`, `Global_en-us`, `Global_zh-tw`, or
`Global_ko-kr` profile. An in-place connection/config switch therefore makes the
backend invalid. A caller performing an intentional reconnect or configuration
switch calls `activate()` with the replacement application/backend and a strictly greater non-zero epoch.
Activation creates a new port and shared immutable token;
the old port returns no current identity and can never target the replacement.
An observed invalid identity permanently tombstones that session and clears its
latest frame. Restoring the same backend fields cannot revive the token (including
an ABA-shaped restore). Production composition must call `invalidate()` before
mutating or replacing the legacy device/config stack, then activate a new epoch.

Activation preallocates the candidate session, immutable identity, and port before
publishing anything. Allocation failure therefore leaves both the existing binding
and the last accepted epoch unchanged. No test-only field changes the production
owner/session/port object layout.

Capture, click, and foreground operations serialize against replacement. An effect
that has already linearized finishes before the old token is withdrawn; after token
withdrawal no old-port effect reaches the legacy backend. Waits use at most 50 ms
slices, are capped at 3,840,000 ms, and are explicitly woken by replacement or
invalidation. Control and identity are checked before and after each operation and
at screenshot/wait checkpoints. Every backend access occurs under the session
operation mutex after checking current/tombstone state. Wait probes acquire that
same mutex; after `invalidate()` or replacement returns, every retired entry point
rejects without touching the old backend, providing a safe ownership handoff barrier.

The live screenshot is normalized to exactly 1280x720 packed BGR8. Input screenshots
must be three-channel 16:9 images no larger than 7680x4320; resizing uses OpenCV
`INTER_AREA`. The published row stride is exactly 3840 bytes and the immutable owned
payload is exactly 2,764,800 bytes. Latest-frame publication accepts only that shape,
byte count, and the exact owner token. Click coordinates use the inclusive Python
clamp range (`0..1280`, `0..720`) and are sent through `BAASControl` without random
offset; its existing
screen ratio conversion remains authoritative. Foreground checks compare the live
package only with the package frozen at activation and do not expose package names.

The concrete backend currently requires the embedding owner to give it exclusive
device-operation ownership for the duration of a runtime lease because the legacy
`BAAS` public API does not expose a cross-consumer operation mutex. Application
composition must create/rebind this owner whenever it replaces a `BAAS` instance;
silently mutating the same instance is permanently tombstoned and fails closed, not
adopted or revived.

## Deadline, cancellation, and effect semantics

The rules in this section are intentional C++ safety hardening, not bit-for-bit
Python behavior. Python samples time before capture, does not poll timeout in
its loading loop, may race from a cleared `flag_run` to a terminal, and normally
schedules click helper work asynchronously. The adapter preserves that
pre-capture iteration time for foreground gates, feature-last-appearance state,
and ordinary duplicate-click decisions while real-time cooperative polls remain
independent. The production adapter uses the stricter control rules below while
retaining Python feature priority, click parameters, timing state, and tentative
transitions.

The adapter is synchronous and cooperatively bounded. It observes the earlier
of the execution-context deadline and `loop.timeout_ms` before and after every
capture, vision batch, input, wait, and foreground check. If deadline and
cancellation are simultaneously observable, deadline wins. Context deadline
uses `HOST004_DEADLINE_EXCEEDED` with `details.deadline_scope=context`; the
payload timeout is a narrower call deadline and uses
`details.deadline_scope=call`. Cancellation uses `HOST003_CANCELLED`.

No cancellation or timeout path produces a successful `end`. Helper work is
joined before `execute()` returns. The request, reporter, support-bundle view,
and local frame views are never retained afterward; only the device session may
retain its single owned normalized latest-frame copy under the cache contract
above.

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
placeholders are forbidden. The concrete archive encoding and media type are frozen
below. No bundle digest is production-authoritative until the external resources
repository publishes and verifies the real locale bundles.

The bundle's internal IDs use only lowercase canonical resource characters.
Runtime feature names remain the exact case-sensitive Python names and are
mapped by the feature graph; lowercasing a resource ID does not rename a
terminal.

### Frozen support-bundle v1 encoding

The outer `ResourceEntry.media_type` is exactly
`application/vnd.baas.co-detect-support-bundle.v1+zip`. The ZIP byte stream has no
prefix or suffix and contains, in physical order:

1. `bundle.magic`, STORE, with the 16 bytes `BAASCDSB`, little-endian major `1`,
   minor `0`, and four reserved zero bytes;
2. `manifest.json`, STORE; and
3. one flat member per manifest item, named `m00000000`, `m00000001`, ... in
   lowercase hexadecimal order.

These fixed ASCII names are container labels, never filesystem paths. ZIP64,
encryption, data descriptors, entry/archive comments, extra fields, directories,
symlinks, path separators, duplicate names, central/local disagreement, unlisted
members, leading/trailing bytes, and compression-ratio bombs are rejected before
payload extraction. Payload members use only STORE or DEFLATE.

The aggregate work budget reserves each entry's declared uncompressed size before
calling miniz, so an inflate cannot begin when its output work is unaffordable.
Cancellation is polled immediately before and after each extraction and throughout
container/JSON/PNG validation. The individual miniz extraction call is not
mid-inflate interruptible; cancellation latency during that call is therefore
bounded by the per-member compressed/uncompressed and ratio limits.

The strict manifest root contains exactly `schema`, `format_version`, `bundle_id`,
`locale`, `profile`, `member_count`, `payload_size`, and `members`. Its schema is
`baas.co-detect-support-bundle/v1`; every ordered member contains exactly `id`,
`kind`, `media_type`, `size`, and lowercase `sha256`. The first logical member is
the sole feature graph, followed by bytewise-sorted RGB members and then
bytewise-sorted PNG members. Member IDs are lowercase canonical resource IDs; the
graph maps them to exact case-sensitive Python feature names.

`BAAS_runtime_co_detect_support_bundle` accepts only a
`RuntimeResourceSnapshotActivation`, its expected generation, the exact support
resource ID, and the frozen locale/profile. A neutral or activity fallback returned
by generic `ResourceSnapshot` resolution is rejected. It verifies every archive and
member bound/digest, strict graph/RGB/crop schema, PNG structure and CRC, then
actually decodes every PNG through OpenCV to an owned immutable packed BGR8
template before publication. A feature omitted from the profile graph is a normal
`false` lookup; an absent or mismatched whole bundle is unavailable before capture.
No native resource path or ambient registry is accepted.

### `navigation.to_main_page` closure

The audited cross-profile union closure has at most 77 logical identities: one
feature graph, seven RGB definitions, and 69 image-template identities. This is
not a per-locale member count. Real locale bundles contain only the exact
crop-metadata/PNG intersection: CN 64, JP 56, Global_en-us 61,
Global_zh-tw 58, and Global_ko-kr 57 members, including the graph. Its
non-image IDs are:

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

The audited cross-profile union closure has at most 25 logical identities: one
feature graph, six RGB definitions, and 18 image-template identities. This is
not a per-locale member count. Real locale bundles contain CN 16, JP 12,
Global_en-us 17, Global_zh-tw 14, and Global_ko-kr 13 members, including the
graph. Its terminals and effects are:

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
      "image_profiled": [
        {"profiles": ["CN"], "feature": "main_page_renewal-month-card", "click": [927, 109]},
        {"profiles": ["Global_en-us", "Global_zh-tw", "Global_ko-kr"], "feature": "main_page_item-expiring-notice", "click": [931, 132]}
      ]
    },
    "popups": {
      "rgb": [
        {"feature": "reward_acquired", "click": [640, 100]},
        {"feature": "relationship_rank_up", "click": [640, 100]},
        {"feature": "level_up", "click": [640, 200]}
      ],
      "profiled_image": [
        {"profiles": ["CN"], "feature": "main_page_net-work-unstable", "click": [767, 501]},
        {"profiles": ["Global_en-us", "Global_zh-tw", "Global_ko-kr"], "feature": "main_page_login-store", "click": [883, 162]}
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

The complete `GAME_ONE_TIME_POP_UPS` table belongs to
`reactions.image_profiled`, after `group_enter-button` and before every common
or server popup, because Python appends it to `img_possible` before calling
`co_detect`. Only the implicit server popup table belongs to
`popups.profiled_image`. The example is deliberately abbreviated and therefore
is not a production definition.

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

1. Review and integrate the implemented `BAASConnectionCoDetectOwner` concrete
   `CoDetectProductionDevicePort` binding with the live application owner; the
   binding contains no ambient resources or mutable retargeting.
2. Publish real locale support bundles from the external resources repository
   by feeding its independently reviewed production lock to
   `baas-runtime-publisher`; the compiler exists, but the external lock and
   generated dynamic data are not stored in `baas-cpp-dev`.
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
