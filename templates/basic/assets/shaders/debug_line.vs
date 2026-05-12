[OPENGL VERSION]

#ifdef GL_ES
    precision mediump float;
#endif

layout(location = 0) in vec2 aPos;

void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
}
