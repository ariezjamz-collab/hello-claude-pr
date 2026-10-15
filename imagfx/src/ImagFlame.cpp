#include "ImagFlame.h"
#include "ImagFlameShaders.h"
#include "PluginLog.h"
#include "PassThrough.h"
#include <algorithm>
#include <cmath>

using namespace ffglex;

namespace
{
enum ParamID : FFUInt32
{
	PID_ENABLED,
	PID_MIX,
	PID_MONO,
	PID_CONTRAST,
	PID_TINT,
	PID_SOURCE,
	PID_THRESHOLD,
	PID_FLAME_AMOUNT,
	PID_FLAME_HEIGHT,
	PID_TURBULENCE,
	PID_LIFETIME,
	PID_EDGES,
	PID_WIND,
	PID_PALETTE,
	PID_GLOW,
	PID_BEHIND,
	PID_HAZE,
	PID_WARP,
	PID_WARP_SCALE,
	PID_DISPERSION,
	PID_DISPERSION_MODE,
	PID_FOCUS,
	PID_TRACK_SMOOTH,
	PID_HUD,
	PID_HUD_OPACITY,
	PID_HUD_DENSITY,
	PID_SQUARE_SIZE,
	PID_BEAT_PULSE,
	PID_RESET,
	PID_COUNT
};

struct ParamDef
{
	FFUInt32 id;
	const char* name;
	FFUInt32 type;
	float defaultValue;
	std::vector< const char* > options;
};

const std::vector< ParamDef > kParams = {
	{ PID_ENABLED, "Effect On", FF_TYPE_BOOLEAN, 1.0f, {} },
	{ PID_MIX, "Mix", FF_TYPE_STANDARD, 1.0f, {} },
	{ PID_MONO, "Mono", FF_TYPE_STANDARD, 1.0f, {} },
	{ PID_CONTRAST, "Contrast", FF_TYPE_STANDARD, 0.6f, {} },
	{ PID_TINT, "Mono Tint", FF_TYPE_OPTION, 0.0f, { "Neutral", "Cold", "Warm", "Sepia" } },
	{ PID_SOURCE, "Subject Source", FF_TYPE_OPTION, 0.0f, { "Motion", "Keyed alpha", "Brightness", "Motion in key" } },
	{ PID_THRESHOLD, "Threshold", FF_TYPE_STANDARD, 0.3f, {} },
	{ PID_FLAME_AMOUNT, "Flame Amount", FF_TYPE_STANDARD, 0.6f, {} },
	{ PID_FLAME_HEIGHT, "Flame Height", FF_TYPE_STANDARD, 0.5f, {} },
	{ PID_TURBULENCE, "Turbulence", FF_TYPE_STANDARD, 0.5f, {} },
	{ PID_LIFETIME, "Flame Life", FF_TYPE_STANDARD, 0.6f, {} },
	{ PID_EDGES, "Burn Edges", FF_TYPE_STANDARD, 0.5f, {} },
	{ PID_WIND, "Wind", FF_TYPE_STANDARD, 0.5f, {} },
	{ PID_PALETTE, "Flame Color", FF_TYPE_OPTION, 0.0f, { "Fire", "Blue", "Toxic", "Violet", "White" } },
	{ PID_GLOW, "Flame Glow", FF_TYPE_STANDARD, 0.6f, {} },
	{ PID_BEHIND, "Flames Behind", FF_TYPE_STANDARD, 0.4f, {} },
	{ PID_HAZE, "Heat Haze", FF_TYPE_STANDARD, 0.5f, {} },
	{ PID_WARP, "Distortion", FF_TYPE_STANDARD, 0.0f, {} },
	{ PID_WARP_SCALE, "Distort Scale", FF_TYPE_STANDARD, 0.4f, {} },
	{ PID_DISPERSION, "Dispersion", FF_TYPE_STANDARD, 0.3f, {} },
	{ PID_DISPERSION_MODE, "Disperse Mode", FF_TYPE_OPTION, 0.0f, { "Follow haze", "Radial from subject" } },
	{ PID_FOCUS, "Track Focus", FF_TYPE_STANDARD, 0.0f, {} },
	{ PID_TRACK_SMOOTH, "Track Smooth", FF_TYPE_STANDARD, 0.6f, {} },
	{ PID_HUD, "Tracker HUD", FF_TYPE_OPTION, 1.0f, { "Off", "Squares", "Squares + Lines", "Box", "Ring" } },
	{ PID_HUD_OPACITY, "HUD Opacity", FF_TYPE_STANDARD, 0.8f, {} },
	{ PID_HUD_DENSITY, "Square Count", FF_TYPE_OPTION, 1.0f, { "Few", "Medium", "Many" } },
	{ PID_SQUARE_SIZE, "Square Size", FF_TYPE_STANDARD, 0.35f, {} },
	{ PID_BEAT_PULSE, "Beat Pulse", FF_TYPE_STANDARD, 0.0f, {} },
	{ PID_RESET, "Reset Flames", FF_TYPE_EVENT, 0.0f, {} },
};

const float kTints[][ 3 ] = { { 1.0f, 1.0f, 1.0f }, { 0.85f, 0.95f, 1.12f }, { 1.1f, 1.0f, 0.85f }, { 1.08f, 0.95f, 0.74f } };

const GLsizei kMomentsSize  = 256;
const int kMomentsTopLevel  = 8;//log2( kMomentsSize ): the 1x1 mip level.

int OptionIndex( float value, int count )
{
	return std::min( std::max( (int)std::lround( value ), 0 ), count - 1 );
}
}// namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< ImagFlame >,
	"IMFL",
	"IOKI Flame",
	2,
	1,
	1,
	0,
	FF_EFFECT,
	"Monochrome camera with tracked flame trails, heat-haze distortion and dispersion",
	"IOKI FX" );

ImagFlame::ImagFlame() :
	params( PID_COUNT, 0.0f )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	for( const ParamDef& def : kParams )
	{
		params[ def.id ] = def.defaultValue;
		if( def.type == FF_TYPE_OPTION )
		{
			SetOptionParamInfo( def.id, def.name, (unsigned int)def.options.size(), def.defaultValue );
			for( unsigned int i = 0; i < def.options.size(); ++i )
				SetParamElementInfo( def.id, i, def.options[ i ], (float)i );
		}
		else if( def.type == FF_TYPE_EVENT )
			SetParamInfo( def.id, def.name, FF_TYPE_EVENT, false );
		else if( def.type == FF_TYPE_BOOLEAN )
			SetParamInfo( def.id, def.name, FF_TYPE_BOOLEAN, def.defaultValue > 0.5f );
		else
			SetParamInfof( def.id, def.name, def.type );
	}
}

FFResult ImagFlame::InitGL( const FFGLViewportStruct* vp )
{
	if( !CompileOrLog( copyShader, kVertexShader, kCopyFragmentShader, "IOKI Flame", "copy" ) ||
		!CompileOrLog( maskShader, kVertexShader, kMaskFragmentShader, "IOKI Flame", "mask" ) ||
		!CompileOrLog( momentsShader, kVertexShader, kMomentsFragmentShader, "IOKI Flame", "moments" ) ||
		!CompileOrLog( trackerShader, kVertexShader, kTrackerFragmentShader, "IOKI Flame", "tracker" ) ||
		!CompileOrLog( fireShader, kVertexShader, kFireFragmentShader, "IOKI Flame", "fire" ) ||
		!CompileOrLog( compositeShader, kVertexShader, kFlameCompositeFragmentShader, "IOKI Flame", "composite" ) ||
		!quad.Initialise() )
	{
		DeInitGL();
		return FF_FAIL;
	}
	FFGLLog::LogToHost( "IOKI Flame: ready" );
	return CFFGLPlugin::InitGL( vp );
}

FFResult ImagFlame::DeInitGL()
{
	copyShader.FreeGLResources();
	maskShader.FreeGLResources();
	momentsShader.FreeGLResources();
	trackerShader.FreeGLResources();
	fireShader.FreeGLResources();
	compositeShader.FreeGLResources();
	quad.Release();
	ReleaseTargets();
	return FF_SUCCESS;
}

bool ImagFlame::EnsureTargets( GLsizei width, GLsizei height )
{
	if( width == targetWidth && height == targetHeight && momentsFBO != 0 )
		return true;

	ReleaseTargets();
	simWidth  = std::max( width / 2, 1 );
	simHeight = std::max( height / 2, 1 );

	bool ok = frames[ 0 ].Initialise( width, height ) && frames[ 1 ].Initialise( width, height ) &&
			  mask.Initialise( simWidth, simHeight, GL_RGBA16F ) &&
			  heat[ 0 ].Initialise( simWidth, simHeight, GL_RGBA16F ) && heat[ 1 ].Initialise( simWidth, simHeight, GL_RGBA16F ) &&
			  trackerState[ 0 ].Initialise( 2, 1, GL_RGBA32F ) && trackerState[ 1 ].Initialise( 2, 1, GL_RGBA32F );
	if( !ok )
	{
		ReleaseTargets();
		return false;
	}

	glGenTextures( 2, momentTextures );
	for( GLuint tex : momentTextures )
	{
		Scoped2DTextureBinding binding( tex );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, kMomentsSize, kMomentsSize, 0, GL_RGBA, GL_FLOAT, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glGenerateMipmap( GL_TEXTURE_2D );//Allocate the full chain once.
	}
	glGenFramebuffers( 1, &momentsFBO );
	{
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
	ResetSimulation();
	return true;
}

void ImagFlame::ReleaseTargets()
{
	for( auto* group : { frames, heat, trackerState } )
		for( int i = 0; i < 2; ++i )
			group[ i ].Release();
	mask.Release();
	if( momentTextures[ 0 ] != 0 )
		glDeleteTextures( 2, momentTextures );
	momentTextures[ 0 ] = momentTextures[ 1 ] = 0;
	if( momentsFBO != 0 )
		glDeleteFramebuffers( 1, &momentsFBO );
	momentsFBO   = 0;
	targetWidth  = 0;
	targetHeight = 0;
}

// Clears the flames and puts the tracker back in the middle of the frame with a person-sized spread.
void ImagFlame::ResetSimulation()
{
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	for( auto& fbo : heat )
	{
		ScopedFBOBinding binding( fbo.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	glClearColor( 0.5f, 0.5f, 0.12f, 0.2f );
	for( auto& fbo : trackerState )
	{
		ScopedFBOBinding binding( fbo.GetGLID(), ScopedFBOBinding::RB_REVERT );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
}

void ImagFlame::DrawCopy( const FFGLTextureStruct& input, FFGLFBO& target )
{
	FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );
	ScopedFBOBinding fboBinding( target.GetGLID(), ScopedFBOBinding::RB_REVERT );
	ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
	ScopedSamplerActivation activateSampler( 0 );
	Scoped2DTextureBinding textureBinding( input.Handle );
	copyShader.Set( "InputTexture", 0 );
	copyShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
	quad.Draw();
}

FFResult ImagFlame::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;

	const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];
	if( params[ PID_ENABLED ] < 0.5f )
	{
		imag::PassThrough( copyShader, quad, input, currentViewport );
		resetRequested = true;//start from clean flames and history when switched back on
		primeHistory   = true;
		lastHostTime   = -1.0;
		return FF_SUCCESS;
	}
	if( !EnsureTargets( (GLsizei)input.Width, (GLsizei)input.Height ) )
		return FF_FAIL;
	if( resetRequested )
	{
		ResetSimulation();
		resetRequested = false;
	}

	float dt = 1.0f / 60.0f;
	if( lastHostTime >= 0.0 && hostTime > lastHostTime )
		dt = std::min( (float)( hostTime - lastHostTime ), 0.1f );
	lastHostTime = hostTime;
	simTime      = std::fmod( simTime + dt, 1000.0f );
	const float step = dt * 60.0f;

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
	const float aspect  = width / height;
	const int previous  = currentFrame;
	currentFrame        = 1 - currentFrame;
	const int heatWrite = 1 - heatRead;
	const int stateWrite = 1 - stateRead;

	// 1. Frame history. On the first frame both slots get the input, so there is no false motion burst.
	glViewport( 0, 0, targetWidth, targetHeight );
	DrawCopy( input, frames[ currentFrame ] );
	if( primeHistory )
	{
		DrawCopy( input, frames[ previous ] );
		primeHistory = false;
	}

	// 2. Subject mask.
	glViewport( 0, 0, simWidth, simHeight );
	{
		ScopedFBOBinding fboBinding( mask.GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( maskShader.GetGLID() );
		bindTexture( 0, frames[ currentFrame ].GetTextureInfo().Handle );
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

	// 3. Mask moments, reduced to 1x1 by mipmapping.
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

	// 4. Tracker update.
	glViewport( 0, 0, 2, 1 );
	{
		ScopedFBOBinding fboBinding( trackerState[ stateWrite ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( trackerShader.GetGLID() );
		bindTexture( 0, momentTextures[ 0 ] );
		bindTexture( 1, momentTextures[ 1 ] );
		bindTexture( 2, trackerState[ stateRead ].GetTextureInfo().Handle );
		trackerShader.Set( "Moment0Tex", 0 );
		trackerShader.Set( "Moment1Tex", 1 );
		trackerShader.Set( "PreviousState", 2 );
		trackerShader.Set( "MaxUV", 1.0f, 1.0f );
		trackerShader.Set( "TopLevel", kMomentsTopLevel );
		const float follow = 1.0f - std::pow( params[ PID_TRACK_SMOOTH ] * 0.97f, step );
		trackerShader.Set( "Follow", std::min( std::max( follow, 0.01f ), 1.0f ) );
		quad.Draw();
		unbindTextures( 3 );
	}

	// 5. Fire simulation.
	glViewport( 0, 0, simWidth, simHeight );
	{
		const float beat      = std::fmod( barPhase * 4.0f, 1.0f );
		const float envelope  = std::pow( 1.0f - beat, 2.0f ) * 2.5f;
		const float pulseGain = 1.0f + params[ PID_BEAT_PULSE ] * ( envelope - 1.0f );

		ScopedFBOBinding fboBinding( heat[ heatWrite ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( fireShader.GetGLID() );
		bindTexture( 0, heat[ heatRead ].GetTextureInfo().Handle );
		bindTexture( 1, mask.GetTextureInfo().Handle );
		bindTexture( 2, trackerState[ stateWrite ].GetTextureInfo().Handle );
		fireShader.Set( "HeatTex", 0 );
		fireShader.Set( "MaskTex", 1 );
		fireShader.Set( "StateTex", 2 );
		fireShader.Set( "MaxUV", 1.0f, 1.0f );
		fireShader.Set( "SimTexel", 1.0f / simWidth, 1.0f / simHeight );
		fireShader.Set( "Aspect", aspect );
		fireShader.Set( "Time", simTime );
		fireShader.Set( "Step", step );
		fireShader.Set( "Emit", params[ PID_FLAME_AMOUNT ] );
		fireShader.Set( "Rise", params[ PID_FLAME_HEIGHT ] );
		fireShader.Set( "Turbulence", params[ PID_TURBULENCE ] );
		fireShader.Set( "Decay", params[ PID_LIFETIME ] );
		fireShader.Set( "Edges", params[ PID_EDGES ] );
		fireShader.Set( "Wind", ( params[ PID_WIND ] - 0.5f ) * 2.0f );
		fireShader.Set( "Focus", params[ PID_FOCUS ] );
		fireShader.Set( "PulseGain", pulseGain );
		quad.Draw();
		unbindTextures( 3 );
	}

	// 6. Composite into the host framebuffer.
	glViewport( currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height );
	{
		const float* tint = kTints[ OptionIndex( params[ PID_TINT ], 4 ) ];
		ScopedShaderBinding shaderBinding( compositeShader.GetGLID() );
		bindTexture( 0, frames[ currentFrame ].GetTextureInfo().Handle );
		bindTexture( 1, heat[ heatWrite ].GetTextureInfo().Handle );
		bindTexture( 2, mask.GetTextureInfo().Handle );
		bindTexture( 3, trackerState[ stateWrite ].GetTextureInfo().Handle );
		bindTexture( 4, momentTextures[ 0 ] );
		compositeShader.Set( "CurrentTex", 0 );
		compositeShader.Set( "HeatTex", 1 );
		compositeShader.Set( "MaskTex", 2 );
		compositeShader.Set( "StateTex", 3 );
		compositeShader.Set( "MomentTex", 4 );
		compositeShader.Set( "MaxUV", 1.0f, 1.0f );
		compositeShader.Set( "Resolution", width, height );
		compositeShader.Set( "SimTexel", 1.0f / simWidth, 1.0f / simHeight );
		compositeShader.Set( "Time", simTime );
		compositeShader.Set( "Mix", params[ PID_MIX ] );
		compositeShader.Set( "Mono", params[ PID_MONO ] );
		compositeShader.Set( "Contrast", 0.5f + params[ PID_CONTRAST ] * 1.5f );
		compositeShader.Set( "Tint", tint[ 0 ], tint[ 1 ], tint[ 2 ] );
		compositeShader.Set( "Haze", params[ PID_HAZE ] );
		compositeShader.Set( "Warp", params[ PID_WARP ] );
		compositeShader.Set( "WarpScale", params[ PID_WARP_SCALE ] );
		compositeShader.Set( "Dispersion", params[ PID_DISPERSION ] );
		compositeShader.Set( "DispersionMode", OptionIndex( params[ PID_DISPERSION_MODE ], 2 ) );
		compositeShader.Set( "Palette", OptionIndex( params[ PID_PALETTE ], 5 ) );
		compositeShader.Set( "Glow", params[ PID_GLOW ] * 2.0f );
		compositeShader.Set( "Behind", params[ PID_BEHIND ] );
		compositeShader.Set( "Hud", OptionIndex( params[ PID_HUD ], 5 ) );
		compositeShader.Set( "HudOpacity", params[ PID_HUD_OPACITY ] );
		compositeShader.Set( "GridLevel", 5 - OptionIndex( params[ PID_HUD_DENSITY ], 3 ) );//8x8, 16x16 or 32x32 cells
		compositeShader.Set( "SquareSize", 3.0f + params[ PID_SQUARE_SIZE ] * 14.0f );
		quad.Draw();
		unbindTextures( 5 );
	}

	heatRead  = heatWrite;
	stateRead = stateWrite;
	return FF_SUCCESS;
}

FFResult ImagFlame::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PID_COUNT )
		return FF_FAIL;
	if( index == PID_RESET && value > 0.5f )
		resetRequested = true;
	params[ index ] = value;
	return FF_SUCCESS;
}

float ImagFlame::GetFloatParameter( unsigned int index )
{
	return index < PID_COUNT ? params[ index ] : 0.0f;
}
