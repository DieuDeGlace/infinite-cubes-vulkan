#include "Smaa.hlsli"

float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    const float2 uv = position.xy * metrics.xy;
    float4 offsets;
    SMAANeighborhoodBlendingVS(uv, offsets);
    return SMAANeighborhoodBlendingPS(uv, offsets, primary, secondary);
}
