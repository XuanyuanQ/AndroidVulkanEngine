#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexcoord0;
layout(location = 2) in vec3 vWorldNormal;
layout(location = 3) in vec4 inShadowCoord;
layout(location = 4) in vec3 vWorldPosition;
layout(location = 5) in vec4 vWorldTangent;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D u_ShadowMap;

layout(set = 0, binding = 0) uniform FrameUbo {
    mat4 view_projection;
    mat4 shadowViewProj;
    vec4 camera_position;
    vec4 light_position_range;
    vec4 light_direction_type;
    vec4 light_color_intensity;
    vec4 ambient_color;
    vec4 clear_color;
    mat4 view;
    mat4 projection;
} frame;

layout(set = 1, binding = 0) uniform MaterialUbo {
    vec4 base_color;
    // x = metallic, y = roughness, z = receivesShadow, w = normalScale
    vec4 params;
} material;

layout(set = 1, binding = 1) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 2) uniform sampler2D normalTexture;
layout(set = 1, binding = 3) uniform sampler2D metallicRoughnessTexture;

layout(set = 0, binding = 2) uniform samplerCube environmentMap;
layout(set = 0, binding = 3) uniform samplerCube irradianceMap;
layout(set = 0, binding = 4) uniform samplerCube prefilterMap;
layout(set = 0, binding = 5) uniform sampler2D brdfLut;

vec4 aveUserColor;

// __AVE_USER_FRAGMENT__

const float AVE_PI = 3.14159265359;

float AveDistributionGGX(vec3 normal, vec3 halfVector, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float nDotH = max(dot(normal, halfVector), 0.0);
    float nDotH2 = nDotH * nDotH;
    float denom = (nDotH2 * (alpha2 - 1.0) + 1.0);
    return alpha2 / max(AVE_PI * denom * denom, 0.0001);
}

float AveGeometrySchlickGGX(float nDotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

float AveGeometrySmith(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness) {
    float nDotV = max(dot(normal, viewDir), 0.0);
    float nDotL = max(dot(normal, lightDir), 0.0);
    return AveGeometrySchlickGGX(nDotV, roughness) * AveGeometrySchlickGGX(nDotL, roughness);
}

vec3 AveFresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 AveEnvironmentSkyColor() {
    vec3 baseSky = mix(frame.clear_color.rgb, frame.ambient_color.rgb, 0.35);
    return baseSky * vec3(1.75, 1.65, 1.55);
}

vec3 AveEnvironmentHorizonColor() {
    vec3 baseHorizon = mix(frame.clear_color.rgb, frame.ambient_color.rgb, 0.55);
    return baseHorizon * vec3(1.05, 1.00, 0.95);
}

vec3 AveEnvironmentGroundColor() {
    vec3 baseGround = mix(frame.clear_color.rgb, frame.ambient_color.rgb, 0.75);
    return baseGround * vec3(0.32, 0.30, 0.28);
}

vec3 AveSampleEnvironment(vec3 direction) {
    float hemi = clamp(direction.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(AveEnvironmentHorizonColor(), AveEnvironmentSkyColor(), smoothstep(0.10, 0.95, hemi));
    vec3 ground = mix(AveEnvironmentGroundColor(), AveEnvironmentHorizonColor(), smoothstep(0.0, 0.5, hemi));
    return mix(ground, sky, hemi);
}

vec3 AveEvaluateEnvironmentDiffuse(vec3 normal) {
    return texture(irradianceMap, normal).rgb;
}

vec3 AveEvaluateEnvironmentSpecular(vec3 normal, vec3 viewDir, vec3 albedo, float metallic, float roughness) {
    vec3 reflectionDir = reflect(-viewDir, normal);
    float lodCount = 6.0;
    vec3 prefilteredColor = textureLod(prefilterMap, reflectionDir, roughness * lodCount).rgb;
    vec2 brdf = texture(brdfLut, vec2(clamp(dot(normal, viewDir), 0.0, 1.0), roughness)).rg;
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    return prefilteredColor * (f0 * brdf.x + brdf.y) * (0.65 + 0.35 * (1.0 - roughness));
}

float AveComputeShadow(vec4 shadowCoord) {
    if (material.params.z < 0.5) {
        return 1.0;
    }

    vec3 projCoord = shadowCoord.xyz / shadowCoord.w;
    projCoord.xy = projCoord.xy * 0.5 + 0.5;
    float currentDepth = projCoord.z;

    if (shadowCoord.w <= 0.0 ||
        currentDepth < 0.0 || currentDepth > 1.0 ||
        projCoord.x < 0.0 || projCoord.x > 1.0 ||
        projCoord.y < 0.0 || projCoord.y > 1.0) {
        return 1.0;
    }

    float closestDepth = texture(u_ShadowMap, projCoord.xy).r;
    float bias = 0.03;
    return (currentDepth - bias) <= closestDepth ? 1.0 : 0.0;
}

void main() {
    aveUserColor = vec4(1.0);
    AveUserFragmentMain();

    vec3 albedo = max(aveUserColor.rgb, vec3(0.0));
    vec4 mrSample = texture(metallicRoughnessTexture, vTexcoord0);
    float metallic = clamp(material.params.x * mrSample.b, 0.0, 1.0);
    float roughness = clamp(material.params.y * mrSample.g, 0.04, 1.0);
    float normalScale = max(material.params.w, 0.0);

    vec3 geometricNormal = normalize(vWorldNormal);
    vec3 tangent = normalize(vWorldTangent.xyz);
    tangent = normalize(tangent - geometricNormal * dot(geometricNormal, tangent));
    vec3 bitangent = normalize(cross(geometricNormal, tangent)) * vWorldTangent.w;
    mat3 tbn = mat3(tangent, bitangent, geometricNormal);
    vec3 sampledNormal = texture(normalTexture, vTexcoord0).xyz * 2.0 - 1.0;
    sampledNormal.xy *= normalScale;
    vec3 normal = normalize(tbn * sampledNormal);
    vec3 viewDir = normalize(frame.camera_position.xyz - vWorldPosition);

    vec3 lightDir;
    float attenuation = 1.0;
    if (frame.light_direction_type.w < 0.5) {
        lightDir = normalize(-frame.light_direction_type.xyz);
    } else {
        vec3 toLight = frame.light_position_range.xyz - vWorldPosition;
        float distanceToLight = length(toLight);
        lightDir = distanceToLight > 0.0001 ? toLight / distanceToLight : vec3(0.0, 1.0, 0.0);
        float range = max(frame.light_position_range.w, 0.0001);
        float normalizedDistance = clamp(distanceToLight / range, 0.0, 1.0);
        attenuation = 1.0 / max(distanceToLight * distanceToLight, 1.0);
        attenuation *= (1.0 - normalizedDistance) * (1.0 - normalizedDistance);
    }

    vec3 halfVector = normalize(viewDir + lightDir);
    float nDotL = max(dot(normal, lightDir), 0.0);
    float nDotV = max(dot(normal, viewDir), 0.0);
    float hDotV = max(dot(halfVector, viewDir), 0.0);

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = AveFresnelSchlick(hDotV, f0);
    float distribution = AveDistributionGGX(normal, halfVector, roughness);
    float geometry = AveGeometrySmith(normal, viewDir, lightDir, roughness);

    vec3 specular = (distribution * geometry * fresnel) / max(4.0 * nDotV * nDotL, 0.0001);
    vec3 diffuse = (vec3(1.0) - fresnel) * (1.0 - metallic) * albedo / AVE_PI;
    vec3 radiance = frame.light_color_intensity.rgb * frame.light_color_intensity.w * attenuation;

    float shadowFactor = AveComputeShadow(inShadowCoord);
    vec3 directLighting = (diffuse + specular) * radiance * nDotL * shadowFactor;

    // Keep indirect light intentionally conservative so the shadowed side remains readable.
    vec3 environmentDiffuse = AveEvaluateEnvironmentDiffuse(geometricNormal) * albedo * (0.30 + 0.20 * (1.0 - metallic));
    vec3 environmentSpecular = AveEvaluateEnvironmentSpecular(normal, viewDir, albedo, metallic, roughness) * 0.75;
    vec3 ambientDiffuse = frame.ambient_color.rgb * albedo * (0.08 + 0.40 * (1.0 - metallic));
    float indirectShadow = mix(0.35, 1.0, shadowFactor);
    vec3 color = ambientDiffuse + environmentDiffuse*indirectShadow + environmentSpecular*indirectShadow  + directLighting;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, aveUserColor.a);
}
