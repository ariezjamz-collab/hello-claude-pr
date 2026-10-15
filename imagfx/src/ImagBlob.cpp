#include "ImagBlob.h"
#include "ImagFlameShaders.h"//Mask and moments passes are shared with IMAG Flame.
#include "PluginLog.h"
#include "ColorUtil.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace ffglex;

namespace
{
enum ParamID : FFUInt32
{
	PID_ENABLED,
	// Detection
	PID_SOURCE,
	PID_THRESHOLD,
	PID_GRID,
	PID_BLOB_MODE,
	PID_MAX_BLOBS,
	PID_SPACING,
	PID_SMOOTH,
	PID_HOLD,
	// Squares
	PID_SQ_STYLE,
	PID_SQ_SIZE,
	PID_SIZE_VAR,
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
	PID_NUMBER_POS,
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

const GLsizei kMomentsSize = 256;
const float kMinCoverage   = 0.08f;//share of a grid cell the subject must fill to count
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
	params( PID_COUNT, 0.0f ),
	status( "Starting" )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	// Defaults follow the classic blob-tracking look: a handful of steady, large, thin-outlined squares,
	// decimal labels inside them, and a single chain of lines.
	params[ PID_ENABLED ]      = 1.0f;
	params[ PID_SOURCE ]       = 2.0f;//Brightness: tracks lit areas, works on a static shot too
	params[ PID_THRESHOLD ]    = 0.45f;
	params[ PID_GRID ]         = 1.0f;
	params[ PID_MAX_BLOBS ]    = 0.12f;
	params[ PID_SPACING ]      = 0.35f;
	params[ PID_SMOOTH ]       = 0.75f;
	params[ PID_HOLD ]         = 0.3f;
	params[ PID_SQ_SIZE ]      = 0.5f;
	params[ PID_SIZE_VAR ]     = 0.0f;
	params[ PID_SQ_THICK ]     = 0.0f;
	params[ PID_SQ_OPACITY ]   = 0.85f;
	params[ PID_LINES ]        = 1.0f;
	params[ PID_LINK_COUNT ]   = 1.0f;
	params[ PID_LINK_DIST ]    = 0.4f;
	params[ PID_LINE_THICK ]   = 0.0f;
	params[ PID_LINE_OPACITY ] = 0.75f;
	params[ PID_NUMBERS ]      = 1.0f;
	params[ PID_DIGITS ]       = 2.0f;
	params[ PID_NUMBER_RATE ]  = 0.15f;
	params[ PID_TEXT_SIZE ]    = 0.12f;
	params[ PID_TEXT_OPACITY ] = 0.9f;
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
		RgbToHsb( &c[ 1 ], &params[ (int)c[ 0 ] ] );//colour controls are hue / saturation / brightness

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
		SetParamInfof( first, name, FF_TYPE_HUE );
		SetParamInfof( first + 1, ( std::string( name ) + "_Sat" ).c_str(), FF_TYPE_SATURATION );
		SetParamInfof( first + 2, ( std::string( name ) + "_Bri" ).c_str(), FF_TYPE_BRIGHTNESS );
	};
	auto group = [ & ]( FFUInt32 first, FFUInt32 last, const char* name ) {
		for( FFUInt32 id = first; id <= last; ++id )
			SetParamGroup( id, name );
	};

	SetParamInfo( PID_ENABLED, "Effect On", FF_TYPE_BOOLEAN, true );
	addOption( PID_SOURCE, "Detect", { "Motion", "Keyed alpha", "Brightness", "Motion in key" } );
	SetParamInfof( PID_THRESHOLD, "Threshold", FF_TYPE_STANDARD );
	addOption( PID_GRID, "Grid", { "8 x 8", "16 x 16", "32 x 32", "64 x 64" } );
	addOption( PID_BLOB_MODE, "Blob Mode", { "Points", "Regions" } );
	SetParamInfof( PID_MAX_BLOBS, "Max Blobs", FF_TYPE_STANDARD );
	SetParamInfof( PID_SPACING, "Spacing", FF_TYPE_STANDARD );
	SetParamInfof( PID_SMOOTH, "Smoothing", FF_TYPE_STANDARD );
	SetParamInfof( PID_HOLD, "Hold Time", FF_TYPE_STANDARD );
	group( PID_SOURCE, PID_HOLD, "Detection" );

	addOption( PID_SQ_STYLE, "Square Style", { "Outline", "Filled", "Corners" } );
	SetParamInfof( PID_SQ_SIZE, "Square Size", FF_TYPE_STANDARD );
	SetParamInfof( PID_SIZE_VAR, "Size Variation", FF_TYPE_STANDARD );
	SetParamInfof( PID_SQ_THICK, "Square Thickness", FF_TYPE_STANDARD );
	SetParamInfof( PID_SQ_OPACITY, "Square Intensity", FF_TYPE_STANDARD );
	addColour( PID_SQ_R, "Square Colour" );
	group( PID_SQ_STYLE, PID_SQ_B, "Squares" );

	addOption( PID_LINES, "Lines", { "Off", "Chain", "Nearest", "Network" } );
	addOption( PID_LINK_COUNT, "Links Per Blob", { "1", "2", "3", "4" } );
	SetParamInfof( PID_LINK_DIST, "Link Distance", FF_TYPE_STANDARD );
	SetParamInfof( PID_LINE_THICK, "Line Thickness", FF_TYPE_STANDARD );
	SetParamInfof( PID_LINE_OPACITY, "Line Intensity", FF_TYPE_STANDARD );
	addColour( PID_LINE_R, "Line Colour" );
	group( PID_LINES, PID_LINE_B, "Lines" );

	addOption( PID_NUMBERS, "Numbers", { "Off", "Decimal", "Random", "Blob ID", "Coordinates" } );
	addOption( PID_DIGITS, "Digits", { "2", "3", "4", "5", "6" } );
	SetParamInfof( PID_NUMBER_RATE, "Number Speed", FF_TYPE_STANDARD );
	SetParamInfo( PID_NUMBERS_ON_BEAT, "Numbers On Beat", FF_TYPE_BOOLEAN, false );
	SetParamInfof( PID_TEXT_SIZE, "Number Size", FF_TYPE_STANDARD );
	addOption( PID_NUMBER_POS, "Number Position", { "Inside top", "Above", "Centre" } );
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
		!overlay.Init( "IMAG Blob", &error ) ||
		!quad.Initialise() )
	{
		SetStatus( error.empty() ? "Could not create GPU resources" : error );
		DeInitGL();
		return FF_FAIL;
	}

	FFGLLog::LogToHost( "IMAG Blob: ready" );
	return CFFGLPlugin::InitGL( vp );
}

FFResult ImagBlob::DeInitGL()
{
	for( FFGLShader* shader : { &copyShader, &maskShader, &momentsShader } )
		shader->FreeGLResources();
	overlay.Release();
	quad.Release();
	ReleaseTargets();
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
	tracker.Reset();
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

// Turns the read-back moments grid into blob detections.
std::vector< imag::Detection > ImagBlob::DetectBlobs( float dt )
{
	const int level = 5 - OptionIndex( params[ PID_GRID ], 4 );//8, 16, 32 or 64 cells per side
	const int cells = kMomentsSize >> level;
	gridData.resize( cells * cells * 4 );
	{
		Scoped2DTextureBinding binding( momentTextures[ 0 ] );
		glGetTexImage( GL_TEXTURE_2D, level, GL_RGBA, GL_FLOAT, gridData.data() );
	}
	// Average the grid over time (Smoothing) so a pixel of noise can't make a blob appear for one frame.
	if( smoothedGrid.size() != gridData.size() )
		smoothedGrid = gridData;
	const float keep = std::pow( params[ PID_SMOOTH ] * 0.6f, dt * 60.0f );
	for( size_t i = 0; i < gridData.size(); ++i )
		smoothedGrid[ i ] = smoothedGrid[ i ] * keep + gridData[ i ] * ( 1.0f - keep );
	gridData = smoothedGrid;
	const float aspect = targetHeight > 0 ? (float)targetWidth / targetHeight : 1.0f;
	auto massAt = [ & ]( int cx, int cy ) { return gridData[ ( cy * cells + cx ) * 4 + 2 ]; };

	std::vector< imag::Detection > found;
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
				imag::Detection b;
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
			imag::Detection b;
			b.x    = sumX / sumM;
			b.y    = sumY / sumM;
			const float side = std::max( ( maxX - minX ) * aspect, maxY - minY );//square on screen
			b.w    = side / aspect;
			b.h    = side;
			b.mass = sumM;
			found.push_back( b );
		}
	}

	// Strongest blobs first; drop any that sit closer than Spacing to one already kept, so squares and
	// their numbers stay readable instead of piling up.
	std::sort( found.begin(), found.end(), []( const imag::Detection& a, const imag::Detection& b ) { return a.mass > b.mass; } );
	const size_t maxBlobs = 1 + (size_t)std::lround( params[ PID_MAX_BLOBS ] * 63.0f );
	const float spacing   = params[ PID_SPACING ] * 0.25f;//in frame heights
	std::vector< imag::Detection > kept;
	for( const imag::Detection& b : found )
	{
		if( kept.size() >= maxBlobs )
			break;
		bool tooClose = false;
		for( const imag::Detection& k : kept )
			tooClose = tooClose || std::hypot( ( b.x - k.x ) * aspect, b.y - k.y ) < spacing;
		if( !tooClose )
			kept.push_back( b );
	}
	return kept;
}

FFResult ImagBlob::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];
	if( params[ PID_ENABLED ] < 0.5f )
	{
		imag::PassThrough( copyShader, quad, input, currentViewport );
		tracker.Reset();
		primeHistory = true;
		lastHostTime = -1.0;
		SetStatus( "Off" );
		return FF_SUCCESS;
	}
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
	const float aspect = width / height;
	imag::TrackerSettings ts;
	ts.aspect        = aspect;
	ts.maxTracks     = 1 + (size_t)std::lround( params[ PID_MAX_BLOBS ] * 63.0f );
	ts.smoothing     = params[ PID_SMOOTH ];
	ts.hold          = params[ PID_HOLD ] * 2.0f;//seconds
	ts.numberRate    = 0.2f + params[ PID_NUMBER_RATE ] * params[ PID_NUMBER_RATE ] * 12.0f;//changes per second
	ts.numbersOnBeat = params[ PID_NUMBERS_ON_BEAT ] > 0.5f;
	ts.beatHit       = beatHit;
	tracker.Update( DetectBlobs( dt ), dt, time, ts );
	tracker.Animate( dt, time, ts );
	// Lines between blobs, distances measured with the frame's aspect so they look right on screen.
	const auto links = imag::ComputeLinks( tracker.tracks, OptionIndex( params[ PID_LINES ], 4 ), OptionIndex( params[ PID_LINK_COUNT ], 4 ) + 1,
										   0.05f + params[ PID_LINK_DIST ] * 0.6f, aspect );

	const float scale = height / 1080.0f;//sizes are tuned for 1080p and scale with the frame
	imag::OverlayStyle style;
	style.squareStyle = OptionIndex( params[ PID_SQ_STYLE ], 3 );
	// Points: one size for all, or varying with how much of the cell is covered (Size Variation).
	// Regions: a square around the whole region.
	style.fitToTarget    = OptionIndex( params[ PID_BLOB_MODE ], 2 ) == 1;
	style.boxScale       = 0.6f + params[ PID_SQ_SIZE ] * 0.8f;
	style.baseSize       = ( 20.0f + params[ PID_SQ_SIZE ] * 280.0f ) * scale;
	style.sizeVariation  = params[ PID_SIZE_VAR ];
	style.thickness      = std::max( 1.0f, ( 1.0f + params[ PID_SQ_THICK ] * 5.0f ) * scale );
	style.squareOpacity  = params[ PID_SQ_OPACITY ];
	style.lineOpacity    = params[ PID_LINE_OPACITY ];
	style.lineThickness  = std::max( 1.0f, ( 1.0f + params[ PID_LINE_THICK ] * 5.0f ) * scale );
	style.numberMode     = OptionIndex( params[ PID_NUMBERS ], 5 );
	style.digits         = OptionIndex( params[ PID_DIGITS ], 5 ) + 2;
	style.numberPosition = OptionIndex( params[ PID_NUMBER_POS ], 3 );
	style.glyphHeight    = std::max( 6.0f, ( 8.0f + params[ PID_TEXT_SIZE ] * 50.0f ) * scale );
	style.textOpacity    = params[ PID_TEXT_OPACITY ];
	HsbToRgb( &params[ PID_SQ_R ], style.squareRGB );
	HsbToRgb( &params[ PID_LINE_R ], style.lineRGB );
	HsbToRgb( &params[ PID_TEXT_R ], style.textRGB );

	imag::FeedSettings feed;
	feed.mode     = OptionIndex( params[ PID_FEED ], imag::OverlayRenderer::kFeedCount );
	feed.contrast = 0.5f + params[ PID_CONTRAST ] * 2.5f;
	feed.cellSize = ( 6.0f + params[ PID_CELL ] * 24.0f ) * scale;
	feed.mix      = params[ PID_FEED_MIX ];
	HsbToRgb( &params[ PID_DARK_R ], feed.dark );
	HsbToRgb( &params[ PID_LIGHT_R ], feed.light );

	// 4. Feed into the host framebuffer (a rejected feed shader shows the plain camera and reports why),
	// then the tracking graphics blended over it.
	glViewport( currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height );
	SetStatus( overlay.DrawFeed( feed, frameTex, width, height, copyShader, quad ) );
	overlay.DrawTracks( tracker.tracks, links, style, width, height );
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
