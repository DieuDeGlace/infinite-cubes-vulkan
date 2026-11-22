// Textured quad vertex-to-pixel interface.
#ifndef TEXTURED_HLSLI
#define TEXTURED_HLSLI

struct PixelInput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float4 color : COLOR;
    [[vk::location(1)]] float2 texcoord : TEXCOORD;
};

#endif
