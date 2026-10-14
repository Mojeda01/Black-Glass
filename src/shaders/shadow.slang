struct ShadowPush {
    float4x4 model;
    float4x4 lightViewProj;
};

[[vk::push_constant]]
ShadowPush push;

struct ShadowInput {
    float3 pos : POSITION;
};

[shader("vertex")]
float4 shadowVertex(ShadowInput input) : SV_Position
{
    return mul(push.lightViewProj, mul(push.model, float4(input.pos, 1.0)));
}
