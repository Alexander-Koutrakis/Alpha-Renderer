# Findings from the descriptors phase

Observed while migrating descriptor layouts (Slice A). Not fixed here because the phase must keep behaviour identical;
the structural call log proves it did.

- **Duplicate layout.** `ResourceManager::createPBRDescriptorSetLayout` and `RenderingResources` (`materialDescriptorSetLayout`)
  create two descriptor set layouts with identical bindings (1 UBO + 4 combined image samplers, fragment stage). Two
  separate `VkDescriptorSetLayout` objects exist for the same shape; materials use the `ResourceManager` one, the
  pass pipelines use the `RenderingResources` one. Candidate for `hygiene`: keep one.
- **Pool sizes are hand-counted** (`RenderingResources::createDescriptorPool`, comments like "18 core sets",
  "gbuffer4 + depth + incident"). Adding a binding or set silently under-sizes the pool. Deriving the sizes from the
  layouts was deferred (see `docs/descriptors-plan.md`, decisions).
- **Two existing validation errors** (`VUID-VkComputePipelineCreateInfo-layout-07988`,
  `VUID-VkGraphicsPipelineCreateInfo-layout-07988`): a compute and a graphics pipeline use a descriptor
  (`uSkybox` in set 1 binding 0; `camera` in set 0 binding 0) that the pipeline layout does not provide for that stage.
  Pre-existing, identical before and after the migration.
- **Stale comments removed.** The old layout blocks had binding numbers in comments that disagreed with the code
  (e.g. "Material UBO (binding = 1)" for binding 0). They were dropped with the blocks.
- **Error messages are now generic.** The per-layout exception texts ("failed to create gbuffer descriptor set
  layout!") became one message with the `VkResult`. The "Creating X descriptor set layout..." lines printed just
  before each call still say which one was being created.
