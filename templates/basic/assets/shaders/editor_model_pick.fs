[OPENGL VERSION]

#ifdef GL_ES
    precision mediump float;
    precision mediump int;
#endif

uniform vec4 albedoValue;

out vec4 color;

void main()
{
    color = albedoValue;
}
