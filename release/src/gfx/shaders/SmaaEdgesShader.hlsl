#include "Smaa.hlsli"

float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    const float2 uv = position.xy * metrics.xy;
    float4 offsets[3];
    SMAAEdgeDetectionVS(uv, offsets);
    return float4(SMAALumaEdgeDetectionPS(uv, offsets, primary), 0, 0);
}
