#include "WorldUniforms.hlsli"
#include "World.hlsli"

#if CUBE_INSTANCED
// The instanced build (InstancedWorldVertexShader) takes each sprite's matrix, colour and alpha from its instance
// record.
#define worldMatrix instanceWorld
#define materialColor instanceMaterial
#endif

PixelInput main([[vk::location(0)]] float4 position : POSITION,
               [[vk::location(1)]] float4 color : COLOR
#if CUBE_INSTANCED
               , [[vk::location(3)]] float4 instanceRow0 : INSTANCE0,
               [[vk::location(4)]] float4 instanceRow1 : INSTANCE1,
               [[vk::location(5)]] float4 instanceRow2 : INSTANCE2,
               [[vk::location(6)]] float4 instanceRow3 : INSTANCE3,
               [[vk::location(7)]] float4 instanceMaterial : INSTANCE4,
               [[vk::location(8)]] float instanceAlpha : INSTANCE5
#endif
               )
{
    PixelInput output;
#if CUBE_INSTANCED
    const float4x4 instanceWorld = float4x4(instanceRow0, instanceRow1, instanceRow2, instanceRow3);
    output.alpha = instanceAlpha;
#else
    output.alpha = alpha;
#endif
    // The fourth normalized byte identifies an axis-aligned voxel face.
    const uint face = faceIndex(position.w);
    float3 normal = face == 0 ? float3(1, 0, 0) :
        face == 1 ? float3(-1, 0, 0) :
        face == 2 ? float3(0, 1, 0) :
        face == 3 ? float3(0, -1, 0) :
        face == 4 ? float3(0, 0, 1) : float3(0, 0, -1);
    float4x4 worldView = mul(worldMatrix, viewMatrix);
    float4x4 worldViewProjection = mul(worldView, projMatrix);
    worldViewProjection[3][2] -= depthOffset;
    output.normal = normalize(mul(normal, (float3x3)worldView));
    const float3 x = worldMatrix[0].xyz;
    const float3 y = worldMatrix[1].xyz;
    const float3 z = worldMatrix[2].xyz;
    const float3 xNormal = cross(y, z);
    const float3 cofactor = normal.x * xNormal + normal.y * cross(z, x) + normal.z * cross(x, y);
    const float handedness = dot(x, xNormal) < 0 ? -1.0 : 1.0;
    const float3 worldNormal = dot(cofactor, cofactor) > 1e-20
                                  ? normalize(cofactor * handedness)
                                  : normalize(mul(normal, (float3x3)worldMatrix));
    output.worldNormal = worldNormal;
    const float incidence = dot(worldNormal, lightDirection.xyz);
    const float exposure = lerp(1, smoothstep(0.06, 0.22, incidence), worldEffects.z);
    const float3 warm = lerp(float3(1, 1, 1), float3(1.03, 1.00, 0.97), worldEffects.z);
    // Front light response: incidence slope 0.7, floor 0.3.
    float frontLight = max(0.7 * incidence + 0.3, 0) * exposure;
    // Back light direction in model coordinates; artistic lighting vector.
    float backLight = 0.5 * abs(dot(worldNormal, normalize(float3(0.6, 0.4, 0.7)))) + 0.5;
    float3 directionalLight = saturate(frontLight * lightFrontColor.rgb * warm + backLight * lightBackColor.rgb);
    float4 vertex = float4(position.xyz * 255, 1);
    // Terrain draws translate only, so the offset applies unchanged before the world matrix.
    vertex.xyz += foliageOffset(position.w, mul(vertex, worldMatrix).xyz);
    output.position = mul(vertex, worldViewProjection);
    output.clipPosition = output.position;
    output.position.xy += temporalJitter.xy * output.position.w;
    float3 worldPosition = mul(vertex, worldMatrix).xyz;
    output.worldPosition = worldPosition;
    output.viewPosition = mul(vertex, worldView).xyz;
    float3 lighting = directionalLight * (color.a * materialColor.a) + backLight * ambientColor.rgb;
    float3 pointLighting = 0;
    for (int light = 0; light < 10; ++light)
    {
        if (pointLightPositions[light].w > 0)
        {
            float3 distance = worldPosition - pointLightPositions[light].xyz;
            float attenuation = 1 - min(dot(distance, distance) /
                (pointLightPositions[light].w * pointLightPositions[light].w), 1);
            // Point light response: incidence gain -4, floor 0.3.
            float incidence = min(max(-4 * dot(worldNormal, distance * rsqrt(max(dot(distance, distance), 1e-8))), 0.3), 1);
            pointLighting += incidence * (attenuation * attenuation) * pointLightColors[light].rgb;
        }
    }
    lighting = saturate(lighting + pointLighting);
    // Vertex colour intensity and saturation curve: double intensity, half-strength saturation gain
    // capped at two.
    float3 vertexColor = 2 * color.rgb;
    float saturation = min(max(1 + (ambientColor.a - lighting.g) * 0.5, 1), 2);
    float luminance = dot(float3(0.299, 0.587, 0.114), vertexColor);
    float3 surfaceColor = min(max(saturation * vertexColor + luminance * (1 - saturation), 0), 2);
    if (glow < 0)
        surfaceColor = luminance;
    float3 indirect = saturate((backLight * lightBackColor.rgb) * (color.a * materialColor.a) +
                               backLight * ambientColor.rgb + pointLighting);
    if (worldEffects.x > 0.5)
    {
        float baked = color.a * materialColor.a * worldEffects.y;
        float3 fill = ambientColor.rgb + max(-incidence, 0) * lightBackColor.rgb;
        float3 sun = max(incidence, 0) * exposure * lightFrontColor.rgb * warm;
        indirect = clamp(fill * baked + pointLighting, 0, 1.2);
        lighting = clamp((sun + fill) * baked + pointLighting, 0, 1.2);
        surfaceColor = vertexColor;
    }
    float3 litColor = surfaceColor * (lighting * materialColor.rgb);
    float3 indirectColor = surfaceColor * (indirect * materialColor.rgb);
    if (glow > 0) {
        litColor = lerp(litColor, vertexColor * materialColor.rgb, glow);
        indirectColor = lerp(indirectColor, vertexColor * materialColor.rgb, glow);
    }
    output.color.rgb = saturate(lerp(litColor, 1, white));
    output.indirectColor = saturate(lerp(indirectColor, 1, white));
    output.albedo = saturate(vertexColor * materialColor.rgb);
    output.color.a = color.a;
    output.lightDirection = mul(lightDirection.xyz, (float3x3)worldView);
    return output;
}
