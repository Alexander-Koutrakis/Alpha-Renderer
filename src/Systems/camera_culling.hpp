#pragma once

#include "Math/view_frustum.hpp"
#include "Math/octree.hpp"
#include "Scene/scene.hpp"
#include "Rendering/Core/frame_context.hpp"


namespace Systems{

    class CameraCulling{   
        public:
           
            static void updateFrameContext(FrameContext& frameContext);

        private:
            static void frustumCullRenderers(
                const ViewFrustum viewFrustum,
                AABB& frameSceneBounds,
                MeshRenderingData& meshRenderingData); 

            static void updateOpaqueModelBuffers(
                FrameContext& frameContext,
                MeshRenderingData& meshRenderingData);
            static void updateTransparentModelBuffers(
                FrameContext& frameContext,
                MeshRenderingData& meshRenderingData);
            
    };
}