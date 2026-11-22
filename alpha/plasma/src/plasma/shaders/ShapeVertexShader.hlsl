[[vk::binding(0, 0)]] cbuffer Constants {
    float4x4 Proj : packoffset(c0);
    float4x4 WorldView : packoffset(c4);
    float4x4 widgetBindMatrix : packoffset(c8);
    float4x4 inverseWidgetBindMatrix : packoffset(c12);
    float4x2 MaskMatrix : packoffset(c16);
    float4x2 TextureMatrix : packoffset(c18);
    float4x2 NormalMatrix : packoffset(c20);
    float2 widgetBindPos : packoffset(c22);
    float2 widgetBindSize : packoffset(c23);
    float2 deformedWidgetPos : packoffset(c24);
    float2 deformedWidgetSize : packoffset(c25);
    float aaOffset : packoffset(c26);
    float4 baseColor : packoffset(c27);
    bool widgetDeformationEnabled : packoffset(c45);
};
// Shape and widget vertex shader. Input provides position, color, edge normals and texture
// coordinates. It deforms widgets inside their rectangles, expands antialiased edges along the
// normal bisector, and passes mask and texture coordinates to the pixel shader. Projection,
// model-view and normal matrices are supplied as shader constants. The antialias offset uses
// A bisector projection of at least 0.7 and a half-unit depth shift.

struct Output {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
    [[vk::location(1)]] float2 maskCoord : TEXCOORD0;
    [[vk::location(2)]] float2 texCoord : TEXCOORD1;
};

// A widget bound to the rectangle widgetBindPos/Size is stretched to deformedWidgetPos/Size:
// points inside the rectangle move proportionally, points outside keep their distance to its
// edges.
float4 deformedPosition(float4 v)
{
    if (widgetDeformationEnabled) {
        float4 p = mul(v, widgetBindMatrix);
        float2 w = float2(0, 0);
        if (p.x < widgetBindPos.x)
            w.x = 0;
        else if (p.x < widgetBindPos.x + widgetBindSize.x)
            w.x = (p.x - widgetBindPos.x) / widgetBindSize.x;
        else
            w.x = 1;
        if (p.y < widgetBindPos.y)
            w.y = 0;
        else if (p.y < widgetBindPos.y + widgetBindSize.y)
            w.y = (p.y - widgetBindPos.y) / widgetBindSize.y;
        else
            w.y = 1;
        float2 q = p.xy + deformedWidgetPos - widgetBindPos + w * (deformedWidgetSize - widgetBindSize);
        v = mul(float4(q.x, q.y, p.z, p.w), inverseWidgetBindMatrix);
    }
    return v;
}

// Normalize with a small bias against zero length (the GLSL used normalize).
float3 safeNormalize(float3 v)
{
    return v / (length(v) + 0.00001);
}

Output main([[vk::location(0)]] float4 position : POSITION, [[vk::location(1)]] float4 color : COLOR0, [[vk::location(2)]] float2 normal1 : TEXCOORD0,
            [[vk::location(3)]] float2 normal2 : TEXCOORD1, [[vk::location(4)]] float2 texCoord : TEXCOORD2)
{
    Output output;
    float4 v = deformedPosition(position);
    float4 p = mul(v, WorldView);
    output.color = color * baseColor;
    // An edge vertex carries the normals of its two edges: move it out along their bisector so that
    // the edge is antialiased over aaOffset pixels.
    if (dot(normal1, normal1) > 0 && dot(normal2, normal2) > 0) {
        float3 n1 = float3(mul(float4(normal1, 0, 0), NormalMatrix), 0);
        float3 n2 = float3(mul(float4(normal2, 0, 0), NormalMatrix), 0);
        n1.z = 0;
        n2.z = 0;
        if (dot(n1, n1) > 0 && dot(n2, n2) > 0) {
            n1 = safeNormalize(n1);
            n2 = safeNormalize(n2);
            float3 n = safeNormalize(n1 + n2);
            float s = max(0.7, dot(n1, n));
            n *= aaOffset / s;
            v.xyz += n;
            p.xyz += n;
            p.z += 0.5;
        }
    }
    // A fixed offset of 0.135 pixels (the GLSL had none; presumably for Direct3D 9's pixel centres).
    p.xyz += float3(-0.135, -0.135, 0);
    output.position = mul(p, Proj);
    output.maskCoord = mul(v, MaskMatrix).xy;
    output.texCoord = mul(float4(texCoord.x, texCoord.y, 0, 1), TextureMatrix).xy;
    return output;
}
