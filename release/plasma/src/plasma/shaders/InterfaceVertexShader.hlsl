#include "InterfaceUniforms.hlsli"
#include "Interface.hlsli"

PixelInput main([[vk::location(0)]] float4 position : POSITION,
               [[vk::location(1)]] float4 color : COLOR)
{
    PixelInput output;
    float4 vertex = float4(position.xy, 0, 1);
    if (widgetDeformationEnabled)
    {
        float4 bindPosition = mul(vertex, widgetBindMatrix);
        float2 amount;
        amount.x = bindPosition.x < widgetBindPos.x ? 0 :
            (bindPosition.x < widgetBindPos.x + widgetBindSize.x ?
            (bindPosition.x - widgetBindPos.x) / widgetBindSize.x : 1);
        amount.y = bindPosition.y < widgetBindPos.y ? 0 :
            (bindPosition.y < widgetBindPos.y + widgetBindSize.y ?
            (bindPosition.y - widgetBindPos.y) / widgetBindSize.y : 1);
        bindPosition.xy = bindPosition.xy + deformedWidgetPos - widgetBindPos +
            amount * (deformedWidgetSize - widgetBindSize);
        vertex = mul(bindPosition, inverseWidgetBindMatrix);
    }
    float4 worldPosition = mul(vertex, worldMatrix);
    // Interface vertex pixel alignment offset, applied equally to screen x and y.
    worldPosition.xy -= 0.135;
    output.position = mul(worldPosition, projMatrix);
    output.color = color * baseColor;
    output.maskCoord = mul(vertex, maskMatrix).xy;
    output.textureCoord = mul(float4(position.zw, 0, 1), textureMatrix).xy;
    return output;
}
