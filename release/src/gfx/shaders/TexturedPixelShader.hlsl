#include "WorldUniforms.hlsli"
#include "Textured.hlsli"

Texture2D tex : register(t0, space1);
SamplerState TextureSampler : register(s2, space1);

float4 main(PixelInput input) : SV_TARGET
{
    if (textureEnabled)
        return tex.Sample(TextureSampler, input.texcoord) * input.color;
    return input.color;
}
