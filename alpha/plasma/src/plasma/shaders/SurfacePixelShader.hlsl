// Draws a render-surface texel, undoing premultiplied alpha before multiplying by vertex color.
#include "Surface.hlsli"

float4 main([[vk::location(0)]] float4 color : COLOR0, [[vk::location(1)]] float2 texCoord : TEXCOORD0) : SV_Target
{
    return unpremultiplied(textureSampler.Sample(textureSamplerState, texCoord)) * color;
}
