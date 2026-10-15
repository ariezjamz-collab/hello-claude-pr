#pragma once

// Shaders shared by every IMAG plugin.

// All passes share one vertex shader. `uv` is the normalised 0..1 coordinate used for our own
// render targets; `inUV` is scaled by MaxUV because the host's input texture may be padded.
static const char kVertexShader[] = R"(#version 410 core
uniform vec2 MaxUV;

layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;
out vec2 inUV;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
	inUV        = vUV * MaxUV;
}
)";

// Copies the host input (scaled by MaxUV) into one of our own full-frame targets.
static const char kCopyFragmentShader[] = R"(#version 410 core
uniform sampler2D InputTexture;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

void main()
{
	fragColor = texture( InputTexture, inUV );
}
)";

// GLSL helpers shared by the plugins' shaders. hash12 is Dave Hoskins' "Hash without Sine" (MIT licence).
#define GLSL_NOISE_COMMON                                                                         \
	"float hash12( vec2 p )\n"                                                                     \
	"{\n"                                                                                          \
	"	vec3 p3 = fract( vec3( p.xyx ) * 0.1031 );\n"                                              \
	"	p3 += dot( p3, p3.yzx + 33.33 );\n"                                                        \
	"	return fract( ( p3.x + p3.y ) * p3.z );\n"                                                 \
	"}\n"                                                                                          \
	"float vnoise( vec2 p )\n"                                                                     \
	"{\n"                                                                                          \
	"	vec2 i = floor( p );\n"                                                                    \
	"	vec2 f = fract( p );\n"                                                                    \
	"	vec2 u = f * f * ( 3.0 - 2.0 * f );\n"                                                     \
	"	return mix( mix( hash12( i ), hash12( i + vec2( 1, 0 ) ), u.x ),\n"                        \
	"	            mix( hash12( i + vec2( 0, 1 ) ), hash12( i + vec2( 1, 1 ) ), u.x ), u.y );\n" \
	"}\n"
