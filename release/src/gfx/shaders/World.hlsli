// World vertex-to-pixel interface, including the projected and view-space positions.
#ifndef WORLD_HLSLI
#define WORLD_HLSLI

struct PixelInput
{
    float4 position : SV_POSITION;
    [[vk::location(0)]] float4 color : COLOR;
    [[vk::location(1)]] float4 clipPosition : TEXCOORD0;
    [[vk::location(2)]] float3 normal : TEXCOORD1;
    [[vk::location(3)]] float3 lightDirection : TEXCOORD2;
    [[vk::location(4)]] float3 viewPosition : TEXCOORD3;
    [[vk::location(5)]] float3 worldPosition : TEXCOORD4;
    [[vk::location(6)]] float3 indirectColor : TEXCOORD5;
    [[vk::location(7)]] nointerpolation float3 worldNormal : TEXCOORD6;
    [[vk::location(8)]] float3 albedo : TEXCOORD7;
    // The draw's alpha (each instance's own in an instanced draw).
    [[vk::location(9)]] nointerpolation float alpha : TEXCOORD8;
};

#endif
