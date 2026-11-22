#ifndef WORLD_UNIFORMS_HLSLI
#define WORLD_UNIFORMS_HLSLI

// Matches gfx::VulkanRenderer::Uniforms.
cbuffer WorldUniforms : register(b0, space0)
{
    float4x4 projMatrix;
    float4x4 viewMatrix;
    float4x4 worldMatrix;
    float4 materialColor;
    float4 pointLightPositions[10];
    float4 pointLightColors[10];
    float4 lightFrontColor;
    float4 lightBackColor;
    float4 lightDirection;
    float4 ambientColor;
    float4 skyColor1;
    float4 skyColor2;
    float4 fogColor;
    float4 sunColor;
    float4 sunPos;
    float white;
    float depthOffset;
    float glow;
    uint textureEnabled;
    float fogScale;
    float fogGradientTranslation;
    float alpha;
    float shininess;
    float4 worldEffects;
    float4 sceneCamera;
    float4 shadowTransform[3];
    float4 temporalJitter;
    float4 shadowProjection;
    float4 waterSurface;
    float4 foliageWind;
};

// The fourth position byte: the face index 0..5, plus 8 on the faces of leaf blocks (gfx::Vertex::FoliageFace).
uint faceIndex(float packed)
{
    return uint(round(packed * 255)) & 7;
}

// Moves a vertex of a leaf face with the wind: a slow sway and a faster flutter whose phases follow the world position,
// so the corners that leaf faces share move together.
float3 foliageOffset(float packed, float3 worldPosition)
{
    if (foliageWind.z == 0 || (uint(round(packed * 255)) & 8) == 0)
        return 0;
    const float sway = foliageWind.x + dot(worldPosition, float3(0.21, 0.17, 0.05));
    const float flutter = foliageWind.y + dot(worldPosition, float3(-0.53, 0.61, 0.37));
    const float2 bend = float2(sin(sway), 0.6 * cos(0.8 * sway + 1.3)) +
                        0.35 * float2(sin(flutter), cos(1.1 * flutter));
    return foliageWind.z * float3(bend, 0.25 * sin(flutter + 0.5));
}

float3 shadowClip(float4 worldPosition)
{
    return float3(dot(shadowTransform[0], worldPosition), dot(shadowTransform[1], worldPosition),
                  dot(shadowTransform[2], worldPosition));
}

#endif
