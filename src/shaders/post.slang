struct PostPush {
    float4 params;
};

[[vk::push_constant]]
PostPush post;

[[vk::binding(0, 0)]]
Sampler2D srcTex;

[[vk::binding(1, 0)]]
Sampler2D bloomTex;

struct FullscreenOutput {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

[shader("vertex")]
FullscreenOutput fullscreenVertex(uint vid : SV_VertexID)
{
    FullscreenOutput output;
    float2 uv = float2(float((vid << 1) & 2), float(vid & 2));
    output.uv = uv;
    output.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

float luma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

[shader("fragment")]
float4 bloomDownFragment(FullscreenOutput input) : SV_Target
{
    float2 t = post.params.xy;
    float2 uv = input.uv;

    float3 a = srcTex.SampleLevel(uv + t * float2(-2.0,  2.0), 0.0).rgb;
    float3 b = srcTex.SampleLevel(uv + t * float2( 0.0,  2.0), 0.0).rgb;
    float3 c = srcTex.SampleLevel(uv + t * float2( 2.0,  2.0), 0.0).rgb;
    float3 d = srcTex.SampleLevel(uv + t * float2(-2.0,  0.0), 0.0).rgb;
    float3 e = srcTex.SampleLevel(uv, 0.0).rgb;
    float3 f = srcTex.SampleLevel(uv + t * float2( 2.0,  0.0), 0.0).rgb;
    float3 g = srcTex.SampleLevel(uv + t * float2(-2.0, -2.0), 0.0).rgb;
    float3 h = srcTex.SampleLevel(uv + t * float2( 0.0, -2.0), 0.0).rgb;
    float3 i = srcTex.SampleLevel(uv + t * float2( 2.0, -2.0), 0.0).rgb;
    float3 j = srcTex.SampleLevel(uv + t * float2(-1.0,  1.0), 0.0).rgb;
    float3 k = srcTex.SampleLevel(uv + t * float2( 1.0,  1.0), 0.0).rgb;
    float3 l = srcTex.SampleLevel(uv + t * float2(-1.0, -1.0), 0.0).rgb;
    float3 m = srcTex.SampleLevel(uv + t * float2( 1.0, -1.0), 0.0).rgb;

    float3 result;
    if (post.params.z > 0.5)
    {
        float3 g0 = (a + b + d + e) * 0.25;
        float3 g1 = (b + c + e + f) * 0.25;
        float3 g2 = (d + e + g + h) * 0.25;
        float3 g3 = (e + f + h + i) * 0.25;
        float3 g4 = (j + k + l + m) * 0.25;

        float w0 = 0.125 / (1.0 + luma(g0));
        float w1 = 0.125 / (1.0 + luma(g1));
        float w2 = 0.125 / (1.0 + luma(g2));
        float w3 = 0.125 / (1.0 + luma(g3));
        float w4 = 0.5 / (1.0 + luma(g4));

        result = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / (w0 + w1 + w2 + w3 + w4);
    }
    else
    {
        result = e * 0.125
               + (a + c + g + i) * 0.03125
               + (b + d + f + h) * 0.0625
               + (j + k + l + m) * 0.125;
    }

    return float4(result, 1.0);
}

[shader("fragment")]
float4 bloomUpFragment(FullscreenOutput input) : SV_Target
{
    float2 t = post.params.xy;
    float2 uv = input.uv;

    float3 a = srcTex.SampleLevel(uv + t * float2(-1.0,  1.0), 0.0).rgb;
    float3 b = srcTex.SampleLevel(uv + t * float2( 0.0,  1.0), 0.0).rgb;
    float3 c = srcTex.SampleLevel(uv + t * float2( 1.0,  1.0), 0.0).rgb;
    float3 d = srcTex.SampleLevel(uv + t * float2(-1.0,  0.0), 0.0).rgb;
    float3 e = srcTex.SampleLevel(uv, 0.0).rgb;
    float3 f = srcTex.SampleLevel(uv + t * float2( 1.0,  0.0), 0.0).rgb;
    float3 g = srcTex.SampleLevel(uv + t * float2(-1.0, -1.0), 0.0).rgb;
    float3 h = srcTex.SampleLevel(uv + t * float2( 0.0, -1.0), 0.0).rgb;
    float3 i = srcTex.SampleLevel(uv + t * float2( 1.0, -1.0), 0.0).rgb;

    float3 result = (e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) / 16.0;
    return float4(result, 1.0);
}

float3 tonemapCustom(float3 color)
{
    float lum = luma(color);
    float mapped = lum / (lum + 1.0);
    float3 hueKept = color * (mapped / max(lum, 1e-4));
    float3 perChannel = color / (color + 1.0);
    return saturate(lerp(hueKept, perChannel, mapped * mapped));
}

float3 tonemapACES(float3 color)
{
    float3x3 inputMat = float3x3(0.59719, 0.35458, 0.04823,
                                 0.07600, 0.90834, 0.01566,
                                 0.02840, 0.13383, 0.83777);
    float3x3 outputMat = float3x3(1.60475, -0.53108, -0.07367,
                                  -0.10208, 1.10813, -0.00605,
                                  -0.00327, -0.07276, 1.07602);
    float3 v = mul(inputMat, color / 0.6);
    float3 a = v * (v + 0.0245786) - 0.000090537;
    float3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return saturate(mul(outputMat, a / b));
}

float3 agxContrast(float3 x)
{
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 + 0.1191 * x - 0.00232;
}

float3 tonemapAgX(float3 color)
{
    float3x3 insetMat = float3x3(0.856627153315983, 0.0951212405381588, 0.0482516061458583,
                                 0.137318972929847, 0.761241990602591, 0.101439036467562,
                                 0.11189821299995, 0.0767994186031903, 0.811302368396859);
    float3x3 outsetMat = float3x3(1.1271005818144368, -0.11060664309660323, -0.016493938717834573,
                                  -0.1413297634984383, 1.157823702216272, -0.016493938717834257,
                                  -0.14132976349843826, -0.11060664309660323, 1.2519364065950405);
    float minEv = -12.47393;
    float maxEv = 4.026069;
    float3 v = mul(insetMat, color);
    v = max(v, float3(1e-10, 1e-10, 1e-10));
    v = clamp(log2(v), float3(minEv, minEv, minEv), float3(maxEv, maxEv, maxEv));
    v = (v - minEv) / (maxEv - minEv);
    v = agxContrast(v);
    v = mul(outsetMat, v);
    v = pow(max(v, float3(0.0, 0.0, 0.0)), float3(2.2, 2.2, 2.2));
    return saturate(v);
}

float3 tonemapNeutral(float3 color)
{
    float startCompression = 0.8 - 0.04;
    float desaturation = 0.15;
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;
    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression)
        return saturate(color);
    float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return saturate(lerp(color, newPeak * float3(1.0, 1.0, 1.0), g));
}

float3 tonemap(float3 color, int mode)
{
    if (mode == 1)
        return tonemapACES(color);
    if (mode == 2)
        return tonemapAgX(color);
    if (mode == 3)
        return tonemapNeutral(color);
    return tonemapCustom(color);
}

[shader("fragment")]
float4 compositeFragment(FullscreenOutput input) : SV_Target
{
    float3 hdr = srcTex.SampleLevel(input.uv, 0.0).rgb;

    if (post.params.z > 0.5)
        return float4(hdr, 1.0);

    float3 color = hdr;
    if (post.params.y > 0.0)
        color += bloomTex.SampleLevel(input.uv, 0.0).rgb * post.params.y;

    color *= post.params.x;
    color = tonemap(color, int(post.params.w + 0.5));

    float2 p = input.pos.xy;
    float noise = frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
    color += (noise - 0.5) / 255.0;

    return float4(color, 1.0);
}
