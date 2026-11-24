#include "World.hlsli"
#include "WorldAtmosphere.hlsli"
#include "SunShadow.hlsli"
#include "IndirectSunlight.hlsli"

float4 main(PixelInput input) : SV_TARGET
{
    float2 projectedPosition = input.clipPosition.xy / input.clipPosition.w;
    float3 sky = skyColorAt(projectedPosition);
    float3 color = lerp(input.indirectColor, input.color.rgb, sunVisibility(input.worldPosition, input.worldNormal));
    if (shadowProjection.w > 0 && glow == 0 && input.alpha >= 0.99)
    {
        // Use the voxel light already baked into the vertices to keep bounce out of closed corners.
        const float3 bounce = indirectSunlight(input.worldPosition, input.worldNormal);
        color += (1 - 0.5 * color) * input.albedo * input.color.a * bounce;
    }
    if (worldEffects.x > 0.5)
    {
        const float3 distanceToCamera = sceneCamera.xyz - input.worldPosition;
        const float distanceSquared = dot(distanceToCamera, distanceToCamera) * fogScale * fogScale;
        const float horizontalSquared = dot(distanceToCamera.xy, distanceToCamera.xy) * fogScale * fogScale;
        float haze = saturate(1.5 * distanceSquared - 0.025);
        haze = 0.75 * (1 - (1 - haze) * (1 - haze));
        float fade = smoothCurve(saturate((horizontalSquared - 0.6) * 2.5));
        color = lerp(color, fogColor.rgb, haze);
        color = lerp(color, sky, fade * fade);
        if (shininess > 0)
            color *= 1 + 0.3 * sin(10 * (projectedPosition.y + fogGradientTranslation) +
                                    5 * projectedPosition.x + 2 * (input.normal.y + input.normal.x));
        return float4(saturate(color), input.alpha);
    }
    if (shininess > 0)
        // World shininess shimmer curve: amplitude 0.3, spatial frequencies 10/5/2.
        color *= 1 + 0.3 * cos(10 * (projectedPosition.y + fogGradientTranslation) +
            5 * projectedPosition.x +
            2 * (input.normal.y + input.normal.x));
    float luminance = dot(float3(0.299, 0.587, 0.114), color);
    // World colour curve: quarter-strength luminance-dependent saturation boost.
    float saturation = 1 + 0.25 * (1 - luminance);
    color = saturation * color + luminance * (1 - saturation);
    // World sky fog curve: fourth power of the fog amount.
    float fog = fogAmount(input.viewPosition);
    color = lerp(color, fogColor.rgb, fog);
    color = saturate(lerp(color, sky, fog * fog * fog * fog));
    return float4(gradeColor(color), input.alpha);
}
