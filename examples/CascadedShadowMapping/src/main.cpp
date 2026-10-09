#include "../../Common/App.h"
#include ".generated/shadowCommon.h"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

//https://johanmedestrom.wordpress.com/2016/03/18/opengl-cascaded-shadow-maps/
//https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch10.html
//http://the-witness.net/news/2010/03/graphics-tech-shadow-maps-part-1/
//https://therealmjp.github.io/posts/shadow-maps/
//https://mynameismjp.wordpress.com/2013/09/10/shadow-maps/
//https://www.researchgate.net/publication/220791941_Sample_distribution_Shadow_Maps
//https://advances.realtimerendering.com/s2010/Lauritzen-SDSM(SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course).pdf

// kShadowMapSize of shaders/shadowCommon.slang: cascades move in steps of its texels, so they do not shimmer.
constexpr float kShadowMapSize = 4096.0f;

// Fits the cascades to the camera's frustum, split between the near and far plane logarithmically blended with
// uniformly (GPU Gems 3), each a sphere around its slice so its size does not change as the camera turns.
CascadeData CalculateCascades(const EOS::View& view, const glm::vec3& lightForward)
{
    constexpr uint32_t cascadeCount = std::tuple_size_v<decltype(CascadeData::viewProjection)>;
    float cascadeSplits[cascadeCount];

    const float nearClip = view.nearPlane;
    const float farClip = view.farPlane;
    const float clipRange = farClip - nearClip;

    const float minZ = nearClip;
    const float maxZ = nearClip + clipRange;

    const float range = maxZ - minZ;
    const float ratio = maxZ / minZ;

    for (uint32_t i = 0; i < cascadeCount; ++i)
    {
        constexpr float cascadeLambda = 0.25f;
        const float p = (static_cast<float>(i) + 1.0f) / static_cast<float>(cascadeCount);
        const float log = minZ * std::pow(ratio, p);
        const float uniform = minZ + range * p;
        const float d = cascadeLambda * (log - uniform) + uniform;
        cascadeSplits[i] = (d - nearClip) / clipRange;
    }

    CascadeData cascades{};
    float lastSplitDist = 0.0;
    for (uint32_t i = 0; i < cascadeCount; ++i)
    {
        const float splitDist = cascadeSplits[i];

        // The corners of the camera's frustum, in NDC (Vulkan depth is [0, 1]), then in world space.
        glm::vec3 frustumCorners[8] =
        {
            glm::vec3(-1.0f,  1.0f, 0.0f),
            glm::vec3( 1.0f,  1.0f, 0.0f),
            glm::vec3( 1.0f, -1.0f, 0.0f),
            glm::vec3(-1.0f, -1.0f, 0.0f),
            glm::vec3(-1.0f,  1.0f,  1.0f),
            glm::vec3( 1.0f,  1.0f,  1.0f),
            glm::vec3( 1.0f, -1.0f,  1.0f),
            glm::vec3(-1.0f, -1.0f,  1.0f),
        };
        for (glm::vec3& frustumCorner : frustumCorners)
        {
            const glm::vec4 invCorner = view.inverseViewProjection * glm::vec4(frustumCorner, 1.0f);
            frustumCorner = invCorner / invCorner.w;
        }

        // This cascade's slice of it.
        for (uint32_t j = 0; j < 4; ++j)
        {
            const glm::vec3 dist = frustumCorners[j + 4] - frustumCorners[j];
            frustumCorners[j + 4] = frustumCorners[j] + (dist * splitDist);
            frustumCorners[j] = frustumCorners[j] + (dist * lastSplitDist);
        }

        glm::vec3 frustumCenter = glm::vec3(0.0f);
        for (const glm::vec3& frustumCorner : frustumCorners) frustumCenter += frustumCorner;
        frustumCenter /= 8.0f;

        // The radius of the sphere around the slice.
        float radius = 0.0f;
        for (const glm::vec3& frustumCorner : frustumCorners) radius = glm::max(radius, glm::length(frustumCorner - frustumCenter));
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // The light's orthographic projection of the sphere.
        const glm::vec3 maxExtents = glm::vec3(radius);
        const glm::vec3 minExtents = -maxExtents;
        constexpr glm::vec3 lightUp = glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::mat4 lightViewMatrix = glm::lookAt(frustumCenter - lightForward * -minExtents.z, frustumCenter, lightUp);
        glm::mat4 lightOrthoMatrix = glm::ortho(minExtents.x, maxExtents.x, minExtents.y, maxExtents.y, 0.0f, maxExtents.z - minExtents.z);

        // Texel grid stabilization: move the projection in whole texels, so the shadow does not shimmer as the camera moves.
        const glm::mat4 shadowMatrix = lightOrthoMatrix * lightViewMatrix;
        glm::vec4 shadowOrigin = shadowMatrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        shadowOrigin = shadowOrigin * (kShadowMapSize * 0.5f);
        const glm::vec4 roundedOrigin = glm::round(shadowOrigin);
        glm::vec4 roundOffset = (roundedOrigin - shadowOrigin) * (2.0f / kShadowMapSize);
        roundOffset.z = 0.0f;
        roundOffset.w = 0.0f;
        lightOrthoMatrix[3] += roundOffset;

        cascades.viewProjection[i] = lightOrthoMatrix * lightViewMatrix;
        cascades.splits[static_cast<glm::length_t>(i)] = nearClip + splitDist * clipRange;
        lastSplitDist = cascadeSplits[i];
    }

    return cascades;
}

// The frame is one of three graph files, picked in the panel: graphs/csmCompute.yaml fits the cascades on the GPU,
// csmCpu.yaml on the CPU with the cpuCascades data below, csmRayQuery.yaml traces rays instead.
int main()
{
    ExampleApp App{{.Name = "EOS - Cascaded Shadow Mapping"}};

    App.Passes.Register(
    {
        .Name = "cpuCascades",
        .Pins =
        {
            EOS::Pin::Typed(EOS::Pin::ReadBuffer("view"), "View"),
            EOS::Pin::Typed(EOS::Pin::ReadBuffer("light"), "DirectionalLight"),
            EOS::Pin::Typed(EOS::Pin::BufferOutput("cascades", 0), "CascadeData"),
        },
        .Setup = [](EOS::PassSetup& setup)
        {
            const EOS::View* view = setup.Data.Host<EOS::View>("view");
            const EOS::DirectionalLight* sun = setup.Data.Host<EOS::DirectionalLight>("light");
            if (!view || !sun) return;

            setup.Upload("cascades", CalculateCascades(*view, glm::normalize(sun->direction)));
        },
    });

    App.Run({"csmCompute", "csmCpu", "csmRayQuery", "csmCompute_game"});
    return 0;
}
