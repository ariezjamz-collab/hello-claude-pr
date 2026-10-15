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

// Tone sampled at the centre of the rotated cell, so each dot has one size.
float cellTone( vec2 pixel, float cellPx, float angle, int channel )
{
	float s = sin( angle ), c = cos( angle );
	vec2 r      = mat2( c, -s, s, c ) * pixel / cellPx;
	vec2 centre = ( floor( r ) + 0.5 ) * cellPx;
	vec2 back   = mat2( c, s, -s, c ) * centre;
	vec4 col    = src( back / Resolution );
	if( channel < 0 )
		return 1.0 - levels( luma( col.rgb ) );
	return 1.0 - col[ channel ];
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
		float cyan    = dotScreen( pixel, detail, 0.2618, cellTone( pixel, detail, 0.2618, 0 ) );
		float magenta = dotScreen( pixel, detail, 1.309, cellTone( pixel, detail, 1.309, 1 ) );
		float yellow  = dotScreen( pixel, detail, 0.0, cellTone( pixel, detail, 0.0, 2 ) );
		float black   = dotScreen( pixel, detail, 0.7854, cellTone( pixel, detail, 0.7854, -1 ) * 0.8 - 0.2 );
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
