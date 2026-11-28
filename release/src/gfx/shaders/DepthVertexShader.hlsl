#include "WorldPosition.hlsli"

[[vk::ext_decorate(18)]] // SPIR-V Invariant
float4 main([[vk::location(0)]] float4 position : POSITION) : SV_POSITION
{
    float3 world, view;
    float4 clip;
    return projectWorldVertex(position, worldMatrix, world, view, clip);
}
