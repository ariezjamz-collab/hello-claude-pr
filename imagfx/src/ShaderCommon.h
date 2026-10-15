#pragma once

// Shaders shared by every IOKI plugin.

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

// Heat (0..1.5) to flame colour. palette: 0 fire, 1 blue, 2 toxic green, 3 violet, 4 white.
#define GLSL_FLAME_PALETTE \
	"vec3 flamePalette( float h, int palette )\n" \
	"{\n" \
	"	h = clamp( h, 0.0, 1.5 );\n" \
	"	float lo  = smoothstep( 0.0, 0.5, h );\n" \
	"	float mid = smoothstep( 0.25, 0.85, h );\n" \
	"	float hi  = smoothstep( 0.6, 1.2, h );\n" \
	"	vec3 c;\n" \
	"	if( palette == 0 )\n" \
	"		c = vec3( lo, mid, hi );// fire\n" \
	"	else if( palette == 1 )\n" \
	"		c = vec3( mid * 0.35 + hi * 0.65, mid * 0.75 + hi * 0.25, lo * 1.3 );// blue, with a cyan-white core\n" \
	"	else if( palette == 2 )\n" \
	"		c = vec3( mid, lo, hi * 0.6 );// toxic green\n" \
	"	else if( palette == 3 )\n" \
	"		c = vec3( smoothstep( 0.0, 0.6, h ), hi, smoothstep( 0.0, 0.4, h ) );// violet\n" \
	"	else\n" \
	"		c = vec3( smoothstep( 0.0, 0.8, h ) );// white\n" \
	"	return c * smoothstep( 0.03, 0.2, h );\n" \
	"}\n"

// Tracking squares drawn from a mipmapped mask-moments texture. Expects these declared before it:
//   in vec2 uv; uniform vec2 Resolution; uniform sampler2D MomentTex; uniform int GridLevel; uniform float SquareSize;
// and GLSL_NOISE_COMMON (for hash12).
#define GLSL_TRACKING_SQUARES \
	"float hudLine( float distance, float px )\n" \
	"{\n" \
	"	return 1.0 - smoothstep( px * 0.5, px * 1.5, distance );\n" \
	"}\n" \
	"\n" \
	"float segmentDistance( vec2 p, vec2 a, vec2 b )\n" \
	"{\n" \
	"	vec2 ab = b - a;\n" \
	"	float t = clamp( dot( p - a, ab ) / max( dot( ab, ab ), 1e-6 ), 0.0, 1.0 );\n" \
	"	return length( p - a - ab * t );\n" \
	"}\n" \
	"\n" \
	"// A grid cell carries a tracking square if the subject's outline passes through it (partial coverage),\n" \
	"// plus a sparse random sprinkle of fully covered cells, so squares trace the silhouette instead of\n" \
	"// tiling the body.\n" \
	"bool squareCell( vec4 m, ivec2 c )\n" \
	"{\n" \
	"	return m.z > 0.08 && ( m.z < 0.85 || hash12( vec2( c ) * 1.37 ) < 0.15 );\n" \
	"}\n" \
	"\n" \
	"// Small squares, each centred on the subject's centroid within its grid cell. With links, neighbouring\n" \
	"// square cells are joined by thin lines.\n" \
	"float drawSquares( bool links )\n" \
	"{\n" \
	"	int cells    = 256 >> GridLevel;\n" \
	"	vec2 cellPx  = Resolution / float( cells );\n" \
	"	ivec2 cell   = ivec2( floor( uv * float( cells ) ) );\n" \
	"	vec2 pixel   = uv * Resolution;\n" \
	"	float px     = 1.0;\n" \
	"	float result = 0.0;\n" \
	"	for( int y = -1; y <= 1; ++y )\n" \
	"		for( int x = -1; x <= 1; ++x )\n" \
	"		{\n" \
	"			ivec2 c = cell + ivec2( x, y );\n" \
	"			if( any( lessThan( c, ivec2( 0 ) ) ) || any( greaterThanEqual( c, ivec2( cells ) ) ) )\n" \
	"				continue;\n" \
	"			vec4 m = texelFetch( MomentTex, c, GridLevel );\n" \
	"			if( !squareCell( m, c ) )\n" \
	"				continue;\n" \
	"			vec2 centre    = m.xy / m.z * Resolution;\n" \
	"			float halfSize = min( SquareSize, 0.42 * min( cellPx.x, cellPx.y ) );\n" \
	"			vec2 d      = abs( pixel - centre );\n" \
	"			float box   = max( d.x, d.y );\n" \
	"			result      = max( result, hudLine( abs( box - halfSize ), px ) );\n" \
	"			result      = max( result, 1.0 - smoothstep( 0.8, 1.6, length( pixel - centre ) ) );// centre dot\n" \
	"			if( links )\n" \
	"			{\n" \
	"				for( int n = 0; n < 2; ++n )\n" \
	"				{\n" \
	"					ivec2 nc = c + ( n == 0 ? ivec2( 1, 0 ) : ivec2( 0, 1 ) );\n" \
	"					if( any( greaterThanEqual( nc, ivec2( cells ) ) ) )\n" \
	"						continue;\n" \
	"					vec4 nm = texelFetch( MomentTex, nc, GridLevel );\n" \
	"					if( !squareCell( nm, nc ) )\n" \
	"						continue;\n" \
	"					vec2 other = nm.xy / nm.z * Resolution;\n" \
	"					result     = max( result, 0.55 * hudLine( segmentDistance( pixel, centre, other ), px * 0.7 ) );\n" \
	"				}\n" \
	"			}\n" \
	"		}\n" \
	"	return result;\n" \
	"}\n"
