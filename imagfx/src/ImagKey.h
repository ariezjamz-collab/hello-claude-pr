#pragma once
#include <string>
#include <vector>
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>

// IMAG Key: keys the performer out of a camera so the other IMAG effects can use "Keyed alpha".
// Put it first on the camera clip; its output is the subject with a transparent background.
//
// Keyers: Difference (learned empty-stage plate; best for a fixed camera on any background), Chroma
// (green/blue screen with spill suppression) and Luma. Per frame:
//   1. Copy      input                      -> frame
//   2. Plate     frame + plate + last matte -> plate (ping-pong): learning average, then slow update
//   3. Matte     frame + plate              -> raw matte
//   4. Clean     raw matte                  -> erode -> dilate (despeckle + choke)
//   5. Feather   matte + previous matte     -> final matte (ping-pong, temporal smoothing)
//   6. Output    frame + matte              -> host (subject premultiplied, or matte / overlay views)
class ImagKey : public CFFGLPlugin
{
public:
	ImagKey();

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
	void UpdateStatus();

	ffglex::FFGLShader copyShader;
	ffglex::FFGLShader plateShader;
	ffglex::FFGLShader rawShader;
	ffglex::FFGLShader morphShader;
	ffglex::FFGLShader featherShader;
	ffglex::FFGLShader outputShader;
	ffglex::FFGLScreenQuad quad;

	ffglex::FFGLFBO frame;
	ffglex::FFGLFBO plate[ 2 ];
	ffglex::FFGLFBO rawMatte;
	ffglex::FFGLFBO morph[ 2 ];
	ffglex::FFGLFBO matte[ 2 ];
	int plateRead = 0;
	int matteRead = 0;
	GLsizei targetWidth  = 0;
	GLsizei targetHeight = 0;

	static const int kLearnFrames = 30;
	int learnFramesLeft = kLearnFrames;
	int learnedFrames   = 0;
	bool learnedByUser  = false;

	std::vector< float > params;
	std::string status;
};
