#ifndef SUN_SHADOW_HLSLI
#define SUN_SHADOW_HLSLI

#include "WorldUniforms.hlsli"
[[vk::binding(3, 1)]] Texture2D<float> sunDepth;

float sunVisibility(float3 worldPosition, float3 worldNormal)
{
    if (worldEffects.z <= 0)
        return 1;
    const float facing = -shadowProjection.z * dot(worldNormal, shadowTransform[2].xyz);
    if (facing <= 0.06)
        return 1;

    const float radius = shadowProjection.y;
    const float cameraFade = 1 - smoothstep(radius * 0.4, radius, length(worldPosition - sceneCamera.xyz));
    if (cameraFade <= 0)
        return 1;
    const float3 clip = shadowClip(float4(worldPosition, 1));
    const float2 uv = clip.xy * float2(0.5, -0.5) + 0.5;
    const float texel = shadowProjection.x;
    const float border = min(min(uv.x, 1 - uv.x), min(uv.y, 1 - uv.y));
    if (clip.z <= 0 || clip.z >= 1 || border <= 2 * texel)
        return 1;

    // Project the receiver plane into the light map. Each comparison uses the
    // depth at that texel's centre, even when the receiver is a sloped model face.
    const float scale = 2 * radius * radius / (shadowProjection.z * facing);
    const float2 depthGradient = scale * float2(dot(worldNormal, shadowTransform[0].xyz),
                                               -dot(worldNormal, shadowTransform[1].xyz));
    const float bias = worldEffects.w * (1 + (1 - saturate(facing)));
    const float inverseDepthFalloff = shadowProjection.z / 0.06;
    const float2 position = uv / texel - 0.5;
    const int2 base = int2(floor(position));
    const float2 fraction = frac(position);
#ifdef CUBE_MOBILE_SHADOWS
    const float2 wx = float2(1 - fraction.x, fraction.x);
    const float2 wy = float2(1 - fraction.y, fraction.y);
    const int filterSize = 2;
    const int filterInset = 0;
    const float filterWeight = 1;
#else
    const float4 wx = float4(1 - fraction.x, 2 - fraction.x, 1 + fraction.x, fraction.x);
    const float4 wy = float4(1 - fraction.y, 2 - fraction.y, 1 + fraction.y, fraction.y);
    const int filterSize = 4;
    const int filterInset = 1;
    const float filterWeight = 16;
#endif
    float visibility = 0;
    [unroll] for (int x = 0; x < filterSize; ++x)
        [unroll] for (int y = 0; y < filterSize; ++y)
        {
            const int2 offset = int2(x - filterInset, y - filterInset);
            const float depth = sunDepth.Load(int3(base + offset, 0));
            const float receiver = clip.z + dot(depthGradient, (float2(offset) - fraction) * texel);
            // Smooth the comparison over a small world-space depth band to suppress acne on voxel faces.
            const float lit = saturate(0.5 + (depth - receiver + bias) * inverseDepthFalloff);
            visibility += lit * wx[x] * wy[y];
        }
    const float edge = smoothstep(2 * texel, 0.08, border);
    const float angleFade = smoothstep(0.06, 0.18, facing);
    return lerp(1, visibility / filterWeight, edge * cameraFade * angleFade * worldEffects.z);
}

#endif
