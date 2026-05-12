[OPENGL VERSION]

//#ifdef GL_ES
//    precision mediump float;
//#endif

in vec3 fragmentPosition;
in vec4 fragmentColor;
in vec2 fragmentUV;

//This is the 3 component float vector that gets outputted to
//the screen for each pixel

out vec4 outFragColor;

uniform sampler2D mySampler;
uniform float TIME;

void main() {
	vec2 movedUV = fragmentUV;// + vec2(TIME, TIME);
	vec4 color = texture(mySampler, movedUV) * fragmentColor;

	if (color.a <= 0.0f)
		discard;

	outFragColor = color;
}