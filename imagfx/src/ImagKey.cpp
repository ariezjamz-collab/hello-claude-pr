#include "ImagKey.h"
#include "ImagKeyShaders.h"
#include "PluginLog.h"
#include <algorithm>
#include <cmath>

using namespace ffglex;

namespace
{
enum ParamID : FFUInt32
{
	PID_MODE,
	PID_LEARN,
	PID_AUTO_UPDATE,
	PID_THRESHOLD,
	PID_SOFTNESS,
	PID_SHADOWS,
	PID_KEY_R,
	PID_KEY_G,
	PID_KEY_B,
	PID_SPILL,
	PID_LUMA_INVERT,
	PID_CLEAN,
	PID_CHOKE,
	PID_FEATHER,
	PID_SMOOTH,
	PID_CROP_LEFT,
	PID_CROP_RIGHT,
	PID_CROP_TOP,
	PID_CROP_BOTTOM,
	PID_VIEW,
	PID_BACKGROUND,
	PID_BG_R,
	PID_BG_G,
	PID_BG_B,
	PID_STATUS,
	PID_COUNT
};

int OptionIndex( float value, int count )
{
	return std::min( std::max( (int)std::lround( value ), 0 ), count - 1 );
}
}// namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< ImagKey >,
	"IMKY",
	"IMAG Key",
	2,
	1,
	1,
	0,
	FF_EFFECT,
	"Keys the performer out of the camera (difference, chroma or luma key) for the other IMAG effects",
	"IMAG FX prototype" );

ImagKey::ImagKey() :
	params( PID_COUNT, 0.0f ),
	status( "Learning background..." )
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	params[ PID_AUTO_UPDATE ] = 0.2f;
	params[ PID_THRESHOLD ]   = 0.25f;
	params[ PID_SOFTNESS ]    = 0.15f;
	params[ PID_SHADOWS ]     = 0.6f;
	params[ PID_KEY_G ]       = 0.75f;
	params[ PID_KEY_B ]       = 0.2f;
	params[ PID_SPILL ]       = 0.6f;
	params[ PID_CLEAN ]       = 0.25f;
	params[ PID_CHOKE ]       = 0.5f;
	params[ PID_FEATHER ]     = 0.25f;
	params[ PID_SMOOTH ]      = 0.4f;

	auto addOption = [ & ]( FFUInt32 id, const char* name, std::initializer_list< const char* > options ) {
		SetOptionParamInfo( id, name, (unsigned int)options.size(), params[ id ] );
		unsigned int i = 0;
		for( const char* option : options )
		{
			SetParamElementInfo( id, i, option, (float)i );
			++i;
		}
	};
	auto group = [ & ]( std::initializer_list< FFUInt32 > ids, const char* name ) {
		for( FFUInt32 id : ids )
			SetParamGroup( id, name );
	};

	addOption( PID_MODE, "Key Mode", { "Difference (learn stage)", "Chroma (green/blue screen)", "Luma (bright subject)" } );
	SetParamInfo( PID_LEARN, "Learn Background", FF_TYPE_EVENT, false );
	SetParamInfof( PID_AUTO_UPDATE, "Auto Update", FF_TYPE_STANDARD );
	SetParamInfof( PID_THRESHOLD, "Threshold", FF_TYPE_STANDARD );
	SetParamInfof( PID_SOFTNESS, "Softness", FF_TYPE_STANDARD );
	SetParamInfof( PID_SHADOWS, "Ignore Shadows", FF_TYPE_STANDARD );
	SetParamInfof( PID_KEY_R, "Key Colour", FF_TYPE_RED );
	SetParamInfof( PID_KEY_G, "Key Colour_Green", FF_TYPE_GREEN );
	SetParamInfof( PID_KEY_B, "Key Colour_Blue", FF_TYPE_BLUE );
	SetParamInfof( PID_SPILL, "Spill Removal", FF_TYPE_STANDARD );
	SetParamInfo( PID_LUMA_INVERT, "Luma Invert", FF_TYPE_BOOLEAN, false );
	group( { PID_MODE, PID_LEARN, PID_AUTO_UPDATE, PID_THRESHOLD, PID_SOFTNESS, PID_SHADOWS, PID_KEY_R, PID_KEY_G, PID_KEY_B,
			 PID_SPILL, PID_LUMA_INVERT },
		   "Key" );

	SetParamInfof( PID_CLEAN, "Clean Specks", FF_TYPE_STANDARD );
	SetParamInfof( PID_CHOKE, "Shrink / Grow", FF_TYPE_STANDARD );
	SetParamInfof( PID_FEATHER, "Feather", FF_TYPE_STANDARD );
	SetParamInfof( PID_SMOOTH, "Edge Smoothing", FF_TYPE_STANDARD );
	SetParamInfof( PID_CROP_LEFT, "Crop Left", FF_TYPE_STANDARD );
	SetParamInfof( PID_CROP_RIGHT, "Crop Right", FF_TYPE_STANDARD );
	SetParamInfof( PID_CROP_TOP, "Crop Top", FF_TYPE_STANDARD );
	SetParamInfof( PID_CROP_BOTTOM, "Crop Bottom", FF_TYPE_STANDARD );
	group( { PID_CLEAN, PID_CHOKE, PID_FEATHER, PID_SMOOTH, PID_CROP_LEFT, PID_CROP_RIGHT, PID_CROP_TOP, PID_CROP_BOTTOM }, "Matte" );

	addOption( PID_VIEW, "View", { "Output", "Matte", "Overlay" } );
	addOption( PID_BACKGROUND, "Background", { "Transparent", "Black", "Colour" } );
	SetParamInfof( PID_BG_R, "Background Colour", FF_TYPE_RED );
	SetParamInfof( PID_BG_G, "Background Colour_Green", FF_TYPE_GREEN );
	SetParamInfof( PID_BG_B, "Background Colour_Blue", FF_TYPE_BLUE );
	SetParamInfo( PID_STATUS, "Status", FF_TYPE_TEXT, status.c_str() );
	group( { PID_VIEW, PID_BACKGROUND, PID_BG_R, PID_BG_G, PID_BG_B, PID_STATUS }, "Output" );
}

FFResult ImagKey::InitGL( const FFGLViewportStruct* vp )
{
	std::string error;
	if( !CompileOrLog( copyShader, kVertexShader, kCopyFragmentShader, "IMAG Key", "copy", &error ) ||
		!CompileOrLog( plateShader, kVertexShader, kPlateFragmentShader, "IMAG Key", "plate", &error ) ||
		!CompileOrLog( rawShader, kVertexShader, kRawMatteFragmentShader, "IMAG Key", "matte", &error ) ||
		!CompileOrLog( morphShader, kVertexShader, kMorphFragmentShader, "IMAG Key", "clean", &error ) ||
		!CompileOrLog( featherShader, kVertexShader, kFeatherFragmentShader, "IMAG Key", "feather", &error ) ||
		!CompileOrLog( outputShader, kVertexShader, kKeyOutputFragmentShader, "IMAG Key", "output", &error ) ||
		!quad.Initialise() )
	{
		SetStatus( error.empty() ? "Could not create GPU resources" : error );
		DeInitGL();
		return FF_FAIL;
	}
	FFGLLog::LogToHost( "IMAG Key: ready" );
	return CFFGLPlugin::InitGL( vp );
}

FFResult ImagKey::DeInitGL()
{
	for( FFGLShader* shader : { &copyShader, &plateShader, &rawShader, &morphShader, &featherShader, &outputShader } )
		shader->FreeGLResources();
	quad.Release();
	ReleaseTargets();
	return FF_SUCCESS;
}

bool ImagKey::EnsureTargets( GLsizei width, GLsizei height )
{
	if( width == targetWidth && height == targetHeight && frame.GetGLID() != 0 )
		return true;
	ReleaseTargets();
	bool ok = frame.Initialise( width, height ) && plate[ 0 ].Initialise( width, height, GL_RGBA16F ) &&
			  plate[ 1 ].Initialise( width, height, GL_RGBA16F ) && rawMatte.Initialise( width, height ) &&
			  morph[ 0 ].Initialise( width, height ) && morph[ 1 ].Initialise( width, height ) &&
			  matte[ 0 ].Initialise( width, height ) && matte[ 1 ].Initialise( width, height );
	if( !ok )
	{
		ReleaseTargets();
		return false;
	}
	for( FFGLFBO* fbo : { &matte[ 0 ], &matte[ 1 ] } )
	{
		ScopedFBOBinding binding( fbo->GetGLID(), ScopedFBOBinding::RB_REVERT );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	targetWidth     = width;
	targetHeight    = height;
	learnFramesLeft = kLearnFrames;//A new size needs a new plate.
	learnedFrames   = 0;
	return true;
}

void ImagKey::ReleaseTargets()
{
	for( FFGLFBO* fbo : { &frame, &plate[ 0 ], &plate[ 1 ], &rawMatte, &morph[ 0 ], &morph[ 1 ], &matte[ 0 ], &matte[ 1 ] } )
		fbo->Release();
	targetWidth  = 0;
	targetHeight = 0;
}

void ImagKey::SetStatus( const std::string& text )
{
	if( text == status )
		return;
	status = text;
	RaiseParamEvent( PID_STATUS, FF_EVENT_FLAG_VALUE );
}

void ImagKey::UpdateStatus()
{
	const int mode = OptionIndex( params[ PID_MODE ], 3 );
	if( mode == 1 )
		SetStatus( "Chroma key: pick the screen colour in Key Colour" );
	else if( mode == 2 )
		SetStatus( "Luma key: bright areas are kept" );
	else if( learnFramesLeft > 0 )
		SetStatus( "Learning background... keep the stage empty" );
	else if( learnedByUser )
		SetStatus( "Background learned" );
	else
		SetStatus( "Learned from the first second. Clear the stage and press Learn Background for a clean key" );
}

FFResult ImagKey::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL->numInputTextures < 1 || pGL->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct& input = *pGL->inputTextures[ 0 ];
	if( !EnsureTargets( (GLsizei)input.Width, (GLsizei)input.Height ) )
		return FF_FAIL;

	auto bindTexture = []( GLenum unit, GLuint tex ) {
		glActiveTexture( GL_TEXTURE0 + unit );
		glBindTexture( GL_TEXTURE_2D, tex );
	};
	auto unbindTextures = [ & ]( GLenum count ) {
		for( GLenum unit = count; unit-- > 0; )
			bindTexture( unit, 0 );
	};
	const float texelX = 1.0f / targetWidth, texelY = 1.0f / targetHeight;
	const float scale  = targetHeight / 1080.0f;//Pixel radii are tuned for 1080p.
	glViewport( 0, 0, targetWidth, targetHeight );

	// 1. Frame.
	{
		FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );
		ScopedFBOBinding fboBinding( frame.GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( copyShader.GetGLID() );
		bindTexture( 0, input.Handle );
		copyShader.Set( "InputTexture", 0 );
		copyShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
		quad.Draw();
		unbindTextures( 1 );
	}

	// 2. Background plate.
	{
		const int plateWrite = 1 - plateRead;
		float learnWeight    = 0.0f;
		if( learnFramesLeft > 0 )
		{
			learnWeight = 1.0f / ( learnedFrames + 1 );
			++learnedFrames;
			--learnFramesLeft;
		}
		ScopedFBOBinding fboBinding( plate[ plateWrite ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( plateShader.GetGLID() );
		bindTexture( 0, frame.GetTextureInfo().Handle );
		bindTexture( 1, plate[ plateRead ].GetTextureInfo().Handle );
		bindTexture( 2, matte[ matteRead ].GetTextureInfo().Handle );
		plateShader.Set( "FrameTex", 0 );
		plateShader.Set( "PlateTex", 1 );
		plateShader.Set( "MatteTex", 2 );
		plateShader.Set( "MaxUV", 1.0f, 1.0f );
		plateShader.Set( "LearnWeight", learnWeight );
		plateShader.Set( "UpdateRate", params[ PID_AUTO_UPDATE ] * params[ PID_AUTO_UPDATE ] * 0.05f );
		quad.Draw();
		unbindTextures( 3 );
		plateRead = plateWrite;
	}

	// 3. Raw matte.
	const int mode = OptionIndex( params[ PID_MODE ], 3 );
	{
		ScopedFBOBinding fboBinding( rawMatte.GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( rawShader.GetGLID() );
		bindTexture( 0, frame.GetTextureInfo().Handle );
		bindTexture( 1, plate[ plateRead ].GetTextureInfo().Handle );
		rawShader.Set( "FrameTex", 0 );
		rawShader.Set( "PlateTex", 1 );
		rawShader.Set( "MaxUV", 1.0f, 1.0f );
		rawShader.Set( "Texel", texelX, texelY );
		rawShader.Set( "Mode", mode );
		rawShader.Set( "Threshold", params[ PID_THRESHOLD ] * 0.8f );
		rawShader.Set( "Softness", params[ PID_SOFTNESS ] * 0.5f );
		rawShader.Set( "ShadowTolerance", params[ PID_SHADOWS ] );
		rawShader.Set( "KeyColor", params[ PID_KEY_R ], params[ PID_KEY_G ], params[ PID_KEY_B ] );
		rawShader.Set( "LumaInvert", params[ PID_LUMA_INVERT ] > 0.5f ? 1 : 0 );
		rawShader.Set( "Crop", params[ PID_CROP_LEFT ] * 0.5f, params[ PID_CROP_RIGHT ] * 0.5f, params[ PID_CROP_BOTTOM ] * 0.5f,
					   params[ PID_CROP_TOP ] * 0.5f );
		quad.Draw();
		unbindTextures( 2 );
	}

	// 4. Clean: erode then dilate removes specks (an "open"); Shrink / Grow biases one side.
	{
		const float clean  = params[ PID_CLEAN ] * 6.0f * scale;
		const float choke  = ( params[ PID_CHOKE ] - 0.5f ) * 2.0f * 8.0f * scale;//+ shrinks, - grows
		const float erode  = clean + std::max( choke, 0.0f );
		const float dilate = clean + std::max( -choke, 0.0f );
		GLuint source      = rawMatte.GetTextureInfo().Handle;
		for( int stage = 0; stage < 2; ++stage )
		{
			ScopedFBOBinding fboBinding( morph[ stage ].GetGLID(), ScopedFBOBinding::RB_REVERT );
			ScopedShaderBinding shaderBinding( morphShader.GetGLID() );
			bindTexture( 0, source );
			morphShader.Set( "MatteTex", 0 );
			morphShader.Set( "MaxUV", 1.0f, 1.0f );
			morphShader.Set( "Texel", texelX, texelY );
			morphShader.Set( "Radius", stage == 0 ? erode : dilate );
			morphShader.Set( "Dilate", stage == 1 ? 1 : 0 );
			quad.Draw();
			unbindTextures( 1 );
			source = morph[ stage ].GetTextureInfo().Handle;
		}
	}

	// 5. Feather + temporal smoothing.
	{
		const int matteWrite = 1 - matteRead;
		ScopedFBOBinding fboBinding( matte[ matteWrite ].GetGLID(), ScopedFBOBinding::RB_REVERT );
		ScopedShaderBinding shaderBinding( featherShader.GetGLID() );
		bindTexture( 0, morph[ 1 ].GetTextureInfo().Handle );
		bindTexture( 1, matte[ matteRead ].GetTextureInfo().Handle );
		featherShader.Set( "MatteTex", 0 );
		featherShader.Set( "PreviousTex", 1 );
		featherShader.Set( "MaxUV", 1.0f, 1.0f );
		featherShader.Set( "Texel", texelX, texelY );
		featherShader.Set( "Feather", params[ PID_FEATHER ] * 12.0f * scale );
		featherShader.Set( "Temporal", params[ PID_SMOOTH ] * 0.85f );
		quad.Draw();
		unbindTextures( 2 );
		matteRead = matteWrite;
	}

	// 6. Output to the host.
	glViewport( currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height );
	{
		ScopedShaderBinding shaderBinding( outputShader.GetGLID() );
		bindTexture( 0, frame.GetTextureInfo().Handle );
		bindTexture( 1, matte[ matteRead ].GetTextureInfo().Handle );
		outputShader.Set( "FrameTex", 0 );
		outputShader.Set( "MatteTex", 1 );
		outputShader.Set( "MaxUV", 1.0f, 1.0f );
		outputShader.Set( "View", OptionIndex( params[ PID_VIEW ], 3 ) );
		outputShader.Set( "Background", OptionIndex( params[ PID_BACKGROUND ], 3 ) );
		outputShader.Set( "BackgroundColor", params[ PID_BG_R ], params[ PID_BG_G ], params[ PID_BG_B ] );
		outputShader.Set( "Mode", mode );
		outputShader.Set( "KeyColor", params[ PID_KEY_R ], params[ PID_KEY_G ], params[ PID_KEY_B ] );
		outputShader.Set( "Spill", params[ PID_SPILL ] );
		quad.Draw();
		unbindTextures( 2 );
	}

	UpdateStatus();
	return FF_SUCCESS;
}

FFResult ImagKey::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PID_COUNT )
		return FF_FAIL;
	if( index == PID_STATUS )
		return FF_SUCCESS;
	if( index == PID_LEARN && value > 0.5f )
	{
		learnFramesLeft = kLearnFrames;
		learnedFrames   = 0;
		learnedByUser   = true;
	}
	params[ index ] = value;
	return FF_SUCCESS;
}

float ImagKey::GetFloatParameter( unsigned int index )
{
	return index < PID_COUNT ? params[ index ] : 0.0f;
}

FFResult ImagKey::SetTextParameter( unsigned int index, const char* )
{
	return index == PID_STATUS ? FF_SUCCESS : FF_FAIL;//Status is read-only.
}

char* ImagKey::GetTextParameter( unsigned int index )
{
	return index == PID_STATUS ? const_cast< char* >( status.c_str() ) : nullptr;
}
