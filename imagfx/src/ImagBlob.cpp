#include "ImagBlob.h"
#include "ImagBlobShaders.h"
#include "ImagFlameShaders.h"//Mask and moments passes are shared with IMAG Flame.
#include "PluginLog.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

using namespace ffglex;

namespace
{
enum ParamID : FFUInt32
{
	// Detection
	PID_SOURCE,
	PID_THRESHOLD,
	PID_GRID,
	PID_BLOB_MODE,
	PID_MAX_BLOBS,
	PID_SPACING,
	PID_SMOOTH,
	// Squares
	PID_SQ_STYLE,
	PID_SQ_SIZE,
	PID_SQ_THICK,
	PID_SQ_OPACITY,
	PID_SQ_R,
	PID_SQ_G,
	PID_SQ_B,
	// Lines
	PID_LINES,
	PID_LINK_COUNT,
	PID_LINK_DIST,
	PID_LINE_THICK,
	PID_LINE_OPACITY,
	PID_LINE_R,
	PID_LINE_G,
	PID_LINE_B,
	// Numbers
	PID_NUMBERS,
	PID_DIGITS,
	PID_NUMBER_RATE,
	PID_NUMBERS_ON_BEAT,
	PID_TEXT_SIZE,
	PID_TEXT_OPACITY,
	PID_TEXT_R,
	PID_TEXT_G,
	PID_TEXT_B,
	// Feed
	PID_FEED,
	PID_FEED_MIX,
	PID_CONTRAST,
	PID_CELL,
	PID_DARK_R,
	PID_DARK_G,
	PID_DARK_B,
	PID_LIGHT_R,
	PID_LIGHT_G,
	PID_LIGHT_B,
	PID_STATUS,
	PID_COUNT
};

const char* kFeedNames[] = { "Normal", "Monochrome", "Dual Tone", "ASCII", "Halftone" };
const int kFeedCount     = 5;

const GLsizei kMomentsSize = 256;
const float kMinCoverage   = 0.08f;//share of a grid cell the subject must fill to count
const int kMaxLines        = 600;

// 4x6 glyphs, top row first. Slots 0-9 are digits; 10-20 are ASCII shades from empty to dense.
const char* kGlyphRows[ 21 ][ 6 ] = {
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
};
const int kAtlasSlot   = 5;//texels per glyph slot horizontally (4 + 1 gap)
const int kAtlasWidth  = 21 * kAtlasSlot;
const int kAtlasHeight = 7;

int OptionIndex( float value, int count )
{
	return std::min( std::max( (int)std::lround( value ), 0 ), count - 1 );
}
}// namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< ImagBlob >,
	"IMBL",
	"IMAG Blob",
	2,
	1,
	1,
	0,
	FF_EFFECT,
	"Blob tracking: squares, connecting lines and numbers on moving subjects, over a normal, mono, dual tone, ASCII or halftone feed",
	"IMAG FX prototype" );

ImagBlob::ImagBlob() :
	feedShaders( kFeedCount ),
	feedStates( kFeedCount, ShaderState::NotCompiled ),
	feedErrors( kFeedCount ),
	params( PID_COUNT, 0.0f ),
	status( "Starting" )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	params[ PID_THRESHOLD ]    = 0.3f;
	params[ PID_GRID ]         = 2.0f;
	params[ PID_MAX_BLOBS ]    = 0.2f;
	params[ PID_SPACING ]      = 0.35f;
	params[ PID_SMOOTH ]       = 0.5f;
	params[ PID_SQ_SIZE ]      = 0.4f;
	params[ PID_SQ_THICK ]     = 0.2f;
	params[ PID_SQ_OPACITY ]   = 1.0f;
	params[ PID_LINES ]        = 1.0f;
	params[ PID_LINK_COUNT ]   = 1.0f;
	params[ PID_LINK_DIST ]    = 0.4f;
	params[ PID_LINE_THICK ]   = 0.1f;
	params[ PID_LINE_OPACITY ] = 0.7f;
	params[ PID_NUMBERS ]      = 1.0f;
	params[ PID_DIGITS ]       = 2.0f;
	params[ PID_NUMBER_RATE ]  = 0.3f;
	params[ PID_TEXT_SIZE ]    = 0.3f;
	params[ PID_TEXT_OPACITY ] = 1.0f;
	params[ PID_FEED_MIX ]     = 1.0f;
	params[ PID_CONTRAST ]     = 0.4f;
	params[ PID_CELL ]         = 0.3f;
	const float colours[][ 4 ] = {
		{ PID_SQ_R, 1.0f, 1.0f, 1.0f },
		{ PID_LINE_R, 1.0f, 1.0f, 1.0f },
		{ PID_TEXT_R, 1.0f, 1.0f, 1.0f },
		{ PID_DARK_R, 0.05f, 0.02f, 0.2f },
		{ PID_LIGHT_R, 1.0f, 0.85f, 0.4f },
	};
	for( const auto& c : colours )
		for( int i = 0; i < 3; ++i )
			params[ (int)c[ 0 ] + i ] = c[ i + 1 ];

	auto addOption = [ & ]( FFUInt32 id, const char* name, std::initializer_list< const char* > options ) {
		SetOptionParamInfo( id, name, (unsigned int)options.size(), params[ id ] );
		unsigned int i = 0;
		for( const char* option : options )
		{
			SetParamElementInfo( id, i, option, (float)i );
			++i;
		}
	};
	auto addColour = [ & ]( FFUInt32 first, const char* name ) {
		SetParamInfof( first, name, FF_TYPE_RED );
		SetParamInfof( first + 1, ( std::string( name ) + "_Green" ).c_str(), FF_TYPE_GREEN );
		SetParamInfof( first + 2, ( std::string( name ) + "_Blue" ).c_str(), FF_TYPE_BLUE );
	};
	auto group = [ & ]( FFUInt32 first, FFUInt32 last, const char* name ) {
		for( FFUInt32 id = first; id <= last; ++id )
			SetParamGroup( id, name );
	};

	addOption( PID_SOURCE, "Detect", { "Motion", "Keyed alpha", "Brightness", "Motion in key" } );
	SetParamInfof( PID_THRESHOLD, "Threshold", FF_TYPE_STANDARD );
	addOption( PID_GRID, "Grid", { "8 x 8", "16 x 16", "32 x 32", "64 x 64" } );
	addOption( PID_BLOB_MODE, "Blob Mode", { "Points", "Regions" } );
	SetParamInfof( PID_MAX_BLOBS, "Max Blobs", FF_TYPE_STANDARD );
	SetParamInfof( PID_SPACING, "Spacing", FF_TYPE_STANDARD );
	SetParamInfof( PID_SMOOTH, "Smoothing", FF_TYPE_STANDARD );
	group( PID_SOURCE, PID_SMOOTH, "Detection" );

	addOption( PID_SQ_STYLE, "Square Style", { "Outline", "Filled", "Corners" } );
	SetParamInfof( PID_SQ_SIZE, "Square Size", FF_TYPE_STANDARD );
	SetParamInfof( PID_SQ_THICK, "Square Thickness", FF_TYPE_STANDARD );
	SetParamInfof( PID_SQ_OPACITY, "Square Intensity", FF_TYPE_STANDARD );
	addColour( PID_SQ_R, "Square Colour" );
	group( PID_SQ_STYLE, PID_SQ_B, "Squares" );

	addOption( PID_LINES, "Lines", { "Off", "Nearest", "Network" } );
	addOption( PID_LINK_COUNT, "Links Per Blob", { "1", "2", "3", "4" } );
	SetParamInfof( PID_LINK_DIST, "Link Distance", FF_TYPE_STANDARD );
	SetParamInfof( PID_LINE_THICK, "Line Thickness", FF_TYPE_STANDARD );
	SetParamInfof( PID_LINE_OPACITY, "Line Intensity", FF_TYPE_STANDARD );
	addColour( PID_LINE_R, "Line Colour" );
	group( PID_LINES, PID_LINE_B, "Lines" );

	addOption( PID_NUMBERS, "Numbers", { "Off", "Random", "Blob ID", "Coordinates" } );
	addOption( PID_DIGITS, "Digits", { "2", "3", "4", "5", "6" } );
	SetParamInfof( PID_NUMBER_RATE, "Number Speed", FF_TYPE_STANDARD );
	SetParamInfo( PID_NUMBERS_ON_BEAT, "Numbers On Beat", FF_TYPE_BOOLEAN, false );
	SetParamInfof( PID_TEXT_SIZE, "Number Size", FF_TYPE_STANDARD );
	SetParamInfof( PID_TEXT_OPACITY, "Number Intensity", FF_TYPE_STANDARD );
	addColour( PID_TEXT_R, "Number Colour" );
	group( PID_NUMBERS, PID_TEXT_B, "Numbers" );

	addOption( PID_FEED, "Feed", { "Normal", "Monochrome", "Dual Tone", "ASCII", "Halftone" } );
	SetParamInfof( PID_FEED_MIX, "Feed Mix", FF_TYPE_STANDARD );
	SetParamInfof( PID_CONTRAST, "Feed Contrast", FF_TYPE_STANDARD );
	SetParamInfof( PID_CELL, "ASCII / Dot Size", FF_TYPE_STANDARD );
	addColour( PID_DARK_R, "Feed Dark" );
	addColour( PID_LIGHT_R, "Feed Light" );
	SetParamInfo( PID_STATUS, "Status", FF_TYPE_TEXT, status.c_str() );
	group( PID_FEED, PID_STATUS, "Feed" );
}

FFResult ImagBlob::InitGL( const FFGLViewportStruct* vp )
{
	std::string error;
	if( !CompileOrLog( copyShader, kVertexShader, kCopyFragmentShader, "IMAG Blob", "copy", &error ) ||
		!CompileOrLog( maskShader, kVertexShader, kMaskFragmentShader, "IMAG Blob", "mask", &error ) ||
		!CompileOrLog( momentsShader, kVertexShader, kMomentsFragmentShader, "IMAG Blob", "moments", &error ) ||
		!CompileOrLog( overlayShader, kOverlayVertexShader, kOverlayFragmentShader, "IMAG Blob", "overlay", &error ) ||
		!quad.Initialise() )
	{
		SetStatus( error.empty() ? "Could not create GPU resources" : error );
		DeInitGL();
		return FF_FAIL;
	}

	// Glyph atlas: one byte per texel, glyph rows stored bottom-up so v grows upward like the screen.
	std::vector< unsigned char > atlas( kAtlasWidth * kAtlasHeight, 0 );
	for( int g = 0; g < 21; ++g )
		for( int row = 0; row < 6; ++row )
			for( int col = 0; col < 4; ++col )
				if( kGlyphRows[ g ][ row ][ col ] == '#' )
					atlas[ ( 5 - row ) * kAtlasWidth + g * kAtlasSlot + col ] = 255;
	glGenTextures( 1, &atlasTexture );
	{
		Scoped2DTextureBinding binding( atlasTexture );
		glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, kAtlasWidth, kAtlasHeight, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data() );
		glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	}

	// Overlay vertices: position (2), atlas uv (2), premultiplied colour (4), glyph flag (1).
	glGenVertexArrays( 1, &overlayVAO );
	glGenBuffers( 1, &overlayVBO );
	glBindVertexArray( overlayVAO );
	glBindBuffer( GL_ARRAY_BUFFER, overlayVBO );
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

	FFGLLog::LogToHost( "IMAG Blob: ready" );
	return CFFGLPlugin::InitGL( vp );
}

FFResult ImagBlob::DeInitGL()
{
	for( FFGLShader* shader : { &copyShader, &maskShader, &momentsShader, &overlayShader } )
		shader->FreeGLResources();
	for( auto& shader : feedShaders )
		shader.FreeGLResources();
	std::fill( feedStates.begin(), feedStates.end(), ShaderState::NotCompiled );
	quad.Release();
	ReleaseTargets();
	if( atlasTexture != 0 )
		glDeleteTextures( 1, &atlasTexture );
	if( overlayVBO != 0 )
		glDeleteBuffers( 1, &overlayVBO );
	if( overlayVAO != 0 )
		glDeleteVertexArrays( 1, &overlayVAO );
	atlasTexture = overlayVBO = overlayVAO = 0;
	return FF_SUCCESS;
}

bool ImagBlob::EnsureTargets( GLsizei width, GLsizei height )
{
	if( width == targetWidth && height == targetHeight && momentsFBO != 0 )
		return true;
	ReleaseTargets();
	maskWidth  = std::max( width / 2, 1 );
	maskHeight = std::max( height / 2, 1 );
	bool ok    = frames[ 0 ].Initialise( width, height ) && frames[ 1 ].Initialise( width, height ) &&
			  mask.Initialise( maskWidth, maskHeight, GL_RGBA16F );
	if( ok )
	{
		glGenTextures( 2, momentTextures );
		for( GLuint tex : momentTextures )
		{
			Scoped2DTextureBinding binding( tex );
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, kMomentsSize, kMomentsSize, 0, GL_RGBA, GL_FLOAT, nullptr );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
			glGenerateMipmap( GL_TEXTURE_2D );
		}
		glGenFramebuffers( 1, &momentsFBO );
		ScopedFBOBinding fboBinding( momentsFBO, ScopedFBOBinding::RB_REVERT );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, momentTextures[ 0 ], 0 );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, momentTextures[ 1 ], 0 );
		const GLenum drawBuffers[] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		glDrawBuffers( 2, drawBuffers );
		ok = glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
	}
	if( !ok )
	{
		ReleaseTargets();
		return false;
	}
	targetWidth  = width;
	targetHeight = height;
	primeHistory = true;
	blobs.clear();
	return true;
}

void ImagBlob::ReleaseTargets()
{
	for( FFGLFBO* fbo : { &frames[ 0 ], &frames[ 1 ], &mask } )
		fbo->Release();
	if( momentTextures[ 0 ] != 0 )
		glDeleteTextures( 2, momentTextures );
	momentTextures[ 0 ] = momentTextures[ 1 ] = 0;
	if( momentsFBO != 0 )
		glDeleteFramebuffers( 1, &momentsFBO );
	momentsFBO   = 0;
	targetWidth  = 0;
	targetHeight = 0;
}

void ImagBlob::SetStatus( const std::string& text )
{
	if( text == status )
		return;
	status = text;
	RaiseParamEvent( PID_STATUS, FF_EVENT_FLAG_VALUE );
}

FFGLShader* ImagBlob::FeedShader( int mode )
{
	if( feedStates[ mode ] == ShaderState::NotCompiled )
	{
		const std::string source = std::string( "#version 410 core\n#define FEED_MODE " ) + std::to_string( mode ) + "\n" + kFeedFragmentBody;
		const std::string pass   = std::string( "feed '" ) + kFeedNames[ mode ] + "'";
		bool ok = CompileOrLog( feedShaders[ mode ], kVertexShader, source.c_str(), "IMAG Blob", pass.c_str(), &feedErrors[ mode ] );
		feedStates[ mode ] = ok ? ShaderState::Ready : ShaderState::Failed;
	}
	if( feedStates[ mode ] == ShaderState::Failed )
	{
		SetStatus( feedErrors[ mode ] );
		return nullptr;
	}
	SetStatus( std::string( "OK: " ) + kFeedNames[ mode ] + " feed" );
	return &feedShaders[ mode ];
}

uint32_t ImagBlob::Random()
{
	rngState ^= rngState << 13;
	rngState ^= rngState >> 17;
	rngState ^= rngState << 5;
	return rngState;
}

// Turns the read-back moments grid into blobs and carries identities over from the previous frame.
void ImagBlob::DetectBlobs( float dt )
{
	const int level = 5 - OptionIndex( params[ PID_GRID ], 4 );//8, 16, 32 or 64 cells per side
	const int cells = kMomentsSize >> level;
	gridData.resize( cells * cells * 4 );
	{
		Scoped2DTextureBinding binding( momentTextures[ 0 ] );
		glGetTexImage( GL_TEXTURE_2D, level, GL_RGBA, GL_FLOAT, gridData.data() );
	}
	auto massAt = [ & ]( int cx, int cy ) { return gridData[ ( cy * cells + cx ) * 4 + 2 ]; };

	std::vector< Blob > found;
	const float cellSize = 1.0f / cells;
	if( OptionIndex( params[ PID_BLOB_MODE ], 2 ) == 0 )
	{
		for( int cy = 0; cy < cells; ++cy )
			for( int cx = 0; cx < cells; ++cx )
			{
				const float m = massAt( cx, cy );
				if( m < kMinCoverage )
					continue;
				const float* d = &gridData[ ( cy * cells + cx ) * 4 ];
				Blob b;
				b.x = d[ 0 ] / m;
				b.y = d[ 1 ] / m;
				b.w = b.h = cellSize * std::sqrt( m );
				b.mass    = m;
				found.push_back( b );
			}
	}
	else
	{
		// Connected regions of covered cells (4-neighbour flood fill).
		std::vector< int > label( cells * cells, -1 );
		std::vector< int > stack;
		for( int start = 0; start < cells * cells; ++start )
		{
			if( label[ start ] >= 0 || gridData[ start * 4 + 2 ] < kMinCoverage )
				continue;
			float sumM = 0, sumX = 0, sumY = 0;
			float minX = 1, minY = 1, maxX = 0, maxY = 0;
			stack.assign( 1, start );
			label[ start ] = (int)found.size();
			while( !stack.empty() )
			{
				const int i = stack.back();
				stack.pop_back();
				const int cx = i % cells, cy = i / cells;
				const float* d = &gridData[ i * 4 ];
				sumM += d[ 2 ];
				sumX += d[ 0 ];
				sumY += d[ 1 ];
				minX = std::min( minX, cx * cellSize );
				maxX = std::max( maxX, ( cx + 1 ) * cellSize );
				minY = std::min( minY, cy * cellSize );
				maxY = std::max( maxY, ( cy + 1 ) * cellSize );
				const int neighbours[ 4 ][ 2 ] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
				for( const auto& n : neighbours )
				{
					const int nx = cx + n[ 0 ], ny = cy + n[ 1 ];
					if( nx < 0 || ny < 0 || nx >= cells || ny >= cells )
						continue;
					const int j = ny * cells + nx;
					if( label[ j ] >= 0 || gridData[ j * 4 + 2 ] < kMinCoverage )
						continue;
					label[ j ] = (int)found.size();
					stack.push_back( j );
				}
			}
			Blob b;
			b.x    = sumX / sumM;
			b.y    = sumY / sumM;
			b.w    = maxX - minX;
			b.h    = maxY - minY;
			b.mass = sumM;
			found.push_back( b );
		}
	}

	// Strongest blobs first; drop any that sit closer than Spacing to one already kept, so squares and
	// their numbers stay readable instead of piling up.
	const float aspect = targetHeight > 0 ? (float)targetWidth / targetHeight : 1.0f;
	std::sort( found.begin(), found.end(), []( const Blob& a, const Blob& b ) { return a.mass > b.mass; } );
	const size_t maxBlobs = 1 + (size_t)std::lround( params[ PID_MAX_BLOBS ] * 63.0f );
	const float spacing   = params[ PID_SPACING ] * 0.25f;//in frame heights
	std::vector< Blob > kept;
	for( const Blob& b : found )
	{
		if( kept.size() >= maxBlobs )
			break;
		bool tooClose = false;
		for( const Blob& k : kept )
			tooClose = tooClose || std::hypot( ( b.x - k.x ) * aspect, b.y - k.y ) < spacing;
		if( !tooClose )
			kept.push_back( b );
	}
	found.swap( kept );

	// Keep identities: each new blob takes over the nearest unclaimed blob from last frame, if close enough.
	const float follow = 1.0f - params[ PID_SMOOTH ] * 0.9f;
	const float rate   = 0.5f + params[ PID_NUMBER_RATE ] * params[ PID_NUMBER_RATE ] * 19.5f;//changes per second
	for( Blob& old : blobs )
		old.matched = false;
	for( Blob& b : found )
	{
		Blob* best     = nullptr;
		float bestDist = 0.08f + 0.5f * std::max( b.w, b.h );
		for( Blob& old : blobs )
		{
			if( old.matched )
				continue;
			const float d = std::hypot( ( b.x - old.x ) * aspect, b.y - old.y );
			if( d < bestDist )
			{
				bestDist = d;
				best     = &old;
			}
		}
		if( best )
		{
			best->matched = true;
			b.id          = best->id;
			b.number      = best->number;
			b.nextChange  = best->nextChange;
			b.x           = best->x + ( b.x - best->x ) * follow;
			b.y           = best->y + ( b.y - best->y ) * follow;
			b.w           = best->w + ( b.w - best->w ) * follow;
			b.h           = best->h + ( b.h - best->h ) * follow;
		}
		else
		{
			b.id         = nextBlobId++;
			b.number     = Random();
			b.nextChange = time + ( 0.5f + ( Random() % 1000 ) / 1000.0f ) / rate;
		}
	}
	const bool onBeat = params[ PID_NUMBERS_ON_BEAT ] > 0.5f;
	for( Blob& b : found )
	{
		if( onBeat ? beatHit : time >= b.nextChange )
		{
			b.number     = Random();
			b.nextChange = time + ( 0.5f + ( Random() % 1000 ) / 1000.0f ) / rate;
		}
	}
	blobs.swap( found );
	(void)dt;

	// Lines between blobs, distances measured with the frame's aspect so they look right on screen.
	links.clear();
	const int lineMode  = OptionIndex( params[ PID_LINES ], 3 );
	const float maxDist = 0.05f + params[ PID_LINK_DIST ] * 0.6f;
	std::set< std::pair< int, int > > unique;
	auto distance = [ & ]( const Blob& a, const Blob& b ) { return std::hypot( ( a.x - b.x ) * aspect, a.y - b.y ); };
	if( lineMode == 1 )
	{
		const int perBlob = OptionIndex( params[ PID_LINK_COUNT ], 4 ) + 1;
		std::vector< std::pair< float, int > > nearest;
		for( int i = 0; i < (int)blobs.size(); ++i )
		{
			nearest.clear();
			for( int j = 0; j < (int)blobs.size(); ++j )
			{
				const float d = distance( blobs[ i ], blobs[ j ] );
				if( j != i && d < maxDist )
					nearest.push_back( { d, j } );
			}
			std::sort( nearest.begin(), nearest.end() );
			for( int k = 0; k < (int)nearest.size() && k < perBlob; ++k )
				unique.insert( { std::min( i, nearest[ k ].second ), std::max( i, nearest[ k ].second ) } );
		}
	}
	else if( lineMode == 2 )
	{
		for( int i = 0; i < (int)blobs.size(); ++i )
			for( int j = i + 1; j < (int)blobs.size() && (int)unique.size() < kMaxLines; ++j )
				if( distance( blobs[ i ], blobs[ j ] ) < maxDist )
					unique.insert( { i, j } );
	}
	links.assign( unique.begin(), unique.end() );
}

// Builds the triangle list for squares, lines and numbers, in pixels converted to clip space.
void ImagBlob::BuildOverlay( float width, float height )
{
	vertices.clear();
	auto colour = [ & ]( FFUInt32 first, float opacity, float out[ 4 ] ) {
		for( int i = 0; i < 3; ++i )
			out[ i ] = params[ first + i ] * opacity;//premultiplied
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
		float dx = bx - ax, dy = by - ay;
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

	const float scale = height / 1080.0f;//sizes are tuned for 1080p and scale with the frame

	// Lines first, so squares and numbers sit on top of them.
	if( !links.empty() )
	{
		float c[ 4 ];
		colour( PID_LINE_R, params[ PID_LINE_OPACITY ], c );
		const float thickness = ( 1.0f + params[ PID_LINE_THICK ] * 5.0f ) * scale;
		for( const auto& l : links )
			line( blobs[ l.first ].x * width, blobs[ l.first ].y * height, blobs[ l.second ].x * width, blobs[ l.second ].y * height,
				  thickness, c );
	}

	float sq[ 4 ], fill[ 4 ], txt[ 4 ];
	colour( PID_SQ_R, params[ PID_SQ_OPACITY ], sq );
	colour( PID_SQ_R, params[ PID_SQ_OPACITY ] * 0.3f, fill );
	colour( PID_TEXT_R, params[ PID_TEXT_OPACITY ], txt );
	const int style        = OptionIndex( params[ PID_SQ_STYLE ], 3 );
	const bool regions     = OptionIndex( params[ PID_BLOB_MODE ], 2 ) == 1;
	const float thickness  = ( 1.0f + params[ PID_SQ_THICK ] * 5.0f ) * scale;
	const float baseSize   = ( 8.0f + params[ PID_SQ_SIZE ] * 50.0f ) * scale;
	const int numberMode   = OptionIndex( params[ PID_NUMBERS ], 4 );
	const int digits       = OptionIndex( params[ PID_DIGITS ], 5 ) + 2;
	const float glyphH     = ( 10.0f + params[ PID_TEXT_SIZE ] * 40.0f ) * scale;
	const float glyphW     = glyphH * 4.0f / 6.0f;
	const float glyphGap   = glyphH / 6.0f;
	uint32_t modulus = 1;
	for( int i = 0; i < digits; ++i )
		modulus *= 10;

	for( const Blob& b : blobs )
	{
		const float cx = b.x * width, cy = b.y * height;
		// Points: size follows how much of the cell is covered. Regions: a square around the region.
		float half = regions ? 0.5f * std::max( b.w * width, b.h * height ) * ( 0.6f + params[ PID_SQ_SIZE ] * 0.8f )
							 : 0.5f * baseSize * ( 0.6f + 0.4f * std::sqrt( std::min( b.mass, 1.0f ) ) );
		half = std::max( half, thickness * 2.0f );
		const float x0 = cx - half, x1 = cx + half, y0 = cy - half, y1 = cy + half, t = thickness;

		if( style == 1 )
			rect( x0, y0, x1, y1, fill );
		if( style == 2 )
		{
			const float arm = half * 0.5f;
			rect( x0, y1 - t, x0 + arm, y1, sq );
			rect( x0, y1 - arm, x0 + t, y1, sq );
			rect( x1 - arm, y1 - t, x1, y1, sq );
			rect( x1 - t, y1 - arm, x1, y1, sq );
			rect( x0, y0, x0 + arm, y0 + t, sq );
			rect( x0, y0, x0 + t, y0 + arm, sq );
			rect( x1 - arm, y0, x1, y0 + t, sq );
			rect( x1 - t, y0, x1, y0 + arm, sq );
		}
		else
		{
			rect( x0, y1 - t, x1, y1, sq );
			rect( x0, y0, x1, y0 + t, sq );
			rect( x0, y0, x0 + t, y1, sq );
			rect( x1 - t, y0, x1, y1, sq );
		}
		rect( cx - t, cy - t, cx + t, cy + t, sq );//centre mark

		if( numberMode == 0 )
			continue;
		char text[ 32 ];
		if( numberMode == 1 )
			std::snprintf( text, sizeof( text ), "%0*u", digits, b.number % modulus );
		else if( numberMode == 2 )
			std::snprintf( text, sizeof( text ), "%0*u", digits, (uint32_t)b.id % modulus );
		else
			std::snprintf( text, sizeof( text ), "%04d %04d", (int)cx, (int)cy );
		const int length  = (int)std::strlen( text );
		const float total = length * glyphW + ( length - 1 ) * glyphGap;
		float x           = cx - total * 0.5f;
		const float y     = y1 + glyphGap * 2.0f;
		for( int i = 0; i < length; ++i, x += glyphW + glyphGap )
			if( text[ i ] >= '0' && text[ i ] <= '9' )
				glyph( text[ i ] - '0', x, y, glyphW, glyphH, txt );
	}
}

FFResult ImagBlob::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];
	if( !EnsureTargets( (GLsizei)input.Width, (GLsizei)input.Height ) )
		return FF_FAIL;

	float dt = 1.0f / 60.0f;
	if( lastHostTime >= 0.0 && hostTime > lastHostTime )
		dt = std::min( (float)( hostTime - lastHostTime ), 0.1f );
	lastHostTime = hostTime;
	time += dt;
	const float beat = std::fmod( barPhase * 4.0f, 1.0f );
	beatHit          = beat < lastBeat;
	lastBeat         = beat;

	auto bindTexture = []( GLenum unit, GLuint tex ) {
		glActiveTexture( GL_TEXTURE0 + unit );
		glBindTexture( GL_TEXTURE_2D, tex );
	};
	auto unbindTextures = [ & ]( GLenum count ) {
		for( GLenum unit = count; unit-- > 0; )
			bindTexture( unit, 0 );
	};
	const float width  = (float)targetWidth;
	const float height = (float)targetHeight;
	const int previous = currentFrame;
	currentFrame       = 1 - currentFrame;
	const GLuint frameTex = frames[ currentFrame ].GetTextureInfo().Handle;

	// 1. Camera frame (both slots on the first frame, so motion detection starts calm).
	glViewport( 0, 0, targetWidth, targetHeight );
	{
		FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );
		for( int slot : { currentFrame, previous } )
		{
			if( slot == previous && !primeHistory )
				continue;
			ScopedFBOBinding fboBinding( frames[ slot ].GetGLID(), ScopedFBOBinding::RB_REVERT );
			ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
			bindTexture( 0, input.Handle );
			copyShader.Set( "InputTexture", 0 );
			copyShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
			quad.Draw();
			unbindTextures( 1 );
		}
		primeHistory = false;
	}

	// 2. Subject mask.
	glViewport( 0, 0, maskWidth, maskHeight );
	{
		ScopedFBOBinding fboBinding( mask.GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( maskShader.GetGLID() );
		bindTexture( 0, frameTex );
		bindTexture( 1, frames[ previous ].GetTextureInfo().Handle );
		maskShader.Set( "CurrentTex", 0 );
		maskShader.Set( "PreviousTex", 1 );
		maskShader.Set( "MaxUV", 1.0f, 1.0f );
		maskShader.Set( "FrameTexel", 1.0f / width, 1.0f / height );
		maskShader.Set( "Source", OptionIndex( params[ PID_SOURCE ], 4 ) );
		maskShader.Set( "Threshold", params[ PID_THRESHOLD ] );
		quad.Draw();
		unbindTextures( 2 );
	}

	// 3. Moments, mipmapped, then read one small level back for blob detection.
	glViewport( 0, 0, kMomentsSize, kMomentsSize );
	{
		ScopedFBOBinding fboBinding( momentsFBO, ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( momentsShader.GetGLID() );
		bindTexture( 0, mask.GetTextureInfo().Handle );
		momentsShader.Set( "MaskTex", 0 );
		momentsShader.Set( "MaxUV", 1.0f, 1.0f );
		quad.Draw();
		unbindTextures( 1 );
	}
	{
		Scoped2DTextureBinding binding( momentTextures[ 0 ] );
		glGenerateMipmap( GL_TEXTURE_2D );
	}
	DetectBlobs( dt );
	BuildOverlay( width, height );

	// 4. Feed into the host framebuffer. A rejected feed shader shows the plain camera and reports why.
	glViewport( currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height );
	{
		FFGLShader* shader = FeedShader( OptionIndex( params[ PID_FEED ], kFeedCount ) );
		FFGLShader& use    = shader ? *shader : copyShader;
		ScopedShaderBinding shaderBinding( use.GetGLID() );
		bindTexture( 0, frameTex );
		bindTexture( 1, atlasTexture );
		if( shader )
		{
			shader->Set( "FrameTex", 0 );
			shader->Set( "AtlasTex", 1 );
			shader->Set( "MaxUV", 1.0f, 1.0f );
			shader->Set( "Resolution", width, height );
			shader->Set( "Contrast", 0.5f + params[ PID_CONTRAST ] * 2.5f );
			shader->Set( "DarkColor", params[ PID_DARK_R ], params[ PID_DARK_G ], params[ PID_DARK_B ] );
			shader->Set( "LightColor", params[ PID_LIGHT_R ], params[ PID_LIGHT_G ], params[ PID_LIGHT_B ] );
			shader->Set( "CellSize", ( 6.0f + params[ PID_CELL ] * 24.0f ) * height / 1080.0f );
			shader->Set( "FeedMix", params[ PID_FEED_MIX ] );
		}
		else
		{
			copyShader.Set( "InputTexture", 0 );
			copyShader.Set( "MaxUV", 1.0f, 1.0f );
		}
		quad.Draw();
		unbindTextures( 2 );
	}

	// 5. Tracking graphics, blended over the feed.
	if( !vertices.empty() )
	{
		glEnable( GL_BLEND );
		glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );//premultiplied
		ScopedShaderBinding shaderBinding( overlayShader.GetGLID() );
		bindTexture( 0, atlasTexture );
		overlayShader.Set( "AtlasTex", 0 );
		glBindVertexArray( overlayVAO );
		glBindBuffer( GL_ARRAY_BUFFER, overlayVBO );
		glBufferData( GL_ARRAY_BUFFER, vertices.size() * sizeof( float ), vertices.data(), GL_STREAM_DRAW );
		glDrawArrays( GL_TRIANGLES, 0, (GLsizei)( vertices.size() / 9 ) );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glBindVertexArray( 0 );
		unbindTextures( 1 );
		glBlendFunc( GL_ONE, GL_ZERO );
		glDisable( GL_BLEND );
	}
	return FF_SUCCESS;
}

FFResult ImagBlob::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PID_COUNT )
		return FF_FAIL;
	if( index != PID_STATUS )
		params[ index ] = value;
	return FF_SUCCESS;
}

float ImagBlob::GetFloatParameter( unsigned int index )
{
	return index < PID_COUNT ? params[ index ] : 0.0f;
}

FFResult ImagBlob::SetTextParameter( unsigned int index, const char* )
{
	return index == PID_STATUS ? FF_SUCCESS : FF_FAIL;//Status is read-only.
}

char* ImagBlob::GetTextParameter( unsigned int index )
{
	return index == PID_STATUS ? const_cast< char* >( status.c_str() ) : nullptr;
}
