struct FrameData {
    float4x4 view;
    float4x4 proj;
    float4 cameraPos;
};

struct MaterialData {
    float4 baseColorFactor;
    float4 emissiveFactor;
    float4 factors;
    int4 flags0;
    int4 flags1;
};

struct PushData {
    float4x4 model;
};

[[vk::push_constant]]
PushData push;

[[vk::binding(0, 0)]]
ConstantBuffer<FrameData> frame;

[[vk::binding(1, 0)]]
ConstantBuffer<MaterialData> material;

[[vk::binding(2, 0)]]
Sampler2D baseColorTex;

[[vk::binding(3, 0)]]
Sampler2D metalRoughTex;

[[vk::binding(4, 0)]]
Sampler2D normalTex;

[[vk::binding(5, 0)]]
Sampler2D occlusionTex;

[[vk::binding(6, 0)]]
Sampler2D emissiveTex;

struct VSInput {
    float3 pos      : POSITION;
    float3 color    : COLOR;
    float2 texCoord : TEXCOORD0;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;
};

struct VSOutput {
    float4 pos      : SV_Position;
    float3 color    : COLOR;
    float2 texCoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 normal   : TEXCOORD2;
    float4 tangent  : TEXCOORD3;
};

static const float PI = 3.14159265359;

[shader("vertex")]
VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    float4 world = mul(push.model, float4(input.pos, 1.0));
    output.pos = mul(frame.proj, mul(frame.view, world));
    output.color = input.color;
    output.texCoord = input.texCoord;
    output.worldPos = world.xyz;
    output.normal = mul(push.model, float4(input.normal, 0.0)).xyz;
    output.tangent = float4(mul(push.model, float4(input.tangent.xyz, 0.0)).xyz, input.tangent.w);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 baseColor = material.baseColorFactor * float4(input.color, 1.0);
    if (material.flags0.x != 0)
        baseColor *= baseColorTex.Sample(input.texCoord);

    if (material.flags1.y == 1 && baseColor.a < material.emissiveFactor.w)
        discard;

    float metallic = material.factors.x;
    float roughness = material.factors.y;
    if (material.flags0.y != 0)
    {
        float4 mr = metalRoughTex.Sample(input.texCoord);
        roughness *= mr.g;
        metallic *= mr.b;
    }
    metallic = saturate(metallic);
    roughness = clamp(roughness, 0.045, 1.0);

    float ao = 1.0;
    if (material.flags0.w != 0)
        ao = 1.0 + material.factors.w * (occlusionTex.Sample(input.texCoord).r - 1.0);

    float3 N = normalize(input.normal);
    if (material.flags0.z != 0)
    {
        float3 T = normalize(input.tangent.xyz);
        T = normalize(T - N * dot(N, T));
        float3 B = cross(N, T) * input.tangent.w;
        float3 tn = normalTex.Sample(input.texCoord).xyz * 2.0 - 1.0;
        tn.xy *= material.factors.z;
        N = normalize(T * tn.x + B * tn.y + N * tn.z);
    }
    if (material.flags1.z != 0 && !isFrontFace)
        N = -N;

    float3 V = normalize(frame.cameraPos.xyz - input.worldPos);
    float3 L = normalize(float3(0.5, 1.0, 0.3));
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * denom * denom);

    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float G = (NdotL / (NdotL * (1.0 - k) + k)) * (NdotV / (NdotV * (1.0 - k) + k));

    float3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);

    float3 specular = D * G * F / (4.0 * NdotV * NdotL + 1e-4);
    float3 kd = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kd * baseColor.rgb / PI;

    float3 radiance = float3(3.0, 3.0, 3.0);
    float3 Lo = (diffuse + specular) * radiance * NdotL;

    float3 ambient = lerp(float3(0.02, 0.02, 0.025), float3(0.12, 0.13, 0.15), N.y * 0.5 + 0.5)
                     * baseColor.rgb * ao;

    float3 emissive = material.emissiveFactor.rgb;
    if (material.flags1.x != 0)
        emissive *= emissiveTex.Sample(input.texCoord).rgb;

    float3 color = ambient + Lo + emissive;
    color = color / (color + 1.0);

    return float4(color, 1.0);
}
