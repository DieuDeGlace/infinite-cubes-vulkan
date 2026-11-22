[[vk::binding(0, 0)]] cbuffer Constants {
    float4 pointLightPositions[10] : packoffset(c0);
    float3 pointLightColors[10] : packoffset(c10);
    float4x4 worldViewProjMatrix : packoffset(c20);
    float4x3 worldViewMatrix : packoffset(c24);
    float4x3 worldMatrix : packoffset(c27);
    float fogScale : packoffset(c30);
    float fogGradientTranslation : packoffset(c31);
    float4 materialColor : packoffset(c32);
    float3 lightDirection : packoffset(c33);
    float4 lightFrontColor : packoffset(c34);
    float4 lightBackColor : packoffset(c35);
    float3 cameraPosition : packoffset(c36);
    float4 darknessColor : packoffset(c37);
    float4 ambientColor : packoffset(c38);
    float white : packoffset(c39);
};
// CubeShader's vertex shader (m_vertexShader), used with every pixel shader. Input: CubeShader's
// vertex declaration, a UBYTE4 position whose fourth byte names the face (0..5: +x, -x, +y, -y,
// +z, -z) and a D3DCOLOR. It lights the vertex (the directional light from both sides, the ambient
// colour and up to ten point lights, darkened towards darknessColor), blends it to white by white,
// and passes the fog terms and directions the pixel shaders use. The registers are those
// CubeShader::initialize sets (pointLightPositions 0, pointLightColors 10, worldViewProjMatrix 20..
// white 39); the C++ uploads ten of each light array. A point light
// counts four times its incidence, between 0.3 and 1; the lighting is capped at 1.2; the
// view-space normal passed on is doubled.

struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float4 fog : TEXCOORD0;
    [[vk::location(2)]] float3 normal : TEXCOORD1;
    [[vk::location(3)]] float3 lightDirection : TEXCOORD2;
    [[vk::location(4)]] float3 cameraDirection : TEXCOORD3;
};

Output main([[vk::location(0)]] uint4 position : POSITION, [[vk::location(1)]] float4 color : COLOR0)
{
    color = color.bgra;
    Output output;
    float3 normal;
    if (position.w == 0)
        normal = float3(1, 0, 0);
    else if (position.w == 1)
        normal = float3(-1, 0, 0);
    else if (position.w == 2)
        normal = float3(0, 1, 0);
    else if (position.w == 3)
        normal = float3(0, -1, 0);
    else if (position.w == 4)
        normal = float3(0, 0, 1);
    else
        normal = float3(0, 0, -1);
    float4 vertex = float4(position.xyz, 1);
    output.position = mul(vertex, worldViewProjMatrix);
    output.fog.x = output.position.x / output.position.w;
    output.fog.y = output.position.y / output.position.w + fogGradientTranslation;
    float3 worldPosition = mul(vertex, worldMatrix).xyz;
    float3 cameraDirection = cameraPosition - worldPosition;
    output.cameraDirection = cameraDirection;
    output.fog.z = (fogScale * fogScale) * dot(cameraDirection, cameraDirection);
    output.fog.w = (fogScale * fogScale) * dot(cameraDirection.xy, cameraDirection.xy);
    float3 worldNormal = normalize(mul(normal, (float3x3)worldMatrix));
    float3 lighting = max(dot(worldNormal, lightDirection), 0) * lightFrontColor.rgb + ambientColor.rgb;
    lighting += max(dot(worldNormal, -lightDirection), 0) * lightBackColor.rgb;
    lighting *= color.a * materialColor.a;
    for (int light = 0; light < 10; ++light) {
        if (pointLightPositions[light].w > 0) {
            float3 distance = worldPosition - pointLightPositions[light].xyz;
            float attenuation = 1 - min(dot(distance, distance) /
                (pointLightPositions[light].w * pointLightPositions[light].w), 1);
            float incidence = min(max(-4 * dot(worldNormal, normalize(distance)), 0.3), 1);
            lighting += (attenuation * attenuation) * incidence * pointLightColors[light];
        }
    }
    lighting = min(max(lighting, 0), 1.2);
    float3 litColor = darknessColor.rgb * (1 - lighting) + lighting * color.rgb * materialColor.rgb;
    output.color.rgb = lerp(litColor, 1, white);
    output.color.a = color.a;
    output.normal = 2 * normalize(mul(normal, (float3x3)worldViewMatrix));
    output.lightDirection = lightDirection;
    return output;
}
