#include "PostUniforms.hlsli"

float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    return primary.Load(int3(int2(position.xy), 0));
}
