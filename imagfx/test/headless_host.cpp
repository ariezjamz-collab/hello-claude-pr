// Minimal FFGL host for testing the plugin without Resolume: GLX on Linux (e.g. under Xvfb), WGL on
// Windows (also runs under Wine, which is how the Windows .dll is tested from Linux).
//
// It loads the plugin through plugMain exactly like a host does, feeds it a synthetic animated "camera"
// frame, drives SetTime / SetBeatInfo at 120 BPM, checks the plugin leaves GL state as FFGL requires,
// and writes selected output frames as PPM images.
//
//   headless_host <plugin.so|plugin.dll> <out_dir> <preset> [frames]
#include <GL/glew.h>
#ifdef _WIN32
#	include <windows.h>
#	include <GL/wglew.h>
#else
#	include <GL/glx.h>
#	include <dlfcn.h>
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <ffgl/FFGL.h>

static const int W = 640, H = 360;

#ifdef _WIN32
static bool CreateContext()
{
	WNDCLASSA wc     = {};
	wc.lpfnWndProc   = DefWindowProcA;
	wc.hInstance     = GetModuleHandleA( nullptr );
	wc.lpszClassName = "ffgl_test_host";
	RegisterClassA( &wc );
	HWND window = CreateWindowA( wc.lpszClassName, "", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr );
	HDC dc      = GetDC( window );

	PIXELFORMATDESCRIPTOR pfd = { sizeof( pfd ), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
	SetPixelFormat( dc, ChoosePixelFormat( dc, &pfd ), &pfd );
	HGLRC legacy = wglCreateContext( dc );//Needed first, to look up wglCreateContextAttribsARB.
	wglMakeCurrent( dc, legacy );
	auto createContext = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress( "wglCreateContextAttribsARB" );
	if( !createContext )
		return false;
	int ctxAttribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 4, WGL_CONTEXT_MINOR_VERSION_ARB, 1,
						 WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
	HGLRC context = createContext( dc, nullptr, ctxAttribs );
	wglMakeCurrent( nullptr, nullptr );
	wglDeleteContext( legacy );
	if( !context || !wglMakeCurrent( dc, context ) )
		return false;
	glewExperimental = GL_TRUE;
	if( glewInit() != GLEW_OK )
		return false;
	while( glGetError() != GL_NO_ERROR ) {}
	return true;
}

static FF_Main_FuncPtr LoadPlugin( const char* path )
{
	HMODULE lib = LoadLibraryA( path );
	if( !lib )
	{
		std::fprintf( stderr, "LoadLibrary failed: %lu\n", GetLastError() );
		return nullptr;
	}
	return (FF_Main_FuncPtr)GetProcAddress( lib, "plugMain" );
}
#else
typedef GLXContext ( *CreateContextAttribs )( Display*, GLXFBConfig, GLXContext, Bool, const int* );

static bool CreateContext()
{
	Display* display = XOpenDisplay( nullptr );
	if( !display )
		return false;
	int fbAttribs[] = { GLX_DRAWABLE_TYPE, GLX_PBUFFER_BIT, GLX_RENDER_TYPE, GLX_RGBA_BIT, None };
	int count       = 0;
	GLXFBConfig* configs = glXChooseFBConfig( display, DefaultScreen( display ), fbAttribs, &count );
	if( !configs || count == 0 )
		return false;
	auto createContext = (CreateContextAttribs)glXGetProcAddressARB( (const GLubyte*)"glXCreateContextAttribsARB" );
	int ctxAttribs[]   = { GLX_CONTEXT_MAJOR_VERSION_ARB, 4, GLX_CONTEXT_MINOR_VERSION_ARB, 1,
                         GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB, None };
	GLXContext context = createContext( display, configs[ 0 ], nullptr, True, ctxAttribs );
	int pbAttribs[]    = { GLX_PBUFFER_WIDTH, 16, GLX_PBUFFER_HEIGHT, 16, None };
	GLXPbuffer pbuffer = glXCreatePbuffer( display, configs[ 0 ], pbAttribs );
	if( !context || !glXMakeContextCurrent( display, pbuffer, pbuffer, context ) )
		return false;
	glewExperimental = GL_TRUE;
	if( glewInit() != GLEW_OK )
		return false;
	while( glGetError() != GL_NO_ERROR ) {}//glewInit may leave a harmless GL_INVALID_ENUM in core profiles.
	return true;
}

static FF_Main_FuncPtr LoadPlugin( const char* path )
{
	void* lib = dlopen( path, RTLD_NOW );
	if( !lib )
	{
		std::fprintf( stderr, "dlopen: %s\n", dlerror() );
		return nullptr;
	}
	return (FF_Main_FuncPtr)dlsym( lib, "plugMain" );
}
#endif

// A stand-in for a stage camera: dark gradient background, a bright "performer" silhouette that sways,
// and a moving light beam, so time-based effects have something to act on.
// With `keyed`, the background is transparent (premultiplied), as after background removal + Auto Mask.
static void DrawCameraFrame( std::vector< unsigned char >& rgba, int frame, bool keyed )
{
	float t  = frame / 60.0f;
	float cx = 0.5f + 0.18f * std::sin( t * 2.1f );
	for( int y = 0; y < H; ++y )
	{
		for( int x = 0; x < W; ++x )
		{
			float u = ( x + 0.5f ) / W, v = ( y + 0.5f ) / H;
			float r = 0.05f + 0.10f * v, g = 0.04f + 0.06f * v, b = 0.10f + 0.20f * v;

			float dx    = ( u - cx ) * W / H;
			float head  = std::hypot( dx, v - 0.70f ) < 0.075f ? 1.0f : 0.0f;
			float torso = std::fabs( dx ) < 0.11f - 0.05f * ( 0.55f - v ) && v > 0.12f && v < 0.60f ? 1.0f : 0.0f;
			float body  = std::fmax( head, torso );

			// Rounded shading plus a dark print on the shirt, so tone-based looks have detail to work with.
			float shade = 0.45f + 0.55f * std::fmax( 0.0f, 1.0f - std::fabs( dx ) / 0.13f );
			if( head > 0.0f )
				shade = 0.5f + 0.5f * std::fmax( 0.0f, 1.0f - std::hypot( dx + 0.02f, v - 0.72f ) / 0.08f );
			float print = torso > 0.0f && std::hypot( dx, v - 0.38f ) < 0.06f && std::hypot( dx, v - 0.38f ) > 0.035f ? 0.35f : 1.0f;
			float fr = 0.85f * shade * print, fg = 0.65f * shade * print, fb = 0.55f * shade * print;
			r += body * ( fr - r );
			g += body * ( fg - g );
			b += body * ( fb - b );

			float beam = std::exp( -std::pow( ( u - 0.5f - 0.4f * std::sin( t * 0.7f ) ) * 9.0f + ( v - 1.0f ) * 2.0f, 2.0f ) );
			r += 0.25f * beam;
			g += 0.30f * beam;
			b += 0.45f * beam;

			float a = 1.0f;
			if( keyed )
			{
				r = body * fr;
				g = body * fg;
				b = body * fb;
				a = body;
			}

			unsigned char* p = &rgba[ ( y * W + x ) * 4 ];
			p[ 0 ]           = (unsigned char)( std::fmin( r, 1.0f ) * 255.0f );
			p[ 1 ]           = (unsigned char)( std::fmin( g, 1.0f ) * 255.0f );
			p[ 2 ]           = (unsigned char)( std::fmin( b, 1.0f ) * 255.0f );
			p[ 3 ]           = (unsigned char)( a * 255.0f );
		}
	}
}

static void WritePPM( const std::string& path, const std::vector< unsigned char >& rgba )
{
	FILE* f = std::fopen( path.c_str(), "wb" );
	if( !f )
		return;
	std::fprintf( f, "P6\n%d %d\n255\n", W, H );
	for( int y = H - 1; y >= 0; --y )//GL rows are bottom-up.
		for( int x = 0; x < W; ++x )
			std::fwrite( &rgba[ ( y * W + x ) * 4 ], 1, 3, f );
	std::fclose( f );
}

static std::map< std::string, std::map< std::string, float > > Presets()
{
	return {
		{ "dry", { { "Mix", 0.0f } } },
		{ "red_delay", { { "Red Delay", 0.4f }, { "Blue Delay", 0.0f }, { "Chroma Split", 0.4f } } },
		{ "trails", { { "Red Delay", 0.0f }, { "Chroma Split", 0.0f }, { "Trails", 0.85f }, { "Trail Zoom", 0.62f }, { "Trail Hue", 0.15f } } },
		{ "sheen_static", { { "Red Delay", 0.0f }, { "Sheen", 0.9f }, { "Sheen Width", 0.25f }, { "Static", 0.35f }, { "Scanlines", 0.5f } } },
		{ "glitch_warp", { { "Glitch", 1.0f }, { "Warp", 0.6f }, { "Warp Scale", 0.4f }, { "Beat Rate", 2.0f } } },
		// IMAG Flame. "_keyed" is not a plugin parameter: it switches the test camera to a transparent background.
		{ "flame_motion", { { "Subject Source", 0.0f }, { "Flame Amount", 0.8f }, { "Burn Edges", 0.3f } } },
		{ "flame_keyed", { { "_keyed", 1.0f }, { "Subject Source", 1.0f }, { "Burn Edges", 0.7f }, { "Tracker HUD", 1.0f } } },
		{ "flame_disperse", { { "Subject Source", 0.0f }, { "Dispersion", 0.9f }, { "Disperse Mode", 1.0f }, { "Heat Haze", 0.9f },
							  { "Distortion", 0.3f }, { "Tracker HUD", 4.0f }, { "Mono Tint", 1.0f } } },
		{ "flame_squares", { { "_keyed", 1.0f }, { "Subject Source", 1.0f }, { "Tracker HUD", 2.0f }, { "Square Count", 2.0f },
							 { "Flame Amount", 0.4f } } },
		{ "flame_blue_track", { { "Subject Source", 0.0f }, { "Flame Color", 1.0f }, { "Track Focus", 0.8f }, { "Tracker HUD", 1.0f },
								{ "Beat Pulse", 0.7f }, { "Wind", 0.75f } } },
		// IMAG Looks: one preset per look, by index in the Look dropdown.
		{ "look_0", { { "Look", 0.0f } } }, { "look_1", { { "Look", 1.0f } } }, { "look_2", { { "Look", 2.0f } } },
		{ "look_3", { { "Look", 3.0f } } }, { "look_4", { { "Look", 4.0f } } }, { "look_5", { { "Look", 5.0f } } },
		{ "look_6", { { "Look", 6.0f } } }, { "look_7", { { "Look", 7.0f } } }, { "look_8", { { "Look", 8.0f } } },
		{ "look_9", { { "Look", 9.0f } } }, { "look_10", { { "Look", 10.0f } } }, { "look_11", { { "Look", 11.0f } } },
		{ "look_12", { { "Look", 12.0f } } },
		{ "everything", { { "Red Delay", 0.3f }, { "Blue Delay", 0.1f }, { "Chroma Split", 0.3f }, { "Trails", 0.7f }, { "Trail Zoom", 0.58f },
						  { "Trail Hue", 0.1f }, { "Glitch", 0.6f }, { "Sheen", 0.6f }, { "Static", 0.15f }, { "Scanlines", 0.3f } } },
	};
}

int main( int argc, char** argv )
{
	if( argc < 4 )
	{
		std::fprintf( stderr, "usage: %s <plugin.so> <out_dir> <preset> [frames]\n", argv[ 0 ] );
		return 2;
	}
	std::string outDir = argv[ 2 ], preset = argv[ 3 ];
	int frames         = argc > 4 ? std::atoi( argv[ 4 ] ) : 120;
	auto presets       = Presets();
	if( !presets.count( preset ) )
	{
		std::fprintf( stderr, "unknown preset %s\n", preset.c_str() );
		return 2;
	}
	if( !CreateContext() )
	{
		std::fprintf( stderr, "could not create a GL 4.1 core context\n" );
		return 1;
	}
	std::printf( "GL: %s / %s\n", glGetString( GL_RENDERER ), glGetString( GL_VERSION ) );

	FF_Main_FuncPtr plugMain = LoadPlugin( argv[ 1 ] );
	if( !plugMain )
		return 1;
	FFMixed in;

	in.UIntValue = 0;
	if( plugMain( FF_INITIALISE_V2, in, nullptr ).UIntValue != FF_SUCCESS )
		return 1;

	FFGLViewportStruct viewport = { 0, 0, W, H };
	in.PointerValue             = &viewport;
	FFInstanceID instance       = plugMain( FF_INSTANTIATE_GL, in, nullptr ).PointerValue;
	if( !instance )
	{
		std::fprintf( stderr, "instantiate failed (shader compile?)\n" );
		return 1;
	}

	std::map< std::string, FFUInt32 > paramIndex;
	in.UIntValue          = 0;
	FFUInt32 numParams    = plugMain( FF_GET_NUM_PARAMETERS, in, nullptr ).UIntValue;
	for( FFUInt32 i = 0; i < numParams; ++i )
	{
		in.UIntValue = i;
		paramIndex[ (const char*)plugMain( FF_GET_PARAMETER_NAME, in, nullptr ).PointerValue ] = i;
	}
	std::printf( "%u parameters\n", numParams );
	bool keyed = false;
	for( auto& kv : presets[ preset ] )
	{
		if( kv.first == "_keyed" )
		{
			keyed = kv.second > 0.5f;
			continue;
		}
		if( !paramIndex.count( kv.first ) )
		{
			std::fprintf( stderr, "plugin has no parameter '%s'\n", kv.first.c_str() );
			return 1;
		}
		SetParameterStruct set;
		set.ParameterNumber = paramIndex[ kv.first ];
		std::memcpy( &set.NewParameterValue.UIntValue, &kv.second, sizeof( float ) );
		in.PointerValue = &set;
		plugMain( FF_SET_PARAMETER, in, instance );
	}

	GLuint inputTex, outputTex, hostFBO;
	glGenTextures( 1, &inputTex );
	glBindTexture( GL_TEXTURE_2D, inputTex );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glGenTextures( 1, &outputTex );
	glBindTexture( GL_TEXTURE_2D, outputTex );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glGenFramebuffers( 1, &hostFBO );
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTex, 0 );

	FFGLTextureStruct inputStruct = { W, H, W, H, inputTex };
	FFGLTextureStruct* inputs[]   = { &inputStruct };
	ProcessOpenGLStruct process   = { 1, inputs, hostFBO };
	std::vector< unsigned char > pixels( W * H * 4 );
	const float bpm               = 120.0f;
	int failures                  = 0;

	for( int frame = 0; frame < frames; ++frame )
	{
		DrawCameraFrame( pixels, frame, keyed );
		glBindTexture( GL_TEXTURE_2D, inputTex );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );

		double seconds        = frame / 60.0;
		in.PointerValue       = &seconds;
		plugMain( FF_SET_TIME, in, instance );
		SetBeatinfoStruct beat = { bpm, (float)std::fmod( seconds * bpm / 60.0 / 4.0, 1.0 ) };
		in.PointerValue        = &beat;
		plugMain( FF_SET_BEATINFO, in, instance );

		glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
		glViewport( 0, 0, W, H );
		glClearColor( 0, 0, 0, 0 );
		glClear( GL_COLOR_BUFFER_BIT );
		in.PointerValue = &process;
		if( plugMain( FF_PROCESS_OPENGL, in, instance ).UIntValue != FF_SUCCESS )
		{
			std::fprintf( stderr, "frame %d: ProcessOpenGL failed\n", frame );
			return 1;
		}

		// FFGL contract: the plugin must hand the context back in its default state.
		GLint program, activeUnit, drawFBO, boundTex;
		glGetIntegerv( GL_CURRENT_PROGRAM, &program );
		glGetIntegerv( GL_ACTIVE_TEXTURE, &activeUnit );
		glGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO );
		glGetIntegerv( GL_TEXTURE_BINDING_2D, &boundTex );
		GLenum err = glGetError();
		if( err != GL_NO_ERROR || program != 0 || activeUnit != GL_TEXTURE0 || drawFBO != (GLint)hostFBO || boundTex != 0 )
		{
			std::fprintf( stderr, "frame %d: GL error 0x%x program %d unit 0x%x fbo %d tex %d\n", frame, err, program, activeUnit, drawFBO, boundTex );
			++failures;
		}

		if( frame == frames - 1 || frame == frames / 2 || frame == frames / 2 + 7 )
		{
			glReadPixels( 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data() );
			WritePPM( outDir + "/" + preset + "_" + std::to_string( frame ) + ".ppm", pixels );
		}
	}

	if( paramIndex.count( "Status" ) )
	{
		in.UIntValue       = paramIndex[ "Status" ];
		const char* status = (const char*)plugMain( FF_GET_PARAMETER, in, instance ).PointerValue;
		std::printf( "Status: %s\n", status ? status : "(none)" );
	}

	plugMain( FF_DEINSTANTIATE_GL, in, instance );
	in.UIntValue = 0;
	plugMain( FF_DEINITIALISE, in, nullptr );
	std::printf( "%s: %d frames, %d state/GL failures\n", preset.c_str(), frames, failures );
	return failures == 0 ? 0 : 1;
}
