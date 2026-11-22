// Temporal resolve adapted from CWVulkan Steam's taa.frag and TemporalAA.
#include "PostUniforms.hlsli"

[[vk::binding(3, 1)]] Texture2D<float> currentDepth;
[[vk::binding(4, 1)]] Texture2D<float> previousDepth;

float maxChannel(float3 v) { return max(v.r, max(v.g, v.b)); }

float3 historyAt(float2 uv)
{
    const float2 position = uv * metrics.zw;
    const float2 center = floor(position - 0.5) + 0.5;
    const float2 f = position - center;
    const float2 w0 = f * (-0.5 + f * (1 - 0.5 * f));
    const float2 w1 = 1 + f * f * (-2.5 + 1.5 * f);
    const float2 w2 = f * (0.5 + f * (2 - 1.5 * f));
    const float2 w3 = f * f * (-0.5 + 0.5 * f);
    const float2 w12 = w1 + w2;
    const float2 t0 = (center - 1) * metrics.xy;
    const float2 t3 = (center + 2) * metrics.xy;
    const float2 t12 = (center + w2 / w12) * metrics.xy;
    const float4 outer = float4(w12.x * w0.y, w0.x * w12.y, w12.x * w3.y, w3.x * w12.y);
    const float centerWeight = w12.x * w12.y;
    const float3 sum =
        secondary.SampleLevel(LinearSampler, float2(t12.x, t0.y), 0).rgb * outer.x +
        secondary.SampleLevel(LinearSampler, float2(t0.x, t12.y), 0).rgb * outer.y +
        secondary.SampleLevel(LinearSampler, float2(t12.x, t3.y), 0).rgb * outer.z +
        secondary.SampleLevel(LinearSampler, float2(t3.x, t12.y), 0).rgb * outer.w +
        secondary.SampleLevel(LinearSampler, t12, 0).rgb * centerWeight;
    return max(0, sum / (dot(outer, 1) + centerWeight));
}

float4 main(float4 position : SV_POSITION) : SV_TARGET
{
    const float2 outputUV = position.xy * metrics.xy;
    const float2 uv = outputUV + jitter.xy * metrics.xy;
    const float4 current = primary.SampleLevel(LinearSampler, uv, 0);
    if (tuning.w < 0.5)
        return current;

    float3 lo = current.rgb, hi = current.rgb;
    float3 axisSum = 0;
    [unroll] for (int y = -1; y <= 1; ++y)
        [unroll] for (int x = -1; x <= 1; ++x)
        {
            const float3 c = primary.SampleLevel(LinearSampler, uv + float2(x, y) * metrics.xy, 0).rgb;
            lo = min(lo, c);
            hi = max(hi, c);
            if (abs(x) + abs(y) == 1)
                axisSum += c;
        }

    const int2 last = int2(metrics.zw) - 1;
    const int2 pixel = int2(position.xy);
    const float depth = currentDepth.Load(int3(pixel, 0));
    const float2 sourceUV = outputUV - jitter.xy * metrics.xy;
    // The game uses an upward clip Y and a negative-height Vulkan viewport.
    const float2 xy = sourceUV * float2(2, -2) + float2(-1, 1);
    const float4 previous = mul(clipToPrevious, float4(xy, depth, 1));
    const float3 previousNdc = previous.xyz / max(previous.w, 1e-8);
    const float2 previousUV = previousNdc.xy * float2(0.5, -0.5) + 0.5;
    const float2 motion = previousUV - sourceUV;
    const float2 historyUV = outputUV + motion;
    const float2 depthUV = previousUV + jitter.zw * metrics.xy;
    const int2 depthBase = int2(floor(saturate(depthUV) * metrics.zw - 0.5));
    const float tolerance = 0.0002 + min(0.005, 2 * (abs(ddx(previousNdc.z)) + abs(ddy(previousNdc.z))));
    const bool sky = depth >= 0.99999;
    const bool inside = previous.w > 0 && all(historyUV >= 0) && all(historyUV <= 1) &&
                        all(depthUV >= 0) && all(depthUV <= 1);
    bool sameSurface = false;
    // Check depths around the projected source ray, where the previous scene was
    // jittered. Resolved history uses the separate, unjittered historyUV.
    [unroll] for (int y = 0; y < 2; ++y)
        [unroll] for (int x = 0; x < 2; ++x)
        {
            const int2 q = clamp(depthBase + int2(x, y), int2(0, 0), last);
            const float d = previousDepth.Load(int3(q, 0));
            sameSurface |= sky ? d >= 0.99999 : d < 0.99999 && abs(d - previousNdc.z) <= tolerance;
        }
    const float3 raw = historyAt(saturate(historyUV));

    // Clip history toward the current sample, using a conservative colour range at edges.
    const float3 delta = raw - current.rgb;
    const float3 room = float3(delta.x > 0 ? hi.x - current.x : current.x - lo.x,
                               delta.y > 0 ? hi.y - current.y : current.y - lo.y,
                               delta.z > 0 ? hi.z - current.z : current.z - lo.z);
    const float3 limits = saturate((room + 1e-5) / max(abs(delta), 1e-5));
    const float3 clipped = current.rgb + delta * min(limits.x, min(limits.y, limits.z));
    const float confidence = 1 - smoothstep(0.02, 0.12, maxChannel(abs(raw - clipped)));
    const float edge = smoothstep(0.04, 0.20, maxChannel(hi - lo));
    const float still = 1 - smoothstep(0.05, 1.0, length(motion * metrics.zw));
    const float historyWeightAtEdge = lerp(tuning.x, tuning.y, edge);
    const float accumulation = inside && sameSurface ? lerp(historyWeightAtEdge, 0.94, still) * confidence : 0.0;
    const float3 filtered = lerp(current.rgb, (current.rgb * 4 + axisSum) / 8, tuning.z);
    const float currentWeight = (1 - accumulation) / (1 + maxChannel(filtered));
    const float historyWeight = accumulation / (1 + maxChannel(clipped));
    return float4((filtered * currentWeight + clipped * historyWeight) / (currentWeight + historyWeight), current.a);
}
