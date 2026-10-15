#pragma once
#include <string>
#include <vector>
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>

// IOKI Looks: an effects panel. One "Look" dropdown picks a camera look (night vision, fire map, gold
// edge, analog TV, glitter, comic, halftone, paper, animated cartoon, ...). On top of any look sits an
// overlay stack with its own controls: subject tracking squares, coloured subject echoes (delay shift),
// ASCII, and levels for distortion, dispersion, flames, depth and dual tone.
//
// Per frame:
//   1. Copy      input                 -> current frame (previous kept for motion detection)
//   2. Mask      frames                -> subject mask (half resolution)
//   3. Moments   mask                  -> 256x256 moments, mipmapped (tracking squares, subject centre)
//   4. Echo      frame + mask          -> echo history ring (only while echoes are on)
//   5. Fire      heat + mask           -> heat field (only while flames are on)
//   6. Look      frame                 -> look target (one small shader per look, compiled on first use)
//   7. Composite look + everything     -> host
// If a look's shader is rejected by the driver, that look passes the camera through and the Status field
// shows the driver's error.
class ImagLooks : public CFFGLPlugin
{
public:
	ImagLooks();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;

private:
	enum class ShaderState
	{
		NotCompiled,
		Ready,
		Failed
	};

	ffglex::FFGLShader* LookShader( int look );
	void SetStatus( const std::string& text );
	bool EnsureTargets( GLsizei width, GLsizei height );
	bool EnsureEchoRing();
	void ReleaseTargets();
	void ClearTarget( GLuint fbo, float r, float g, float b, float a );
	void DrawCopy( GLuint texture, float maxU, float maxV );

	ffglex::FFGLShader copyShader;
	ffglex::FFGLShader maskShader;
	ffglex::FFGLShader momentsShader;
	ffglex::FFGLShader echoShader;
	ffglex::FFGLShader fireShader;
	ffglex::FFGLShader compositeShader;
	std::vector< ffglex::FFGLShader > lookShaders;
	std::vector< ShaderState > lookStates;
	std::vector< std::string > lookErrors;
	ffglex::FFGLScreenQuad quad;

	ffglex::FFGLFBO frames[ 2 ];
	ffglex::FFGLFBO lookTarget;
	ffglex::FFGLFBO mask;
	ffglex::FFGLFBO heat[ 2 ];
	ffglex::FFGLFBO trackerState;//Fixed centre; the fire shader expects one.
	GLuint momentsFBO        = 0;
	GLuint momentTextures[ 2 ] = { 0, 0 };
	GLuint echoFBO           = 0;
	std::vector< GLuint > echoTextures;

	int currentFrame  = 0;
	int heatRead      = 0;
	int echoHead      = 0;
	bool primeHistory = true;
	bool heatActive   = false;
	GLsizei targetWidth  = 0;
	GLsizei targetHeight = 0;
	GLsizei maskWidth    = 0;
	GLsizei maskHeight   = 0;

	std::vector< float > params;
	std::string status;
	double lastHostTime = -1.0;
	float time          = 0.0f;
	float freePhase     = 0.0f;
	float lastPhase     = 0.0f;
	float beatIndex     = 0.0f;
};
