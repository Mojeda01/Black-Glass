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
    float4 lightInfo;
    float4 lightPositions[16];
    float4 lightColors[16];
    float4 lightDirections[16];
};

struct MaterialData {
    float4 baseColorFactor;
    float4 emissiveFactor;
    float4 factors;
    int4 flags0;
    int4 flags1;
    float4 uvLinear[5];
    float4 uvOffset[5];
    float4 transmissionParams;
    float4 clearcoatParams;
    float4 worldUV;
};

struct PushData {
    float4x4 model;
    float4 highlight;
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

[[vk::binding(11, 0)]]
Sampler2DShadow shadowMap;

[[vk::binding(12, 0)]]
Sampler2D sceneColorTex;

[[vk::binding(13, 0)]]
Sampler2D aoTex;

[[vk::binding(14, 0)]]
Sampler2D reflectionTex;

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
    float3 col0 = mul(push.model, float4(1.0, 0.0, 0.0, 0.0)).xyz;
    float3 col1 = mul(push.model, float4(0.0, 1.0, 0.0, 0.0)).xyz;
    float3 col2 = mul(push.model, float4(0.0, 0.0, 1.0, 0.0)).xyz;
    float handed = dot(col0, cross(col1, col2)) < 0.0 ? -1.0 : 1.0;
    output.normal = (cross(col1, col2) * input.normal.x
                   + cross(col2, col0) * input.normal.y
                   + cross(col0, col1) * input.normal.z) * handed;
    output.tangent = float4(mul(push.model, float4(input.tangent.xyz, 0.0)).xyz, input.tangent.w);
    float3 c0 = push.model[0].xyz;
    float3 c1 = push.model[1].xyz;
    float3 c2 = push.model[2].xyz;
    output.mirrored = dot(c0, cross(c1, c2)) < 0.0 ? -1.0 : 1.0;
    return output;
}

[shader("vertex")]
float4 prepassVertex(VSInput input) : SV_Position
{
    float4 world = mul(push.model, float4(input.pos, 1.0));
    return mul(frame.proj, mul(frame.view, world));
}

float shadowFactor(float3 worldPos, float3 geometricNormal)
{
    if (frame.shadowParams.z < 0.5)
        return 1.0;

    float3 p = worldPos + geometricNormal * frame.shadowParams.y;
    float4 lightClip = mul(frame.lightViewProj, float4(p, 1.0));
    float3 ndc = lightClip.xyz / lightClip.w;
    float2 uv = ndc.xy * 0.5 + 0.5;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0;

    float texel = frame.shadowParams.x;
    float sum = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            sum += shadowMap.SampleCmpLevelZero(uv + float2(float(x), float(y)) * texel, ndc.z);
        }
    }
    return sum / 9.0;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float2 baseUV = input.texCoord;
    if (material.worldUV.x > 0.5)
    {
        float3 surfaceNormal = normalize(input.normal);
        float tileSize = max(material.worldUV.y, 0.01);
        if (abs(surfaceNormal.y) > 0.999)
        {
            baseUV = float2(input.worldPos.x, input.worldPos.z) / tileSize;
        }
        else
        {
            float3 uAxis = normalize(cross(float3(0.0, 1.0, 0.0), surfaceNormal));
            float3 vAxis = cross(surfaceNormal, uAxis);
            baseUV = float2(dot(input.worldPos, uAxis), -dot(input.worldPos, vAxis)) / tileSize;
        }
    }

    float4 baseColor = material.baseColorFactor * float4(input.color, 1.0);
    if (material.flags0.x != 0)
        baseColor *= baseColorTex.Sample(slotUV(baseUV, 0));

    if (material.flags1.y == 1 && baseColor.a < material.emissiveFactor.w)
        discard;

    float metallic = material.factors.x;
    float roughness = material.factors.y;
    if (material.flags0.y != 0)
    {
        float4 mr = metalRoughTex.Sample(slotUV(baseUV, 1));
        roughness *= mr.g;
        metallic *= mr.b;
    }
    metallic = saturate(metallic);
    roughness = clamp(roughness, 0.045, 1.0);
    if (material.flags1.w == 1)
        roughness = clamp(frame.effectParams.w, 0.045, 1.0);

    float ao = 1.0;
    if (material.flags0.w != 0)
        ao = 1.0 + material.factors.w * (occlusionTex.Sample(slotUV(baseUV, 3)).r - 1.0);

    bool reflectionPass = frame.passParams.x > 0.5;
    bool opaquePath = material.transmissionParams.x <= 0.0 && material.flags1.y != 2;
    if (frame.effectParams.x > 0.5 && opaquePath && !reflectionPass)
        ao *= aoTex.SampleLevel(input.pos.xy / frame.debugParams.zw, 0.0).r;

    int debugView = int(frame.debugParams.x + 0.5);

    bool frontFacing = (input.mirrored < 0.0) ? !isFrontFace : isFrontFace;
    if (reflectionPass)
        frontFacing = !frontFacing;

    float3 N = normalize(input.normal);
    if (material.flags0.z != 0 && debugView != 1)
    {
        float3 T = normalize(input.tangent.xyz);
        T = normalize(T - N * dot(N, T));
        float3 B = cross(N, T) * input.tangent.w;
        float3 tn = normalTex.Sample(slotUV(baseUV, 2)).xyz * 2.0 - 1.0;
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
    float3 L = normalize(frame.lightDir.xyz);
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

    float3 Ng = normalize(input.normal);
    if (material.flags1.z != 0 && !frontFacing)
        Ng = -Ng;

    bool isGround = material.flags1.w == 1;
    float shadow = shadowFactor(input.worldPos, Ng);

    float3 radiance = float3(1.0, 1.0, 1.0) * frame.lightDir.w * frame.iblParams.z;
    float3 diffuseLo = diffuse * radiance * NdotL * shadow;
    float3 specularLo = specular * radiance * NdotL * shadow;

    int pointLightCount = int(frame.lightInfo.x + 0.5);
    for (int li = 0; li < pointLightCount; li++)
    {
        float3 toLight = frame.lightPositions[li].xyz - input.worldPos;
        float lightRadius = max(frame.lightPositions[li].w, 0.1);
        float distSq = dot(toLight, toLight);
        float ratio = distSq / (lightRadius * lightRadius);
        float window = saturate(1.0 - ratio * ratio);
        float attenuation = window * window / (distSq + 1.0);
        float4 coneData = frame.lightDirections[li];
        if (coneData.w > -1.5)
        {
            float3 lightToPoint = -toLight * rsqrt(max(distSq, 1e-6));
            float cosAngle = dot(lightToPoint, coneData.xyz);
            float cosInner = frame.lightColors[li].w;
            float cone = saturate((cosAngle - coneData.w) / max(cosInner - coneData.w, 1e-3));
            attenuation *= cone * cone * (3.0 - 2.0 * cone);
        }
        if (attenuation > 0.0)
        {
            float3 Lp = toLight * rsqrt(max(distSq, 1e-6));
            float3 Hp = normalize(V + Lp);
            float NdotLp = saturate(dot(N, Lp));
            float NdotHp = saturate(dot(N, Hp));
            float VdotHp = saturate(dot(V, Hp));
            float denomP = NdotHp * NdotHp * (a2 - 1.0) + 1.0;
            float Dp = a2 / (PI * denomP * denomP);
            float Gp = (NdotLp / (NdotLp * (1.0 - k) + k)) * (NdotV / (NdotV * (1.0 - k) + k));
            float3 Fp = F0 + (1.0 - F0) * pow(1.0 - VdotHp, 5.0);
            float3 specP = Dp * Gp * Fp / (4.0 * NdotV * NdotLp + 1e-4);
            float3 kdP = (1.0 - Fp) * (1.0 - metallic);
            float3 lightRadiance = frame.lightColors[li].rgb * attenuation;
            diffuseLo += kdP * baseColor.rgb / PI * lightRadiance * NdotLp;
            specularLo += specP * lightRadiance * NdotLp;
        }
    }

    float3 Lo = diffuseLo + specularLo;

    float3 R = reflect(-V, N);
    R = normalize(lerp(R, N, roughness * roughness));
    float2 brdf = brdfLut.SampleLevel(float2(NdotV, roughness), 0.0).xy;
    float3 envBrdf = F0 * brdf.x + brdf.y;
    float specOcc = saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
    float3 prefiltered = prefilterMap.SampleLevel(R, roughness * frame.iblParams.x).rgb;
    float3 specularIBL = prefiltered * envBrdf * specOcc * frame.iblParams.y;
    float3 diffuseIBL = irradianceMap.SampleLevel(N, 0.0).rgb * baseColor.rgb
                        * saturate(1.0 - envBrdf) * (1.0 - metallic) * ao * frame.iblParams.y;

    if (isGround)
    {
        float footprintDist = 1000000.0;
        int boxCount = int(frame.groundExtent.x + 0.5);
        for (int i = 0; i < boxCount; i++)
        {
            float4 box = frame.groundBoxes[i];
            float2 q = abs(input.worldPos.xz - box.xy) - box.zw * 0.9;
            float d = length(max(q, float2(0.0, 0.0))) + min(max(q.x, q.y), 0.0);
            footprintDist = min(footprintDist, d);
        }
        float contact = lerp(0.3, 1.0, smoothstep(0.0, frame.groundParams.w, footprintDist));
        float groundShadow = lerp(1.0, shadow, 0.55);
        diffuseIBL *= contact * groundShadow;
        specularIBL *= contact * groundShadow;

        if (frame.effectParams.z > 0.0)
        {
            float2 screenUV = input.pos.xy / frame.debugParams.zw;
            float blurRadius = roughness * 0.02;
            float spin = frac(52.9829189 * frac(dot(input.pos.xy, float2(0.06711056, 0.00583715)))) * 6.2831853;
            float3 reflSum = reflectionTex.SampleLevel(screenUV, 0.0).rgb;
            for (int i = 0; i < 8; i++)
            {
                float angle = float(i) * 2.399963 + spin;
                float rr = sqrt((float(i) + 0.5) / 8.0) * blurRadius;
                reflSum += reflectionTex.SampleLevel(screenUV + float2(cos(angle), sin(angle)) * rr, 0.0).rgb;
            }
            float3 reflColor = reflSum / 9.0;
            specularIBL = reflColor * envBrdf * frame.effectParams.z;
        }
    }

    float ccFactor = material.clearcoatParams.x;
    if (ccFactor > 0.0)
    {
        float ccRoughness = clamp(material.clearcoatParams.y, 0.045, 1.0);
        float NdotVc = max(dot(Ng, V), 1e-4);
        float NdotLc = saturate(dot(Ng, L));
        float NdotHc = saturate(dot(Ng, H));

        float ccA = ccRoughness * ccRoughness;
        float ccA2 = ccA * ccA;
        float ccDenom = NdotHc * NdotHc * (ccA2 - 1.0) + 1.0;
        float ccD = ccA2 / (PI * ccDenom * ccDenom);
        float ccK = (ccRoughness + 1.0) * (ccRoughness + 1.0) / 8.0;
        float ccG = (NdotLc / (NdotLc * (1.0 - ccK) + ccK)) * (NdotVc / (NdotVc * (1.0 - ccK) + ccK));
        float ccFresnelDirect = 0.04 + 0.96 * pow(1.0 - VdotH, 5.0);
        float3 ccSpecularLo = float3(1.0, 1.0, 1.0) * (ccD * ccG * ccFresnelDirect / (4.0 * NdotVc * NdotLc + 1e-4))
                                * radiance * NdotLc * shadow * ccFactor;

        float3 Rc = reflect(-V, Ng);
        float2 ccBrdf = brdfLut.SampleLevel(float2(NdotVc, ccRoughness), 0.0).xy;
        float ccEnv = 0.04 * ccBrdf.x + ccBrdf.y;
        float3 ccPrefiltered = prefilterMap.SampleLevel(Rc, ccRoughness * frame.iblParams.x).rgb;
        float3 ccSpecularIBL = ccPrefiltered * ccEnv * frame.iblParams.y * ccFactor;

        float ccFresnelView = 0.04 + 0.96 * pow(1.0 - NdotVc, 5.0);
        float ccAttenuation = 1.0 - ccFactor * ccFresnelView;
        diffuseIBL *= ccAttenuation;
        specularIBL *= ccAttenuation;
        diffuseLo *= ccAttenuation;
        specularLo *= ccAttenuation;
        specularIBL += ccSpecularIBL;
        specularLo += ccSpecularLo;
    }

    float3 emissive = material.emissiveFactor.rgb;
    if (material.flags1.x != 0)
        emissive *= emissiveTex.Sample(slotUV(baseUV, 4)).rgb;
    emissive += push.highlight.rgb * push.highlight.w;

    float transmission = material.transmissionParams.x;
    bool blendMode = material.flags1.y == 2 && transmission <= 0.0;
    float alphaOut = blendMode ? saturate(baseColor.a) : 1.0;

    float3 transmitted = float3(0.0, 0.0, 0.0);
    if (transmission > 0.0)
    {
        float2 screenUV = input.pos.xy / frame.debugParams.zw;
        float lod = roughness * frame.debugParams.y;
        float3 sceneColor = sceneColorTex.SampleLevel(screenUV, lod).rgb;
        transmitted = sceneColor * baseColor.rgb * transmission
                        * saturate(1.0 - envBrdf) * (1.0 - metallic);
        diffuseIBL *= (1.0 - transmission);
        diffuseLo *= (1.0 - transmission);
    }

    float3 color;
    if (blendMode)
        color = (diffuseIBL + diffuseLo) * alphaOut + specularIBL + specularLo + emissive;
    else
        color = diffuseIBL + diffuseLo + specularIBL + specularLo + transmitted + emissive;

    if (debugView == 2) return float4(N * 0.5 + 0.5, 1.0);
    if (debugView == 3) return float4(ao, ao, ao, 1.0);
    if (debugView == 4) return float4(baseColor.rgb, 1.0);
    if (debugView == 5) return float4(roughness, metallic, 0.0, 1.0);
    if (debugView == 6) return float4(saturate(specularIBL), 1.0);
    if (debugView == 7) return float4(saturate(diffuseIBL + Lo), 1.0);
    if (debugView == 8) return float4(frontFacing ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0), 1.0);
    if (debugView == 9) return float4(normalize(input.normal) * 0.5 + 0.5, 1.0);

    if (isGround)
    {
        float3 skyColor = envMap.SampleLevel(normalize(input.worldPos - frame.cameraPos.xyz), 0.0).rgb
                            * frame.iblParams.y;
        float fade = smoothstep(0.3 * frame.groundParams.z, 0.9 * frame.groundParams.z,
                                length(input.worldPos.xz - frame.groundParams.xy));
        color = lerp(color, skyColor, fade);
    }

    color = min(color, float3(60.0, 60.0, 60.0));

    return float4(color, alphaOut);
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
