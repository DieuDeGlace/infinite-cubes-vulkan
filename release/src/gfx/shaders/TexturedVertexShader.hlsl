#include "WorldUniforms.hlsli"

#include "Textured.hlsli"

PixelInput main([[vk::location(0)]] float4 position : POSITION,
               [[vk::location(1)]] float4 color : COLOR,
               [[vk::location(2)]] float2 texcoord : TEXCOORD)
{
    PixelInput output;
    float4x4 worldViewProjection = mul(mul(worldMatrix, viewMatrix), projMatrix);
    output.position = mul(position, worldViewProjection);
    output.position.xy += temporalJitter.xy * output.position.w;
    output.color = color;
    output.texcoord = texcoord;
    return output;
}
