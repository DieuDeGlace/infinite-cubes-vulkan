[[vk::binding(0, 0)]] cbuffer Constants {
    row_major float4x4 world : packoffset(c47);
    row_major float4x4 view : packoffset(c51);
    row_major float4x4 projection : packoffset(c55);
};
struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 uv : TEXCOORD0;
};
Output main([[vk::location(0)]] float3 position : POSITION,
            [[vk::location(1)]] float4 color : COLOR0, [[vk::location(2)]] float2 uv : TEXCOORD0)
{
    Output result;
    result.position = mul(mul(mul(float4(position, 1), world), view), projection);
    result.color = color.bgra;
    result.uv = uv;
    return result;
}
