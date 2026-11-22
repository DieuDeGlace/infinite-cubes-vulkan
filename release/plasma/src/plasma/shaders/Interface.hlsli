// Interface vertex-to-pixel colors and separate mask and object texture coordinates.
#ifndef INTERFACE_HLSLI
#define INTERFACE_HLSLI

struct PixelInput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float4 color : COLOR;
    [[vk::location(1)]] float2 maskCoord : TEXCOORD0;
    [[vk::location(2)]] float2 textureCoord : TEXCOORD1;
};

#endif
