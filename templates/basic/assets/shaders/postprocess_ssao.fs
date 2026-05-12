[OPENGL VERSION]

in vec2 fragmentUV;

uniform sampler2D inputTexture;
uniform sampler2D sceneDepthTexture;
uniform mat4 inverseProjection;
uniform vec2 texelSize;
uniform float ssaoRadius;
uniform float ssaoBias;
uniform float ssaoStrength;

out vec4 color;

vec3 ReconstructViewPosition(vec2 uv, float depthValue)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depthValue * 2.0 - 1.0, 1.0);
    vec4 view = inverseProjection * clip;
    return view.xyz / max(view.w, 0.00001);
}

void main()
{
    vec4 source = texture(inputTexture, fragmentUV);
    float centerDepth = texture(sceneDepthTexture, fragmentUV).r;
    vec3 centerPosition = ReconstructViewPosition(fragmentUV, centerDepth);
    float occlusion = 0.0;
    int sampleCount = 0;

    const vec2 sampleOffsets[8] = vec2[](
        vec2( 1.0,  0.0),
        vec2(-1.0,  0.0),
        vec2( 0.0,  1.0),
        vec2( 0.0, -1.0),
        vec2( 1.0,  1.0),
        vec2(-1.0,  1.0),
        vec2( 1.0, -1.0),
        vec2(-1.0, -1.0)
    );

    for (int i = 0; i < 8; ++i)
    {
        vec2 sampleUV = fragmentUV + (sampleOffsets[i] * texelSize * ssaoRadius);
        float sampleDepth = texture(sceneDepthTexture, sampleUV).r;
        vec3 samplePosition = ReconstructViewPosition(sampleUV, sampleDepth);

        float depthDifference = samplePosition.z - centerPosition.z;
        float rangeCheck = smoothstep(0.0, ssaoRadius, abs(depthDifference));
        occlusion += step(ssaoBias, depthDifference) * rangeCheck;
        sampleCount++;
    }

    if (sampleCount > 0)
        occlusion /= float(sampleCount);

    float ambient = clamp(1.0 - (occlusion * ssaoStrength), 0.0, 1.0);
    color = vec4(source.rgb * ambient, source.a);
}
