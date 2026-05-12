[OPENGL VERSION]

//#ifdef GL_ES
//    precision mediump float;
//#endif

layout (location = 0) in vec3 vertexPosition;
layout (location = 1) in vec4 vertexColor;
layout (location = 2) in vec2 vertexUV;

out vec3 fragmentPosition;
out vec4 fragmentColor;
out vec2 fragmentUV;

uniform mat4 P;
uniform float TIME;

void main()
{
    /*gl_Position = P * vec4(vertexPosition, 1.0);
    fragmentPosition = vertexPosition;
    fragmentColor = vertexColor;
    fragmentUV = vec2(vertexUV.x, 1.0 - vertexUV.y);*/

    gl_Position.xyz = (P * vec4(vertexPosition, 1.0)).xyz;
    gl_Position.w = 1.0;
    
    fragmentPosition = vertexPosition;
    fragmentColor = vertexColor;
    fragmentUV = vec2(vertexUV.x, 1.0 - vertexUV.y);
}

/*
glyphs[0]->topLeft.uv = Vector2(1.0f,0.0f);
            vertices[cv++] = glyphs[0]->topLeft;
            glyphs[0]->bottomLeft.uv = Vector2(0.0f,0.0f);
            vertices[cv++] = glyphs[0]->bottomLeft;
            glyphs[0]->bottomRight.uv = Vector2(0.0f,1.0f);
            vertices[cv++] = glyphs[0]->bottomRight;
            glyphs[0]->topLeft.uv = Vector2(0.0f,1.0f);
            vertices[cv++] = glyphs[0]->topLeft;
            glyphs[0]->bottomRight.uv = Vector2(1.0f,0.0f);
            vertices[cv++] = glyphs[0]->bottomRight;
            glyphs[0]->topRight.uv = Vector2(1.0f,1.0f);
            vertices[cv++] = glyphs[0]->topRight;
*/