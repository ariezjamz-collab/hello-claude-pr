#pragma once
#include "ShaderCommon.h"

// IMAG Key shader passes, in the order they run each frame:
//   plate update -> raw matte -> erode -> dilate -> feather + temporal smoothing -> output

// Background plate: while learning, a running average of the frame (weight 1/n); afterwards it slowly
// follows the frame where the key says there is background, to absorb lighting drift.
static const char kPlateFragmentShader[] = R"(#version 410 core
uniform sampler2D FrameTex;
uniform sampler2D PlateTex;
uniform sampler2D MatteTex;
uniform float LearnWeight;// > 0 while learning
uniform float UpdateRate;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

void main()
{
	vec4 frame = texture( FrameTex, uv );
	vec4 plate = texture( PlateTex, uv );
	float w    = LearnWeight > 0.0 ? LearnWeight : UpdateRate * ( 1.0 - texture( MatteTex, uv ).r );
	fragColor  = mix( plate, frame, w );
}
)";

// Raw matte (1 = subject) from one of three keyers, averaged over a small neighbourhood to calm noise,
// then cut by the garbage-matte crop.
static const char kRawMatteFragmentShader[] = R"(#version 410 core
uniform sampler2D FrameTex;
uniform sampler2D PlateTex;
uniform vec2 Texel;
uniform int Mode;            // 0 difference, 1 chroma, 2 luma
uniform float Threshold;
uniform float Softness;
uniform float ShadowTolerance;
uniform vec3 KeyColor;
uniform bool LumaInvert;
uniform vec4 Crop;           // left, right, bottom, top (fractions of the frame to remove)

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

float luma( vec3 c )
{
	return dot( c, vec3( 0.2126, 0.7152, 0.0722 ) );
}

// Colour-difference keying, as in broadcast keyers: a pixel is screen when the key's dominant channel
// (green for a green screen, blue for a blue one) rises clearly above the other two. Unlike matching one
// exact colour, this holds up across shades, brightness and uneven screen lighting.
float screenness( vec3 c )
{
	if( KeyColor.g >= KeyColor.r && KeyColor.g >= KeyColor.b )
		return c.g - max( c.r, c.b );
	if( KeyColor.b >= KeyColor.r )
		return c.b - max( c.r, c.g );
	return c.r - max( c.g, c.b );
}

// How strongly a pixel is "subject" before thresholding.
float signal( vec2 p )
{
	vec3 c = texture( FrameTex, p ).rgb;
	if( Mode == 1 )
		return 1.0 - clamp( screenness( c ) * 3.0, 0.0, 1.0 );
	if( Mode == 2 )
		return LumaInvert ? 1.0 - luma( c ) : luma( c );

	// Difference from the learned empty stage. Chroma difference ignores shadows and lighting changes but
	// is unreliable in the dark, so it is weighted by brightness; luma difference is scaled down by the
	// shadow tolerance.
	vec3 b       = texture( PlateTex, p ).rgb;
	float lc     = luma( c ), lb = luma( b );
	vec3 cn      = c / max( c.r + c.g + c.b, 1e-3 );
	vec3 bn      = b / max( b.r + b.g + b.b, 1e-3 );
	float trust  = clamp( min( lc, lb ) * 6.0, 0.0, 1.0 );
	float chromaD = length( cn - bn ) * 3.0 * trust;
	float lumaD   = abs( lc - lb ) * 2.0 * ( 1.0 - ShadowTolerance * 0.85 );
	return max( chromaD, lumaD );
}

void main()
{
	float s = signal( uv ) * 0.4;
	s += ( signal( uv + vec2( Texel.x, 0.0 ) ) + signal( uv - vec2( Texel.x, 0.0 ) ) +
		   signal( uv + vec2( 0.0, Texel.y ) ) + signal( uv - vec2( 0.0, Texel.y ) ) ) * 0.15;
	float m = smoothstep( Threshold, Threshold + max( Softness, 0.002 ), s );

	vec4 keep = step( vec4( Crop.x, Crop.z, uv.x, uv.y ), vec4( uv.x, uv.y, 1.0 - Crop.y, 1.0 - Crop.w ) );
	m *= keep.x * keep.y * keep.z * keep.w;
	fragColor = vec4( m, 0.0, 0.0, 1.0 );
}
)";

// Morphology: minimum (erode) or maximum (dilate) over a disc of the given radius, sampled on two rings.
static const char kMorphFragmentShader[] = R"(#version 410 core
uniform sampler2D MatteTex;
uniform vec2 Texel;
uniform float Radius;// pixels
uniform bool Dilate;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

void main()
{
	float v = texture( MatteTex, uv ).r;
	if( Radius >= 0.5 )
	{
		for( int i = 0; i < 12; ++i )
		{
			float a  = float( i ) * 0.5235988;
			vec2 dir = vec2( cos( a ), sin( a ) ) * Texel;
			float o  = texture( MatteTex, uv + dir * Radius ).r;
			float h  = texture( MatteTex, uv + dir * Radius * 0.5 ).r;
			v        = Dilate ? max( v, max( o, h ) ) : min( v, min( o, h ) );
		}
	}
	fragColor = vec4( v, 0.0, 0.0, 1.0 );
}
)";

// Feather (soft edge blur) and temporal smoothing against the previous final matte, to stop edge flicker.
static const char kFeatherFragmentShader[] = R"(#version 410 core
uniform sampler2D MatteTex;
uniform sampler2D PreviousTex;
uniform vec2 Texel;
uniform float Feather;// pixels
uniform float Temporal;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

void main()
{
	float v = texture( MatteTex, uv ).r;
	if( Feather >= 0.5 )
	{
		float sum = v * 2.0, weight = 2.0;
		for( int ring = 1; ring <= 2; ++ring )
			for( int i = 0; i < 8; ++i )
			{
				float a = float( i ) * 0.7853982 + float( ring ) * 0.39;
				float r = Feather * float( ring ) * 0.5;
				float w = ring == 1 ? 1.0 : 0.6;
				sum += texture( MatteTex, uv + vec2( cos( a ), sin( a ) ) * Texel * r ).r * w;
				weight += w;
			}
		v = sum / weight;
	}
	float previous = texture( PreviousTex, uv ).r;
	fragColor      = vec4( mix( v, previous, Temporal ), 0.0, 0.0, 1.0 );
}
)";

// Final output. Output view: premultiplied subject over the chosen background. Matte view: black and
// white key. Overlay view: subject as-is, keyed-out area tinted red, for checking the key during setup.
static const char kKeyOutputFragmentShader[] = R"(#version 410 core
uniform sampler2D FrameTex;
uniform sampler2D MatteTex;
uniform int View;        // 0 output, 1 matte, 2 overlay
uniform int Background;  // 0 transparent, 1 black, 2 colour
uniform vec3 BackgroundColor;
uniform int Mode;
uniform vec3 KeyColor;
uniform float Spill;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

void main()
{
	vec4 frame = texture( FrameTex, uv );
	float m    = clamp( texture( MatteTex, uv ).r, 0.0, 1.0 );

	if( View == 1 )
	{
		fragColor = vec4( vec3( m ), 1.0 );
		return;
	}
	if( View == 2 )
	{
		vec3 tinted = mix( frame.rgb * 0.35, vec3( 0.8, 0.1, 0.1 ), 0.35 );
		fragColor   = vec4( mix( tinted, frame.rgb, m ), 1.0 );
		return;
	}

	// Spill suppression for chroma keys: pull the key's dominant channel down to the larger of the others.
	vec3 c = frame.rgb;
	if( Mode == 1 && Spill > 0.0 )
	{
		if( KeyColor.g >= KeyColor.r && KeyColor.g >= KeyColor.b )
			c.g = mix( c.g, min( c.g, max( c.r, c.b ) ), Spill );
		else if( KeyColor.b >= KeyColor.r )
			c.b = mix( c.b, min( c.b, max( c.r, c.g ) ), Spill );
		else
			c.r = mix( c.r, min( c.r, max( c.g, c.b ) ), Spill );
	}

	float alpha = m * frame.a;
	vec4 subject = vec4( c * alpha, alpha );// premultiplied
	if( Background == 0 )
		fragColor = subject;
	else
	{
		vec3 back = Background == 1 ? vec3( 0.0 ) : BackgroundColor;
		fragColor = vec4( subject.rgb + back * ( 1.0 - alpha ), 1.0 );
	}
}
)";
