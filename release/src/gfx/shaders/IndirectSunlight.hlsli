#ifndef CUBE_INDIRECT_SUNLIGHT_HLSLI
#define CUBE_INDIRECT_SUNLIGHT_HLSLI

// One-bounce irradiance from a low-resolution reflective shadow map.
#include "WorldUniforms.hlsli"
[[vk::binding(1, 1)]] Texture2D<float4> reflectanceMap;
[[vk::binding(4, 1)]] Texture2D<float> reflectiveDepth;

float3 indirectSunlight(float3 worldPosition, float3 normal)
{
    const float3 toCamera = worldPosition - sceneCamera.xyz;
    const float fade = 1 - smoothstep(28 * 28, 50 * 50, dot(toCamera, toCamera));
    if (fade <= 0)
        return 0;

    const float3 clip = shadowClip(float4(worldPosition, 1));
    const float texel = shadowProjection.w;
    const float2 uv = clip.xy * float2(0.5, -0.5) + 0.5;
    if (clip.z <= 0 || clip.z >= 1 || any(uv < 18 * texel) || any(uv > 1 - 18 * texel))
        return 0;

    const float radius = shadowProjection.y;
    const float depthRange = shadowProjection.z;
    const float3 lightDirectionRsm = -shadowTransform[2].xyz * depthRange;
    const float3 dx = shadowTransform[0].xyz * (2 * radius * radius);
    const float3 dy = shadowTransform[1].xyz * (-2 * radius * radius);
    const float3 dz = -lightDirectionRsm * depthRange;
    const float2 samplePosition = uv / texel;
    const int2 center = int2(samplePosition);
    const float2 subtexel = (frac(samplePosition) - 0.5) * texel;
    const float3 sampleShift = subtexel.x * dx + subtexel.y * dy;
    // Three rings; reuse the same pattern across frames and account for the area of each ring.
    static const int2 offsets[12] = {
        int2(4, 2), int2(-2, 4), int2(-4, -2), int2(2, -4),
        int2(7, -7), int2(7, 7), int2(-7, 7), int2(-7, -7),
        int2(17, 3), int2(-3, 17), int2(-17, -3), int2(3, -17)
    };
    static const float3 normals[6] = {
        float3(1, 0, 0), float3(-1, 0, 0), float3(0, 1, 0), float3(0, -1, 0),
        float3(0, 0, 1), float3(0, 0, -1)
    };
    float3 bounce = 0;
    [unroll] for (int i = 0; i < 12; ++i)
    {
        const int2 at = center + offsets[i];
        const float depth = reflectiveDepth.Load(int3(at, 0));
        if (depth >= 1)
            continue;
        const float2 lateral = (float2(at) + 0.5) * texel - uv;
        const float3 texelDelta = lateral.x * dx + lateral.y * dy + (depth - clip.z) * dz;
        if (dot(texelDelta, texelDelta) >= 8.25 * 8.25)
            continue;
        const float4 material = reflectanceMap.Load(int3(at, 0));
        const int face = clamp(int(round(material.a * 8)) - 1, 0, 5);
        const float3 emitter = normals[face];
        const float facing = dot(emitter, lightDirectionRsm);
        // Follow the emitter plane within its texel. A sloped but continuous surface then contributes
        // the same irradiance on either side of a texel edge.
        const float3 delta = texelDelta + sampleShift -
                             lightDirectionRsm * (dot(emitter, sampleShift) / max(facing, 0.18));
        const float d2 = dot(delta, delta);
        const float receiving = max(dot(normal, delta) - 0.09, 0);
        if (receiving > 0 && d2 < 49)
        {
            const float emitting = max(dot(emitter, -delta) - 0.09, 0);
            const float area = i < 4 ? 1.4 : i < 8 ? 6.0 : 19.0;
            const float weight = area * receiving * emitting / (d2 * max(d2, 1));
            const float distanceFade = 1 - smoothstep(25, 49, d2);
            bounce += material.rgb * weight * distanceFade * smoothstep(0, 0.18, facing);
        }
    }
    const float peak = max(bounce.r, max(bounce.g, bounce.b));
    const float sun = smoothstep(0.08, 0.33, lightDirectionRsm.z);
    return bounce * lightFrontColor.rgb * (0.75 * fade * sun / (1 + 2.8 * peak));
}

#endif
