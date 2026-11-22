[[vk::binding(0, 1)]] Texture2D<float4> image;
[[vk::binding(6, 1)]] SamplerState imageSampler;
float4 main([[vk::location(0)]] float4 color : COLOR0, [[vk::location(1)]] float2 uv : TEXCOORD0) : SV_Target
{
    return color * image.Sample(imageSampler, uv);
}
