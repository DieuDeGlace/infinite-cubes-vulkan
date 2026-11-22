#include "World.hlsli"
#include "WorldUniforms.hlsli"
#include "Sky.hlsli"

float4 main(PixelInput input) : SV_TARGET
{
    float3 sky = saturate(skyColorAt(input.clipPosition.xy / input.clipPosition.w));
    return float4(gradeColor(sky), 1);
}
