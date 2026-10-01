struct UBO {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};

[[vk::binding(0, 0)]]
ConstantBuffer<UBO> ubo;

struct VSInput {
    float3 pos   : POSITION;
    float3 color : COLOR;
};

struct VSOutput {
    float4 pos   : SV_Position;
    float3 color : COLOR;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    float4 world = mul(ubo.model, float4(input.pos, 1.0));
    output.pos = mul(ubo.proj, mul(ubo.view, world));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
