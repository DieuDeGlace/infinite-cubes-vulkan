#ifndef CUBE_SMAA_HLSLI
#define CUBE_SMAA_HLSLI

#include "PostUniforms.hlsli"

// SMAA Ultra as in CWVulkan Steam. The bundled textures use a top-left origin.
#define SMAA_CUSTOM_SL
#define SMAA_PRESET_ULTRA
#define SMAA_RT_METRICS metrics
#define SMAATexture2D(tex) Texture2D<float4> tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) tex.SampleLevel(LinearSampler, coord, 0)
#define SMAASampleLevelZeroPoint(tex, coord) tex.SampleLevel(PointSampler, coord, 0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.SampleLevel(LinearSampler, coord, 0, offset)
#define SMAASample(tex, coord) tex.Sample(LinearSampler, coord)
#define SMAASamplePoint(tex, coord) tex.Sample(PointSampler, coord)
#define SMAASampleOffset(tex, coord, offset) tex.Sample(LinearSampler, coord, offset)
#define SMAA_FLATTEN [flatten]
#define SMAA_BRANCH [branch]
#include "../../../thirdparty/smaa/SMAA.hlsl"

#endif
