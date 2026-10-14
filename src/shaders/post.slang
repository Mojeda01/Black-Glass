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

float3 tonemap(float3 color)
{
    float lum = luma(color);
    float mapped = lum / (lum + 1.0);
    float3 hueKept = color * (mapped / max(lum, 1e-4));
    float3 perChannel = color / (color + 1.0);
    return saturate(lerp(hueKept, perChannel, mapped * mapped));
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
    color = tonemap(color);

    float2 p = input.pos.xy;
    float noise = frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
    color += (noise - 0.5) / 255.0;

    return float4(color, 1.0);
}
