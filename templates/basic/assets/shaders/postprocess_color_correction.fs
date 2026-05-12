[OPENGL VERSION]

in vec2 fragmentUV;

uniform sampler2D inputTexture;
uniform float exposure;
uniform float contrast;
uniform float saturation;

out vec4 color;

vec3 ApplySaturation(vec3 value, float amount)
{
    float luminance = dot(value, vec3(0.2126, 0.7152, 0.0722));
    return mix(vec3(luminance), value, amount);
}

void main()
{
    vec4 source = texture(inputTexture, fragmentUV);
    vec3 adjusted = source.rgb;
    adjusted = (adjusted - vec3(0.5)) * contrast + vec3(0.5);
    adjusted = ApplySaturation(adjusted, saturation);
    adjusted *= exposure;
    color = vec4(clamp(adjusted, vec3(0.0), vec3(1.0)), source.a);
}
