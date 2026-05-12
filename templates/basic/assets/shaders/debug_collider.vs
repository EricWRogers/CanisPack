[OPENGL VERSION]

layout(location = 0) in vec3 vertexPosition;

uniform mat4 P;
uniform mat4 V;

void main()
{
    gl_Position = P * V * vec4(vertexPosition, 1.0);
}
