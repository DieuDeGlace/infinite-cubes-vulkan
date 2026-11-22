// Omega-style water optics adapted from CWVulkan Steam. The colours and absorption were measured from Cube World
// Omega footage: the lakebed stays visible, every channel but blue fades with depth, and the deep water settles on a
// bright saturated blue. Travelling waves tilt the surface normal, and the sun glints off their slopes.
#include "WorldUniforms.hlsli"
#include "Sky.hlsli"
#include "SunShadow.hlsli"

[[vk::binding(0, 1)]] Texture2D<float4> opaqueColor;
[[vk::binding(4, 1)]] Texture2D<float> opaqueDepth;
[[vk::binding(2, 1)]] SamplerState colorSampler;
[[vk::binding(5, 1)]] SamplerState depthSampler;

#include "WaterOptics.hlsli"

float linearDepth(float depth)
{
    return projMatrix[3][2] / (depth - projMatrix[2][2]);
}

float4 main(float4 position : SV_POSITION, [[vk::location(0)]] float3 worldPosition : TEXCOORD0) : SV_TARGET
{
    const float3 toCamera = sceneCamera.xyz - worldPosition;
    const float distance = length(toCamera);
    float3 normal = normalize(cross(ddx(worldPosition), ddy(worldPosition)));
    if (normal.z < 0)
        normal = -normal;
    const bool surface = normal.z > 0.5;
    if (surface)
        normal = waveNormal(worldPosition.xy, distance, waterSurface.xy);
    if (dot(normal, toCamera) < 0)
        normal = -normal;

    const float3 fog = gradeColor(fogColor.rgb);
    const float haze = smoothstep(0.6, 1, distance * fogScale);
    if (haze >= 1)
        return float4(fog, 1);

    uint width, height;
    opaqueDepth.GetDimensions(width, height);
    const float2 pixel = 1.0 / float2(width, height);
    const float2 uv = position.xy * pixel;
    float2 refracted = clamp(uv + normal.xy * 0.006, 0.5 * pixel, 1 - 0.5 * pixel);
    const float surfaceDepth = linearDepth(position.z);
    float sceneDepth = linearDepth(opaqueDepth.SampleLevel(depthSampler, refracted, 0));
    // A refracted ray that reaches foreground geometry would pull in color from above the water.
    if (sceneDepth < surfaceDepth)
    {
        refracted = uv;
        sceneDepth = linearDepth(opaqueDepth.SampleLevel(depthSampler, uv, 0));
    }
    const float thickness = clamp(sceneDepth - surfaceDepth, 0.3, 100);
    const float3 transmission = exp(-Absorption * thickness);
    const float brightness = waterSurface.z;
    const bool submerged = waterSurface.w > 0.5;
    const float3 body = submerged ? fog : DeepWater * brightness;
    float3 color = opaqueColor.SampleLevel(colorSampler, refracted, 0).rgb * transmission + body * (1 - transmission);

    if (surface && !submerged)
    {
        // lightDirection points towards the sun; the glints fade as it sets.
        const float sunUp = saturate(lightDirection.z * 5);
        const float reflected = saturate(dot(reflect(-toCamera / distance, normal), lightDirection.xyz));
        const float glint = saturate(pow(reflected, GlintPower) * GlintStrength) * sunUp * brightness;
        if (glint > 0.001)
            color += glint * sunVisibility(worldPosition, float3(0, 0, 1));
    }

    // The background is already included in the refraction. Write opaque color and the nearest surface depth.
    return float4(saturate(lerp(color, fog, haze)), 1);
}
