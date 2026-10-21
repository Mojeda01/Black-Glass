struct BakeParams {
    float4x4 viewProj[2];
    float4 direction[2];
    float4 volumeMin;
    uint4 dims;
    float4 invAxisWeight[2];
};

[[vk::binding(0, 0)]]
ConstantBuffer<BakeParams> bake;

[[vk::binding(1, 0)]]
Sampler2DArrayShadow depthMaps;

[[vk::binding(2, 0)]]
RWStructuredBuffer<float> scratch;

[[vk::binding(3, 0)]]
RWStructuredBuffer<uint> packedA;

[[vk::binding(4, 0)]]
RWStructuredBuffer<uint> packedB;

static const float3 AXES[6] = {
    float3(1.0, 0.0, 0.0), float3(-1.0, 0.0, 0.0),
    float3(0.0, 1.0, 0.0), float3(0.0, -1.0, 0.0),
    float3(0.0, 0.0, 1.0), float3(0.0, 0.0, -1.0)
};

uint packBytes(float4 c)
{
    uint4 b = uint4(saturate(c) * 255.0 + 0.5);
    return b.x | (b.y << 8) | (b.z << 16) | (b.w << 24);
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ambientAccumulate(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= bake.dims.x || id.y >= bake.dims.y || id.z >= bake.dims.z)
        return;

    float3 p = bake.volumeMin.xyz + (float3(id) + 0.5) * bake.volumeMin.w;
    uint index = (id.z * bake.dims.y + id.y) * bake.dims.x + id.x;

    float invWeight[6] = {
        bake.invAxisWeight[0].x, bake.invAxisWeight[0].y, bake.invAxisWeight[0].z, bake.invAxisWeight[0].w,
        bake.invAxisWeight[1].x, bake.invAxisWeight[1].y
    };

    float sums[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

    for (uint j = 0; j < bake.dims.w; j++)
    {
        float4 clip = mul(bake.viewProj[j], float4(p, 1.0));
        float3 ndc = clip.xyz;
        float2 uv = ndc.xy * 0.5 + 0.5;

        float visible = 1.0;
        if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 && ndc.z >= 0.0 && ndc.z <= 1.0)
        {
            visible = depthMaps.SampleCmpLevelZero(float3(uv, float(j)), ndc.z - bake.direction[j].w);
        }

        float3 d = bake.direction[j].xyz;
        for (uint a = 0; a < 6; a++)
        {
            float w = max(dot(d, AXES[a]), 0.0);
            sums[a] += visible * w * invWeight[a];
        }
    }

    for (uint a = 0; a < 6; a++)
    {
        scratch[index * 6 + a] += sums[a];
    }
}

[shader("compute")]
[numthreads(4, 4, 4)]
void ambientFinalize(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= bake.dims.x || id.y >= bake.dims.y || id.z >= bake.dims.z)
        return;

    uint index = (id.z * bake.dims.y + id.y) * bake.dims.x + id.x;

    float v[6];
    for (uint a = 0; a < 6; a++)
    {
        v[a] = sqrt(saturate(scratch[index * 6 + a]));
    }

    packedA[index] = packBytes(float4(v[0], v[1], v[2], v[3]));
    packedB[index] = packBytes(float4(v[4], v[5], 0.0, 0.0));
}
