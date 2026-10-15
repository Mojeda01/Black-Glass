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
    float4 projParams;
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

float linearDepth(float depth)
{
    return frame.projParams.y / (depth + frame.projParams.x);
}

float3 viewFromDepth(float2 uv, float depth)
{
    float L = linearDepth(depth);
    float2 ndc = uv * 2.0 - 1.0;
    return float3(ndc.x * L / frame.projParams.z, ndc.y * L / frame.projParams.w, -L);
}

float interleavedGradient(float2 p)
{
    return frac(52.9829189 * frac(dot(p, float2(0.06711056, 0.00583715))));
}

[shader("fragment")]
float4 ssaoFragment(FullscreenOutput input) : SV_Target
{
    float2 fullSize = frame.debugParams.zw;
    float2 texel = 1.0 / fullSize;
    float2 uv = (2.0 * floor(input.pos.xy) + 0.5) / fullSize;

    float depth = texA.SampleLevel(uv, 0.0).r;
    if (depth >= 1.0)
        return float4(1.0, 1.0, 1.0, 1.0);

    float3 P = viewFromDepth(uv, depth);

    float2 uvR = uv + float2(texel.x, 0.0);
    float2 uvL = uv - float2(texel.x, 0.0);
    float2 uvU = uv + float2(0.0, texel.y);
    float2 uvD = uv - float2(0.0, texel.y);
    float3 pR = viewFromDepth(uvR, texA.SampleLevel(uvR, 0.0).r);
    float3 pL = viewFromDepth(uvL, texA.SampleLevel(uvL, 0.0).r);
    float3 pU = viewFromDepth(uvU, texA.SampleLevel(uvU, 0.0).r);
    float3 pD = viewFromDepth(uvD, texA.SampleLevel(uvD, 0.0).r);

    float3 dx = length(pR - P) < length(P - pL) ? pR - P : P - pL;
    float3 dy = length(pU - P) < length(P - pD) ? pU - P : P - pD;
    float3 N = normalize(cross(dx, dy));
    if (dot(N, -P) < 0.0)
        N = -N;

    float3 T = normalize(abs(N.y) < 0.99 ? cross(N, float3(0.0, 1.0, 0.0)) : cross(N, float3(1.0, 0.0, 0.0)));
    float3 B = cross(N, T);

    float noise = interleavedGradient(input.pos.xy);
    float pointL = -P.z;
    float maxRadius = 0.12 * pointL / (0.5 * abs(frame.projParams.w));
    float radius = min(fx.params.x, maxRadius);

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

        float sampleL = -S.z;
        if (sampleL <= 1e-4)
            continue;

        float2 ndc = float2(S.x * frame.projParams.z, S.y * frame.projParams.w) / sampleL;
        float2 suv = ndc * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0)
            continue;

        float sd = texA.SampleLevel(suv, 0.0).r;
        if (sd >= 1.0)
            continue;

        float sceneL = linearDepth(sd);
        float diff = sampleL - sceneL;
        if (diff > radius * 0.05)
        {
            float rangeCheck = saturate(radius / max(abs(pointL - sceneL), 1e-5));
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
    float2 uv = input.uv;
    float2 aoSize = fx.params.zw;
    float2 fullSize = frame.debugParams.zw;

    float d0 = texB.SampleLevel(uv, 0.0).r;
    if (d0 >= 1.0)
        return float4(1.0, 1.0, 1.0, 1.0);

    float L0 = linearDepth(d0);

    float2 pixelPos = floor(input.pos.xy) + 0.5;
    float2 baseIndex = floor(pixelPos * 0.5);

    float sum = 0.0;
    float weightSum = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 tapIndex = baseIndex + float2(float(x), float(y));
            if (tapIndex.x < 0.0 || tapIndex.y < 0.0 || tapIndex.x >= aoSize.x || tapIndex.y >= aoSize.y)
                continue;

            float2 samplePos = tapIndex * 2.0 + 0.5;
            float d = texB.SampleLevel(samplePos / fullSize, 0.0).r;
            if (d >= 1.0)
                continue;

            float L = linearDepth(d);
            float depthWeight = saturate(1.0 - abs(L - L0) / (0.05 * L0 + 1e-4));
            float spatialWeight = saturate(1.0 - length(pixelPos - samplePos) / 4.0);
            float w = depthWeight * spatialWeight;
            sum += texA.SampleLevel((tapIndex + 0.5) / aoSize, 0.0).r * w;
            weightSum += w;
        }
    }

    if (weightSum < 1e-3)
    {
        float ao = texA.SampleLevel((baseIndex + 0.5) / aoSize, 0.0).r;
        return float4(ao, ao, ao, 1.0);
    }

    float ao = sum / weightSum;
    return float4(ao, ao, ao, 1.0);
}
