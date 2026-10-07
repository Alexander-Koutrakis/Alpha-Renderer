#pragma once

#include "Math/view_frustum.hpp"
#include "Math/octree.hpp"
#include "Scene/scene.hpp"
#include "Rendering/Core/frame_context.hpp"

namespace Systems {

class CameraCulling {
public:
    static void updateFrameContext(Rendering::FrameContext& frameContext);

private:
    static void frustumCullRenderers(const Math::ViewFrustum viewFrustum, Math::AABB& frameSceneBounds,
                                     Rendering::MeshRenderingData& meshRenderingData);

    static void updateOpaqueModelBuffers(Rendering::FrameContext& frameContext,
                                         Rendering::MeshRenderingData& meshRenderingData);
    static void updateTransparentModelBuffers(Rendering::FrameContext& frameContext,
                                              Rendering::MeshRenderingData& meshRenderingData);
};
} // namespace Systems