// Averages 4 x 4 texels of a render surface (m_downsample4PixelShader).
#include "Downsample.hlsli"

float4 main([[vk::location(0)]] float4 color : COLOR0, [[vk::location(1)]] float2 texCoord : TEXCOORD0) : SV_Target
{
    float2 t0 = firstTexel(texCoord, 4);
    float2 t1 = t0 + textureScale;
    float2 t2 = t0 + 2 * textureScale;
    float2 t3 = t0 + 3 * textureScale;
    float4 sum = textureSampler.Sample(textureSamplerState, float2(t0.x, t0.y)) + textureSampler.Sample(textureSamplerState, float2(t1.x, t0.y)) +
                 textureSampler.Sample(textureSamplerState, float2(t2.x, t0.y)) + textureSampler.Sample(textureSamplerState, float2(t3.x, t0.y)) +
                 textureSampler.Sample(textureSamplerState, float2(t0.x, t1.y)) + textureSampler.Sample(textureSamplerState, float2(t1.x, t1.y)) +
                 textureSampler.Sample(textureSamplerState, float2(t2.x, t1.y)) + textureSampler.Sample(textureSamplerState, float2(t3.x, t1.y)) +
                 textureSampler.Sample(textureSamplerState, float2(t0.x, t2.y)) + textureSampler.Sample(textureSamplerState, float2(t1.x, t2.y)) +
                 textureSampler.Sample(textureSamplerState, float2(t2.x, t2.y)) + textureSampler.Sample(textureSamplerState, float2(t3.x, t2.y)) +
                 textureSampler.Sample(textureSamplerState, float2(t0.x, t3.y)) + textureSampler.Sample(textureSamplerState, float2(t1.x, t3.y)) +
                 textureSampler.Sample(textureSamplerState, float2(t2.x, t3.y)) + textureSampler.Sample(textureSamplerState, float2(t3.x, t3.y));
    return unpremultiplied(1 / 16.0 * sum) * color;
}
