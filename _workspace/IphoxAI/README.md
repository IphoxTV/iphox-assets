# IphoxAI Native R0

Recovery branch for the native C++ rewrite.

This checkpoint contains the first compiled-design slice of the new cognitive core:

- typed closed-decision questions (Noul / Choice / Score)
- vendor-neutral IDecisionBackend
- deterministic BaselineDecisionBackend
- strict DecisionValidator
- StateProjection allow-list
- DecisionReceipt with stale-state protection
- Supervisor that never gives the decision backend execution authority
- tests for fail-closed behavior

JEV/TypeSafe is treated as an optional backend behind IDecisionBackend. The rest of IphoxAI never depends on its service-specific limits or wire format.

This branch is staging only. The canonical recovered IphoxAI ZIP remains the migration source.
