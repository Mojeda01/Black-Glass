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

[[vk::binding(0, 0)]]
ConstantBuffer<FrameData> frame;

struct LineInput {
    float3 pos   : POSITION;
    float4 color : COLOR;
};

struct LineOutput {
    float4 pos   : SV_Position;
    float4 color : COLOR;
};

[shader("vertex")]
LineOutput lineVertex(LineInput input)
{
    LineOutput output;
    output.pos = mul(frame.proj, mul(frame.view, float4(input.pos, 1.0)));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 lineFragment(LineOutput input) : SV_Target
{
    return input.color;
}
