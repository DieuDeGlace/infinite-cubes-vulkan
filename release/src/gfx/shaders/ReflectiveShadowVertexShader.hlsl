#include "WorldUniforms.hlsli"

struct ReflectanceVertex
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float4 reflectance : COLOR;
};

ReflectanceVertex main([[vk::location(0)]] float4 position : POSITION,
                       [[vk::location(1)]] float4 color : COLOR)
{
    ReflectanceVertex result;
    float4 world = mul(float4(position.xyz * 255, 1), worldMatrix);
    world.xyz += foliageOffset(position.w, world.xyz);
    result.position = float4(shadowClip(world), 1);
    // Terrain faces are axis-aligned. The fourth vertex byte already gives the normal.
    result.reflectance = float4(saturate(color.rgb * 2), (faceIndex(position.w) + 1) / 8.0);
    return result;
}
