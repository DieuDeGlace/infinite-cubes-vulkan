#ifndef INTERFACE_UNIFORMS_HLSLI
#define INTERFACE_UNIFORMS_HLSLI

// Matches plasma::VulkanEngine::Uniforms.
cbuffer InterfaceUniforms : register(b0, space0)
{
    float4x4 projMatrix;
    float4x4 worldMatrix;
    float4x4 maskMatrix;
    float4x4 textureMatrix;
    float4x4 widgetBindMatrix;
    float4x4 inverseWidgetBindMatrix;
    float2 widgetBindPos;
    float2 widgetBindSize;
    float2 deformedWidgetPos;
    float2 deformedWidgetSize;
    float4 baseColor;
    float textureOpacity;
    float textureBrightness;
    float textureContrast;
    float textureSaturation;
    uint widgetDeformationEnabled;
    uint textureEnabled;
    int filter;
};

#endif
