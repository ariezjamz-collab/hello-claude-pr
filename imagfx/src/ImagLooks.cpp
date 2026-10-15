#include "ImagLooks.h"
#include "ImagLooksShaders.h"
#include "PluginLog.h"
#include <algorithm>
#include <cmath>
#include <string>

using namespace ffglex;

namespace
{
enum ParamID : FFUInt32
{
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
	PID_COUNT
};

// Order must match the look constants in ImagLooksShaders.h.
const char* kLookNames[] = { "Night Vision", "Fire Map", "Gold Edge", "Blood Red", "Analog TV", "Glitter Ice", "Ice Cyan",
							 "Faded Film", "RGB Split", "Comic", "Halftone", "Paper Texture", "Animated Cartoon" };
const unsigned int kLookCount        = sizeof( kLookNames ) / sizeof( kLookNames[ 0 ] );
const unsigned int kFirstStylizeLook = 9;//Comic onwards sit under a separator in the dropdown.
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
	"Camera looks panel: night vision, fire, gold edge, analog, glitter, comic, halftone, paper, cartoon",
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

	params[ PID_MIX ]       = 1.0f;
	params[ PID_INTENSITY ] = 0.85f;
	params[ PID_CRUSH ]     = 0.15f;
	params[ PID_CONTRAST ]  = 0.5f;
	params[ PID_EDGE_GLOW ] = 0.3f;
	params[ PID_DETAIL ]    = 0.35f;
	params[ PID_TEXTURE ]   = 0.4f;
	params[ PID_BPM_SYNC ]  = 1.0f;
	params[ PID_ANIM_RATE ] = 0.4f;

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
}

FFResult ImagLooks::InitGL( const FFGLViewportStruct* vp )
{
	std::string error;
	if( !CompileOrLog( copyShader, kVertexShader, kCopyFragmentShader, "IMAG Looks", "copy", &error ) || !quad.Initialise() )
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
	copyShader.FreeGLResources();
	for( auto& shader : lookShaders )
		shader.FreeGLResources();
	std::fill( lookStates.begin(), lookStates.end(), ShaderState::NotCompiled );
	quad.Release();
	frame.Release();
	return FF_SUCCESS;
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

	if( frame.GetWidth() != input.Width || frame.GetHeight() != input.Height )
	{
		frame.Release();
		if( !frame.Initialise( (GLsizei)input.Width, (GLsizei)input.Height ) )
			return FF_FAIL;
	}

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

	glViewport( 0, 0, (GLsizei)input.Width, (GLsizei)input.Height );
	{
		FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );
		ScopedFBOBinding fboBinding( frame.GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
		ScopedSamplerActivation activateSampler( 0 );
		Scoped2DTextureBinding textureBinding( input.Handle );
		copyShader.Set( "InputTexture", 0 );
		copyShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		quad.Draw();
	}

	glViewport( currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height );
	const int look     = std::min( std::max( (int)std::lround( params[ PID_LOOK ] ), 0 ), (int)kLookCount - 1 );
	FFGLShader* shader = LookShader( look );
	if( shader == nullptr )
	{
		// This look's shader was rejected: pass the image through and leave the reason in Status.
		ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
		ScopedSamplerActivation activateSampler( 0 );
		Scoped2DTextureBinding textureBinding( frame.GetTextureInfo().Handle );
		copyShader.Set( "InputTexture", 0 );
		copyShader.Set( "MaxUV", 1.0f, 1.0f );
		quad.Draw();
		return FF_SUCCESS;
	}
	{
		ScopedShaderBinding shaderBinding( shader->GetGLID() );
		ScopedSamplerActivation activateSampler( 0 );
		Scoped2DTextureBinding textureBinding( frame.GetTextureInfo().Handle );
		shader->Set( "InputTex", 0 );
		shader->Set( "MaxUV", 1.0f, 1.0f );
		shader->Set( "Resolution", (float)input.Width, (float)input.Height );
		shader->Set( "Time", time );
		shader->Set( "BeatPhase", beatPhase );
		shader->Set( "BeatIndex", beatIndex );
		shader->Set( "Step", std::fmod( step, 997.0f ) );
		shader->Set( "Mix", params[ PID_MIX ] );
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
	return FF_SUCCESS;
}

FFResult ImagLooks::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PID_COUNT )
		return FF_FAIL;
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
