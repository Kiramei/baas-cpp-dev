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

## Boundary and verification

The catalog classifies a supplied byte string only. It intentionally does not
duplicate `TriggerSession` command syntax, UTF-8, or 128-byte admission limits;
a transport must admit an envelope before catalog dispatch. Consequently a
large or embedded-NUL input still receives exact length-aware selector
semantics when the catalog is tested in isolation, even though normal envelope
admission rejects it.

`BAAS_service_trigger_catalog_tests` freezes the complete inventory and
metadata, precedence collisions, the intentionally broad wildcard families,
stream-only commands, `config_id` and binary policies, embedded-NUL and invalid
byte cases, oversized inputs, stable descriptor identity, and total enum-name
fallbacks. No application, service, socket, or device is started by the suite.
