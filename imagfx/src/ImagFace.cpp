#include "ImagFace.h"
#include "ColorUtil.h"
#include "PluginLog.h"
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
	PID_DETECT_SIZE,
	PID_CONFIDENCE,
	PID_MAX_FACES,
	PID_SMOOTH,
	PID_HOLD,
	// Boxes
	PID_BOX_STYLE,
	PID_BOX_SHAPE,
	PID_BOX_SIZE,
	PID_BOX_THICK,
	PID_BOX_OPACITY,
	PID_BOX_R,
	PID_BOX_G,
	PID_BOX_B,
	PID_LANDMARKS,
	PID_LANDMARK_SIZE,
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

const int kDetectWidths[] = { 320, 480, 640 };
const char* kFeedNames[]  = { "Normal", "Monochrome", "Dual Tone", "ASCII", "Halftone" };

int OptionIndex( float value, int count )
{
	return std::min( std::max( (int)std::lround( value ), 0 ), count - 1 );
}
}// namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< ImagFace >,
	"IMFC",
	"IOKI Face",
	2,
	1,
	1,
	0,
	FF_EFFECT,
	"Multi-face tracking: boxes, connecting lines, numbers and landmarks on every face, over a normal, mono, dual tone, ASCII or halftone feed",
	"IOKI FX. Face detection: libfacedetection (BSD 3-Clause), Shiqi Yu" );

ImagFace::ImagFace() :
	params( PID_COUNT, 0.0f ),
	status( "Starting" )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	// Defaults match IOKI Blob: steady thin white boxes, decimal labels inside them, one chain of lines.
	params[ PID_ENABLED ]       = 1.0f;
	params[ PID_DETECT_SIZE ]   = 1.0f;
	params[ PID_CONFIDENCE ]    = 0.6f;
	params[ PID_MAX_FACES ]     = 0.4f;
	params[ PID_SMOOTH ]        = 0.6f;
	params[ PID_HOLD ]          = 0.3f;
	params[ PID_BOX_SHAPE ]     = 1.0f;
	params[ PID_BOX_SIZE ]      = 0.5f;
	params[ PID_BOX_THICK ]     = 0.0f;
	params[ PID_BOX_OPACITY ]   = 0.9f;
	params[ PID_LANDMARK_SIZE ] = 0.3f;
	params[ PID_LINES ]         = 1.0f;
	params[ PID_LINK_COUNT ]    = 1.0f;
	params[ PID_LINK_DIST ]     = 0.6f;
	params[ PID_LINE_THICK ]    = 0.0f;
	params[ PID_LINE_OPACITY ]  = 0.75f;
	params[ PID_NUMBERS ]       = 1.0f;
	params[ PID_DIGITS ]        = 2.0f;
	params[ PID_NUMBER_RATE ]   = 0.15f;
	params[ PID_TEXT_SIZE ]     = 0.12f;
	params[ PID_TEXT_OPACITY ]  = 0.9f;
	params[ PID_FEED_MIX ]      = 1.0f;
	params[ PID_CONTRAST ]      = 0.4f;
	params[ PID_CELL ]          = 0.3f;
	const float colours[][ 4 ] = {
		{ PID_BOX_R, 1.0f, 1.0f, 1.0f },
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
	addOption( PID_DETECT_SIZE, "Detect Size", { "320 (fastest)", "480", "640 (small faces)" } );
	SetParamInfof( PID_CONFIDENCE, "Min Confidence", FF_TYPE_STANDARD );
	SetParamInfof( PID_MAX_FACES, "Max Faces", FF_TYPE_STANDARD );
	SetParamInfof( PID_SMOOTH, "Smoothing", FF_TYPE_STANDARD );
	SetParamInfof( PID_HOLD, "Hold Time", FF_TYPE_STANDARD );
	group( PID_DETECT_SIZE, PID_HOLD, "Detection" );

	addOption( PID_BOX_STYLE, "Box Style", { "Outline", "Filled", "Corners" } );
	addOption( PID_BOX_SHAPE, "Box Shape", { "Face", "Square" } );
	SetParamInfof( PID_BOX_SIZE, "Box Size", FF_TYPE_STANDARD );
	SetParamInfof( PID_BOX_THICK, "Box Thickness", FF_TYPE_STANDARD );
	SetParamInfof( PID_BOX_OPACITY, "Box Intensity", FF_TYPE_STANDARD );
	addColour( PID_BOX_R, "Box Colour" );
	SetParamInfo( PID_LANDMARKS, "Landmarks", FF_TYPE_BOOLEAN, false );
	SetParamInfof( PID_LANDMARK_SIZE, "Landmark Size", FF_TYPE_STANDARD );
	group( PID_BOX_STYLE, PID_LANDMARK_SIZE, "Boxes" );

	addOption( PID_LINES, "Lines", { "Off", "Chain", "Nearest", "Network" } );
	addOption( PID_LINK_COUNT, "Links Per Face", { "1", "2", "3", "4" } );
	SetParamInfof( PID_LINK_DIST, "Link Distance", FF_TYPE_STANDARD );
	SetParamInfof( PID_LINE_THICK, "Line Thickness", FF_TYPE_STANDARD );
	SetParamInfof( PID_LINE_OPACITY, "Line Intensity", FF_TYPE_STANDARD );
	addColour( PID_LINE_R, "Line Colour" );
	group( PID_LINES, PID_LINE_B, "Lines" );

	addOption( PID_NUMBERS, "Numbers", { "Off", "Decimal", "Random", "Face ID", "Coordinates", "Confidence" } );
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

ImagFace::~ImagFace()
{
	detector.Stop();
}

FFResult ImagFace::InitGL( const FFGLViewportStruct* vp )
{
	std::string error;
	if( !CompileOrLog( copyShader, kVertexShader, kCopyFragmentShader, "IOKI Face", "copy", &error ) || !overlay.Init( "IOKI Face", &error ) ||
		!quad.Initialise() )
	{
		SetStatus( error.empty() ? "Could not create GPU resources" : error );
		DeInitGL();
		return FF_FAIL;
	}
	detectorSupported = imag::FaceDetectorSupported();
	if( detectorSupported )
		detector.Start();
	else
		FFGLLog::LogToHost( "IOKI Face: this CPU has no AVX2, face detection is off" );
	FFGLLog::LogToHost( "IOKI Face: ready" );
	return CFFGLPlugin::InitGL( vp );
}

FFResult ImagFace::DeInitGL()
{
	detector.Stop();
	copyShader.FreeGLResources();
	overlay.Release();
	quad.Release();
	ReleaseTargets();
	return FF_SUCCESS;
}

bool ImagFace::EnsureTargets( GLsizei width, GLsizei height )
{
	const GLsizei wantWidth  = std::min( (GLsizei)kDetectWidths[ OptionIndex( params[ PID_DETECT_SIZE ], 3 ) ], width );
	const GLsizei wantHeight = std::max( (GLsizei)2, (GLsizei)( ( (long long)wantWidth * height / width ) & ~1LL ) );
	if( width == targetWidth && height == targetHeight && wantWidth == detectWidth && wantHeight == detectHeight )
		return true;
	ReleaseTargets();
	if( !frame.Initialise( width, height ) || !detect.Initialise( wantWidth, wantHeight ) )
	{
		ReleaseTargets();
		return false;
	}
	targetWidth  = width;
	targetHeight = height;
	detectWidth  = wantWidth;
	detectHeight = wantHeight;
	return true;
}

void ImagFace::ReleaseTargets()
{
	frame.Release();
	detect.Release();
	targetWidth = targetHeight = detectWidth = detectHeight = 0;
}

void ImagFace::SetStatus( const std::string& text )
{
	if( text == status )
		return;
	status = text;
	RaiseParamEvent( PID_STATUS, FF_EVENT_FLAG_VALUE );
}

// Scales the frame down, reads it back and hands it to the detector as top-down BGR.
void ImagFace::SubmitFrame( GLuint frameTexture )
{
	if( !detectorSupported || !detector.Idle() )
		return;
	glViewport( 0, 0, detectWidth, detectHeight );
	{
		ScopedFBOBinding fboBinding( detect.GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, frameTexture );
		copyShader.Set( "InputTexture", 0 );
		copyShader.Set( "MaxUV", 1.0f, 1.0f );
		quad.Draw();
		glBindTexture( GL_TEXTURE_2D, 0 );

		rgba.resize( (size_t)detectWidth * detectHeight * 4 );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, detectWidth, detectHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data() );
		glPixelStorei( GL_PACK_ALIGNMENT, 4 );
	}
	bgr.resize( (size_t)detectWidth * detectHeight * 3 );
	for( GLsizei y = 0; y < detectHeight; ++y )
	{
		const unsigned char* src = &rgba[ (size_t)( detectHeight - 1 - y ) * detectWidth * 4 ];//GL rows are bottom-up
		unsigned char* dst       = &bgr[ (size_t)y * detectWidth * 3 ];
		for( GLsizei x = 0; x < detectWidth; ++x, src += 4, dst += 3 )
		{
			// Premultiplied input: a keyed subject on transparent black reads as on black, which is fine.
			dst[ 0 ] = src[ 2 ];
			dst[ 1 ] = src[ 1 ];
			dst[ 2 ] = src[ 0 ];
		}
	}
	detector.Submit( bgr, detectWidth, detectHeight );
}

// Turns the detector's latest faces into tracker input.
void ImagFace::CollectFaces()
{
	int width = 0, height = 0;
	float ms  = 0.0f;
	if( !detector.Take( faces, width, height, ms ) || width <= 0 || height <= 0 )
		return;
	detectMs = ms;

	const float minScore   = params[ PID_CONFIDENCE ];
	const bool square      = OptionIndex( params[ PID_BOX_SHAPE ], 2 ) == 1;
	const float frameRatio = (float)targetWidth / targetHeight;
	std::vector< imag::Detection > found;
	for( const imag::FaceResult& f : faces )
	{
		if( f.score < minScore || f.w <= 0 || f.h <= 0 )
			continue;
		imag::Detection d;
		d.x = ( f.x + f.w * 0.5f ) / width;
		d.y = 1.0f - ( f.y + f.h * 0.5f ) / height;//detector rows run top-down, the frame's bottom-up
		d.w = f.w / width;
		d.h = f.h / height;
		if( square )
		{
			const float side = std::max( d.w * frameRatio, d.h );//in frame heights
			d.w              = side / frameRatio;
			d.h              = side;
		}
		d.mass          = d.w * d.h;
		d.score         = f.score;
		d.landmarkCount = 5;
		for( int i = 0; i < 5; ++i )
		{
			d.landmarks[ 2 * i ]     = f.landmarks[ 2 * i ] / width;
			d.landmarks[ 2 * i + 1 ] = 1.0f - f.landmarks[ 2 * i + 1 ] / height;
		}
		found.push_back( d );
	}
	facesSeen = (int)found.size();

	imag::TrackerSettings ts;
	ts.aspect     = frameRatio;
	ts.maxTracks  = 1 + (size_t)std::lround( params[ PID_MAX_FACES ] * 15.0f );
	ts.smoothing  = params[ PID_SMOOTH ];
	ts.hold       = params[ PID_HOLD ] * 2.0f;
	ts.numberRate = 0.2f + params[ PID_NUMBER_RATE ] * params[ PID_NUMBER_RATE ] * 12.0f;
	ts.showAfter  = 2;
	const float dt = lastDetection < 0.0f ? 1.0f / 30.0f : std::min( time - lastDetection, 0.5f );
	lastDetection  = time;
	tracker.Update( found, dt, time, ts );
}

FFResult ImagFace::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];
	if( params[ PID_ENABLED ] < 0.5f )
	{
		imag::PassThrough( copyShader, quad, input, currentViewport );
		tracker.Reset();
		lastHostTime  = -1.0;
		lastDetection = -1.0f;
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

	const float width     = (float)targetWidth;
	const float height    = (float)targetHeight;
	const GLuint frameTex = frame.GetTextureInfo().Handle;

	// 1. Camera frame.
	glViewport( 0, 0, targetWidth, targetHeight );
	{
		const FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );
		ScopedFBOBinding fboBinding( frame.GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, input.Handle );
		copyShader.Set( "InputTexture", 0 );
		copyShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		quad.Draw();
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	// 2-3. Faces: take what the detector found since last frame, then give it this frame if it's free.
	CollectFaces();
	SubmitFrame( frameTex );

	imag::TrackerSettings ts;
	ts.aspect        = width / height;
	ts.maxTracks     = 1 + (size_t)std::lround( params[ PID_MAX_FACES ] * 15.0f );
	ts.smoothing     = params[ PID_SMOOTH ];
	ts.hold          = params[ PID_HOLD ] * 2.0f;
	ts.numberRate    = 0.2f + params[ PID_NUMBER_RATE ] * params[ PID_NUMBER_RATE ] * 12.0f;
	ts.numbersOnBeat = params[ PID_NUMBERS_ON_BEAT ] > 0.5f;
	ts.beatHit       = beatHit;
	ts.showAfter     = 2;
	tracker.Animate( dt, time, ts );
	const auto links = imag::ComputeLinks( tracker.tracks, OptionIndex( params[ PID_LINES ], 4 ), OptionIndex( params[ PID_LINK_COUNT ], 4 ) + 1,
										   0.05f + params[ PID_LINK_DIST ] * 0.6f, ts.aspect );

	const float scale = height / 1080.0f;//sizes are tuned for 1080p and scale with the frame
	imag::OverlayStyle style;
	style.squareStyle    = OptionIndex( params[ PID_BOX_STYLE ], 3 );
	style.fitToTarget    = true;
	style.boxScale       = 0.8f + params[ PID_BOX_SIZE ] * 0.8f;
	style.thickness      = std::max( 1.0f, ( 1.0f + params[ PID_BOX_THICK ] * 5.0f ) * scale );
	style.squareOpacity  = params[ PID_BOX_OPACITY ];
	style.lineOpacity    = params[ PID_LINE_OPACITY ];
	style.lineThickness  = std::max( 1.0f, ( 1.0f + params[ PID_LINE_THICK ] * 5.0f ) * scale );
	style.numberMode     = OptionIndex( params[ PID_NUMBERS ], 6 );
	style.digits         = OptionIndex( params[ PID_DIGITS ], 5 ) + 2;
	style.numberPosition = OptionIndex( params[ PID_NUMBER_POS ], 3 );
	style.glyphHeight    = std::max( 6.0f, ( 8.0f + params[ PID_TEXT_SIZE ] * 50.0f ) * scale );
	style.textOpacity    = params[ PID_TEXT_OPACITY ];
	style.landmarks      = params[ PID_LANDMARKS ] > 0.5f;
	style.landmarkSize   = std::max( 3.0f, ( 2.0f + params[ PID_LANDMARK_SIZE ] * 14.0f ) * scale );
	HsbToRgb( &params[ PID_BOX_R ], style.squareRGB );
	HsbToRgb( &params[ PID_LINE_R ], style.lineRGB );
	HsbToRgb( &params[ PID_TEXT_R ], style.textRGB );

	imag::FeedSettings feed;
	feed.mode     = OptionIndex( params[ PID_FEED ], imag::OverlayRenderer::kFeedCount );
	feed.contrast = 0.5f + params[ PID_CONTRAST ] * 2.5f;
	feed.cellSize = ( 6.0f + params[ PID_CELL ] * 24.0f ) * scale;
	feed.mix      = params[ PID_FEED_MIX ];
	HsbToRgb( &params[ PID_DARK_R ], feed.dark );
	HsbToRgb( &params[ PID_LIGHT_R ], feed.light );

	// 4. Feed into the host framebuffer, then the tracking graphics blended over it.
	glViewport( currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height );
	const std::string feedStatus = overlay.DrawFeed( feed, frameTex, width, height, copyShader, quad );
	overlay.DrawTracks( tracker.tracks, links, style, width, height );

	if( feedStatus.compare( 0, 3, "OK:" ) != 0 )
		SetStatus( feedStatus );
	else if( !detectorSupported )
		SetStatus( "No face detection: this CPU has no AVX2. Feed only." );
	else
	{
		char text[ 128 ];
		std::snprintf( text, sizeof( text ), "OK: %d face%s, %s feed, detector %d x %d (%d ms)", facesSeen, facesSeen == 1 ? "" : "s",
					   kFeedNames[ feed.mode ], (int)detectWidth, (int)detectHeight, (int)std::lround( detectMs / 5.0f ) * 5 );
		SetStatus( text );
	}
	return FF_SUCCESS;
}

FFResult ImagFace::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PID_COUNT )
		return FF_FAIL;
	if( index != PID_STATUS )
		params[ index ] = value;
	return FF_SUCCESS;
}

float ImagFace::GetFloatParameter( unsigned int index )
{
	return index < PID_COUNT ? params[ index ] : 0.0f;
}

FFResult ImagFace::SetTextParameter( unsigned int index, const char* )
{
	return index == PID_STATUS ? FF_SUCCESS : FF_FAIL;//Status is read-only.
}

char* ImagFace::GetTextParameter( unsigned int index )
{
	return index == PID_STATUS ? const_cast< char* >( status.c_str() ) : nullptr;
}
