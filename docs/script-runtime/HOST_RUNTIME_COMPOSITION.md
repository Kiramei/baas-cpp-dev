# Host runtime composition

Production BAAS Script execution constructs Host adapters independently, then
combines their immutable metadata and native bindings with
`compose_host_runtime`. The final registry reconstruction is the trust boundary:
duplicate module versions, exports, binding identifiers, malformed contracts,
and configured limits are checked again after concatenation. Every export must
resolve to a native binding in the same contribution, and orphan bindings are
rejected; adapters cannot silently cross-wire each other's contracts.

The composition owns copied descriptors and callbacks, optional adapter lifetime
owners, and at most one typed-handle release dispatcher. A dispatcher belongs to
one evaluator/heap context, so combining distinct dispatchers fails closed with
`HCOMP006_MULTIPLE_RELEASE_DISPATCHERS`.

Create evaluator options with `ComposedHostRuntime::options()`. The returned
options retain the full composition state, so explicit adapter owners remain
alive until after evaluator handle teardown even if the composition result was a
temporary.

Composition never reads configuration, repository paths, resources, or script
packages. Those inputs remain dynamically activated by their dedicated runtime
owners. Host composition only joins already validated, in-memory adapters for a
single pinned execution context.

Stable composition failures are `HCOMP001_INVALID_LIMITS` through
`HCOMP009_LINK_VALIDATION_WORK_EXCEEDED`. Registry and binding validation errors
remain their original typed errors so operators can distinguish an invalid
adapter contract from a composition ownership failure.
