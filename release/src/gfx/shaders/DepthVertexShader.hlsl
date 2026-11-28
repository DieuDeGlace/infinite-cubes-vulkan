#include "WorldPosition.hlsli"

#if CUBE_INSTANCED
// The instanced build (InstancedDepthVertexShader) takes each sprite's matrix from its instance record.
#define worldMatrix instanceWorld
#endif

// The world's depth-only draws: the position alone, rasterized exactly as WorldVertexShader rasterizes it.
[[vk::ext_decorate(18)]] // SPIR-V Invariant
float4 main([[vk::location(0)]] float4 position : POSITION
#if CUBE_INSTANCED
            , [[vk::location(3)]] float4 instanceRow0 : INSTANCE0,
            [[vk::location(4)]] float4 instanceRow1 : INSTANCE1,
            [[vk::location(5)]] float4 instanceRow2 : INSTANCE2,
            [[vk::location(6)]] float4 instanceRow3 : INSTANCE3
#endif
            ) : SV_POSITION
{
#if CUBE_INSTANCED
    const float4x4 instanceWorld = float4x4(instanceRow0, instanceRow1, instanceRow2, instanceRow3);
#endif
    float3 worldPosition, viewPosition;
    float4 clip;
    return projectWorldVertex(position, worldMatrix, worldPosition, viewPosition, clip);
}
