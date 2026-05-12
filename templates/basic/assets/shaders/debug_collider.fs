[OPENGL VERSION]

#ifdef GL_ES
    precision mediump float;
#endif

uniform vec4 lineColor;

out vec4 color;

void main()
{
    color = lineColor;
}
