#include "WorldUniforms.hlsli"

// Both passes must rasterize the same depth, including leaf motion, the draw's depth offset and temporal jitter.
float4 projectWorldVertex(float4 position, float4x4 modelMatrix, out float3 worldPosition, out float3 viewPosition,
                          out float4 clip)
{
    float4 vertex = float4(position.xyz * 255, 1);
    // Terrain draws translate only, so the offset applies unchanged before the world matrix.
    vertex.xyz += foliageOffset(position.w, mul(vertex, modelMatrix).xyz);
    // The vertex in the world, the view and the clip space in turn; the depth offset moves the clip depth.
    const float4 world = mul(vertex, modelMatrix);
    const float4 view = mul(world, viewMatrix);
    clip = mul(view, projMatrix);
    clip.z -= depthOffset;
    worldPosition = world.xyz;
    viewPosition = view.xyz;
    float4 rasterPosition = clip;
    rasterPosition.xy += temporalJitter.xy * clip.w;
    return rasterPosition;
}
