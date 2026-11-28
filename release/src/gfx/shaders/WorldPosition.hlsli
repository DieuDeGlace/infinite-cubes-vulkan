#include "WorldUniforms.hlsli"

// Both passes must rasterize the same depth, including leaf motion, the draw's depth offset and temporal jitter.
float4 projectWorldVertex(float4 position, float4x4 modelMatrix, out float3 worldPosition, out float3 viewPosition,
                          out float4 clip)
{
    const float4x4 worldView = mul(modelMatrix, viewMatrix);
    float4x4 worldViewProjection = mul(worldView, projMatrix);
    worldViewProjection[3][2] -= depthOffset;
    float4 vertex = float4(position.xyz * 255, 1);
    // Terrain draws translate only, so the offset applies unchanged before the world matrix.
    vertex.xyz += foliageOffset(position.w, mul(vertex, modelMatrix).xyz);
    clip = mul(vertex, worldViewProjection);
    worldPosition = mul(vertex, modelMatrix).xyz;
    viewPosition = mul(vertex, worldView).xyz;
    float4 rasterPosition = clip;
    rasterPosition.xy += temporalJitter.xy * clip.w;
    return rasterPosition;
}
