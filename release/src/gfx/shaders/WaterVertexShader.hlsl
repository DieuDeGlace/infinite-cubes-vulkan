#include "WorldUniforms.hlsli"

float4 main([[vk::location(0)]] float4 position : POSITION,
            [[vk::location(0)]] out float3 worldPosition : TEXCOORD0) : SV_POSITION
{
    const float4 world = mul(float4(position.xyz * 255, 1), worldMatrix);
    worldPosition = world.xyz;
    float4 clip = mul(mul(world, viewMatrix), projMatrix);
    clip.xy += temporalJitter.xy * clip.w;
    return clip;
}
