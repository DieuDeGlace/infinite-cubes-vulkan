#include "WorldUniforms.hlsli"

#if CUBE_INSTANCED
// The instanced build (InstancedShadowVertexShader) takes each sprite's matrix from its instance record.
#define worldMatrix instanceWorld
#endif

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
    float4 world = mul(float4(position.xyz * 255, 1), worldMatrix);
    // Cast the shadow of the leaves where the world pass draws them.
    world.xyz += foliageOffset(position.w, world.xyz);
    return float4(shadowClip(world), 1);
}
