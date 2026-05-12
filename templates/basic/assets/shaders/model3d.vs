[OPENGL VERSION]

layout (location = 0) in vec3 vertexPosition;
layout (location = 1) in vec3 vertexNormal;
layout (location = 2) in vec2 vertexUV;
layout (location = 3) in mat4 instanceM;

uniform mat4 P;
uniform mat4 V;
uniform mat4 M;
uniform bool useInstanceMatrix;

out vec3 fragmentNormal;
out vec2 fragmentUV;
out vec3 fragmentWorldPos;

void main()
{
    mat4 modelMatrix = useInstanceMatrix ? instanceM : M;
    vec4 worldPos = modelMatrix * vec4(vertexPosition, 1.0);
    mat3 normalMatrix = mat3(transpose(inverse(modelMatrix)));
    fragmentNormal = normalize(normalMatrix * vertexNormal);
    fragmentUV = vec2(vertexUV.x, vertexUV.y);
    fragmentWorldPos = worldPos.xyz;

    gl_Position = P * V * worldPos;
}
