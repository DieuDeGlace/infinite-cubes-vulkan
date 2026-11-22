[[vk::binding(0, 0)]] cbuffer Constants {
    float blurScale : packoffset(c40);
};
// The vertical pass of the render-surface blur (m_verticalBlurPixelShader): nine taps blurScale
// apart (a quarter of the blur radius over the surface width, set by the C++), weighted as in the
// 2012 engine's GLSL blur.
#include "Surface.hlsli"

float4 main([[vk::location(1)]] float2 texCoord : TEXCOORD0) : SV_Target
{
    float4 sum = float4(0, 0, 0, 0);
    sum += textureSampler.Sample(textureSamplerState, float2(texCoord.x, texCoord.y - 4.0 * blurScale)) * 0.05;
    sum += textureSampler.Sample(textureSamplerState, float2(texCoord.x, texCoord.y - 3.0 * blurScale)) * 0.09;
    sum += textureSampler.Sample(textureSamplerState, float2(texCoord.x, texCoord.y - 2.0 * blurScale)) * 0.12;
    sum += textureSampler.Sample(textureSamplerState, float2(texCoord.x, texCoord.y - blurScale)) * 0.15;
    sum += textureSampler.Sample(textureSamplerState, float2(texCoord.x, texCoord.y)) * 0.18;
    sum += textureSampler.Sample(textureSamplerState, float2(texCoord.x, texCoord.y + blurScale)) * 0.15;
    sum += textureSampler.Sample(textureSamplerState, float2(texCoord.x, texCoord.y + 2.0 * blurScale)) * 0.12;
    sum += textureSampler.Sample(textureSamplerState, float2(texCoord.x, texCoord.y + 3.0 * blurScale)) * 0.09;
    sum += textureSampler.Sample(textureSamplerState, float2(texCoord.x, texCoord.y + 4.0 * blurScale)) * 0.05;
    return sum;
}
