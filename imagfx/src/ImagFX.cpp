#include "ImagFX.h"
#include "ImagFXShaders.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace ffglex;

enum ParamID : FFUInt32
{
	PID_MIX,
	PID_DELAY_R,
	PID_DELAY_B,
	PID_CHROMA,
	PID_TRAILS,
	PID_TRAIL_ZOOM,
	PID_TRAIL_ROTATE,
	PID_TRAIL_HUE,
	PID_TRAIL_BLEND,
	PID_WARP,
	PID_WARP_SCALE,
	PID_WARP_SPEED,
	PID_GLITCH,
	PID_SHEEN,
	PID_SHEEN_WIDTH,
	PID_SHEEN_ANGLE,
	PID_SCANLINES,
	PID_STATIC,
	PID_BPM_SYNC,
	PID_BEAT_RATE,
	PID_FREE_RATE,
	PID_RESET,
	PID_COUNT
};

// Beats per sync cycle for each element of the Beat Rate option.
static const float kBeatRates[]       = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
static const char* kBeatRateNames[]   = { "1/4 beat", "1/2 beat", "1 beat", "2 beats", "1 bar" };
static const char* kTrailBlendNames[] = { "Lighten", "Screen", "Add" };
static const float kPi                = 3.14159265f;

static CFFGLPluginInfo PluginInfo(
	PluginFactory< ImagFX >,
	"IMFX",
	"IMAG FX",
	2,
	1,
	1,
	0,
	FF_EFFECT,
	"Camera look: RGB time delay, feedback trails, warp, glitch, sheen, static",
	"IMAG FX prototype" );

ImagFX::ImagFX() :
	params( PID_COUNT, 0.0f )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	params[ PID_MIX ]          = 1.0f;
	params[ PID_DELAY_R ]      = 0.2f;
	params[ PID_CHROMA ]       = 0.15f;
	params[ PID_TRAIL_ZOOM ]   = 0.5f;
	params[ PID_TRAIL_ROTATE ] = 0.5f;
	params[ PID_WARP_SCALE ]   = 0.3f;
	params[ PID_WARP_SPEED ]   = 0.3f;
	params[ PID_SHEEN_WIDTH ]  = 0.3f;
	params[ PID_SHEEN_ANGLE ]  = 0.25f;
	params[ PID_BPM_SYNC ]     = 1.0f;
	params[ PID_BEAT_RATE ]    = 2.0f;
	params[ PID_FREE_RATE ]    = 0.25f;

	SetParamInfof( PID_MIX, "Mix", FF_TYPE_STANDARD );
	SetParamInfof( PID_DELAY_R, "Red Delay", FF_TYPE_STANDARD );
	SetParamInfof( PID_DELAY_B, "Blue Delay", FF_TYPE_STANDARD );
	SetParamInfof( PID_CHROMA, "Chroma Split", FF_TYPE_STANDARD );
	SetParamInfof( PID_TRAILS, "Trails", FF_TYPE_STANDARD );
	SetParamInfof( PID_TRAIL_ZOOM, "Trail Zoom", FF_TYPE_STANDARD );
	SetParamInfof( PID_TRAIL_ROTATE, "Trail Rotate", FF_TYPE_STANDARD );
	SetParamInfof( PID_TRAIL_HUE, "Trail Hue", FF_TYPE_STANDARD );

	SetOptionParamInfo( PID_TRAIL_BLEND, "Trail Blend", 3, params[ PID_TRAIL_BLEND ] );
	for( unsigned int i = 0; i < 3; ++i )
		SetParamElementInfo( PID_TRAIL_BLEND, i, kTrailBlendNames[ i ], (float)i );

	SetParamInfof( PID_WARP, "Warp", FF_TYPE_STANDARD );
	SetParamInfof( PID_WARP_SCALE, "Warp Scale", FF_TYPE_STANDARD );
	SetParamInfof( PID_WARP_SPEED, "Warp Speed", FF_TYPE_STANDARD );
	SetParamInfof( PID_GLITCH, "Glitch", FF_TYPE_STANDARD );
	SetParamInfof( PID_SHEEN, "Sheen", FF_TYPE_STANDARD );
	SetParamInfof( PID_SHEEN_WIDTH, "Sheen Width", FF_TYPE_STANDARD );
	SetParamInfof( PID_SHEEN_ANGLE, "Sheen Angle", FF_TYPE_STANDARD );
	SetParamInfof( PID_SCANLINES, "Scanlines", FF_TYPE_STANDARD );
	SetParamInfof( PID_STATIC, "Static", FF_TYPE_STANDARD );
	SetParamInfo( PID_BPM_SYNC, "BPM Sync", FF_TYPE_BOOLEAN, true );

	SetOptionParamInfo( PID_BEAT_RATE, "Beat Rate", 5, params[ PID_BEAT_RATE ] );
	for( unsigned int i = 0; i < 5; ++i )
		SetParamElementInfo( PID_BEAT_RATE, i, kBeatRateNames[ i ], (float)i );

	SetParamInfof( PID_FREE_RATE, "Free Rate", FF_TYPE_STANDARD );
	SetParamInfo( PID_RESET, "Reset Trails", FF_TYPE_EVENT, false );
}

FFResult ImagFX::InitGL( const FFGLViewportStruct* vp )
{
	if( !copyShader.Compile( kVertexShader, kCopyFragmentShader ) ||
		!trailShader.Compile( kVertexShader, kTrailFragmentShader ) ||
		!finishShader.Compile( kVertexShader, kFinishFragmentShader ) ||
		!quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}
	return CFFGLPlugin::InitGL( vp );
}

FFResult ImagFX::DeInitGL()
{
	copyShader.FreeGLResources();
	trailShader.FreeGLResources();
	finishShader.FreeGLResources();
	quad.Release();
	ReleaseTargets();
	return FF_SUCCESS;
}

bool ImagFX::EnsureTargets( GLsizei width, GLsizei height )
{
	if( width == targetWidth && height == targetHeight && ringFBO != 0 )
		return true;

	ReleaseTargets();

	for( auto& fbo : feedback )
	{
		if( !fbo.Initialise( width, height, GL_RGBA16F ) )
		{
			ReleaseTargets();
			return false;
		}
	}

	glGenFramebuffers( 1, &ringFBO );
	ringTextures.resize( kMaxDelay + 1 );
	glGenTextures( (GLsizei)ringTextures.size(), ringTextures.data() );
	for( GLuint tex : ringTextures )
	{
		Scoped2DTextureBinding binding( tex );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	}

	// Start from transparent black so early delayed frames don't show uninitialised memory.
	{
		ScopedFBOBinding fboBinding( ringFBO, ScopedFBOBinding::RB_REVERT );
		glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
		for( GLuint tex : ringTextures )
		{
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 );
			glClear( GL_COLOR_BUFFER_BIT );
		}
	}

	targetWidth  = width;
	targetHeight = height;
	ringHead     = 0;
	ClearFeedback();
	return true;
}

void ImagFX::ReleaseTargets()
{
	for( auto& fbo : feedback )
		fbo.Release();
	if( !ringTextures.empty() )
		glDeleteTextures( (GLsizei)ringTextures.size(), ringTextures.data() );
	ringTextures.clear();
	if( ringFBO != 0 )
		glDeleteFramebuffers( 1, &ringFBO );
	ringFBO      = 0;
	targetWidth  = 0;
	targetHeight = 0;
}

void ImagFX::ClearFeedback()
{
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	for( auto& fbo : feedback )
	{
		ScopedFBOBinding fboBinding( fbo.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glClear( GL_COLOR_BUFFER_BIT );
	}
}

GLuint ImagFX::HistoryFrame( int framesAgo ) const
{
	int count = (int)ringTextures.size();
	return ringTextures[ ( ringHead - framesAgo + count ) % count ];
}

// Produces `phase` (0..1 ramp per cycle) and `seed` (new value each cycle). When BPM Sync is on the cycle
// follows Resolume's clock via barPhase; otherwise it free-runs from host time.
void ImagFX::UpdateClock()
{
	float dt = 1.0f / 60.0f;
	if( lastHostTime >= 0.0 && hostTime > lastHostTime )
		dt = std::min( (float)( hostTime - lastHostTime ), 0.25f );
	lastHostTime = hostTime;

	warpTime += dt * params[ PID_WARP_SPEED ] * 2.0f;
	frameCounter = std::fmod( frameCounter + 1.0f, 1000.0f );

	if( params[ PID_BPM_SYNC ] > 0.5f )
	{
		int rateIndex = std::min( std::max( (int)std::lround( params[ PID_BEAT_RATE ] ), 0 ), 4 );
		float beats   = barPhase * 4.0f;//Resolume reports phase within a 4/4 bar.
		phase         = std::fmod( beats / kBeatRates[ rateIndex ], 1.0f );
	}
	else
	{
		freeRunPhase = std::fmod( freeRunPhase + dt * params[ PID_FREE_RATE ] * 4.0f, 1.0f );
		phase        = freeRunPhase;
	}

	if( phase < lastPhase )
		seed = std::fmod( seed + 1.0f, 997.0f );
	lastPhase = phase;
}

FFResult ImagFX::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];
	if( !EnsureTargets( (GLsizei)input.Width, (GLsizei)input.Height ) )
		return FF_FAIL;

	if( resetRequested )
	{
		ClearFeedback();
		resetRequested = false;
	}
	UpdateClock();

	ringHead                 = ( ringHead + 1 ) % (int)ringTextures.size();
	FFGLTexCoords maxCoords  = GetMaxGLTexCoords( input );
	const float width        = (float)targetWidth;
	const float height       = (float)targetHeight;
	const int feedbackWrite  = 1 - feedbackRead;
	auto delayFrames         = [ & ]( FFUInt32 id ) { return (int)std::lround( params[ id ] * kMaxDelay ); };
	auto bindTexture         = []( GLenum unit, GLuint tex ) {
		glActiveTexture( GL_TEXTURE0 + unit );
		glBindTexture( GL_TEXTURE_2D, tex );
	};
	auto unbindTextures = [ & ]( GLenum count ) {
		for( GLenum unit = count; unit-- > 0; )
			bindTexture( unit, 0 );
	};

	glViewport( 0, 0, targetWidth, targetHeight );

	// Pass 1: store this frame in the history ring.
	{
		ScopedFBOBinding fboBinding( ringFBO, ScopedFBOBinding::RB_REVERT );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, HistoryFrame( 0 ), 0 );
		ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
		bindTexture( 0, input.Handle );
		copyShader.Set( "InputTexture", 0 );
		copyShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		quad.Draw();
		unbindTextures( 1 );
	}

	// Pass 2: temporal stage into the feedback buffer.
	{
		ScopedFBOBinding fboBinding( feedback[ feedbackWrite ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( trailShader.GetGLID() );
		bindTexture( 0, HistoryFrame( 0 ) );
		bindTexture( 1, HistoryFrame( delayFrames( PID_DELAY_R ) ) );
		bindTexture( 2, HistoryFrame( delayFrames( PID_DELAY_B ) ) );
		bindTexture( 3, feedback[ feedbackRead ].GetTextureInfo().Handle );
		trailShader.Set( "CurrentTex", 0 );
		trailShader.Set( "DelayRTex", 1 );
		trailShader.Set( "DelayBTex", 2 );
		trailShader.Set( "FeedbackTex", 3 );
		trailShader.Set( "MaxUV", 1.0f, 1.0f );
		trailShader.Set( "Resolution", width, height );
		trailShader.Set( "Time", warpTime );
		trailShader.Set( "Phase", phase );
		trailShader.Set( "Seed", seed );
		trailShader.Set( "Chroma", params[ PID_CHROMA ] );
		trailShader.Set( "Warp", params[ PID_WARP ] );
		trailShader.Set( "WarpScale", params[ PID_WARP_SCALE ] );
		trailShader.Set( "Glitch", params[ PID_GLITCH ] );
		trailShader.Set( "Trails", std::pow( params[ PID_TRAILS ], 0.35f ) );//Most of the useful range is near 1.
		trailShader.Set( "TrailZoom", 1.0f + ( params[ PID_TRAIL_ZOOM ] - 0.5f ) * 0.1f );
		trailShader.Set( "TrailRotate", ( params[ PID_TRAIL_ROTATE ] - 0.5f ) * 10.0f * kPi / 180.0f );
		trailShader.Set( "TrailHue", params[ PID_TRAIL_HUE ] * 0.5f );
		trailShader.Set( "TrailBlend", (int)std::lround( params[ PID_TRAIL_BLEND ] ) );
		quad.Draw();
		unbindTextures( 4 );
	}

	// Pass 3: surface stage straight into the host's framebuffer.
	glViewport( currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height );
	{
		ScopedShaderBinding shaderBinding( finishShader.GetGLID() );
		bindTexture( 0, feedback[ feedbackWrite ].GetTextureInfo().Handle );
		bindTexture( 1, HistoryFrame( 0 ) );
		finishShader.Set( "TrailTex", 0 );
		finishShader.Set( "OriginalTex", 1 );
		finishShader.Set( "MaxUV", 1.0f, 1.0f );
		finishShader.Set( "Resolution", width, height );
		finishShader.Set( "Phase", phase );
		finishShader.Set( "Frame", frameCounter );
		finishShader.Set( "Mix", params[ PID_MIX ] );
		finishShader.Set( "Sheen", params[ PID_SHEEN ] );
		finishShader.Set( "SheenWidth", 0.02f + params[ PID_SHEEN_WIDTH ] * 0.4f );
		finishShader.Set( "SheenAngle", params[ PID_SHEEN_ANGLE ] * kPi );
		finishShader.Set( "Scanlines", params[ PID_SCANLINES ] );
		finishShader.Set( "Static", params[ PID_STATIC ] );
		quad.Draw();
		unbindTextures( 2 );
	}

	feedbackRead = feedbackWrite;
	return FF_SUCCESS;
}

FFResult ImagFX::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PID_COUNT )
		return FF_FAIL;
	if( index == PID_RESET && value > 0.5f )
		resetRequested = true;
	params[ index ] = value;
	return FF_SUCCESS;
}

float ImagFX::GetFloatParameter( unsigned int index )
{
	return index < PID_COUNT ? params[ index ] : 0.0f;
}

char* ImagFX::GetParameterDisplay( unsigned int index )
{
	switch( index )
	{
	case PID_DELAY_R:
	case PID_DELAY_B:
		std::snprintf( displayBuffer, sizeof( displayBuffer ), "%d frames", (int)std::lround( params[ index ] * kMaxDelay ) );
		return displayBuffer;
	case PID_TRAIL_ROTATE:
		std::snprintf( displayBuffer, sizeof( displayBuffer ), "%.2f deg/frame", ( params[ index ] - 0.5f ) * 10.0f );
		return displayBuffer;
	case PID_SHEEN_ANGLE:
		std::snprintf( displayBuffer, sizeof( displayBuffer ), "%.0f deg", params[ index ] * 180.0f );
		return displayBuffer;
	default:
		return CFFGLPlugin::GetParameterDisplay( index );
	}
}
