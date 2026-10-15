#pragma once

// All three passes share one vertex shader. `uv` is the normalised 0..1 coordinate used for our own
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

// Pass 1: copy the host input into the current slot of the frame-history ring buffer.
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

// Shared helpers for passes 2 and 3. hash12 is Dave Hoskins' "Hash without Sine" (MIT licence).
#define IMAGFX_GLSL_COMMON                                                                         \
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

// Pass 2: the "temporal" stage. Builds the image that is fed back into itself next frame.
//   glitch slices -> noise warp -> per-channel time delay + radial chroma split -> feedback trails
static const char kTrailFragmentShader[] = "#version 410 core\n"
	IMAGFX_GLSL_COMMON
	R"(
uniform sampler2D CurrentTex;  // unit 0: this frame
uniform sampler2D DelayRTex;   // unit 1: frame used for the red channel
uniform sampler2D DelayBTex;   // unit 2: frame used for the blue channel
uniform sampler2D FeedbackTex; // unit 3: previous output of this pass

uniform vec2 Resolution;
uniform float Time;
uniform float Phase; // 0..1 ramp, one cycle per sync period
uniform float Seed;  // changes once per sync period

uniform float Chroma;
uniform float Warp;
uniform float WarpScale;
uniform float Glitch;
uniform float Trails;
uniform float TrailZoom;
uniform float TrailRotate;
uniform float TrailHue;
uniform int TrailBlend; // 0 lighten, 1 screen, 2 add

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

vec3 hueRotate( vec3 c, float a )
{
	const vec3 k = vec3( 0.57735 );
	float ca     = cos( a );
	return c * ca + cross( k, c ) * sin( a ) + k * dot( k, c ) * ( 1.0 - ca );
}

void main()
{
	vec2 p       = uv;
	float aspect = Resolution.x / Resolution.y;

	// Horizontal slice displacement, triggered at the start of each cycle and decaying over it.
	float env = Glitch * pow( 1.0 - Phase, 3.0 );
	if( env > 0.001 )
	{
		float rows = floor( mix( 6.0, 40.0, hash12( vec2( Seed, 1.0 ) ) ) );
		float row  = floor( p.y * rows );
		if( hash12( vec2( row, Seed ) ) > 1.0 - env * 0.7 )
			p.x += ( hash12( vec2( row, Seed + 7.0 ) ) - 0.5 ) * 0.3 * env;
	}

	// Smooth value-noise displacement map, scrolling over time.
	if( Warp > 0.0 )
	{
		vec2 q = p * vec2( aspect, 1.0 ) * mix( 1.0, 14.0, WarpScale );
		vec2 d = vec2( vnoise( q + vec2( Time, 0.0 ) ), vnoise( q + vec2( 5.2, 1.3 - Time ) ) ) - 0.5;
		p += d * Warp * 0.08;
	}

	// RGB split: each channel can come from a different moment in time and a different radial offset.
	vec2 dir = ( p - 0.5 ) * Chroma * 0.06;
	vec4 g   = texture( CurrentTex, p );
	vec4 r   = texture( DelayRTex, p + dir );
	vec4 b   = texture( DelayBTex, p - dir );
	vec4 col = vec4( r.r, g.g, b.b, max( g.a, max( r.a, b.a ) ) );

	// Feedback: last frame's result, slightly zoomed / rotated / hue-shifted, blended under the new one.
	if( Trails > 0.0 )
	{
		vec2 c = uv - 0.5;
		c.x *= aspect;
		float s = sin( TrailRotate ), k = cos( TrailRotate );
		c = mat2( k, s, -s, k ) * c / TrailZoom;
		c.x /= aspect;
		vec2 fuv = c + 0.5;

		vec4 fb = texture( FeedbackTex, fuv );
		fb *= step( 0.0, fuv.x ) * step( fuv.x, 1.0 ) * step( 0.0, fuv.y ) * step( fuv.y, 1.0 );
		fb.rgb = max( hueRotate( fb.rgb, TrailHue ), 0.0 );
		fb *= Trails;

		if( TrailBlend == 0 )
			col = max( col, fb );
		else if( TrailBlend == 1 )
			col = col + fb - col * fb;
		else
			col = col + fb;
	}

	col     = clamp( col, 0.0, 1.0 );
	col.rgb = min( col.rgb, vec3( col.a ) );
	fragColor = col;
}
)";

// Pass 3: the "surface" stage. Effects that must NOT smear into the trails: sheen sweep, scanlines,
// static. Then dry/wet mix against the untouched input. Writes premultiplied output to the host.
static const char kFinishFragmentShader[] = "#version 410 core\n"
	IMAGFX_GLSL_COMMON
	R"(
uniform sampler2D TrailTex;    // unit 0: output of pass 2
uniform sampler2D OriginalTex; // unit 1: this frame, untouched

uniform vec2 Resolution;
uniform float Phase;
uniform float Frame;

uniform float Mix;
uniform float Sheen;
uniform float SheenWidth;
uniform float SheenAngle;
uniform float Scanlines;
uniform float Static;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

void main()
{
	float aspect = Resolution.x / Resolution.y;
	vec4 col     = texture( TrailTex, uv );

	// A soft bright band sweeping across the frame once per cycle. Scaled by alpha so that with a
	// keyed / masked camera only the subject catches the light.
	if( Sheen > 0.0 )
	{
		vec2 d       = vec2( cos( SheenAngle ), sin( SheenAngle ) );
		vec2 c       = ( uv - 0.5 ) * vec2( aspect, 1.0 );
		float extent = 0.5 * ( abs( d.x ) * aspect + abs( d.y ) ) + SheenWidth;
		float pos    = mix( -extent, extent, Phase );
		float band   = 1.0 - smoothstep( 0.0, SheenWidth, abs( dot( c, d ) - pos ) );
		col.rgb += band * band * Sheen * col.a;
	}

	if( Scanlines > 0.0 )
	{
		float line = 0.5 + 0.5 * sin( uv.y * Resolution.y * 3.14159265 );
		col.rgb *= 1.0 - Scanlines * 0.6 * line;
	}

	if( Static > 0.0 )
	{
		vec2 px     = floor( uv * Resolution );
		float grain = hash12( px + vec2( Frame * 17.13, Frame * 3.71 ) );
		float tear  = step( 0.992, hash12( vec2( floor( px.y / 3.0 ), Frame ) ) );
		col.rgb     = mix( col.rgb, vec3( grain ) * col.a, Static * 0.5 );
		col.rgb += tear * Static * 0.6 * col.a;
	}

	vec4 original = texture( OriginalTex, uv );
	col           = mix( original, clamp( col, 0.0, 1.0 ), Mix );
	col.rgb       = min( col.rgb, vec3( col.a ) );
	fragColor     = col;
}
)";
