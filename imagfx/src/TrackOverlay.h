#pragma once
// Shared by IMAG Blob and IMAG Face: steady multi-object tracking, the tracking graphics (squares, lines,
// numbers, face landmarks) and the restyled feed underneath them (normal, mono, dual tone, ASCII,
// halftone). Each plugin only supplies its own detections and maps its parameters onto the settings here.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <FFGLSDK.h>
#include "PassThrough.h"
#include "PluginLog.h"
#include "TrackOverlayShaders.h"

namespace imag
{
// ---------------------------------------------------------------------------------------------------
// Tracking

// One detection from a detector, in frame units (0..1, y up).
struct Detection
{
	float x = 0, y = 0;// centre
	float w = 0, h = 0;// extent
	float mass  = 0;   // detector strength, used for sorting and for size variation
	float score = 0;   // detector confidence 0..1 (faces)
	int landmarkCount = 0;
	float landmarks[ 10 ] = {};// up to five x,y points, frame units
};

// A track outlives single detections: it holds its place for a moment when a detection is missed, its
// drawn position eases toward the measurement, and it fades in and out, so the graphics glide.
struct Track
{
	Detection target;     // latest measurement (or prediction while missing)
	Detection shown;      // what is drawn; eases toward target every frame
	float vx = 0, vy = 0; // velocity, frame units per second
	int id   = 0;
	int slot = 0;         // small stable label index (1, 2, 3...), reused after a track ends
	int age  = 0;         // detections matched so far
	float missing = 0.0f; // seconds since last matched
	float alpha   = 0.0f; // current visibility 0..1
	uint32_t number  = 0;
	float nextChange = 0.0f;
};

struct TrackerSettings
{
	float aspect     = 16.0f / 9.0f;
	size_t maxTracks = 8;
	float smoothing  = 0.75f; // 0 snappy .. 1 very steady
	float hold       = 0.6f;  // seconds a missed track keeps its place
	float numberRate = 1.0f;  // random number changes per second
	bool numbersOnBeat = false;
	bool beatHit       = false;
	int showAfter      = 3;   // detections before a track is drawn
};

class Tracker
{
public:
	std::vector< Track > tracks;

	void Reset()
	{
		tracks.clear();
	}

	// Match new detections to tracks. Call whenever the detector produced results; `dt` is the time since
	// the previous call.
	void Update( std::vector< Detection > found, float dt, float time, const TrackerSettings& s )
	{
		std::sort( found.begin(), found.end(), []( const Detection& a, const Detection& b ) { return a.mass > b.mass; } );
		std::sort( tracks.begin(), tracks.end(), []( const Track& a, const Track& b ) { return a.age > b.age; } );
		std::vector< bool > claimed( found.size(), false );
		for( Track& t : tracks )
		{
			// Match against where the track should be now, so fast movement doesn't lose it.
			const float px = t.target.x + t.vx * dt, py = t.target.y + t.vy * dt;
			int best       = -1;
			float bestDist = 0.08f + 0.8f * std::max( t.target.w, t.target.h );
			for( size_t i = 0; i < found.size(); ++i )
			{
				if( claimed[ i ] )
					continue;
				const float d = std::hypot( ( found[ i ].x - px ) * s.aspect, found[ i ].y - py );
				if( d < bestDist )
				{
					bestDist = d;
					best     = (int)i;
				}
			}
			if( best >= 0 )
			{
				claimed[ best ] = true;
				const Detection& f = found[ best ];
				if( dt > 0.0f )
				{
					t.vx += ( ( f.x - t.target.x ) / dt - t.vx ) * 0.5f;
					t.vy += ( ( f.y - t.target.y ) / dt - t.vy ) * 0.5f;
				}
				t.target  = f;
				t.missing = 0.0f;
				++t.age;
			}
			else
			{
				// Lost for now: coast along the last motion while it holds its place, slowing down.
				t.target.x = px;
				t.target.y = py;
				t.vx *= 0.85f;
				t.vy *= 0.85f;
				t.missing += dt;
			}
		}
		for( size_t i = 0; i < found.size(); ++i )
		{
			if( claimed[ i ] || tracks.size() >= s.maxTracks )
				continue;
			Track t;
			t.target = t.shown = found[ i ];
			t.id               = nextId++;
			t.age              = 1;
			t.number           = Random();
			t.nextChange       = time + ( 0.5f + ( Random() % 1000 ) / 1000.0f ) / s.numberRate;
			std::set< int > used;
			for( const Track& other : tracks )
				used.insert( other.slot );
			t.slot = 1;
			while( used.count( t.slot ) )
				++t.slot;
			tracks.push_back( t );
		}
	}

	// Ease drawn positions, fade, change numbers and drop finished tracks. Call every rendered frame.
	void Animate( float dt, float time, const TrackerSettings& s )
	{
		const float follow = 1.0f - std::pow( s.smoothing * 0.9f, dt * 60.0f );
		const float fade   = std::min( dt * 6.0f, 1.0f );
		for( Track& t : tracks )
		{
			Detection& a       = t.shown;
			const Detection& b = t.target;
			a.x += ( b.x - a.x ) * follow;
			a.y += ( b.y - a.y ) * follow;
			a.w += ( b.w - a.w ) * follow;
			a.h += ( b.h - a.h ) * follow;
			a.mass          = b.mass;
			a.score         = b.score;
			a.landmarkCount = b.landmarkCount;
			for( int i = 0; i < 2 * b.landmarkCount; ++i )
				a.landmarks[ i ] += ( b.landmarks[ i ] - a.landmarks[ i ] ) * follow;
			const bool show = t.age >= s.showAfter && t.missing <= s.hold;
			t.alpha += ( ( show ? 1.0f : 0.0f ) - t.alpha ) * fade;
			if( s.numbersOnBeat ? s.beatHit : time >= t.nextChange )
			{
				t.number     = Random();
				t.nextChange = time + ( 0.5f + ( Random() % 1000 ) / 1000.0f ) / s.numberRate;
			}
		}
		tracks.erase( std::remove_if( tracks.begin(), tracks.end(), [ & ]( const Track& t ) { return t.missing > s.hold && t.alpha < 0.01f; } ),
					  tracks.end() );
		std::sort( tracks.begin(), tracks.end(), []( const Track& a, const Track& b ) { return a.slot < b.slot; } );
	}

private:
	uint32_t Random()
	{
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return rng;
	}

	int nextId   = 1;
	uint32_t rng = 0x9E3779B9u;
};

// Lines between tracks. mode: 0 off, 1 chain (label order), 2 nearest, 3 network. Indices into `tracks`.
inline std::vector< std::pair< int, int > > ComputeLinks( const std::vector< Track >& tracks, int mode, int perTrack, float maxDist, float aspect )
{
	const int kMaxLines = 600;
	std::set< std::pair< int, int > > unique;
	auto distance = [ & ]( const Track& a, const Track& b ) { return std::hypot( ( a.shown.x - b.shown.x ) * aspect, a.shown.y - b.shown.y ); };
	if( mode == 1 )
	{
		for( int i = 0; i + 1 < (int)tracks.size(); ++i )
			unique.insert( { i, i + 1 } );
	}
	else if( mode == 2 )
	{
		std::vector< std::pair< float, int > > nearest;
		for( int i = 0; i < (int)tracks.size(); ++i )
		{
			nearest.clear();
			for( int j = 0; j < (int)tracks.size(); ++j )
			{
				const float d = distance( tracks[ i ], tracks[ j ] );
				if( j != i && d < maxDist )
					nearest.push_back( { d, j } );
			}
			std::sort( nearest.begin(), nearest.end() );
			for( int k = 0; k < (int)nearest.size() && k < perTrack; ++k )
				unique.insert( { std::min( i, nearest[ k ].second ), std::max( i, nearest[ k ].second ) } );
		}
	}
	else if( mode == 3 )
	{
		for( int i = 0; i < (int)tracks.size(); ++i )
			for( int j = i + 1; j < (int)tracks.size() && (int)unique.size() < kMaxLines; ++j )
				if( distance( tracks[ i ], tracks[ j ] ) < maxDist )
					unique.insert( { i, j } );
	}
	return std::vector< std::pair< int, int > >( unique.begin(), unique.end() );
}

// ---------------------------------------------------------------------------------------------------
// Drawing

struct OverlayStyle
{
	int squareStyle     = 0;    // 0 outline, 1 filled, 2 corners
	bool fitToTarget    = false;// box follows the target's own width/height (faces) instead of a square
	float boxScale      = 1.0f; // with fitToTarget: padding around the target
	float baseSize      = 100.0f;// square side in pixels
	float sizeVariation = 0.0f;
	float thickness     = 1.0f; // pixels
	float squareRGB[ 3 ] = { 1, 1, 1 };
	float squareOpacity  = 1.0f;
	float lineRGB[ 3 ]   = { 1, 1, 1 };
	float lineOpacity    = 1.0f;
	float lineThickness  = 1.0f;
	int numberMode      = 1;    // 0 off, 1 decimal, 2 random, 3 ID, 4 coordinates, 5 confidence
	int digits          = 4;
	int numberPosition  = 0;    // 0 inside top, 1 above, 2 centre
	float glyphHeight   = 14.0f;
	float textRGB[ 3 ]  = { 1, 1, 1 };
	float textOpacity   = 1.0f;
	bool landmarks      = false;
	float landmarkSize  = 3.0f;
};

struct FeedSettings
{
	int mode         = 0;// 0 normal, 1 monochrome, 2 dual tone, 3 ASCII, 4 halftone
	float contrast   = 1.5f;
	float dark[ 3 ]  = { 0, 0, 0 };
	float light[ 3 ] = { 1, 1, 1 };
	float cellSize   = 12.0f;
	float mix        = 1.0f;
};

class OverlayRenderer
{
public:
	static const int kFeedCount = 5;

	OverlayRenderer() :
		feedShaders( kFeedCount ),
		feedStates( kFeedCount, 0 ),
		feedErrors( kFeedCount )
	{
	}

	bool Init( const char* plugin, std::string* error )
	{
		pluginName = plugin;
		if( !CompileOrLog( overlayShader, kOverlayVertexShader, kOverlayFragmentShader, plugin, "overlay", error ) )
			return false;

		// Glyph atlas: one byte per texel, glyph rows stored bottom-up so v grows upward like the screen.
		std::vector< unsigned char > atlas( kAtlasWidth * kAtlasHeight, 0 );
		for( int g = 0; g < kGlyphCount; ++g )
			for( int row = 0; row < 6; ++row )
				for( int col = 0; col < 4; ++col )
					if( GlyphRow( g, row )[ col ] == '#' )
						atlas[ ( 5 - row ) * kAtlasWidth + g * kAtlasSlot + col ] = 255;
		glGenTextures( 1, &atlasTexture );
		{
			ffglex::Scoped2DTextureBinding binding( atlasTexture );
			glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
			glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, kAtlasWidth, kAtlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data() );
			glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		}

		// Overlay vertices: position (2), atlas uv (2), premultiplied colour (4), glyph flag (1).
		glGenVertexArrays( 1, &vao );
		glGenBuffers( 1, &vbo );
		glBindVertexArray( vao );
		glBindBuffer( GL_ARRAY_BUFFER, vbo );
		const GLsizei stride = 9 * sizeof( float );
		glEnableVertexAttribArray( 0 );
		glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0 );
		glEnableVertexAttribArray( 1 );
		glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, stride, (void*)( 2 * sizeof( float ) ) );
		glEnableVertexAttribArray( 2 );
		glVertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, stride, (void*)( 4 * sizeof( float ) ) );
		glEnableVertexAttribArray( 3 );
		glVertexAttribPointer( 3, 1, GL_FLOAT, GL_FALSE, stride, (void*)( 8 * sizeof( float ) ) );
		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		return true;
	}

	void Release()
	{
		overlayShader.FreeGLResources();
		for( auto& shader : feedShaders )
			shader.FreeGLResources();
		std::fill( feedStates.begin(), feedStates.end(), 0 );
		if( atlasTexture != 0 )
			glDeleteTextures( 1, &atlasTexture );
		if( vbo != 0 )
			glDeleteBuffers( 1, &vbo );
		if( vao != 0 )
			glDeleteVertexArrays( 1, &vao );
		atlasTexture = vbo = vao = 0;
	}

	// Draws the camera (restyled) into the bound framebuffer. If the mode's shader was rejected, draws the
	// plain camera with `copyShader`. Returns the status line for the plugin's Status field.
	std::string DrawFeed( const FeedSettings& f, GLuint frameTexture, float width, float height, ffglex::FFGLShader& copyShader,
						  ffglex::FFGLScreenQuad& quad )
	{
		static const char* names[ kFeedCount ] = { "Normal", "Monochrome", "Dual Tone", "ASCII", "Halftone" };
		const int mode = std::min( std::max( f.mode, 0 ), kFeedCount - 1 );
		if( feedStates[ mode ] == 0 )
		{
			const std::string source = std::string( "#version 410 core\n#define FEED_MODE " ) + std::to_string( mode ) + "\n" + kFeedFragmentBody;
			const std::string pass   = std::string( "feed '" ) + names[ mode ] + "'";
			feedStates[ mode ]       = CompileOrLog( feedShaders[ mode ], kVertexShader, source.c_str(), pluginName.c_str(), pass.c_str(),
													 &feedErrors[ mode ] )
										   ? 1
										   : -1;
		}
		const bool ok                = feedStates[ mode ] == 1;
		ffglex::FFGLShader& shader   = ok ? feedShaders[ mode ] : copyShader;
		ffglex::ScopedShaderBinding binding( shader.GetGLID() );
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, atlasTexture );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, frameTexture );
		if( ok )
		{
			shader.Set( "FrameTex", 0 );
			shader.Set( "AtlasTex", 1 );
			shader.Set( "MaxUV", 1.0f, 1.0f );
			shader.Set( "Resolution", width, height );
			shader.Set( "Contrast", f.contrast );
			shader.Set( "DarkColor", f.dark[ 0 ], f.dark[ 1 ], f.dark[ 2 ] );
			shader.Set( "LightColor", f.light[ 0 ], f.light[ 1 ], f.light[ 2 ] );
			shader.Set( "CellSize", f.cellSize );
			shader.Set( "FeedMix", f.mix );
			shader.Set( "AtlasWidth", (float)kAtlasWidth );
		}
		else
		{
			shader.Set( "InputTexture", 0 );
			shader.Set( "MaxUV", 1.0f, 1.0f );
		}
		quad.Draw();
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, 0 );
		return ok ? std::string( "OK: " ) + names[ mode ] + " feed" : feedErrors[ mode ];
	}

	// Builds and draws squares, lines, numbers and landmarks, blended over the bound framebuffer.
	void DrawTracks( const std::vector< Track >& tracks, const std::vector< std::pair< int, int > >& links, const OverlayStyle& style,
					 float width, float height )
	{
		Build( tracks, links, style, width, height );
		if( vertices.empty() )
			return;
		glEnable( GL_BLEND );
		glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );//premultiplied
		ffglex::ScopedShaderBinding binding( overlayShader.GetGLID() );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, atlasTexture );
		overlayShader.Set( "AtlasTex", 0 );
		glBindVertexArray( vao );
		glBindBuffer( GL_ARRAY_BUFFER, vbo );
		glBufferData( GL_ARRAY_BUFFER, vertices.size() * sizeof( float ), vertices.data(), GL_STREAM_DRAW );
		glDrawArrays( GL_TRIANGLES, 0, (GLsizei)( vertices.size() / 9 ) );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glBindVertexArray( 0 );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glBlendFunc( GL_ONE, GL_ZERO );
		glDisable( GL_BLEND );
	}

private:
	// 4x6 glyphs, top row first. Slots 0-9 are digits, 10-20 ASCII shades from empty to dense, 21 a full stop.
	static const int kGlyphCount  = 22;
	static const int kPeriodGlyph = 21;
	static const int kAtlasSlot   = 5;//texels per glyph slot horizontally (4 + 1 gap)
	static const int kAtlasWidth  = kGlyphCount * kAtlasSlot;
	static const int kAtlasHeight = 7;

	static const char* GlyphRow( int glyph, int row )
	{
		static const char* rows[ kGlyphCount ][ 6 ] = {
			{ ".##.", "#..#", "#..#", "#..#", "#..#", ".##." },// 0
			{ ".#..", "##..", ".#..", ".#..", ".#..", "###." },// 1
			{ "###.", "...#", ".##.", "#...", "#...", "####" },// 2
			{ "###.", "...#", ".##.", "...#", "...#", "###." },// 3
			{ "#..#", "#..#", "####", "...#", "...#", "...#" },// 4
			{ "####", "#...", "###.", "...#", "...#", "###." },// 5
			{ ".##.", "#...", "###.", "#..#", "#..#", ".##." },// 6
			{ "####", "...#", "..#.", ".#..", ".#..", ".#.." },// 7
			{ ".##.", "#..#", ".##.", "#..#", "#..#", ".##." },// 8
			{ ".##.", "#..#", "#..#", ".###", "...#", ".##." },// 9
			{ "....", "....", "....", "....", "....", "...." },
			{ "....", "....", "....", "....", "....", ".#.." },
			{ "....", ".#..", "....", "....", ".#..", "...." },
			{ "....", "....", "###.", "....", "....", "...." },
			{ "....", ".#..", "###.", ".#..", "....", "...." },
			{ "....", "###.", "....", "###.", "....", "...." },
			{ "....", ".##.", "#..#", "#..#", ".##.", "...." },
			{ "#.#.", ".#..", "###.", ".#..", "#.#.", "...." },
			{ ".#.#", "####", ".#.#", "####", ".#.#", "...." },
			{ "##.#", "##.#", "..#.", ".#..", "#.##", "#.##" },
			{ ".##.", "#..#", "#.##", "#.##", "#...", ".###" },
			{ "....", "....", "....", "....", "....", ".#.." },// .
		};
		return rows[ glyph ][ row ];
	}

	void Build( const std::vector< Track >& tracks, const std::vector< std::pair< int, int > >& links, const OverlayStyle& s, float width,
				float height )
	{
		vertices.clear();
		auto colour = [ & ]( const float rgb[ 3 ], float opacity, float out[ 4 ] ) {
			for( int i = 0; i < 3; ++i )
				out[ i ] = rgb[ i ] * opacity;//premultiplied
			out[ 3 ] = opacity;
		};
		auto vertex = [ & ]( float x, float y, float u, float v, const float c[ 4 ], float glyph ) {
			const float data[ 9 ] = { x / width * 2.0f - 1.0f, y / height * 2.0f - 1.0f, u, v, c[ 0 ], c[ 1 ], c[ 2 ], c[ 3 ], glyph };
			vertices.insert( vertices.end(), data, data + 9 );
		};
		auto rect = [ & ]( float x0, float y0, float x1, float y1, const float c[ 4 ] ) {
			vertex( x0, y0, 0, 0, c, 0 );
			vertex( x1, y0, 0, 0, c, 0 );
			vertex( x1, y1, 0, 0, c, 0 );
			vertex( x0, y0, 0, 0, c, 0 );
			vertex( x1, y1, 0, 0, c, 0 );
			vertex( x0, y1, 0, 0, c, 0 );
		};
		auto line = [ & ]( float ax, float ay, float bx, float by, float thickness, const float c[ 4 ] ) {
			const float dx = bx - ax, dy = by - ay;
			const float len = std::hypot( dx, dy );
			if( len < 1e-3f )
				return;
			const float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
			vertex( ax + nx, ay + ny, 0, 0, c, 0 );
			vertex( bx + nx, by + ny, 0, 0, c, 0 );
			vertex( bx - nx, by - ny, 0, 0, c, 0 );
			vertex( ax + nx, ay + ny, 0, 0, c, 0 );
			vertex( bx - nx, by - ny, 0, 0, c, 0 );
			vertex( ax - nx, ay - ny, 0, 0, c, 0 );
		};
		auto glyph = [ & ]( int slot, float x0, float y0, float glyphW, float glyphH, const float c[ 4 ] ) {
			const float u0 = (float)( slot * kAtlasSlot ) / kAtlasWidth, u1 = (float)( slot * kAtlasSlot + 4 ) / kAtlasWidth;
			const float v0 = 0.0f, v1 = 6.0f / kAtlasHeight;
			const float x1 = x0 + glyphW, y1 = y0 + glyphH;
			vertex( x0, y0, u0, v0, c, 1 );
			vertex( x1, y0, u1, v0, c, 1 );
			vertex( x1, y1, u1, v1, c, 1 );
			vertex( x0, y0, u0, v0, c, 1 );
			vertex( x1, y1, u1, v1, c, 1 );
			vertex( x0, y1, u0, v1, c, 1 );
		};

		// Lines first, so squares and numbers sit on top of them.
		float c[ 4 ];
		for( const auto& l : links )
		{
			const Detection &a = tracks[ l.first ].shown, &b = tracks[ l.second ].shown;
			colour( s.lineRGB, s.lineOpacity * std::min( tracks[ l.first ].alpha, tracks[ l.second ].alpha ), c );
			if( c[ 3 ] > 0.004f )
				line( a.x * width, a.y * height, b.x * width, b.y * height, s.lineThickness, c );
		}

		const float glyphH   = s.glyphHeight;
		const float glyphW   = glyphH * 4.0f / 6.0f;
		const float glyphGap = glyphH / 6.0f;
		uint32_t modulus = 1;
		for( int i = 0; i < s.digits; ++i )
			modulus *= 10;

		float sq[ 4 ], fill[ 4 ], txt[ 4 ], dot[ 4 ];
		for( const Track& tr : tracks )
		{
			if( tr.alpha < 0.004f )
				continue;
			const Detection& b = tr.shown;
			colour( s.squareRGB, s.squareOpacity * tr.alpha, sq );
			colour( s.squareRGB, s.squareOpacity * 0.3f * tr.alpha, fill );
			colour( s.textRGB, s.textOpacity * tr.alpha, txt );
			colour( s.squareRGB, s.squareOpacity * tr.alpha, dot );
			const float cx = b.x * width, cy = b.y * height;
			float halfW, halfH;
			if( s.fitToTarget )
			{
				halfW = 0.5f * b.w * width * s.boxScale;
				halfH = 0.5f * b.h * height * s.boxScale;
			}
			else
				halfW = halfH = 0.5f * s.baseSize * ( 1.0f - s.sizeVariation + s.sizeVariation * std::sqrt( std::min( b.mass, 1.0f ) ) );
			halfW = std::max( halfW, s.thickness * 2.0f );
			halfH = std::max( halfH, s.thickness * 2.0f );
			const float x0 = cx - halfW, x1 = cx + halfW, y0 = cy - halfH, y1 = cy + halfH, t = s.thickness;

			if( s.squareStyle == 1 )
				rect( x0, y0, x1, y1, fill );
			if( s.squareStyle == 2 )
			{
				const float armX = halfW * 0.5f, armY = halfH * 0.5f;
				rect( x0, y1 - t, x0 + armX, y1, sq );
				rect( x0, y1 - armY, x0 + t, y1, sq );
				rect( x1 - armX, y1 - t, x1, y1, sq );
				rect( x1 - t, y1 - armY, x1, y1, sq );
				rect( x0, y0, x0 + armX, y0 + t, sq );
				rect( x0, y0, x0 + t, y0 + armY, sq );
				rect( x1 - armX, y0, x1, y0 + t, sq );
				rect( x1 - t, y0, x1, y0 + armY, sq );
			}
			else
			{
				rect( x0, y1 - t, x1, y1, sq );
				rect( x0, y0, x1, y0 + t, sq );
				rect( x0, y0, x0 + t, y1, sq );
				rect( x1 - t, y0, x1, y1, sq );
			}

			if( s.landmarks )
				for( int i = 0; i < b.landmarkCount; ++i )
				{
					const float lx = b.landmarks[ 2 * i ] * width, ly = b.landmarks[ 2 * i + 1 ] * height, r = s.landmarkSize * 0.5f;
					rect( lx - r, ly - r, lx + r, ly + r, dot );
				}

			if( s.numberMode == 0 )
				continue;
			char text[ 32 ];
			if( s.numberMode == 1 )
				std::snprintf( text, sizeof( text ), "%.4f", tr.slot / 7.0f );//0.1429, 0.2857, ... stays with the box
			else if( s.numberMode == 2 )
				std::snprintf( text, sizeof( text ), "%0*u", s.digits, tr.number % modulus );
			else if( s.numberMode == 3 )
				std::snprintf( text, sizeof( text ), "%0*u", s.digits, (uint32_t)tr.id % modulus );
			else if( s.numberMode == 4 )
				std::snprintf( text, sizeof( text ), "%04d %04d", (int)cx, (int)cy );
			else
				std::snprintf( text, sizeof( text ), "%.2f", std::min( std::max( b.score, 0.0f ), 1.0f ) );
			const int length  = (int)std::strlen( text );
			const float total = length * glyphW + ( length - 1 ) * glyphGap;
			float x           = cx - total * 0.5f;
			const float y     = s.numberPosition == 0 ? y1 - glyphH * 2.2f : ( s.numberPosition == 1 ? y1 + glyphGap * 2.0f : cy - glyphH * 0.5f );
			for( int i = 0; i < length; ++i, x += glyphW + glyphGap )
			{
				if( text[ i ] >= '0' && text[ i ] <= '9' )
					glyph( text[ i ] - '0', x, y, glyphW, glyphH, txt );
				else if( text[ i ] == '.' )
					glyph( kPeriodGlyph, x, y, glyphW, glyphH, txt );
			}
		}
	}

	std::string pluginName;
	ffglex::FFGLShader overlayShader;
	std::vector< ffglex::FFGLShader > feedShaders;
	std::vector< int > feedStates;//0 not compiled, 1 ready, -1 failed
	std::vector< std::string > feedErrors;
	GLuint atlasTexture = 0;
	GLuint vao = 0, vbo = 0;
	std::vector< float > vertices;
};

}// namespace imag
