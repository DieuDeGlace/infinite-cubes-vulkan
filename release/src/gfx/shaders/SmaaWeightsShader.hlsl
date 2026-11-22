#include "Smaa.hlsli"

[[vk::binding(3, 1)]] Texture2D<float4> searchTexture;

float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    const float2 uv = position.xy * metrics.xy;
    float2 pixel;
    float4 offsets[3];
    SMAABlendingWeightCalculationVS(uv, pixel, offsets);
    return SMAABlendingWeightCalculationPS(uv, pixel, offsets, primary, secondary, searchTexture, float4(0, 0, 0, 0));
}
