# Trigger command catalog

`BAAS_service_trigger_catalog` is the dependency-free C++20 command-selection
catalog for the legacy `trigger` business channel. It mirrors the selection
order in `baas-dev/service/channels/trigger.py` and
`baas-dev/service/api/commands.py`; it does not execute commands, inspect JSON
payloads, own tasks, or perform network I/O.

## Selection order

The catalog is an ordered, immutable table. The two stream commands are exact
matches selected before the ordinary command rules. The ordinary rules then
preserve Python order: exact `start_scheduler`, `stop_scheduler`, and `solve`;
the broad `start_`, `add_config`, and `remove_config` prefix families; and the
remaining exact commands.

Prefix matching deliberately mirrors Python `str.startswith`:

- `start_` itself and `start_scheduler_extra` select canonical `start_*`, while
  exact `start_scheduler` wins first;
- `add_config`, `add_configuration`, and any other `add_config...` spelling
  select canonical `add_config*`;
- the same rule applies to canonical `remove_config*`.

There is no normalization, case folding, truncation, or allocation. Unknown
input returns the stable `unknown` lookup classification and no descriptor.

## Descriptor contract

Every known selector supplies:

- a canonical exact name or `*`-suffixed prefix selector;
- a stable family (`scheduler`, `task`, `configuration`, `diagnostics`,
  `update`, `backend`, `device`, or `status`);
- `single` or `stream` response mode;
- whether the top-level `config_id` is required; and
- inbound binary policy.

Only `import_config` has `required` inbound binary policy. All other commands
have `forbidden`. Transports must additionally enforce the legacy payload
declaration before receiving that frame. `export_config` produces outbound
binary, which is a response/executor concern and is intentionally not modeled
as inbound policy.

The commands requiring top-level `config_id` are `start_scheduler`,
`stop_scheduler`, `solve`, every `start_*` family member, and `control_device`.
Payload fields such as configuration `id`, `task`, or `operation` are outside
this catalog.

## Admission integration and compatibility

`TriggerIngress` resolves every decoded envelope against this table before it
can become ready. Unknown commands, absent or empty required `config_id`, a
missing required `payload.binary:true`, and a marker forbidden by the selected
descriptor receive stable ingress errors. A present-empty config ID remains
valid for commands such as `status` that do not require one. The descriptor's
single/stream mode is converted once into protocol `ResponseMode`, stored in
the ready admission, and passed directly to `TriggerSession` by `admit_to()`.

This is deliberately stricter than the current Python handler at the transport
edge. Python ignores `payload.binary:true` on non-import commands and lets an
`import_config` without the true marker reach later execution; C++ ingress
rejects both before frame ownership or session correlation changes. Accepted
command inventory, prefix order, and config-ID truthiness still mirror Python.
Whether a schema/framing rejection is connection-fatal or recoverable belongs
to the still-pending live adapter and is not claimed here.

The catalog itself remains allocation-free and does not duplicate
`TriggerSession` command syntax, UTF-8, or 128-byte admission limits. A large
or embedded-NUL input therefore still receives exact length-aware selector
semantics in isolated catalog tests, even though envelope/session boundaries
reject it in the integrated path.

`BAAS_service_trigger_catalog_tests` freezes the complete inventory and
metadata, precedence collisions, the intentionally broad wildcard families,
stream-only commands, `config_id` and binary policies, embedded-NUL and invalid
byte cases, oversized inputs, stable descriptor identity, and total enum-name
fallbacks. `BAAS_service_trigger_ingress_tests` freezes integrated policy
errors, response-mode derivation, direct session admission, and zero-length
binary presence. No application, service, socket, or device is started.
