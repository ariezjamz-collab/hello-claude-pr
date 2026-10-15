#pragma once
#include "ShaderCommon.h"

// IMAG Flame shader passes, in the order they run each frame:
//   mask -> moments -> tracker -> fire -> composite

// Subject mask at simulation resolution. A blurred 3x3 gather of one of four subject signals, thresholded.
static const char kMaskFragmentShader[] = R"(#version 410 core
uniform sampler2D CurrentTex;  // unit 0: this frame
uniform sampler2D PreviousTex; // unit 1: last frame
uniform vec2 FrameTexel;       // 1 / full-resolution size
uniform int Source;            // 0 motion, 1 keyed alpha, 2 brightness, 3 motion inside key
uniform float Threshold;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

float luma( vec3 c )
{
	return dot( c, vec3( 0.2126, 0.7152, 0.0722 ) );
}

float subjectSignal( vec2 p )
{
	vec4 now    = texture( CurrentTex, p );
	vec4 before = texture( PreviousTex, p );
	float motion = abs( luma( now.rgb ) - luma( before.rgb ) ) * 4.0;
	if( Source == 0 )
		return motion;
	if( Source == 1 )
		return now.a;
	if( Source == 2 )
		return luma( now.rgb );
	return motion * now.a;
}

void main()
{
	float sum = 0.0;
	for( int y = -1; y <= 1; ++y )
		for( int x = -1; x <= 1; ++x )
			sum += subjectSignal( uv + vec2( x, y ) * FrameTexel * 1.5 );
	float signal = sum / 9.0;
	fragColor    = vec4( smoothstep( Threshold, Threshold + 0.15, signal ), 0.0, 0.0, 1.0 );
}
)";

// Writes per-pixel moments of the mask into two float targets. After mipmapping, the 1x1 level holds the
// mask's mean, from which the tracker recovers its centroid and spread with no CPU readback.
static const char kMomentsFragmentShader[] = R"(#version 410 core
uniform sampler2D MaskTex;

in vec2 uv;
in vec2 inUV;
layout( location = 0 ) out vec4 moment0;// m*x, m*y, m
layout( location = 1 ) out vec4 moment1;// m*x^2, m*y^2

void main()
{
	float m = texture( MaskTex, uv ).r;
	moment0 = vec4( m * uv.x, m * uv.y, m, 1.0 );
	moment1 = vec4( m * uv.x * uv.x, m * uv.y * uv.y, 0.0, 1.0 );
}
)";

// Tracker state lives in a 2x1 float texture that feeds back into itself:
//   texel 0 = (centre x, centre y, spread x, spread y) in uv units, texel 1 = (confidence, -, -, -)
static const char kTrackerFragmentShader[] = R"(#version 410 core
uniform sampler2D Moment0Tex;
uniform sampler2D Moment1Tex;
uniform sampler2D PreviousState;
uniform int TopLevel;
uniform float Follow;// 0..1 blend toward the new measurement this frame

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

void main()
{
	vec4 a     = texelFetch( Moment0Tex, ivec2( 0 ), TopLevel );
	vec4 b     = texelFetch( Moment1Tex, ivec2( 0 ), TopLevel );
	vec4 prev  = texelFetch( PreviousState, ivec2( 0, 0 ), 0 );
	float prevConfidence = texelFetch( PreviousState, ivec2( 1, 0 ), 0 ).x;

	float mass       = a.z;
	float confidence = smoothstep( 0.0005, 0.006, mass );
	vec4 measured    = prev;
	if( mass > 1e-5 )
	{
		vec2 centre  = a.xy / mass;
		vec2 spread  = sqrt( max( b.xy / mass - centre * centre, vec2( 1e-6 ) ) );
		measured     = vec4( centre, spread );
	}

	if( int( gl_FragCoord.x ) == 0 )
		fragColor = mix( prev, measured, Follow * confidence );
	else
		fragColor = vec4( mix( prevConfidence, confidence, 0.15 ), 0.0, 0.0, 1.0 );
}
)";

// Fire simulation on a heat field (red channel): advect upward through curl-noise turbulence, cool with
// noise so the heat breaks into tongues, then inject new heat from the subject mask.
static const char kFireFragmentShader[] = "#version 410 core\n"
	GLSL_NOISE_COMMON
	R"(
uniform sampler2D HeatTex;  // unit 0: previous heat
uniform sampler2D MaskTex;  // unit 1
uniform sampler2D StateTex; // unit 2: tracker state

uniform vec2 SimTexel;
uniform float Aspect;
uniform float Time;
uniform float Step;   // frame time relative to 60 fps
uniform float Emit;
uniform float Rise;
uniform float Turbulence;
uniform float Decay;
uniform float Edges;
uniform float Wind;
uniform float Focus;
uniform float PulseGain;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

float fbm( vec2 p )
{
	return 0.5 * vnoise( p ) + 0.3 * vnoise( p * 2.03 + 7.1 ) + 0.2 * vnoise( p * 4.01 + 3.7 );
}

void main()
{
	vec2 q  = uv * vec2( Aspect, 1.0 ) * 5.0 + vec2( 0.0, -Time * 0.8 );
	float e = 0.05;
	vec2 curl = vec2( fbm( q + vec2( 0.0, e ) ) - fbm( q - vec2( 0.0, e ) ),
					  fbm( q - vec2( e, 0.0 ) ) - fbm( q + vec2( e, 0.0 ) ) ) / ( 2.0 * e );

	vec2 velocity = vec2( Wind * 0.012, mix( 0.002, 0.02, Rise ) ) + curl * Turbulence * 0.006;
	vec2 from     = uv - velocity * Step;
	float heat    = texture( HeatTex, from ).r;

	float coolNoise = fbm( uv * vec2( Aspect, 1.0 ) * 9.0 + vec2( 0.0, -Time * 3.0 ) );
	heat *= pow( mix( 0.90, 0.995, Decay ), Step );
	heat -= coolNoise * 0.012 * ( 1.2 - Decay ) * Step;

	float m  = texture( MaskTex, uv ).r;
	float gx = texture( MaskTex, uv + vec2( SimTexel.x, 0.0 ) ).r - texture( MaskTex, uv - vec2( SimTexel.x, 0.0 ) ).r;
	float gy = texture( MaskTex, uv + vec2( 0.0, SimTexel.y ) ).r - texture( MaskTex, uv - vec2( 0.0, SimTexel.y ) ).r;
	float edge   = clamp( length( vec2( gx, gy ) ) * 1.5, 0.0, 1.0 );
	float source = mix( m, edge, Edges );

	// Optionally concentrate emission around the tracked subject.
	vec4 state = texelFetch( StateTex, ivec2( 0, 0 ), 0 );
	vec2 d     = ( uv - state.xy ) / max( state.zw * 2.5, vec2( 0.02 ) );
	source *= mix( 1.0, exp( -dot( d, d ) ), Focus );

	float flicker = 0.6 + 0.4 * vnoise( uv * vec2( Aspect, 1.0 ) * 40.0 + vec2( 0.0, -Time * 9.0 ) );
	heat += source * Emit * 0.35 * flicker * PulseGain * Step;

	fragColor = vec4( clamp( heat, 0.0, 1.5 ), 0.0, 0.0, 1.0 );
}
)";

// Final image: heat-haze + noise distortion, spectral dispersion of the monochrome camera, flame colour
// composited (optionally behind the subject), tracking HUD, dry/wet mix. Premultiplied output.
static const char kFlameCompositeFragmentShader[] = "#version 410 core\n"
	GLSL_NOISE_COMMON
	R"(
uniform sampler2D CurrentTex; // unit 0
uniform sampler2D HeatTex;    // unit 1
uniform sampler2D MaskTex;    // unit 2
uniform sampler2D StateTex;   // unit 3
uniform sampler2D MomentTex;  // unit 4: mask moments; mip level GridLevel holds per-cell centroids

uniform vec2 Resolution;
uniform vec2 SimTexel;
uniform float Time;

uniform float Mix;
uniform float Mono;
uniform float Contrast;
uniform vec3 Tint;
uniform float Haze;
uniform float Warp;
uniform float WarpScale;
uniform float Dispersion;
uniform int DispersionMode;// 0 follows the distortion, 1 radial from the tracked subject
uniform int Palette;
uniform float Glow;
uniform float Behind;
uniform int Hud;           // 0 off, 1 squares, 2 squares + lines, 3 box, 4 ring
uniform float HudOpacity;
uniform int GridLevel;     // mip level of MomentTex used for the squares grid
uniform float SquareSize;  // pixels

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

vec3 toMono( vec4 c )
{
	float l = dot( c.rgb, vec3( 0.2126, 0.7152, 0.0722 ) );
	l       = ( l - 0.5 * c.a ) * Contrast + 0.5 * c.a;
	return mix( c.rgb, clamp( l * Tint, 0.0, 1.0 ), Mono );
}

vec3 spectrum( float t )// 0 red .. 1 violet
{
	float h = t * 0.8 * 6.0;
	return clamp( vec3( abs( h - 3.0 ) - 1.0, 2.0 - abs( h - 2.0 ), 2.0 - abs( h - 4.0 ) ), 0.0, 1.0 );
}
)"
	GLSL_FLAME_PALETTE
	GLSL_TRACKING_SQUARES
	R"(
float drawHud( vec4 state )
{
	if( Hud == 1 || Hud == 2 )
		return drawSquares( Hud == 2 );

	float aspect = Resolution.x / Resolution.y;
	float px     = 1.5 / Resolution.y;
	vec2 p       = ( uv - state.xy ) * vec2( aspect, 1.0 );
	vec2 halfBox = max( state.zw * 2.0, vec2( 0.03 ) ) * vec2( aspect, 1.0 );

	if( Hud == 3 )
	{
		// Corner brackets: each box edge drawn only in its outer 30% near the corners.
		vec2 a      = abs( p );
		float edgeH = hudLine( abs( a.y - halfBox.y ), px ) * step( halfBox.x * 0.7, a.x ) * step( a.x, halfBox.x + px );
		float edgeV = hudLine( abs( a.x - halfBox.x ), px ) * step( halfBox.y * 0.7, a.y ) * step( a.y, halfBox.y + px );
		return max( edgeH, edgeV );
	}
	if( Hud == 4 )
	{
		float r     = max( halfBox.x, halfBox.y ) * 1.1;
		float angle = atan( p.y, p.x ) + Time * 0.8;
		float dash  = step( 0.35, fract( angle * 12.0 / 6.2831853 ) );
		return max( hudLine( abs( length( p ) - r ), px ) * dash, hudLine( abs( length( p ) - r * 0.92 ), px ) * 0.5 );
	}
	return 0.0;
}

void main()
{
	float aspect = Resolution.x / Resolution.y;
	vec4 state   = texelFetch( StateTex, ivec2( 0, 0 ), 0 );
	float confidence = texelFetch( StateTex, ivec2( 1, 0 ), 0 ).x;

	// Distortion: heat haze bends the image along the heat gradient and shimmers where it is hot.
	float heat = texture( HeatTex, uv ).r;
	vec2 grad  = vec2( texture( HeatTex, uv + vec2( SimTexel.x, 0.0 ) ).r - texture( HeatTex, uv - vec2( SimTexel.x, 0.0 ) ).r,
					   texture( HeatTex, uv + vec2( 0.0, SimTexel.y ) ).r - texture( HeatTex, uv - vec2( 0.0, SimTexel.y ) ).r );
	vec2 q       = uv * vec2( aspect, 1.0 );
	vec2 shimmer = vec2( vnoise( q * 30.0 + vec2( 0.0, -Time * 4.0 ) ), vnoise( q * 30.0 + vec2( 9.1, -Time * 4.0 ) ) ) - 0.5;
	vec2 offset  = ( grad * 0.03 + shimmer * heat * 0.025 ) * Haze;
	if( Warp > 0.0 )
	{
		vec2 w = q * mix( 1.0, 10.0, WarpScale ) + vec2( Time * 0.3, -Time * 0.2 );
		offset += ( vec2( vnoise( w ), vnoise( w + 4.3 ) ) - 0.5 ) * Warp * 0.06;
	}

	// Dispersion: split the monochrome image into spectral taps, each displaced a little differently.
	vec2 spreadDir = DispersionMode == 0 ? offset * 6.0 + vec2( 0.004, 0.0 ) : ( uv - state.xy ) * 0.12;
	vec3 sum       = vec3( 0.0 );
	vec3 weight    = vec3( 0.0 );
	float alpha    = 0.0;
	for( int i = 0; i < 7; ++i )
	{
		float t  = float( i ) / 6.0;
		vec3 w   = spectrum( t );
		vec4 tap = texture( CurrentTex, uv + offset + spreadDir * Dispersion * ( t - 0.5 ) );
		sum += toMono( tap ) * w;
		weight += w;
		alpha = max( alpha, tap.a );
	}
	vec4 col = vec4( sum / weight, alpha );

	// Flames, optionally hidden where the subject is so they appear to burn behind them.
	vec3 fire = flamePalette( heat, Palette ) * Glow;
	fire *= 1.0 - Behind * texture( MaskTex, uv ).r;
	col.rgb = 1.0 - ( 1.0 - col.rgb ) * ( 1.0 - clamp( fire, 0.0, 1.0 ) );
	col.a   = max( col.a, clamp( max( fire.r, max( fire.g, fire.b ) ), 0.0, 1.0 ) );

	if( Hud > 0 && HudOpacity > 0.0 )
	{
		float line = drawHud( state ) * HudOpacity * ( Hud <= 2 ? 1.0 : confidence );
		col.rgb    = mix( col.rgb, vec3( 1.0, 0.92, 0.8 ), line );
		col.a      = max( col.a, line );
	}

	vec4 original = texture( CurrentTex, uv );
	col           = mix( original, clamp( col, 0.0, 1.0 ), Mix );
	col.rgb       = min( col.rgb, vec3( col.a ) );
	fragColor     = col;
}
)";
