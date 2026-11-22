[[vk::binding(0, 0)]] cbuffer Constants {
    float2 textureScale : packoffset(c40);
};
// Downsampling shaders average a factor-by-factor texel block, for factors two, three and four.
// Coordinates are normalized and textureScale is the size of one source texel.
#ifndef DOWNSAMPLE_HLSLI
#define DOWNSAMPLE_HLSLI

#include "Surface.hlsli"

// The texture coordinate of the centre of the first texel of the block the output pixel covers.
float2 firstTexel(float2 texCoord, float factor)
{
    return (factor * floor(texCoord / textureScale) + 0.5) * textureScale;
}

#endif
