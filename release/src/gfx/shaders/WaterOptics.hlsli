// Light lost per block of water along the view ray (red, green, blue).
static const float3 Absorption = float3(0.60, 0.45, 1.00);
// The colour deep water converges on in daylight.
static const float3 DeepWater = float3(0.00, 0.33, 0.76);

// waterSurface.xy hold the phases dot(PhaseDirection, world position) + speed * time, wrapped to 2 pi, as
// VulkanRenderer::beginWater computes them.
static const float2 PhaseDirectionX = float2(0.65, 0.31);
static const float2 PhaseDirectionY = float2(-0.27, 0.72);
// Each wave: whole multiples of the two phases, so it stays continuous where they wrap, and its height in blocks.
static const float3 Waves[5] = {float3(1, 0, 0.12), float3(0, 1, 0.11), float3(2, 1, 0.05), float3(1, -2, 0.05),
                                float3(3, 2, 0.025)};
// Distant waves are smaller than a pixel and would only shimmer.
static const float WaveNear = 40.0;
static const float WaveFar = 120.0;
// How tightly the sun's reflection is focused: higher gives smaller, sharper glints.
static const float GlintPower = 300.0;
static const float GlintStrength = 3.0;

float3 waveNormal(float2 position, float distance, float2 originPhase)
{
    const float2 phase = float2(dot(position, PhaseDirectionX), dot(position, PhaseDirectionY)) + originPhase;
    float2 slope = 0;
    [unroll] for (int i = 0; i < 5; ++i)
    {
        const float2 frequency = Waves[i].x * PhaseDirectionX + Waves[i].y * PhaseDirectionY;
        slope += Waves[i].z * frequency * cos(dot(Waves[i].xy, phase));
    }
    return normalize(float3(-slope * (1 - smoothstep(WaveNear, WaveFar, distance)), 1));
}
