#pragma once
#include <string>
#include <vector>
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>
#include "FaceDetector.h"
#include "TrackOverlay.h"

// IMAG Face: multi-face tracking. Finds every face in the camera and draws tracking boxes on them, lines
// between them, a number on each and optional landmark dots, over a restyled feed (normal, monochrome,
// dual tone, ASCII or halftone). The tracking and graphics are IMAG Blob's (TrackOverlay.h).
//
// Per frame:
//   1. Copy      input           -> frame (full resolution)
//   2. Detect    frame           -> small image, read back as BGR and handed to the detector thread
//                                   (only when it is free, so rendering never waits for it)
//   3. CPU       latest faces    -> imag::Tracker (boxes glide between detections and keep their labels)
//   4. Feed + overlay            -> host
class ImagFace : public CFFGLPlugin
{
public:
	ImagFace();
	~ImagFace();

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
	void SubmitFrame( GLuint frameTexture );
	void CollectFaces();

	ffglex::FFGLShader copyShader;
	ffglex::FFGLScreenQuad quad;
	imag::OverlayRenderer overlay;
	imag::Tracker tracker;
	imag::FaceDetector detector;
	bool detectorSupported = false;

	ffglex::FFGLFBO frame;
	ffglex::FFGLFBO detect;
	GLsizei targetWidth  = 0;
	GLsizei targetHeight = 0;
	GLsizei detectWidth  = 0;
	GLsizei detectHeight = 0;

	std::vector< unsigned char > rgba;
	std::vector< unsigned char > bgr;
	std::vector< imag::FaceResult > faces;
	int facesSeen       = 0;
	float detectMs      = 0.0f;
	float lastDetection = -1.0f;
	double lastHostTime = -1.0;
	float time          = 0.0f;
	float lastBeat      = 0.0f;
	bool beatHit        = false;

	std::vector< float > params;
	std::string status;
};
