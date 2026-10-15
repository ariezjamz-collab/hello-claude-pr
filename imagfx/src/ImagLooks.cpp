#include "ImagLooks.h"
#include "ImagLooksShaders.h"
#include "ImagFlameShaders.h"//Mask, moments and fire passes are shared with IMAG Flame.
#include "PluginLog.h"
#include "ColorUtil.h"
#include <algorithm>
#include <cmath>
#include <string>

using namespace ffglex;

namespace
{
enum ParamID : FFUInt32
{
	// Look
	PID_MIX,
	PID_LOOK,
	PID_INTENSITY,
	PID_CRUSH,
	PID_CONTRAST,
	PID_EDGE_GLOW,
	PID_DETAIL,
	PID_TEXTURE,
	PID_SPARKLES,
	PID_HUE,
	PID_BPM_SYNC,
	PID_ANIM_RATE,
	PID_STATUS,
	// Subject
	PID_SOURCE,
	PID_THRESHOLD,
	// Tracking
	PID_SQUARES,
	PID_SQUARE_COUNT,
	PID_SQUARE_SIZE,
	PID_SQUARE_R,
	PID_SQUARE_G,
	PID_SQUARE_B,
	// Echo
	PID_ECHO,
	PID_ECHO_DELAY,
	PID_ECHO_COUNT,
	PID_ECHO_A_R,
	PID_ECHO_A_G,
	PID_ECHO_A_B,
	PID_ECHO_B_R,
	PID_ECHO_B_G,
	PID_ECHO_B_B,
	// ASCII
	PID_ASCII,
	PID_ASCII_SIZE,
	PID_ASCII_R,
	PID_ASCII_G,
	PID_ASCII_B,
	// Levels
	PID_DISTORTION,
	PID_DISPERSION,
	PID_FLAMES,
	PID_FLAME_COLOR,
	PID_DEPTH,
	PID_DUAL,
	PID_DUAL_DARK_R,
	PID_DUAL_DARK_G,
	PID_DUAL_DARK_B,
	PID_DUAL_LIGHT_R,
	PID_DUAL_LIGHT_G,
	PID_DUAL_LIGHT_B,
	PID_COUNT
};

// Order must match the look constants in ImagLooksShaders.h.
const char* kLookNames[] = { "Night Vision", "Fire Map", "Gold Edge", "Blood Red", "Analog TV", "Glitter Ice", "Ice Cyan",
							 "Faded Film", "RGB Split", "Comic", "Halftone", "Paper Texture", "Animated Cartoon" };
const unsigned int kLookCount        = sizeof( kLookNames ) / sizeof( kLookNames[ 0 ] );
const unsigned int kFirstStylizeLook = 9;//Comic onwards sit under a separator in the dropdown.

const GLsizei kMomentsSize    = 256;
const int kMaxEchoes          = 4;
const int kMaxEchoSpacing     = 12;//frames between echo copies at Echo Delay = 1
const int kEchoRingLength     = kMaxEchoes * kMaxEchoSpacing + 1;

int OptionIndex( float value, int count )
{
	return std::min( std::max( (int)std::lround( value ), 0 ), count - 1 );
}
}// namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< ImagLooks >,
	"IMLK",
	"IMAG Looks",
	2,
	1,
	1,
	0,
	FF_EFFECT,
	"Camera looks panel with tracking squares, coloured echoes, ASCII, distortion, dispersion, flames, depth and dual tone",
	"IMAG FX prototype" );

ImagLooks::ImagLooks() :
	lookShaders( kLookCount ),
	lookStates( kLookCount, ShaderState::NotCompiled ),
	lookErrors( kLookCount ),
	params( PID_COUNT, 0.0f ),
	status( "Starting" )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	params[ PID_MIX ]          = 1.0f;
	params[ PID_INTENSITY ]    = 0.85f;
	params[ PID_CRUSH ]        = 0.15f;
	params[ PID_CONTRAST ]     = 0.5f;
	params[ PID_EDGE_GLOW ]    = 0.3f;
	params[ PID_DETAIL ]       = 0.35f;
	params[ PID_TEXTURE ]      = 0.4f;
	params[ PID_BPM_SYNC ]     = 1.0f;
	params[ PID_ANIM_RATE ]    = 0.4f;
	params[ PID_THRESHOLD ]    = 0.3f;
	params[ PID_SQUARE_COUNT ] = 1.0f;
	params[ PID_SQUARE_SIZE ]  = 0.35f;
	params[ PID_ECHO_DELAY ]   = 0.35f;
	params[ PID_ECHO_COUNT ]   = 1.0f;
	params[ PID_ASCII_SIZE ]   = 0.3f;
	const float colours[][ 4 ] = {
		{ PID_SQUARE_R, 1.0f, 0.92f, 0.8f },      // warm white
		{ PID_ECHO_A_R, 1.0f, 0.1f, 0.6f },       // magenta
		{ PID_ECHO_B_R, 0.1f, 0.8f, 1.0f },       // cyan
		{ PID_ASCII_R, 0.3f, 1.0f, 0.45f },       // terminal green
		{ PID_DUAL_DARK_R, 0.08f, 0.03f, 0.3f },  // deep indigo
		{ PID_DUAL_LIGHT_R, 1.0f, 0.72f, 0.3f },  // amber
	};
	for( const auto& c : colours )
		RgbToHsb( &c[ 1 ], &params[ (int)c[ 0 ] ] );//colour controls are hue / saturation / brightness

	auto addColour = [ & ]( FFUInt32 first, const char* name, const char* group ) {
		SetParamInfof( first, name, FF_TYPE_HUE );
		SetParamInfof( first + 1, ( std::string( name ) + "_Sat" ).c_str(), FF_TYPE_SATURATION );
		SetParamInfof( first + 2, ( std::string( name ) + "_Bri" ).c_str(), FF_TYPE_BRIGHTNESS );
		for( FFUInt32 i = 0; i < 3; ++i )
			SetParamGroup( first + i, group );
	};
	auto addOption = [ & ]( FFUInt32 id, const char* name, std::initializer_list< const char* > options ) {
		SetOptionParamInfo( id, name, (unsigned int)options.size(), params[ id ] );
		unsigned int i = 0;
		for( const char* option : options )
		{
			SetParamElementInfo( id, i, option, (float)i );
			++i;
		}
	};

	SetParamInfof( PID_MIX, "Mix", FF_TYPE_STANDARD );
	SetOptionParamInfo( PID_LOOK, "Look", kLookCount, 0.0f );
	for( unsigned int i = 0; i < kLookCount; ++i )
		SetParamElementInfo( PID_LOOK, i, kLookNames[ i ], (float)i );
	AddElementSeparator( PID_LOOK, kFirstStylizeLook );
	SetParamInfof( PID_INTENSITY, "Intensity", FF_TYPE_STANDARD );
	SetParamInfof( PID_CRUSH, "Black Crush", FF_TYPE_STANDARD );
	SetParamInfof( PID_CONTRAST, "Contrast", FF_TYPE_STANDARD );
	SetParamInfof( PID_EDGE_GLOW, "Edge Glow", FF_TYPE_STANDARD );
	SetParamInfof( PID_DETAIL, "Dot / Cell Size", FF_TYPE_STANDARD );
	SetParamInfof( PID_TEXTURE, "Texture", FF_TYPE_STANDARD );
	SetParamInfof( PID_SPARKLES, "Sparkles", FF_TYPE_STANDARD );
	SetParamInfof( PID_HUE, "Hue Shift", FF_TYPE_STANDARD );
	SetParamInfo( PID_BPM_SYNC, "BPM Sync", FF_TYPE_BOOLEAN, true );
	SetParamInfof( PID_ANIM_RATE, "Animation Rate", FF_TYPE_STANDARD );
	SetParamInfo( PID_STATUS, "Status", FF_TYPE_TEXT, status.c_str() );
	for( FFUInt32 id = PID_MIX; id <= PID_STATUS; ++id )
		SetParamGroup( id, "Look" );

	addOption( PID_SOURCE, "Subject Source", { "Motion", "Keyed alpha", "Brightness", "Motion in key" } );
	SetParamInfof( PID_THRESHOLD, "Subject Threshold", FF_TYPE_STANDARD );
	SetParamGroup( PID_SOURCE, "Subject" );
	SetParamGroup( PID_THRESHOLD, "Subject" );

	addOption( PID_SQUARES, "Tracking", { "Off", "Squares", "Squares + Lines" } );
	addOption( PID_SQUARE_COUNT, "Square Count", { "Few", "Medium", "Many" } );
	SetParamInfof( PID_SQUARE_SIZE, "Square Size", FF_TYPE_STANDARD );
	for( FFUInt32 id : { PID_SQUARES, PID_SQUARE_COUNT, PID_SQUARE_SIZE } )
		SetParamGroup( id, "Tracking" );
	addColour( PID_SQUARE_R, "Square Colour", "Tracking" );

	SetParamInfof( PID_ECHO, "Echo Level", FF_TYPE_STANDARD );
	SetParamInfof( PID_ECHO_DELAY, "Echo Delay", FF_TYPE_STANDARD );
	addOption( PID_ECHO_COUNT, "Echo Copies", { "1", "2", "3", "4" } );
	for( FFUInt32 id : { PID_ECHO, PID_ECHO_DELAY, PID_ECHO_COUNT } )
		SetParamGroup( id, "Echo" );
	addColour( PID_ECHO_A_R, "Echo Colour A", "Echo" );
	addColour( PID_ECHO_B_R, "Echo Colour B", "Echo" );

	addOption( PID_ASCII, "ASCII", { "Off", "Subject", "Background", "Whole frame" } );
	SetParamInfof( PID_ASCII_SIZE, "ASCII Size", FF_TYPE_STANDARD );
	SetParamGroup( PID_ASCII, "ASCII" );
	SetParamGroup( PID_ASCII_SIZE, "ASCII" );
	addColour( PID_ASCII_R, "ASCII Colour", "ASCII" );

	SetParamInfof( PID_DISTORTION, "Distortion", FF_TYPE_STANDARD );
	SetParamInfof( PID_DISPERSION, "Dispersion", FF_TYPE_STANDARD );
	SetParamInfof( PID_FLAMES, "Flames", FF_TYPE_STANDARD );
	addOption( PID_FLAME_COLOR, "Flame Colour", { "Fire", "Blue", "Toxic", "Violet", "White" } );
	SetParamInfof( PID_DEPTH, "Depth", FF_TYPE_STANDARD );
	SetParamInfof( PID_DUAL, "Dual Tone", FF_TYPE_STANDARD );
	for( FFUInt32 id : { PID_DISTORTION, PID_DISPERSION, PID_FLAMES, PID_FLAME_COLOR, PID_DEPTH, PID_DUAL } )
		SetParamGroup( id, "Levels" );
	addColour( PID_DUAL_DARK_R, "Dual Dark", "Levels" );
	addColour( PID_DUAL_LIGHT_R, "Dual Light", "Levels" );
}

FFResult ImagLooks::InitGL( const FFGLViewportStruct* vp )
{
	std::string error;
	if( !CompileOrLog( copyShader, kVertexShader, kCopyFragmentShader, "IMAG Looks", "copy", &error ) ||
		!CompileOrLog( maskShader, kVertexShader, kMaskFragmentShader, "IMAG Looks", "mask", &error ) ||
		!CompileOrLog( momentsShader, kVertexShader, kMomentsFragmentShader, "IMAG Looks", "moments", &error ) ||
		!CompileOrLog( echoShader, kVertexShader, kEchoStoreFragmentShader, "IMAG Looks", "echo", &error ) ||
		!CompileOrLog( fireShader, kVertexShader, kFireFragmentShader, "IMAG Looks", "fire", &error ) ||
		!CompileOrLog( compositeShader, kVertexShader, kLooksCompositeFragmentShader, "IMAG Looks", "composite", &error ) ||
		!quad.Initialise() )
	{
		SetStatus( error.empty() ? "Could not create GPU resources" : error );
		DeInitGL();
		return FF_FAIL;
	}
	FFGLLog::LogToHost( "IMAG Looks: ready" );
	return CFFGLPlugin::InitGL( vp );
}

FFResult ImagLooks::DeInitGL()
{
	for( FFGLShader* shader : { &copyShader, &maskShader, &momentsShader, &echoShader, &fireShader, &compositeShader } )
		shader->FreeGLResources();
	for( auto& shader : lookShaders )
		shader.FreeGLResources();
	std::fill( lookStates.begin(), lookStates.end(), ShaderState::NotCompiled );
	quad.Release();
	ReleaseTargets();
	return FF_SUCCESS;
}

bool ImagLooks::EnsureTargets( GLsizei width, GLsizei height )
{
	if( width == targetWidth && height == targetHeight && momentsFBO != 0 )
		return true;

	ReleaseTargets();
	maskWidth  = std::max( width / 2, 1 );
	maskHeight = std::max( height / 2, 1 );

	bool ok = frames[ 0 ].Initialise( width, height ) && frames[ 1 ].Initialise( width, height ) &&
			  lookTarget.Initialise( width, height ) && mask.Initialise( maskWidth, maskHeight, GL_RGBA16F ) &&
			  heat[ 0 ].Initialise( maskWidth, maskHeight, GL_RGBA16F ) && heat[ 1 ].Initialise( maskWidth, maskHeight, GL_RGBA16F ) &&
			  trackerState.Initialise( 2, 1, GL_RGBA32F );
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
	ClearTarget( heat[ 0 ].GetGLID(), 0, 0, 0, 0 );
	ClearTarget( heat[ 1 ].GetGLID(), 0, 0, 0, 0 );
	ClearTarget( trackerState.GetGLID(), 0.5f, 0.5f, 0.12f, 0.2f );
	return true;
}

// The echo history is allocated the first time echoes are turned on, so it costs no memory otherwise.
bool ImagLooks::EnsureEchoRing()
{
	if( echoFBO != 0 )
		return true;
	echoTextures.resize( kEchoRingLength );
	glGenTextures( (GLsizei)echoTextures.size(), echoTextures.data() );
	for( GLuint tex : echoTextures )
	{
		Scoped2DTextureBinding binding( tex );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, maskWidth, maskHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	}
	glGenFramebuffers( 1, &echoFBO );
	ScopedFBOBinding fboBinding( echoFBO, ScopedFBOBinding::RB_REVERT );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	for( GLuint tex : echoTextures )
	{
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	echoHead = 0;
	return true;
}

void ImagLooks::ReleaseTargets()
{
	for( FFGLFBO* fbo : { &frames[ 0 ], &frames[ 1 ], &lookTarget, &mask, &heat[ 0 ], &heat[ 1 ], &trackerState } )
		fbo->Release();
	if( momentTextures[ 0 ] != 0 )
		glDeleteTextures( 2, momentTextures );
	momentTextures[ 0 ] = momentTextures[ 1 ] = 0;
	if( momentsFBO != 0 )
		glDeleteFramebuffers( 1, &momentsFBO );
	momentsFBO = 0;
	if( !echoTextures.empty() )
		glDeleteTextures( (GLsizei)echoTextures.size(), echoTextures.data() );
	echoTextures.clear();
	if( echoFBO != 0 )
		glDeleteFramebuffers( 1, &echoFBO );
	echoFBO      = 0;
	targetWidth  = 0;
	targetHeight = 0;
}

void ImagLooks::ClearTarget( GLuint fbo, float r, float g, float b, float a )
{
	ScopedFBOBinding binding( fbo, ScopedFBOBinding::RB_REVERT );
	glClearColor( r, g, b, a );
	glClear( GL_COLOR_BUFFER_BIT );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
}

// Draws `texture` with the copy shader into whatever framebuffer is bound.
void ImagLooks::DrawCopy( GLuint texture, float maxU, float maxV )
{
	ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( texture );
	copyShader.Set( "InputTexture", 0 );
	copyShader.Set( "MaxUV", maxU, maxV );
	quad.Draw();
}

// Compiles a look's program on first use. Returns nullptr if the driver rejected it.
FFGLShader* ImagLooks::LookShader( int look )
{
	if( lookStates[ look ] == ShaderState::NotCompiled )
	{
		const std::string source = std::string( "#version 410 core\n#define LOOK_ID " ) + std::to_string( look ) + "\n" + kLooksFragmentBody;
		const std::string pass   = std::string( "look '" ) + kLookNames[ look ] + "'";
		bool ok = CompileOrLog( lookShaders[ look ], kVertexShader, source.c_str(), "IMAG Looks", pass.c_str(), &lookErrors[ look ] );
		lookStates[ look ] = ok ? ShaderState::Ready : ShaderState::Failed;
	}
	if( lookStates[ look ] == ShaderState::Failed )
	{
		SetStatus( lookErrors[ look ] );
		return nullptr;
	}
	SetStatus( std::string( "OK: " ) + kLookNames[ look ] );
	return &lookShaders[ look ];
}

void ImagLooks::SetStatus( const std::string& text )
{
	if( text == status )
		return;
	status = text;
	RaiseParamEvent( PID_STATUS, FF_EVENT_FLAG_VALUE );
}

FFResult ImagLooks::ProcessOpenGL( ProcessOpenGLStruct* pGL )
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
	time         = std::fmod( time + dt, 3600.0f );

	// Beat clock: Resolume's tempo when synced, otherwise free-running at the animation rate.
	const float animRate = 1.0f + params[ PID_ANIM_RATE ] * 7.0f;//cycles per second when free-running
	float beatPhase;
	if( params[ PID_BPM_SYNC ] > 0.5f )
		beatPhase = std::fmod( barPhase * 4.0f, 1.0f );
	else
	{
		freePhase = std::fmod( freePhase + dt * animRate * 0.5f, 1.0f );
		beatPhase = freePhase;
	}
	if( beatPhase < lastPhase )
		beatIndex = std::fmod( beatIndex + 1.0f, 997.0f );
	lastPhase = beatPhase;

	// Stop-motion step for cartoon boil and paper jitter: 4..24 redraws per second.
	const float step = std::floor( time * ( 4.0f + params[ PID_ANIM_RATE ] * 20.0f ) );

	auto bindTexture = []( GLenum unit, GLuint tex ) {
		glActiveTexture( GL_TEXTURE0 + unit );
		glBindTexture( GL_TEXTURE_2D, tex );
	};
	auto unbindTextures = [ & ]( GLenum count ) {
		for( GLenum unit = count; unit-- > 0; )
			bindTexture( unit, 0 );
	};

	const float width   = (float)targetWidth;
	const float height  = (float)targetHeight;
	const int previous  = currentFrame;
	currentFrame        = 1 - currentFrame;
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
			DrawCopy( input.Handle, maxCoords.s, maxCoords.t );
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

	// 3. Mask moments, mipmapped: per-cell centroids for squares, 1x1 level for the subject centre.
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
	for( GLuint tex : momentTextures )
	{
		Scoped2DTextureBinding binding( tex );
		glGenerateMipmap( GL_TEXTURE_2D );
	}

	// 4. Echo history.
	const bool echoOn   = params[ PID_ECHO ] > 0.0f;
	const int echoCount = OptionIndex( params[ PID_ECHO_COUNT ], kMaxEchoes ) + 1;
	const int spacing   = 1 + (int)std::lround( params[ PID_ECHO_DELAY ] * ( kMaxEchoSpacing - 1 ) );
	if( echoOn && EnsureEchoRing() )
	{
		echoHead = ( echoHead + 1 ) % kEchoRingLength;
		glViewport( 0, 0, maskWidth, maskHeight );
		ScopedFBOBinding fboBinding( echoFBO, ScopedFBOBinding::RB_REVERT );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, echoTextures[ echoHead ], 0 );
		ScopedShaderBinding shaderBinding( echoShader.GetGLID() );
		bindTexture( 0, frameTex );
		bindTexture( 1, mask.GetTextureInfo().Handle );
		echoShader.Set( "FrameTex", 0 );
		echoShader.Set( "MaskTex", 1 );
		echoShader.Set( "MaxUV", 1.0f, 1.0f );
		quad.Draw();
		unbindTextures( 2 );
	}

	// 5. Flames.
	const bool flamesOn = params[ PID_FLAMES ] > 0.0f;
	if( flamesOn )
	{
		const int heatWrite = 1 - heatRead;
		glViewport( 0, 0, maskWidth, maskHeight );
		ScopedFBOBinding fboBinding( heat[ heatWrite ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( fireShader.GetGLID() );
		bindTexture( 0, heat[ heatRead ].GetTextureInfo().Handle );
		bindTexture( 1, mask.GetTextureInfo().Handle );
		bindTexture( 2, trackerState.GetTextureInfo().Handle );
		fireShader.Set( "HeatTex", 0 );
		fireShader.Set( "MaskTex", 1 );
		fireShader.Set( "StateTex", 2 );
		fireShader.Set( "MaxUV", 1.0f, 1.0f );
		fireShader.Set( "SimTexel", 1.0f / maskWidth, 1.0f / maskHeight );
		fireShader.Set( "Aspect", width / height );
		fireShader.Set( "Time", time );
		fireShader.Set( "Step", dt * 60.0f );
		fireShader.Set( "Emit", 0.25f + params[ PID_FLAMES ] * 0.6f );
		fireShader.Set( "Rise", 0.35f + params[ PID_FLAMES ] * 0.3f );
		fireShader.Set( "Turbulence", 0.5f );
		fireShader.Set( "Decay", 0.6f );
		fireShader.Set( "Edges", 0.6f );
		fireShader.Set( "Wind", 0.0f );
		fireShader.Set( "Focus", 0.0f );
		fireShader.Set( "PulseGain", 1.0f );
		quad.Draw();
		unbindTextures( 3 );
		heatRead   = heatWrite;
		heatActive = true;
	}
	else if( heatActive )
	{
		ClearTarget( heat[ 0 ].GetGLID(), 0, 0, 0, 0 );
		ClearTarget( heat[ 1 ].GetGLID(), 0, 0, 0, 0 );
		heatActive = false;
	}

	// 6. Look. A rejected look passes the camera through and reports why in Status.
	glViewport( 0, 0, targetWidth, targetHeight );
	{
		ScopedFBOBinding fboBinding( lookTarget.GetGLID(), ScopedFBOBinding::RB_REVERT );
		const int look     = OptionIndex( params[ PID_LOOK ], (int)kLookCount );
		FFGLShader* shader = LookShader( look );
		if( shader == nullptr )
			DrawCopy( frameTex, 1.0f, 1.0f );
		else
		{
			ScopedShaderBinding shaderBinding( shader->GetGLID() );
			ScopedSamplerActivation activateSampler( 0 );
			Scoped2DTextureBinding textureBinding( frameTex );
			shader->Set( "InputTex", 0 );
			shader->Set( "MaxUV", 1.0f, 1.0f );
			shader->Set( "Resolution", width, height );
			shader->Set( "Time", time );
			shader->Set( "BeatPhase", beatPhase );
			shader->Set( "BeatIndex", beatIndex );
			shader->Set( "Step", std::fmod( step, 997.0f ) );
			shader->Set( "Mix", 1.0f );//Dry/wet happens in the composite, after the overlays.
			shader->Set( "Intensity", params[ PID_INTENSITY ] );
			shader->Set( "Crush", params[ PID_CRUSH ] * 0.6f );
			shader->Set( "Contrast", params[ PID_CONTRAST ] );
			shader->Set( "EdgeGlow", params[ PID_EDGE_GLOW ] * 2.0f );
			shader->Set( "Detail", params[ PID_DETAIL ] );
			shader->Set( "Texture", params[ PID_TEXTURE ] );
			shader->Set( "Sparkles", params[ PID_SPARKLES ] );
			shader->Set( "HueShift", params[ PID_HUE ] );
			quad.Draw();
		}
	}

	// 7. Overlays and dry/wet, straight into the host framebuffer.
	auto setColour = [ & ]( const char* name, FFUInt32 first ) {
		float rgb[ 3 ];
		HsbToRgb( &params[ first ], rgb );
		compositeShader.Set( name, rgb[ 0 ], rgb[ 1 ], rgb[ 2 ] );
	};
	glViewport( currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height );
	{
		ScopedShaderBinding shaderBinding( compositeShader.GetGLID() );
		bindTexture( 0, lookTarget.GetTextureInfo().Handle );
		bindTexture( 1, frameTex );
		bindTexture( 2, mask.GetTextureInfo().Handle );
		bindTexture( 3, heat[ heatRead ].GetTextureInfo().Handle );
		bindTexture( 4, momentTextures[ 0 ] );
		const char* echoNames[] = { "EchoTex0", "EchoTex1", "EchoTex2", "EchoTex3" };
		for( int i = 0; i < kMaxEchoes; ++i )
		{
			GLuint tex = 0;
			if( echoOn && !echoTextures.empty() )
				tex = echoTextures[ ( echoHead - spacing * ( i + 1 ) + kEchoRingLength * kMaxEchoes ) % kEchoRingLength ];
			bindTexture( 5 + i, tex );
			compositeShader.Set( echoNames[ i ], 5 + i );
		}
		compositeShader.Set( "LookTex", 0 );
		compositeShader.Set( "FrameTex", 1 );
		compositeShader.Set( "MaskTex", 2 );
		compositeShader.Set( "HeatTex", 3 );
		compositeShader.Set( "MomentTex", 4 );
		compositeShader.Set( "MaxUV", 1.0f, 1.0f );
		compositeShader.Set( "Resolution", width, height );
		compositeShader.Set( "MaskTexel", 1.0f / maskWidth, 1.0f / maskHeight );
		compositeShader.Set( "Time", time );
		compositeShader.Set( "Mix", params[ PID_MIX ] );
		compositeShader.Set( "Distortion", params[ PID_DISTORTION ] );
		compositeShader.Set( "Dispersion", params[ PID_DISPERSION ] );
		compositeShader.Set( "Depth", params[ PID_DEPTH ] );
		compositeShader.Set( "EchoLevel", echoOn ? params[ PID_ECHO ] : 0.0f );
		compositeShader.Set( "EchoCount", echoCount );
		setColour( "EchoColorA", PID_ECHO_A_R );
		setColour( "EchoColorB", PID_ECHO_B_R );
		compositeShader.Set( "Flames", flamesOn ? params[ PID_FLAMES ] : 0.0f );
		compositeShader.Set( "Palette", OptionIndex( params[ PID_FLAME_COLOR ], 5 ) );
		compositeShader.Set( "DualTone", params[ PID_DUAL ] );
		setColour( "DualDark", PID_DUAL_DARK_R );
		setColour( "DualLight", PID_DUAL_LIGHT_R );
		compositeShader.Set( "AsciiArea", OptionIndex( params[ PID_ASCII ], 4 ) );
		compositeShader.Set( "AsciiSize", 8.0f + params[ PID_ASCII_SIZE ] * 24.0f );
		setColour( "AsciiColor", PID_ASCII_R );
		compositeShader.Set( "Squares", OptionIndex( params[ PID_SQUARES ], 3 ) );
		compositeShader.Set( "GridLevel", 5 - OptionIndex( params[ PID_SQUARE_COUNT ], 3 ) );//8x8, 16x16 or 32x32 cells
		compositeShader.Set( "SquareSize", 3.0f + params[ PID_SQUARE_SIZE ] * 14.0f );
		setColour( "SquareColor", PID_SQUARE_R );
		quad.Draw();
		unbindTextures( 5 + kMaxEchoes );
	}
	return FF_SUCCESS;
}

FFResult ImagLooks::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PID_COUNT || index == PID_STATUS )
		return index == PID_STATUS ? FF_SUCCESS : FF_FAIL;
	params[ index ] = value;
	return FF_SUCCESS;
}

float ImagLooks::GetFloatParameter( unsigned int index )
{
	return index < PID_COUNT ? params[ index ] : 0.0f;
}

FFResult ImagLooks::SetTextParameter( unsigned int index, const char* )
{
	return index == PID_STATUS ? FF_SUCCESS : FF_FAIL;//Status is read-only; typing into it changes nothing.
}

char* ImagLooks::GetTextParameter( unsigned int index )
{
	return index == PID_STATUS ? const_cast< char* >( status.c_str() ) : nullptr;
}
