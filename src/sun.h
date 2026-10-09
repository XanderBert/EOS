#pragma once

#include "renderGraphFile.h"
#include ".generated/eos/lighting.h"    // DirectionalLight

// The sun, graph file data: a directional light every pass that needs it reads from one place.
//
//     data:
//       Sun:   { type: sun, rotation: [-73, -90], intensity: 3 }
//     passes:
//       Shade: { shader: shade }
//     edges:
//       - Sun.light -> Shade.sun
namespace EOS
{
    /**
     * @brief The direction a light travels in for a rotation of pitch and yaw, in degrees. Pitch -90 shines straight
     *        down; yaw turns it about the up axis, starting from +x.
     */
    [[nodiscard]] glm::vec3 DirectionFromRotation(glm::vec2 rotation);

    /**
     * @brief Registers the sun data type: the properties rotation (pitch, yaw, as DirectionFromRotation), color and
     *        intensity, and a `light` output: eos.lighting's DirectionalLight, uploaded every frame. C++ types
     *        downstream read it with PassData::Host<DirectionalLight>.
     */
    void RegisterSun(PassRegistry& registry);
}
