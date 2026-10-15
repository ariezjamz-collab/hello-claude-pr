#pragma once
#include <vector>
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>

// IMAG FX: a camera-look effect for Resolume (FFGL 2.x effect plugin).
//
// Per frame it runs three full-screen passes:
//   1. Copy    input            -> history ring buffer slot (last kMaxDelay frames)
//   2. Trail   ring + feedback  -> feedback buffer (ping-pong)
//   3. Finish  feedback + input -> host framebuffer
class ImagFX : public CFFGLPlugin
{
public:
	ImagFX();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetParameterDisplay( unsigned int index ) override;

	static const int kMaxDelay = 30;//Frames of history kept for the RGB delay.

private:
	bool EnsureTargets( GLsizei width, GLsizei height );
	void ReleaseTargets();
	void ClearFeedback();
	void UpdateClock();
	GLuint HistoryFrame( int framesAgo ) const;

	ffglex::FFGLShader copyShader;
	ffglex::FFGLShader trailShader;
	ffglex::FFGLShader finishShader;
	ffglex::FFGLScreenQuad quad;

	// History ring: plain textures sharing one FBO, so each slot costs only its colour storage.
	GLuint ringFBO = 0;
	std::vector< GLuint > ringTextures;
	int ringHead = 0;

	ffglex::FFGLFBO feedback[ 2 ];//Ping-pong; RGBA16F so long trails don't band.
	int feedbackRead = 0;

	GLsizei targetWidth  = 0;
	GLsizei targetHeight = 0;

	std::vector< float > params;
	bool resetRequested = false;

	double lastHostTime = -1.0;
	float freeRunPhase  = 0.0f;
	float warpTime      = 0.0f;
	float phase         = 0.0f;
	float lastPhase     = 0.0f;
	float seed          = 0.0f;
	float frameCounter  = 0.0f;

	char displayBuffer[ 32 ];
};
