struct FrameData {
    float4x4 view;
    float4x4 proj;
    float4x4 invViewProj;
    float4x4 lightViewProj;
    float4 cameraPos;
    float4 debugParams;
    float4 iblParams;
    float4 lightDir;
    float4 shadowParams;
    float4 groundParams;
    float4 groundExtent;
    float4 groundBoxes[8];
    float4 effectParams;
    float4 passParams;
};

struct FxPush {
    float4 params;
};

[[vk::push_constant]]
FxPush fx;

[[vk::binding(0, 0)]]
ConstantBuffer<FrameData> frame;

[[vk::binding(1, 0)]]
Sampler2D texA;

[[vk::binding(2, 0)]]
Sampler2D texB;

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

float3 worldFromDepth(float2 uv, float depth)
{
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 w = mul(frame.invViewProj, ndc);
    return w.xyz / w.w;
}

float viewDepthOf(float3 world)
{
    return -mul(frame.view, float4(world, 1.0)).z;
}

float interleavedGradient(float2 p)
{
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

[shader("fragment")]
float4 ssaoFragment(FullscreenOutput input) : SV_Target
{
    float2 texel = 1.0 / frame.debugParams.zw;
    float2 uv = input.uv;

    float depth = texA.SampleLevel(uv, 0.0).r;
    if (depth >= 1.0)
        return float4(1.0, 1.0, 1.0, 1.0);

    float3 P = worldFromDepth(uv, depth);

    float2 uvR = uv + float2(texel.x, 0.0);
    float2 uvL = uv - float2(texel.x, 0.0);
    float2 uvU = uv + float2(0.0, texel.y);
    float2 uvD = uv - float2(0.0, texel.y);
    float3 pR = worldFromDepth(uvR, texA.SampleLevel(uvR, 0.0).r);
    float3 pL = worldFromDepth(uvL, texA.SampleLevel(uvL, 0.0).r);
    float3 pU = worldFromDepth(uvU, texA.SampleLevel(uvU, 0.0).r);
    float3 pD = worldFromDepth(uvD, texA.SampleLevel(uvD, 0.0).r);

    float3 dx = length(pR - P) < length(P - pL) ? pR - P : P - pL;
    float3 dy = length(pU - P) < length(P - pD) ? pU - P : P - pD;
    float3 N = normalize(cross(dx, dy));
    if (dot(N, frame.cameraPos.xyz - P) < 0.0)
        N = -N;

    float3 T = normalize(abs(N.y) < 0.99 ? cross(N, float3(0.0, 1.0, 0.0)) : cross(N, float3(1.0, 0.0, 0.0)));
    float3 B = cross(N, T);

    float noise = interleavedGradient(input.pos.xy);
    float radius = fx.params.x;
    float pointViewDepth = viewDepthOf(P);

    const int COUNT = 16;
    float occlusion = 0.0;
    for (int i = 0; i < COUNT; i++)
    {
        float t = (float(i) + 0.5) / float(COUNT);
        float phi = float(i) * 2.399963 + noise * 6.2831853;
        float r = sqrt(t);
        float z = sqrt(1.0 - t);
        float3 dir = T * (cos(phi) * r) + B * (sin(phi) * r) + N * z;

        float s = frac(float(i) * 0.618034 + noise);
        float len = radius * lerp(0.1, 1.0, s * s);
        float3 S = P + dir * len;

        float4 clip = mul(frame.proj, mul(frame.view, float4(S, 1.0)));
        float2 suv = clip.xy / clip.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0)
            continue;

        float sd = texA.SampleLevel(suv, 0.0).r;
        if (sd >= 1.0)
            continue;

        float sceneViewDepth = viewDepthOf(worldFromDepth(suv, sd));
        float sampleViewDepth = viewDepthOf(S);
        float diff = sampleViewDepth - sceneViewDepth;
        if (diff > radius * 0.05)
        {
            float rangeCheck = saturate(radius / max(abs(pointViewDepth - sceneViewDepth), 1e-5));
            occlusion += rangeCheck;
        }
    }

    float ao = saturate(1.0 - occlusion / float(COUNT));
    ao = pow(ao, fx.params.y);
    return float4(ao, ao, ao, 1.0);
}

[shader("fragment")]
float4 ssaoBlurFragment(FullscreenOutput input) : SV_Target
{
    float2 texel = 1.0 / frame.debugParams.zw;
    float2 uv = input.uv;

    float d0 = texB.SampleLevel(uv, 0.0).r;
    if (d0 >= 1.0)
        return float4(1.0, 1.0, 1.0, 1.0);

    float v0 = viewDepthOf(worldFromDepth(uv, d0));

    float sum = 0.0;
    float weightSum = 0.0;
    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            float2 o = uv + float2(float(x), float(y)) * texel;
            float d = texB.SampleLevel(o, 0.0).r;
            if (d >= 1.0)
                continue;
            float v = viewDepthOf(worldFromDepth(o, d));
            float w = saturate(1.0 - abs(v - v0) / (0.05 * v0 + 1e-4));
            sum += texA.SampleLevel(o, 0.0).r * w;
            weightSum += w;
        }
    }

    float ao = sum / max(weightSum, 1e-4);
    return float4(ao, ao, ao, 1.0);
}
