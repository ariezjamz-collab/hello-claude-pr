#pragma once
#include <vector>
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>

// IMAG Flame: monochrome camera with flame trails, heat-haze distortion, spectral dispersion and
// GPU subject tracking (FFGL 2.x effect plugin).
//
// Per frame:
//   1. Copy      input                  -> frame history (current + previous, for motion)
//   2. Mask      frames                 -> subject mask (half resolution)
//   3. Moments   mask                   -> 256x256 moment targets, mipmapped down to 1x1
//   4. Tracker   moments + last state   -> tracker state (centre, spread, confidence), smoothed
//   5. Fire      heat + mask + tracker  -> new heat field (ping-pong, half resolution)
//   6. Composite frame + heat + mask    -> host framebuffer
// Tracking never reads back to the CPU; every pass samples the tracker state texture directly.
class ImagFlame : public CFFGLPlugin
{
public:
	ImagFlame();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

private:
	bool EnsureTargets( GLsizei width, GLsizei height );
	void ReleaseTargets();
	void ResetSimulation();
	void DrawCopy( const FFGLTextureStruct& input, ffglex::FFGLFBO& target );

	ffglex::FFGLShader copyShader;
	ffglex::FFGLShader maskShader;
	ffglex::FFGLShader momentsShader;
	ffglex::FFGLShader trackerShader;
	ffglex::FFGLShader fireShader;
	ffglex::FFGLShader compositeShader;
	ffglex::FFGLScreenQuad quad;

	ffglex::FFGLFBO frames[ 2 ];//Current / previous camera frame, swapped each frame.
	ffglex::FFGLFBO mask;
	ffglex::FFGLFBO heat[ 2 ];
	ffglex::FFGLFBO trackerState[ 2 ];

	// Moment targets: two mipmapped RGBA32F textures rendered together through one FBO.
	GLuint momentsFBO = 0;
	GLuint momentTextures[ 2 ] = { 0, 0 };

	int currentFrame = 0;
	int heatRead     = 0;
	int stateRead    = 0;
	bool primeHistory = true;

	GLsizei targetWidth  = 0;
	GLsizei targetHeight = 0;
	GLsizei simWidth     = 0;
	GLsizei simHeight    = 0;

	std::vector< float > params;
	bool resetRequested = false;
	double lastHostTime = -1.0;
	float simTime       = 0.0f;
};
