struct IblPush {
    uint size;
    uint useEquirect;
    float roughness;
    uint sampleCount;
    float srcSize;
    float envScale;
    float pad0;
    float pad1;
};

[[vk::push_constant]]
IblPush push;

[[vk::binding(0, 0)]]
Sampler2D equirectTex;

[[vk::binding(1, 0)]]
SamplerCube sourceCube;

[[vk::binding(2, 0)]]
[[vk::image_format("rgba16f")]]
RWTexture2DArray<float4> outCube;

[[vk::binding(3, 0)]]
[[vk::image_format("rg16f")]]
RWTexture2D<float2> outLut;

static const float PI = 3.14159265359;

float3 faceDirection(uint face, float2 uv)
{
    float sc = uv.x * 2.0 - 1.0;
    float tc = uv.y * 2.0 - 1.0;
    float3 d;
    if (face == 0)
        d = float3(1.0, -tc, -sc);
    else if (face == 1)
        d = float3(-1.0, -tc, sc);
    else if (face == 2)
        d = float3(sc, 1.0, tc);
    else if (face == 3)
        d = float3(sc, -1.0, -tc);
    else if (face == 4)
        d = float3(sc, -tc, 1.0);
    else
        d = float3(-sc, -tc, -1.0);
    return normalize(d);
}

float softRect(float2 p, float2 halfSize, float soft)
{
    float2 q = abs(p) - halfSize;
    float d = max(q.x, q.y);
    return 1.0 - smoothstep(-soft, soft, d);
}

float3 proceduralEnvironment(float3 d)
{
    float blur = 0.03;

    float3 zenith = float3(0.3, 0.45, 0.75);
    float3 horizon = float3(1.0, 0.92, 0.85);
    float3 groundNear = float3(0.22, 0.2, 0.19);

    float3 sky = lerp(horizon, zenith, pow(saturate(d.y), 0.5));
    float3 ground = lerp(horizon * 0.5, groundNear, saturate(-d.y * 2.0));
    float horizonWidth = 0.08 + blur * 0.4;
    float t = smoothstep(-horizonWidth, horizonWidth, d.y);
    float3 col = lerp(ground, sky, t);

    float soft = 0.05 + blur * 0.9;

    if (d.y > 0.25)
    {
        float2 p = float2(d.x, d.z) / d.y;
        col += float3(1.0, 0.97, 0.92) * 3.5 * softRect(p, float2(0.9, 0.55), soft);
    }

    if (abs(d.x) > 0.25)
    {
        float s = d.x > 0.0 ? 1.0 : -1.0;
        float2 p = float2(d.z * s, d.y) / abs(d.x);
        float3 tint = d.x > 0.0 ? float3(0.75, 0.88, 1.0) : float3(1.0, 0.82, 0.65);
        col += tint * 3.0 * softRect(p - float2(0.0, 0.35), float2(1.2, 0.18), soft);
    }

    return col;
}

float2 equirectUV(float3 d)
{
    float u = atan2(d.z, d.x) / (2.0 * PI) + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) / PI;
    return float2(u, v);
}

float radicalInverse(uint i)
{
    uint bits = reversebits(i);
    return float(bits) * 2.3283064365386963e-10;
}

float2 hammersley(uint i, uint n)
{
    return float2(float(i) / float(n), radicalInverse(i));
}

float3x3 tangentFrame(float3 n)
{
    float3 up = abs(n.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(up, n));
    float3 b = cross(n, t);
    return float3x3(t, b, n);
}

float3 importanceSampleGGX(float2 xi, float3 n, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float3 h = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
    float3x3 frame = tangentFrame(n);
    return normalize(frame[0] * h.x + frame[1] * h.y + frame[2] * h.z);
}

float distributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void genEnvironment(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= push.size || id.y >= push.size)
        return;

    float2 uv = (float2(id.xy) + 0.5) / float(push.size);
    float3 d = faceDirection(id.z, uv);

    float3 color;
    if (push.useEquirect != 0)
        color = equirectTex.SampleLevel(equirectUV(d), 0.0).rgb * push.envScale;
    else
        color = proceduralEnvironment(d) * push.envScale;

    outCube[uint3(id.xy, id.z)] = float4(color, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void genIrradiance(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= push.size || id.y >= push.size)
        return;

    float2 uv = (float2(id.xy) + 0.5) / float(push.size);
    float3 n = faceDirection(id.z, uv);
    float3x3 frame = tangentFrame(n);

    float texelSolidAngle = 4.0 * PI / (6.0 * push.srcSize * push.srcSize);
    float3 sum = float3(0.0, 0.0, 0.0);

    for (uint i = 0; i < push.sampleCount; i++)
    {
        float2 xi = hammersley(i, push.sampleCount);
        float phi = 2.0 * PI * xi.x;
        float cosTheta = sqrt(1.0 - xi.y);
        float sinTheta = sqrt(xi.y);
        float3 local = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
        float3 l = normalize(frame[0] * local.x + frame[1] * local.y + frame[2] * local.z);

        float pdf = max(cosTheta, 1e-4) / PI;
        float sampleSolidAngle = 1.0 / (float(push.sampleCount) * pdf);
        float lod = max(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0);

        sum += sourceCube.SampleLevel(l, lod).rgb;
    }

    outCube[uint3(id.xy, id.z)] = float4(sum / float(push.sampleCount), 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void genPrefilter(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= push.size || id.y >= push.size)
        return;

    float2 uv = (float2(id.xy) + 0.5) / float(push.size);
    float3 n = faceDirection(id.z, uv);

    if (push.roughness < 0.001)
    {
        float lod = log2(push.srcSize / float(push.size));
        outCube[uint3(id.xy, id.z)] = float4(sourceCube.SampleLevel(n, lod).rgb, 1.0);
        return;
    }

    float3 v = n;
    float texelSolidAngle = 4.0 * PI / (6.0 * push.srcSize * push.srcSize);
    float3 sum = float3(0.0, 0.0, 0.0);
    float weight = 0.0;

    for (uint i = 0; i < push.sampleCount; i++)
    {
        float2 xi = hammersley(i, push.sampleCount);
        float3 h = importanceSampleGGX(xi, n, push.roughness);
        float3 l = normalize(2.0 * dot(v, h) * h - v);
        float NdotL = saturate(dot(n, l));
        if (NdotL > 0.0)
        {
            float NdotH = saturate(dot(n, h));
            float VdotH = saturate(dot(v, h));
            float D = distributionGGX(NdotH, push.roughness);
            float pdf = D * NdotH / (4.0 * VdotH + 1e-4);
            float sampleSolidAngle = 1.0 / (float(push.sampleCount) * pdf + 1e-4);
            float lod = max(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0);

            sum += sourceCube.SampleLevel(l, lod).rgb * NdotL;
            weight += NdotL;
        }
    }

    outCube[uint3(id.xy, id.z)] = float4(sum / max(weight, 1e-4), 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void genBrdfLut(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= push.size || id.y >= push.size)
        return;

    float NdotV = (float(id.x) + 0.5) / float(push.size);
    float roughness = (float(id.y) + 0.5) / float(push.size);

    float3 v = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);
    float3 n = float3(0.0, 0.0, 1.0);

    float a = roughness * roughness;
    float k = a * 0.5;

    float A = 0.0;
    float B = 0.0;

    for (uint i = 0; i < push.sampleCount; i++)
    {
        float2 xi = hammersley(i, push.sampleCount);
        float3 h = importanceSampleGGX(xi, n, roughness);
        float3 l = normalize(2.0 * dot(v, h) * h - v);

        float NdotL = saturate(l.z);
        float NdotH = saturate(h.z);
        float VdotH = saturate(dot(v, h));

        if (NdotL > 0.0)
        {
            float gl = NdotL / (NdotL * (1.0 - k) + k);
            float gv = NdotV / (NdotV * (1.0 - k) + k);
            float gVis = gl * gv * VdotH / (NdotH * NdotV + 1e-5);
            float fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - fc) * gVis;
            B += fc * gVis;
        }
    }

    outLut[id.xy] = float2(A, B) / float(push.sampleCount);
}
