[OPENGL VERSION]

in vec3 fragmentNormal;
in vec2 fragmentUV;
in vec3 fragmentWorldPos;

uniform sampler2D albedoMap;
uniform sampler2D specularMap;
uniform sampler2D roughnessMap;
uniform sampler2D metallicMap;
uniform sampler2D directionalShadowMap;

uniform bool useAlbedoMap;
uniform bool useSpecularMap;
uniform bool useRoughnessMap;
uniform bool useMetallicMap;
uniform bool useDirectionalLight;
uniform bool useDirectionalShadow;

uniform vec4 albedoValue;
uniform float specularValue;
uniform float roughnessValue;
uniform float metallicValue;

uniform vec3 cameraPosition;
uniform vec3 ambientLightColor;
uniform float ambientLightIntensity;

uniform vec3 directionalLightDirection;
uniform vec3 directionalLightColor;
uniform float directionalLightIntensity;
uniform mat4 directionalLightSpaceMatrix;

const int MAX_POINT_LIGHTS = 8;
uniform int pointLightCount;
uniform vec3 pointLightPositions[MAX_POINT_LIGHTS];
uniform vec3 pointLightColors[MAX_POINT_LIGHTS];
uniform float pointLightIntensities[MAX_POINT_LIGHTS];
uniform float pointLightRanges[MAX_POINT_LIGHTS];

out vec4 color;

vec3 SRGBToLinear(vec3 value)
{
    return pow(max(value, vec3(0.0)), vec3(2.2));
}

vec3 LinearToSRGB(vec3 value)
{
    return pow(max(value, vec3(0.0)), vec3(1.0 / 2.2));
}

vec3 ACESFilm(vec3 value)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((value * (a * value + b)) / (value * (c * value + d) + e), 0.0, 1.0);
}

vec3 EvaluateLightContribution(
    vec3 n,
    vec3 viewDir,
    vec3 lightDir,
    vec3 lightColor,
    float lightIntensity,
    float roughness,
    float specular,
    float metallic,
    vec3 albedoLinear)
{
    float diffuse = max(dot(n, lightDir), 0.0);
    vec3 halfDir = normalize(lightDir + viewDir);
    float specularTerm = pow(max(dot(n, halfDir), 0.0), 32.0);

    float diffuseLighting = diffuse * (1.0 - 0.4 * roughness);
    vec3 diffuseColor = albedoLinear * diffuseLighting * mix(1.0, 0.65, metallic);
    vec3 specColor = vec3(specular * specularTerm * (1.0 - 0.5 * roughness));
    return (diffuseColor + specColor) * lightColor * lightIntensity;
}

float ComputeDirectionalShadow(vec3 n, vec3 lightDir)
{
    if (!useDirectionalShadow)
        return 0.0;

    vec4 fragPosLightSpace = directionalLightSpaceMatrix * vec4(fragmentWorldPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / max(fragPosLightSpace.w, 0.00001);
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0)
        return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.0008 * (1.0 - dot(n, lightDir)), 0.0006);
    vec2 texelSize = 1.0 / vec2(textureSize(directionalShadowMap, 0));
    float shadow = 0.0;

    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            float sampleDepth = texture(directionalShadowMap, projCoords.xy + offset).r;
            shadow += (currentDepth - bias > sampleDepth) ? 1.0 : 0.0;
        }
    }

    return shadow / 9.0;
}

void main()
{
    vec3 n = normalize(fragmentNormal);
    vec3 viewDir = normalize(cameraPosition - fragmentWorldPos);

    vec4 albedoTex = useAlbedoMap ? texture(albedoMap, fragmentUV) : vec4(1.0);
    vec4 albedo = albedoValue * albedoTex;
    vec3 albedoLinear = SRGBToLinear(albedo.rgb);

    float specularTex = useSpecularMap ? texture(specularMap, fragmentUV).r : 1.0;
    float roughnessTex = useRoughnessMap ? texture(roughnessMap, fragmentUV).r : 1.0;
    float metallicTex = useMetallicMap ? texture(metallicMap, fragmentUV).r : 1.0;

    float specular = clamp(specularValue * specularTex, 0.0, 1.0);
    float roughness = clamp(roughnessValue * roughnessTex, 0.0, 1.0);
    float metallic = clamp(metallicValue * metallicTex, 0.0, 1.0);

    vec3 ambientLinear = clamp(ambientLightColor, vec3(0.0), vec3(1.0)) * max(ambientLightIntensity, 0.0);
    vec3 litColor = albedoLinear * ambientLinear;

    if (useDirectionalLight)
    {
        vec3 lightDir = normalize(-directionalLightDirection);
        float shadow = ComputeDirectionalShadow(n, lightDir);
        vec3 lightContribution = EvaluateLightContribution(n, viewDir, lightDir, directionalLightColor, directionalLightIntensity, roughness, specular, metallic, albedoLinear);
        litColor += lightContribution * (1.0 - (shadow * 0.95));
    }

    for (int i = 0; i < pointLightCount && i < MAX_POINT_LIGHTS; ++i)
    {
        vec3 toLight = pointLightPositions[i] - fragmentWorldPos;
        float distanceToLight = length(toLight);
        vec3 lightDir = (distanceToLight > 0.0001) ? (toLight / distanceToLight) : vec3(0.0, 1.0, 0.0);

        float attenuation = 1.0;
        if (pointLightRanges[i] > 0.0001)
        {
            float rangeFactor = clamp(1.0 - (distanceToLight / pointLightRanges[i]), 0.0, 1.0);
            attenuation = rangeFactor * rangeFactor;
        }

        vec3 lightColor = pointLightColors[i] * pointLightIntensities[i];
        litColor += EvaluateLightContribution(n, viewDir, lightDir, lightColor, attenuation, roughness, specular, metallic, albedoLinear);
    }

    color = vec4(LinearToSRGB(ACESFilm(litColor)), albedo.a);
}
