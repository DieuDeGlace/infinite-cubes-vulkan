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

// Light lost per block of water along the view ray (red, green, blue).
static const float3 Absorption = float3(0.60, 0.45, 1.00);
// The colour deep water converges on in daylight.
static const float3 DeepWater = float3(0.00, 0.33, 0.76);

// waterSurface.xy hold the phases dot(PhaseDirection, world position) + speed * time, wrapped to 2 pi, as
// VulkanRenderer::beginWater computes them.
static const float2 PhaseDirectionX = float2(0.65, 0.31);
static const float2 PhaseDirectionY = float2(-0.27, 0.72);
// Each wave: whole multiples of the two phases, so it stays continuous where they wrap, and its height in blocks.
static const float3 Waves[5] = {float3(1, 0, 0.12), float3(0, 1, 0.11), float3(2, 1, 0.05), float3(1, -2, 0.05),
                                float3(3, 2, 0.025)};
// Distant waves are smaller than a pixel and would only shimmer.
static const float WaveNear = 40.0;
static const float WaveFar = 120.0;
// How tightly the sun's reflection is focused: higher gives smaller, sharper glints.
static const float GlintPower = 300.0;
static const float GlintStrength = 3.0;

float linearDepth(float depth)
{
    return projMatrix[3][2] / (depth - projMatrix[2][2]);
}

float3 waveNormal(float2 position, float distance)
{
    const float2 phase = float2(dot(position, PhaseDirectionX), dot(position, PhaseDirectionY)) + waterSurface.xy;
    float2 slope = 0;
    [unroll] for (int i = 0; i < 5; ++i)
    {
        const float2 frequency = Waves[i].x * PhaseDirectionX + Waves[i].y * PhaseDirectionY;
        slope += Waves[i].z * frequency * cos(dot(Waves[i].xy, phase));
    }
    return normalize(float3(-slope * (1 - smoothstep(WaveNear, WaveFar, distance)), 1));
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
        normal = waveNormal(worldPosition.xy, distance);
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
