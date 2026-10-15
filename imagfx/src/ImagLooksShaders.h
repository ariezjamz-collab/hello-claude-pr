#pragma once
#include "ShaderCommon.h"

// IMAG Looks: one stylise pass over the copied input. Every look is built from the same blocks:
//   levels (black crush + contrast) -> gradient map or stylise -> edges / glow -> texture -> sparkles
// Look indices must match kLookNames in ImagLooks.cpp.
//
// This is a body, not a complete shader: the plugin prepends "#version" and "#define LOOK_ID n" and
// compiles one small program per look, so the compiler drops every other look's code. One shader with
// all looks behind a runtime switch was too large for some drivers.
static const char kLooksFragmentBody[] =
	GLSL_NOISE_COMMON
	R"(
uniform sampler2D InputTex;
uniform vec2 Resolution;
uniform float Time;
uniform float BeatPhase; // 0..1 per beat (or free-running)
uniform float BeatIndex; // increments once per beat
uniform float Step;      // increments at the animation rate, for stop-motion / boil effects

const int Look = LOOK_ID;
uniform float Mix;
uniform float Intensity;
uniform float Crush;
uniform float Contrast;
uniform float EdgeGlow;
uniform float Detail;
uniform float Texture;
uniform float Sparkles;
uniform float HueShift;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

const int NIGHT_VISION = 0, FIRE_MAP = 1, GOLD_EDGE = 2, BLOOD_RED = 3, ANALOG_TV = 4, GLITTER_ICE = 5,
		  ICE_CYAN = 6, FADED_FILM = 7, RGB_SPLIT = 8, COMIC = 9, HALFTONE = 10, PAPER = 11, CARTOON = 12;

vec2 px;// one pixel in uv

vec4 src( vec2 p )
{
	return texture( InputTex, p );
}

float luma( vec3 c )
{
	return dot( c, vec3( 0.2126, 0.7152, 0.0722 ) );
}

// Levels: crush the blacks (stage backgrounds go to black) and apply an S-curve.
float levels( float l )
{
	l = clamp( ( l - Crush ) / max( 1.0 - Crush, 0.01 ), 0.0, 1.0 );
	float k = 0.5 + Contrast * 2.5;
	float s = l < 0.5 ? 0.5 * pow( 2.0 * l, k ) : 1.0 - 0.5 * pow( 2.0 * ( 1.0 - l ), k );
	return s;
}

float L( vec2 p )
{
	return levels( luma( src( p ).rgb ) );
}

float sobel( vec2 p, float r )
{
	vec2 o = px * r;
	float tl = L( p + vec2( -o.x, o.y ) ), t = L( p + vec2( 0.0, o.y ) ), tr = L( p + o );
	float l = L( p - vec2( o.x, 0.0 ) ), rr = L( p + vec2( o.x, 0.0 ) );
	float bl = L( p - o ), b = L( p - vec2( 0.0, o.y ) ), br = L( p + vec2( o.x, -o.y ) );
	float gx = ( tr + 2.0 * rr + br ) - ( tl + 2.0 * l + bl );
	float gy = ( tl + 2.0 * t + tr ) - ( bl + 2.0 * b + br );
	return clamp( length( vec2( gx, gy ) ) * 0.5, 0.0, 1.0 );
}

float glowAround( vec2 p, float r )
{
	float s = 0.0;
	for( int i = 0; i < 8; ++i )
	{
		float a = float( i ) * 0.7853982;
		s += L( p + vec2( cos( a ), sin( a ) ) * px * r );
	}
	return s / 8.0;
}

vec3 gradientMap( float t, vec3 c0, vec3 c1, vec3 c2, vec3 c3 )
{
	t = clamp( t, 0.0, 1.0 );
	if( t < 0.35 )
		return mix( c0, c1, t / 0.35 );
	if( t < 0.7 )
		return mix( c1, c2, ( t - 0.35 ) / 0.35 );
	return mix( c2, c3, ( t - 0.7 ) / 0.3 );
}

vec3 hueRotate( vec3 c, float a )
{
	const vec3 k = vec3( 0.57735 );
	float ca     = cos( a );
	return c * ca + cross( k, c ) * sin( a ) + k * dot( k, c ) * ( 1.0 - ca );
}

vec3 lookPalette( float t )
{
	if( Look == NIGHT_VISION ) return gradientMap( t, vec3( 0.0 ), vec3( 0.03, 0.3, 0.06 ), vec3( 0.3, 0.95, 0.35 ), vec3( 0.85, 1.0, 0.85 ) );
	if( Look == FIRE_MAP ) return gradientMap( t, vec3( 0.0 ), vec3( 0.55, 0.03, 0.0 ), vec3( 1.0, 0.4, 0.0 ), vec3( 1.0, 0.92, 0.45 ) );
	if( Look == GOLD_EDGE ) return gradientMap( t, vec3( 0.0 ), vec3( 0.32, 0.16, 0.02 ), vec3( 0.92, 0.62, 0.24 ), vec3( 1.0, 0.93, 0.72 ) );
	if( Look == BLOOD_RED ) return gradientMap( t, vec3( 0.0 ), vec3( 0.45, 0.04, 0.08 ), vec3( 0.95, 0.5, 0.56 ), vec3( 1.0, 0.92, 0.93 ) );
	if( Look == GLITTER_ICE ) return gradientMap( t, vec3( 0.0 ), vec3( 0.1, 0.17, 0.38 ), vec3( 0.55, 0.7, 0.95 ), vec3( 0.97, 0.98, 1.0 ) );
	if( Look == ICE_CYAN ) return gradientMap( t, vec3( 0.0 ), vec3( 0.06, 0.23, 0.28 ), vec3( 0.5, 0.82, 0.88 ), vec3( 0.9, 1.0, 1.0 ) );
	if( Look == FADED_FILM ) return gradientMap( t, vec3( 0.07, 0.05, 0.05 ), vec3( 0.36, 0.26, 0.24 ), vec3( 0.75, 0.6, 0.56 ), vec3( 0.96, 0.87, 0.82 ) );
	if( Look == COMIC ) return gradientMap( t, vec3( 0.0 ), vec3( 0.7, 0.05, 0.08 ), vec3( 0.98, 0.35, 0.1 ), vec3( 1.0, 0.9, 0.25 ) );
	return vec3( t );// greys: Analog TV, RGB Split
}

// Round dot screen. Returns ink coverage 0..1 for a tone, in a grid rotated by `angle`.
float dotScreen( vec2 pixel, float cellPx, float angle, float tone )
{
	float s = sin( angle ), c = cos( angle );
	vec2 r  = mat2( c, -s, s, c ) * pixel / cellPx;
	vec2 f  = fract( r ) - 0.5;
	float radius = sqrt( clamp( tone, 0.0, 1.0 ) ) * 0.72;
	return 1.0 - smoothstep( radius - 0.06, radius + 0.06, length( f ) );
}

// Ink coverage sampled at the centre of the rotated cell, so each dot has one size. `channel` picks the
// colour to measure (e.g. vec3(1,0,0) for cyan ink, which absorbs red); vec3(0) measures black ink from
// the levelled luma. Selecting with a weight vector, not col[i], keeps every driver's compiler happy.
float cellTone( vec2 pixel, float cellPx, float angle, vec3 channel )
{
	float s = sin( angle ), c = cos( angle );
	vec2 r      = mat2( c, -s, s, c ) * pixel / cellPx;
	vec2 centre = ( floor( r ) + 0.5 ) * cellPx;
	vec2 back   = mat2( c, s, -s, c ) * centre;
	vec3 col    = src( back / Resolution ).rgb;
	float black = 1.0 - levels( luma( col ) );
	return dot( channel, vec3( 1.0 ) ) > 0.5 ? 1.0 - dot( col, channel ) : black;
}

float paperFibres( vec2 pixel, float seed )
{
	vec2 q = pixel + seed * 37.0;
	float base   = vnoise( q * 0.015 ) * 0.6 + vnoise( q * 0.05 ) * 0.4;
	float fibres = vnoise( vec2( q.x * 0.6, q.y * 0.04 ) ) * 0.5 + vnoise( vec2( q.x * 0.05, q.y * 0.7 ) ) * 0.5;
	float speck  = step( 0.997, hash12( floor( q / 2.0 ) ) );
	return 0.86 + base * 0.08 + fibres * 0.06 - speck * 0.25;
}

// Sparkles: one particle per grid cell, born on a bright edge, drifting out and fading.
float sparkles( vec2 pixel )
{
	float cell  = 16.0;
	vec2 id     = floor( pixel / cell );
	float total = 0.0;
	for( int y = -1; y <= 1; ++y )
		for( int x = -1; x <= 1; ++x )
		{
			vec2 c   = id + vec2( x, y );
			float h  = hash12( c );
			float life = fract( Time * 0.6 + h );
			float gen  = floor( Time * 0.6 + h );
			vec2 seed  = vec2( hash12( c + gen * 1.7 ), hash12( c + gen * 3.1 + 5.0 ) );
			vec2 birth = ( c + seed ) * cell;
			vec2 bUV   = birth / Resolution;
			float edge = abs( L( bUV + vec2( px.x * 5.0, 0.0 ) ) - L( bUV - vec2( px.x * 5.0, 0.0 ) ) ) +
						 abs( L( bUV + vec2( 0.0, px.y * 5.0 ) ) - L( bUV - vec2( 0.0, px.y * 5.0 ) ) );
			vec2 dir   = normalize( seed - 0.5 + vec2( 0.0, -0.4 ) );
			vec2 pos   = birth + dir * life * cell * 0.9;
			float d    = length( pixel - pos );
			// Soft core plus a four-point flare.
			vec2 o     = abs( pixel - pos );
			float star = exp( -d * d * 0.12 ) + 0.6 * exp( -o.y * 1.5 - o.x * 0.25 ) + 0.6 * exp( -o.x * 1.5 - o.y * 0.25 );
			float twinkle = 0.5 + 0.5 * sin( Time * 20.0 + h * 50.0 );
			total += star * smoothstep( 0.1, 0.35, edge ) * ( 1.0 - life * life ) * twinkle;
		}
	return total;
}

void main()
{
	px           = 1.0 / Resolution;
	vec2 pixel   = uv * Resolution;
	vec4 inColor = src( uv );
	float alpha  = inColor.a;
	float tone   = L( uv );
	float detail = mix( 3.0, 16.0, Detail );// cell size in pixels for screens
	vec3 col;
	float fillBackground = 0.0;

	if( Look == RGB_SPLIT )
	{
		float slice = floor( uv.y * 24.0 );
		float hit   = Intensity * pow( 1.0 - BeatPhase, 4.0 ) * step( 0.7, hash12( vec2( slice, BeatIndex ) ) );
		vec2 shift  = vec2( ( hash12( vec2( slice, BeatIndex + 3.0 ) ) - 0.5 ) * 0.08 * hit, 0.0 );
		vec2 split  = vec2( px.x * ( 3.0 + 10.0 * Intensity ), 0.0 );
		col = vec3( L( uv + shift - split ), L( uv + shift ), L( uv + shift + split ) );
		col.gb = mix( col.gb, vec2( L( uv + shift + split ) ), 0.5 );// cyan fringe on the right
	}
	else if( Look == HALFTONE )
	{
		// CMYK screen on paper, each ink at its classic angle.
		float cyan    = dotScreen( pixel, detail, 0.2618, cellTone( pixel, detail, 0.2618, vec3( 1.0, 0.0, 0.0 ) ) );
		float magenta = dotScreen( pixel, detail, 1.309, cellTone( pixel, detail, 1.309, vec3( 0.0, 1.0, 0.0 ) ) );
		float yellow  = dotScreen( pixel, detail, 0.0, cellTone( pixel, detail, 0.0, vec3( 0.0, 0.0, 1.0 ) ) );
		float black   = dotScreen( pixel, detail, 0.7854, cellTone( pixel, detail, 0.7854, vec3( 0.0 ) ) * 0.8 - 0.2 );
		vec3 paper    = vec3( 0.97, 0.95, 0.9 ) * mix( 1.0, paperFibres( pixel, 0.0 ), Texture );
		col           = paper * ( 1.0 - vec3( cyan, magenta, yellow ) * 0.85 ) * ( 1.0 - black );
		fillBackground = 1.0;
	}
	else if( Look == PAPER )
	{
		// Printed onto paper: faded ink multiplied into fibrous stock, re-jittered on each step like stop motion.
		vec2 jitter = ( vec2( hash12( vec2( Step, 1.0 ) ), hash12( vec2( Step, 2.0 ) ) ) - 0.5 ) * px * 2.0 * Intensity;
		vec3 image  = src( uv + jitter ).rgb;
		float ink   = 1.0 - levels( luma( image ) );
		vec3 inkCol = mix( vec3( 0.12, 0.1, 0.09 ), image * 0.7, 0.35 );
		vec3 paper  = vec3( 0.95, 0.91, 0.83 ) * paperFibres( pixel, Step * Intensity );
		col         = mix( paper, paper * inkCol * 1.6, ink * 0.9 );
		col *= 1.0 - Texture * 0.35 * smoothstep( 0.4, 0.95, length( uv - 0.5 ) * 1.4 );
		fillBackground = 1.0;
	}
	else if( Look == CARTOON )
	{
		// Flat cel shading + ink outlines that "boil" (redrawn a few times a second, like hand animation).
		vec2 boil = ( vec2( vnoise( pixel * 0.05 + Step * 7.3 ), vnoise( pixel * 0.05 + Step * 3.1 + 9.0 ) ) - 0.5 ) * px * 3.0 * Intensity;
		vec3 c = ( src( uv ).rgb * 2.0 + src( uv + px * vec2( 1.5, 0.0 ) ).rgb + src( uv - px * vec2( 1.5, 0.0 ) ).rgb +
				   src( uv + px * vec2( 0.0, 1.5 ) ).rgb + src( uv - px * vec2( 0.0, 1.5 ) ).rgb ) / 6.0;
		float lum    = max( luma( c ), 1e-3 );
		float bands  = floor( mix( 6.0, 2.0, Detail ) );
		float banded = ( floor( levels( lum ) * bands ) + 0.6 ) / bands;
		vec3 chroma  = c / lum;
		col          = clamp( mix( vec3( 1.0 ), chroma, 1.35 ) * banded, 0.0, 1.0 );
		float ink    = smoothstep( 0.25, 0.45, sobel( uv + boil, 1.5 ) );
		col *= 1.0 - ink;
	}
	else if( Look == COMIC )
	{
		// Pop-art: posterised gradient map, Ben-Day dots in the mid tones, heavy ink lines.
		// Each tone band is a flat colour with dots of the next-brighter colour; dot size carries the shading
		// within the band, so gradients read as Ben-Day dots.
		float bands  = 3.0;
		float scaled = tone * bands;
		float band   = min( floor( scaled ), bands - 1.0 );
		float within = clamp( scaled - band, 0.0, 1.0 );
		vec3 base    = lookPalette( band / bands );
		vec3 dotCol  = lookPalette( ( band + 1.0 ) / bands );
		float dots   = dotScreen( pixel, detail, 0.7854, within );
		col          = mix( base, dotCol, dots );
		col *= 1.0 - smoothstep( 0.3, 0.5, sobel( uv, 1.5 ) ) * 0.95;
	}
	else
	{
		// Gradient-map looks.
		col = lookPalette( tone );
		if( Look == NIGHT_VISION )
		{
			col += lookPalette( glowAround( uv, 6.0 ) ) * 0.45;
			col *= 0.85 + 0.15 * vnoise( vec2( 0.0, uv.y * 3.0 + Time * 6.0 ) );
		}
		if( Look == ANALOG_TV || Look == ICE_CYAN )
		{
			float ghost = L( uv - vec2( px.x * 6.0, 0.0 ) );
			col         = mix( col, lookPalette( max( tone, ghost * 0.6 ) ), 0.5 );
			float band  = smoothstep( 0.06, 0.0, abs( uv.y - fract( Time * 0.12 ) ) );
			col += band * 0.15 * ( hash12( vec2( floor( pixel.y ), Time ) ) - 0.3 );
		}
	}

	// Edge glow: crisp and wide Sobel combined, coloured by the look's highlight.
	if( EdgeGlow > 0.0 && Look != CARTOON && Look != COMIC && Look != HALFTONE && Look != PAPER )
	{
		float edges = sobel( uv, 1.0 ) * 0.6 + sobel( uv, 3.0 ) * 0.6;
		float boost = ( Look == GOLD_EDGE || Look == FIRE_MAP ) ? 1.6 : 1.0;
		col += lookPalette( 0.95 ) * edges * EdgeGlow * boost;
	}

	// Surface texture: grain everywhere, plus scanlines on the video-style looks.
	if( Texture > 0.0 && Look != HALFTONE && Look != PAPER )
	{
		float grain = hash12( floor( pixel ) + fract( Time * 13.0 ) * 113.0 ) - 0.5;
		col += grain * Texture * 0.12;
		if( Look == ANALOG_TV || Look == ICE_CYAN || Look == NIGHT_VISION || Look == FADED_FILM )
			col *= 1.0 - Texture * 0.45 * ( 0.5 + 0.5 * sin( pixel.y * 3.14159265 * 0.5 ) );
	}

	if( Sparkles > 0.0 || Look == GLITTER_ICE )
	{
		float amount = Look == GLITTER_ICE ? max( Sparkles, 0.6 * Intensity ) : Sparkles;
		col += lookPalette( 0.9 ) * sparkles( pixel ) * amount * 1.5;
	}

	col = max( hueRotate( clamp( col, 0.0, 1.5 ), HueShift * 6.2831853 ), 0.0 );

	// Stylised result over the plain monochrome version by Intensity, then dry/wet by Mix.
	float outAlpha = max( alpha, fillBackground );
	vec3 styled    = mix( vec3( tone ), clamp( col, 0.0, 1.0 ), Look == RGB_SPLIT || Look == PAPER || Look == CARTOON ? 1.0 : Intensity );
	vec4 result    = vec4( styled * outAlpha, outAlpha );// premultiplied; keyed backgrounds stay transparent unless papered
	vec4 mixed = mix( inColor, result, Mix );
	mixed.rgb  = min( mixed.rgb, vec3( mixed.a ) );
	fragColor  = mixed;
}
)";

// Echo history: stores this frame's subject (luma inside the mask, and the mask) at mask resolution.
static const char kEchoStoreFragmentShader[] = R"(#version 410 core
uniform sampler2D FrameTex;
uniform sampler2D MaskTex;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

void main()
{
	vec4 c   = texture( FrameTex, uv );
	float m  = texture( MaskTex, uv ).r;
	float l  = dot( c.rgb, vec3( 0.2126, 0.7152, 0.0722 ) );
	fragColor = vec4( l * m, m, 0.0, 1.0 );
}
)";

// Overlay stack drawn over whichever look is active, in this order:
//   distortion -> dispersion -> depth -> coloured echoes -> flames -> dual tone -> ASCII -> tracking squares
// then dry/wet against the original camera. Premultiplied in and out.
static const char kLooksCompositeFragmentShader[] = "#version 410 core\n"
	GLSL_NOISE_COMMON
	R"(
uniform sampler2D LookTex;   // unit 0: the active look
uniform sampler2D FrameTex;  // unit 1: original camera
uniform sampler2D MaskTex;   // unit 2: subject mask (half resolution)
uniform sampler2D HeatTex;   // unit 3: flame heat field
uniform sampler2D MomentTex; // unit 4: mask moments, mipmapped
uniform sampler2D EchoTex0;  // units 5-8: past subject frames, newest first
uniform sampler2D EchoTex1;
uniform sampler2D EchoTex2;
uniform sampler2D EchoTex3;

uniform vec2 Resolution;
uniform vec2 MaskTexel;
uniform float Time;
uniform float Mix;

uniform float Distortion;
uniform float Dispersion;
uniform float Depth;
uniform float EchoLevel;
uniform int EchoCount;
uniform vec3 EchoColorA;
uniform vec3 EchoColorB;
uniform float Flames;
uniform int Palette;
uniform float DualTone;
uniform vec3 DualDark;
uniform vec3 DualLight;
uniform int AsciiArea;     // 0 off, 1 subject, 2 background, 3 whole frame
uniform float AsciiSize;   // cell height in pixels
uniform vec3 AsciiColor;
uniform int Squares;       // 0 off, 1 squares, 2 squares + lines
uniform int GridLevel;
uniform float SquareSize;
uniform vec3 SquareColor;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;
)"
	GLSL_FLAME_PALETTE
	GLSL_TRACKING_SQUARES
	R"(
// 4x6 glyphs ordered from empty to dense; bit (row * 4 + column) is ink, row 0 at the top.
const int kGlyphs[ 11 ] = int[]( 0, 2097152, 131104, 1792, 10016, 28784, 432480, 337701, 719610, 14492859, 14802326 );

float luma( vec3 c )
{
	return dot( c, vec3( 0.2126, 0.7152, 0.0722 ) );
}

float mask( vec2 p )
{
	return texture( MaskTex, p ).r;
}

vec3 spectrum( float t )// 0 red .. 1 violet
{
	float h = t * 0.8 * 6.0;
	return clamp( vec3( abs( h - 3.0 ) - 1.0, 2.0 - abs( h - 2.0 ), 2.0 - abs( h - 4.0 ) ), 0.0, 1.0 );
}

// Separate samplers and a branch instead of a sampler array: older drivers handle this more reliably.
vec2 echoSample( int i, vec2 p )
{
	if( i == 0 )
		return texture( EchoTex0, p ).rg;
	if( i == 1 )
		return texture( EchoTex1, p ).rg;
	if( i == 2 )
		return texture( EchoTex2, p ).rg;
	return texture( EchoTex3, p ).rg;
}

vec3 screen( vec3 a, vec3 b )
{
	return 1.0 - ( 1.0 - a ) * ( 1.0 - clamp( b, 0.0, 1.0 ) );
}

void main()
{
	vec2 px      = 1.0 / Resolution;
	float aspect = Resolution.x / Resolution.y;
	vec2 q       = uv * vec2( aspect, 1.0 );

	// Subject centre from the 1x1 moments level; used to centre dispersion.
	vec4 top      = texelFetch( MomentTex, ivec2( 0 ), 8 );
	vec2 centre   = top.z > 1e-4 ? top.xy / top.z : vec2( 0.5 );

	// Distortion: noise warp everywhere, plus heat haze where the flames are.
	float heat  = Flames > 0.0 ? texture( HeatTex, uv ).r : 0.0;
	vec2 offset = vec2( 0.0 );
	if( Distortion > 0.0 )
	{
		vec2 w = q * 4.0 + vec2( Time * 0.3, -Time * 0.25 );
		offset += ( vec2( vnoise( w ), vnoise( w + 7.3 ) ) - 0.5 ) * Distortion * 0.07;
	}
	if( heat > 0.0 )
	{
		vec2 shimmer = vec2( vnoise( q * 30.0 + vec2( 0.0, -Time * 4.0 ) ), vnoise( q * 30.0 + vec2( 9.1, -Time * 4.0 ) ) ) - 0.5;
		offset += shimmer * heat * 0.02 * Flames;
	}
	vec2 p = uv + offset;

	// Dispersion: seven spectral taps spread outward from the subject.
	vec4 col;
	if( Dispersion > 0.0 )
	{
		vec2 dir    = ( p - centre ) * Dispersion * 0.08;
		vec3 sum    = vec3( 0.0 );
		vec3 weight = vec3( 0.0 );
		float alpha = 0.0;
		for( int i = 0; i < 7; ++i )
		{
			float t  = float( i ) / 6.0;
			vec3 w   = spectrum( t );
			vec4 tap = texture( LookTex, p + dir * ( t - 0.5 ) );
			sum += tap.rgb * w;
			weight += w;
			alpha = max( alpha, tap.a );
		}
		col = vec4( sum / weight, alpha );
	}
	else
		col = texture( LookTex, p );

	float subject = mask( p );

	// Depth: background defocuses and dims, and the subject extrudes down-right as a dimmed copy of
	// itself, so it reads as 3D on dark and bright backgrounds alike.
	if( Depth > 0.0 )
	{
		vec4 blur = vec4( 0.0 );
		for( int i = 0; i < 8; ++i )
		{
			float a = float( i ) * 0.7853982;
			blur += texture( LookTex, p + vec2( cos( a ), sin( a ) ) * px * Depth * 10.0 );
		}
		blur /= 8.0;
		col = mix( col, blur * ( 1.0 - 0.45 * Depth ), ( 1.0 - subject ) * Depth );
		float extrude  = 0.0;
		vec3 extrudeCol = vec3( 0.0 );
		for( int k = 1; k <= 8; ++k )
		{
			vec2 from  = p - vec2( 1.0, -1.0 ) * px * Depth * 3.0 * float( k );
			float m    = mask( from ) * ( 1.0 - float( k ) / 9.0 );
			if( m > extrude )
			{
				extrude    = m;
				extrudeCol = texture( LookTex, from ).rgb;
			}
		}
		float amount = extrude * ( 1.0 - subject ) * Depth;
		col.rgb      = mix( col.rgb, extrudeCol * 0.35, amount );
		col.a        = max( col.a, amount );
	}

	// Coloured echoes: the subject from earlier frames, each copy tinted along colour A -> colour B.
	if( EchoLevel > 0.0 )
	{
		for( int i = 0; i < 4; ++i )
		{
			if( i >= EchoCount )
				break;
			vec2 e     = echoSample( i, p );// luma*mask, mask
			float s    = e.y * ( 0.45 + 0.55 * e.x / max( e.y, 1e-3 ) );
			float t    = EchoCount > 1 ? float( i ) / float( EchoCount - 1 ) : 0.0;
			float fade = 1.0 - float( i ) / float( EchoCount + 1 );
			vec3 tint  = mix( EchoColorA, EchoColorB, t );
			float vis  = s * ( 1.0 - subject * 0.9 ) * EchoLevel * fade;
			col.rgb    = screen( col.rgb, tint * vis );
			col.a      = max( col.a, vis );
		}
	}

	// Flames from the heat simulation.
	if( Flames > 0.0 )
	{
		vec3 fire = flamePalette( heat, Palette ) * Flames * 1.6;
		fire *= 1.0 - 0.4 * subject;// mostly behind the subject
		col.rgb = screen( col.rgb, fire );
		col.a   = max( col.a, clamp( max( fire.r, max( fire.g, fire.b ) ), 0.0, 1.0 ) );
	}

	// Dual tone: brightness mapped between two colours.
	if( DualTone > 0.0 )
	{
		float l  = col.a > 0.0 ? luma( col.rgb ) / col.a : 0.0;
		vec3 duo = mix( DualDark, DualLight, smoothstep( 0.0, 1.0, l ) ) * col.a;
		col.rgb  = mix( col.rgb, duo, DualTone );
	}

	// ASCII: replace the chosen area with characters picked by brightness.
	if( AsciiArea > 0 )
	{
		vec2 cell     = vec2( AsciiSize * 4.0 / 6.0, AsciiSize );
		vec2 pixel    = p * Resolution;
		vec2 cellId   = floor( pixel / cell );
		vec2 cellUV   = ( cellId + 0.5 ) * cell / Resolution;
		float inCell  = mask( cellUV );
		float area    = AsciiArea == 1 ? inCell : ( AsciiArea == 2 ? 1.0 - inCell : 1.0 );
		if( area > 0.5 )
		{
			vec4 c        = texture( LookTex, cellUV );
			float bright  = c.a > 0.0 ? luma( c.rgb ) / c.a : 0.0;
			int glyph     = int( clamp( bright, 0.0, 0.999 ) * 11.0 );
			vec2 local    = fract( pixel / cell );
			ivec2 bit     = ivec2( floor( local.x * 4.0 ), floor( ( 1.0 - local.y ) * 6.0 ) );
			float ink     = float( ( kGlyphs[ glyph ] >> ( bit.y * 4 + bit.x ) ) & 1 );
			col           = vec4( AsciiColor * ink, 1.0 );
		}
	}

	if( Squares > 0 )
	{
		float line = drawSquares( Squares == 2 );
		col.rgb    = mix( col.rgb, SquareColor, line );
		col.a      = max( col.a, line );
	}

	vec4 original = texture( FrameTex, uv );
	col           = mix( original, clamp( col, 0.0, 1.0 ), Mix );
	col.rgb       = min( col.rgb, vec3( col.a ) );
	fragColor     = col;
}
)";
