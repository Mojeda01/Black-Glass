struct FrameData {
    float4x4 view;
    float4x4 proj;
    float4 cameraPos;
    float4 debugParams;
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

float softRect(float2 p, float2 halfSize, float soft)
{
    float2 q = abs(p) - halfSize;
    float d = max(q.x, q.y);
    return 1.0 - smoothstep(-soft, soft, d);
}

float3 environment(float3 d, float roughness)
{
    float blur = 0.03 + roughness * roughness * 1.2;

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

float3 irradiance(float3 n)
{
    float3 skyAmbient = float3(0.31, 0.41, 0.63);
    float3 groundAmbient = float3(0.1, 0.09, 0.085);
    float t = n.y * 0.5 + 0.5;
    float3 c = lerp(groundAmbient, skyAmbient, t * t * (3.0 - 2.0 * t));
    c += float3(0.55, 0.5, 0.45) * saturate(dot(n, normalize(float3(0.5, 1.0, 0.3)))) * 0.35;
    c += float3(0.12, 0.14, 0.2) * saturate(n.x) + float3(0.2, 0.14, 0.1) * saturate(-n.x);
    return c;
}

float3 envBRDFApprox(float3 F0, float roughness, float NdotV)
{
    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
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

    float3 radiance = float3(3.0, 3.0, 3.0);
    float3 Lo = (diffuse + specular) * radiance * NdotL;

    float3 R = reflect(-V, N);
    R = normalize(lerp(R, N, roughness * roughness));
    float3 envBrdf = envBRDFApprox(F0, roughness, NdotV);
    float specOcc = saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
    float3 specularIBL = environment(R, roughness) * envBrdf * specOcc;
    float3 diffuseIBL = irradiance(N) * baseColor.rgb * (1.0 - envBrdf) * (1.0 - metallic) * ao;

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

    float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
    float mapped = lum / (lum + 1.0);
    float3 hueKept = color * (mapped / max(lum, 1e-4));
    float3 perChannel = color / (color + 1.0);
    color = saturate(lerp(hueKept, perChannel, mapped * mapped));

    return float4(color, 1.0);
}
