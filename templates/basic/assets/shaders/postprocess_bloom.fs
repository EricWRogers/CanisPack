[OPENGL VERSION]

in vec2 fragmentUV;

uniform sampler2D inputTexture;
uniform vec2 texelSize;
uniform float bloomThreshold;
uniform float bloomIntensity;

out vec4 color;

void main()
{
    vec3 source = texture(inputTexture, fragmentUV).rgb;
    vec3 bloom = vec3(0.0);
    float totalWeight = 0.0;

    for (int y = -2; y <= 2; ++y)
    {
        for (int x = -2; x <= 2; ++x)
        {
            float weight = 1.0 - (abs(float(x)) + abs(float(y))) * 0.125;
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            vec3 sampleColor = texture(inputTexture, fragmentUV + offset).rgb;
            vec3 brightColor = max(sampleColor - vec3(bloomThreshold), vec3(0.0));
            bloom += brightColor * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0)
        bloom /= totalWeight;

    color = vec4(clamp(source + (bloom * bloomIntensity), vec3(0.0), vec3(1.0)), 1.0);
}
