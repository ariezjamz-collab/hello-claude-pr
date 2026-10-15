#pragma once
#include "ShaderCommon.h"

// IMAG Blob shaders. The mask and moments passes are shared with IMAG Flame (ImagFlameShaders.h).
//
// Written to be conservative for every driver: one small program per feed mode (FEED_MODE is #defined
// when compiling), glyphs come from a texture rather than a lookup table, and nothing indexes a vector
// or a sampler array with a runtime value.

// Feed: restyles the camera underneath the tracking graphics. Compiled once per mode:
//   0 normal, 1 monochrome, 2 dual tone, 3 ASCII, 4 halftone.
// The plugin prepends "#version 410 core" and "#define FEED_MODE n".
static const char kFeedFragmentBody[] = R"(
uniform sampler2D FrameTex;
uniform sampler2D AtlasTex;// glyph atlas: slots of 5x7 texels (4x6 glyph + gap); slots 10-20 are ASCII density
uniform float AtlasWidth;  // texels
uniform vec2 Resolution;
uniform float Contrast;
uniform vec3 DarkColor;
uniform vec3 LightColor;
uniform float CellSize;// pixels
uniform float FeedMix;

in vec2 uv;
in vec2 inUV;
out vec4 fragColor;

float tone( vec4 c )
{
	vec3 straight = c.a > 0.0 ? c.rgb / c.a : vec3( 0.0 );
	float l       = dot( straight, vec3( 0.2126, 0.7152, 0.0722 ) );
	return clamp( ( l - 0.5 ) * Contrast + 0.5, 0.0, 1.0 );
}

void main()
{
	vec4 src  = texture( FrameTex, uv );
	vec3 col  = vec3( 0.0 );
	vec2 px   = uv * Resolution;

#if FEED_MODE == 0
	col = src.a > 0.0 ? src.rgb / src.a : vec3( 0.0 );
#elif FEED_MODE == 1
	col = vec3( tone( src ) );
#elif FEED_MODE == 2
	col = mix( DarkColor, LightColor, tone( src ) );
#elif FEED_MODE == 3
	vec2 cell     = vec2( CellSize * 4.0 / 6.0, CellSize );
	vec2 cellId   = floor( px / cell );
	vec2 centre   = ( cellId + 0.5 ) * cell / Resolution;
	float t       = tone( texture( FrameTex, centre ) );
	float glyph   = floor( t * 10.999 );
	vec2 local    = fract( px / cell );
	vec2 atlasUV  = vec2( ( ( 10.0 + glyph ) * 5.0 + local.x * 4.0 ) / AtlasWidth, ( local.y * 6.0 ) / 7.0 );
	float ink     = texture( AtlasTex, atlasUV ).r;
	col           = mix( DarkColor, LightColor, ink );
#elif FEED_MODE == 4
	const float angle = 0.7853982;
	float s = sin( angle ), c = cos( angle );
	vec2 r       = mat2( c, -s, s, c ) * px / CellSize;
	vec2 centre  = mat2( c, s, -s, c ) * ( ( floor( r ) + 0.5 ) * CellSize );
	float ink    = 1.0 - tone( texture( FrameTex, centre / Resolution ) );
	float radius = sqrt( ink ) * 0.72;
	float dotInk = 1.0 - smoothstep( radius - 0.06, radius + 0.06, length( fract( r ) - 0.5 ) );
	col          = mix( LightColor, DarkColor, dotInk );
#endif

	vec4 styled = vec4( clamp( col, 0.0, 1.0 ) * src.a, src.a );// premultiplied; keyed backgrounds stay clear
	fragColor   = mix( src, styled, FeedMix );
}
)";

// Tracking graphics: CPU-built triangles for squares, lines and number glyphs, premultiplied colour.
static const char kOverlayVertexShader[] = R"(#version 410 core
layout( location = 0 ) in vec2 aPosition;// normalised device coordinates
layout( location = 1 ) in vec2 aUV;      // atlas coordinates for glyphs
layout( location = 2 ) in vec4 aColor;   // premultiplied
layout( location = 3 ) in float aGlyph;  // 1 for glyph quads, 0 for solid shapes

out vec2 vUV;
out vec4 vColor;
out float vGlyph;

void main()
{
	gl_Position = vec4( aPosition, 0.0, 1.0 );
	vUV         = aUV;
	vColor      = aColor;
	vGlyph      = aGlyph;
}
)";

static const char kOverlayFragmentShader[] = R"(#version 410 core
uniform sampler2D AtlasTex;

in vec2 vUV;
in vec4 vColor;
in float vGlyph;
out vec4 fragColor;

void main()
{
	float coverage = vGlyph > 0.5 ? texture( AtlasTex, vUV ).r : 1.0;
	fragColor      = vColor * coverage;
}
)";
