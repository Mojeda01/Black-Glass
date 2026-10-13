
struct UBO {
    float4x4 view;
    float4x4 proj;
};

struct PushData {
    float4x4 model;
};

[[vk::push_constant]]
PushData push;

[[vk::binding(0, 0)]]
ConstantBuffer<UBO> ubo;

[[vk::binding(1, 0)]]
Sampler2D texSampler;

struct VSInput {
    float3 pos      : POSITION;
    float3 color    : COLOR;
    float2 texCoord : TEXCOORD0;
};

struct VSOutput {
    float4 pos      : SV_Position;
    float3 color    : COLOR;
    float2 texCoord : TEXCOORD0;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    float4 world = mul(push.model, float4(input.pos, 1.0));
    output.pos = mul(ubo.proj, mul(ubo.view, world));
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return texSampler.Sample(input.texCoord) * float4(input.color, 1.0);
}
