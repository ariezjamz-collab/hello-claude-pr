#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>
#include "TrackOverlay.h"

// IOKI Blob: live blob tracking. Finds the moving / keyed / bright regions of the camera and draws
// tracking squares on them, connecting lines between them and a number above each square, over a
// restyled feed (normal, monochrome, dual tone, ASCII or halftone).
//
// Per frame:
//   1. Copy     input        -> current frame (previous kept for motion detection)
//   2. Mask     frames       -> subject mask (half resolution)
//   3. Moments  mask         -> 256x256 moments, mipmapped; one small level is read back (a few KB)
//   4. CPU      grid         -> blobs (cells or connected regions), handed to imag::Tracker so each keeps
//                               its identity and number
//   5. Feed + overlay        -> host (imag::OverlayRenderer, shared with IOKI Face)
class ImagBlob : public CFFGLPlugin
{
public:
	ImagBlob();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char* GetTextParameter( unsigned int index ) override;

private:
	bool EnsureTargets( GLsizei width, GLsizei height );
	void ReleaseTargets();
	void SetStatus( const std::string& text );
	std::vector< imag::Detection > DetectBlobs( float dt );

	ffglex::FFGLShader copyShader;
	ffglex::FFGLShader maskShader;
	ffglex::FFGLShader momentsShader;
	imag::OverlayRenderer overlay;
	imag::Tracker tracker;
	ffglex::FFGLScreenQuad quad;

	ffglex::FFGLFBO frames[ 2 ];
	ffglex::FFGLFBO mask;
	GLuint momentsFBO          = 0;
	GLuint momentTextures[ 2 ] = { 0, 0 };

	int currentFrame  = 0;
	bool primeHistory = true;
	GLsizei targetWidth  = 0;
	GLsizei targetHeight = 0;
	GLsizei maskWidth    = 0;
	GLsizei maskHeight   = 0;

	std::vector< float > gridData;
	std::vector< float > smoothedGrid;//detection grid averaged over time, so blobs don't flicker
	double lastHostTime = -1.0;
	float time          = 0.0f;
	float lastBeat      = 0.0f;
	bool beatHit        = false;

	std::vector< float > params;
	std::string status;
};
