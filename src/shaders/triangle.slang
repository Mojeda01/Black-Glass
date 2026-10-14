struct FrameData {
    float4x4 view;
    float4x4 proj;
    float4x4 invViewProj;
    float4 cameraPos;
    float4 debugParams;
    float4 iblParams;
};

struct MaterialData {
    float4 baseColorFactor;
    float4 emissiveFactor;
    float4 factors;
    int4 flags0;
    int4 flags1;
    float4 uvLinear[5];
    float4 uvOffset[5];
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

float2 slotUV(float2 uv, int slot)
{
    float4 l = material.uvLinear[slot];
    return float2(l.x * uv.x + l.y * uv.y, l.z * uv.x + l.w * uv.y) + material.uvOffset[slot].xy;
}

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

[[vk::binding(7, 0)]]
SamplerCube irradianceMap;

[[vk::binding(8, 0)]]
SamplerCube prefilterMap;

[[vk::binding(9, 0)]]
Sampler2D brdfLut;

[[vk::binding(10, 0)]]
SamplerCube envMap;

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
    nointerpolation float mirrored : TEXCOORD4;
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
    float3 c0 = push.model[0].xyz;
    float3 c1 = push.model[1].xyz;
    float3 c2 = push.model[2].xyz;
    output.mirrored = dot(c0, cross(c1, c2)) < 0.0 ? -1.0 : 1.0;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 baseColor = material.baseColorFactor * float4(input.color, 1.0);
    if (material.flags0.x != 0)
        baseColor *= baseColorTex.Sample(slotUV(input.texCoord, 0));

    if (material.flags1.y == 1 && baseColor.a < material.emissiveFactor.w)
        discard;

    float metallic = material.factors.x;
    float roughness = material.factors.y;
    if (material.flags0.y != 0)
    {
        float4 mr = metalRoughTex.Sample(slotUV(input.texCoord, 1));
        roughness *= mr.g;
        metallic *= mr.b;
    }
    metallic = saturate(metallic);
    roughness = clamp(roughness, 0.045, 1.0);

    float ao = 1.0;
    if (material.flags0.w != 0)
        ao = 1.0 + material.factors.w * (occlusionTex.Sample(slotUV(input.texCoord, 3)).r - 1.0);

    int debugView = int(frame.debugParams.x + 0.5);

    bool frontFacing = (input.mirrored < 0.0) ? !isFrontFace : isFrontFace;

    float3 N = normalize(input.normal);
    if (material.flags0.z != 0 && debugView != 1)
    {
        float3 T = normalize(input.tangent.xyz);
        T = normalize(T - N * dot(N, T));
        float3 B = cross(N, T) * input.tangent.w;
        float3 tn = normalTex.Sample(slotUV(input.texCoord, 2)).xyz * 2.0 - 1.0;
        tn.xy *= material.factors.z;
        N = normalize(T * tn.x + B * tn.y + N * tn.z);
    }
    if (material.flags1.z != 0 && !frontFacing)
        N = -N;

    float3 dNdx = ddx(N);
    float3 dNdy = ddy(N);
    float normalVariance = 0.25 * (dot(dNdx, dNdx) + dot(dNdy, dNdy));
    float kernelRoughness = min(2.0 * normalVariance, 0.04);
    float baseAlpha = roughness * roughness;
    float filteredAlpha2 = saturate(baseAlpha * baseAlpha + kernelRoughness);
    roughness = clamp(pow(filteredAlpha2, 0.25), 0.045, 1.0);

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

    float3 radiance = float3(3.0, 3.0, 3.0) * frame.iblParams.z;
    float3 Lo = (diffuse + specular) * radiance * NdotL;

    float3 R = reflect(-V, N);
    R = normalize(lerp(R, N, roughness * roughness));
    float2 brdf = brdfLut.SampleLevel(float2(NdotV, roughness), 0.0).xy;
    float3 envBrdf = F0 * brdf.x + brdf.y;
    float specOcc = saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
    float3 prefiltered = prefilterMap.SampleLevel(R, roughness * frame.iblParams.x).rgb;
    float3 specularIBL = prefiltered * envBrdf * specOcc * frame.iblParams.y;
    float3 diffuseIBL = irradianceMap.SampleLevel(N, 0.0).rgb * baseColor.rgb
                        * saturate(1.0 - envBrdf) * (1.0 - metallic) * ao * frame.iblParams.y;

    float3 emissive = material.emissiveFactor.rgb;
    if (material.flags1.x != 0)
        emissive *= emissiveTex.Sample(slotUV(input.texCoord, 4)).rgb;

    float3 color = diffuseIBL + specularIBL + Lo + emissive;

    if (debugView == 2) return float4(N * 0.5 + 0.5, 1.0);
    if (debugView == 3) return float4(ao, ao, ao, 1.0);
    if (debugView == 4) return float4(baseColor.rgb, 1.0);
    if (debugView == 5) return float4(roughness, metallic, 0.0, 1.0);
    if (debugView == 6) return float4(saturate(specularIBL), 1.0);
    if (debugView == 7) return float4(saturate(diffuseIBL + Lo), 1.0);
    if (debugView == 8) return float4(frontFacing ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0), 1.0);
    if (debugView == 9) return float4(normalize(input.normal) * 0.5 + 0.5, 1.0);

    color = min(color, float3(60.0, 60.0, 60.0));

    return float4(color, 1.0);
}

struct SkyOutput {
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};

[shader("vertex")]
SkyOutput skyVertex(uint vid : SV_VertexID)
{
    SkyOutput output;
    float2 uv = float2(float((vid << 1) & 2), float(vid & 2));
    float2 ndc = uv * 2.0 - 1.0;
    output.pos = float4(ndc, 1.0, 1.0);
    float4 farPoint = mul(frame.invViewProj, float4(ndc, 1.0, 1.0));
    output.dir = farPoint.xyz / farPoint.w - frame.cameraPos.xyz;
    return output;
}

[shader("fragment")]
float4 skyFragment(SkyOutput input) : SV_Target
{
    if (int(frame.debugParams.x + 0.5) != 0)
        return float4(0.0, 0.0, 0.0, 1.0);

    float3 color = envMap.SampleLevel(normalize(input.dir), 0.0).rgb * frame.iblParams.y;
    return float4(min(color, float3(60.0, 60.0, 60.0)), 1.0);
}
