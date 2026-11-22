#include "WorldUniforms.hlsli"

float4 main([[vk::location(0)]] float4 position : POSITION) : SV_POSITION
{
    float4 world = mul(float4(position.xyz * 255, 1), worldMatrix);
    // Cast the shadow of the leaves where the world pass draws them.
    world.xyz += foliageOffset(position.w, world.xyz);
    return float4(shadowClip(world), 1);
}
