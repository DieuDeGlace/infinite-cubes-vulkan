#ifndef CUBE_POST_UNIFORMS
#define CUBE_POST_UNIFORMS

cbuffer PostUniforms : register(b0, space0)
{
    float4x4 clipToPrevious;
    float4 metrics; // inverse width, inverse height, width, height
    float4 jitter;  // current and previous jitter in screen pixels
    float4 tuning;  // history, edge history, blur, valid
};

[[vk::binding(0, 1)]] Texture2D<float4> primary;
[[vk::binding(1, 1)]] Texture2D<float4> secondary;
[[vk::binding(2, 1)]] SamplerState LinearSampler;
[[vk::binding(5, 1)]] SamplerState PointSampler;

#endif
