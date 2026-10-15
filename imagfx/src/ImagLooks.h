#pragma once
#include <vector>
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>

// IMAG Looks: an effects panel. One "Look" dropdown picks a camera look (night vision, fire map, gold
// edge, analog TV, glitter, comic, halftone, paper, animated cartoon, ...), and a shared set of controls
// shapes whichever look is active. Two passes: copy the input, then one stylise pass to the host.
class ImagLooks : public CFFGLPlugin
{
public:
	ImagLooks();

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;

private:
	ffglex::FFGLShader copyShader;
	ffglex::FFGLShader looksShader;
	ffglex::FFGLScreenQuad quad;
	ffglex::FFGLFBO frame;

	std::vector< float > params;
	double lastHostTime = -1.0;
	float time          = 0.0f;
	float freePhase     = 0.0f;
	float lastPhase     = 0.0f;
	float beatIndex     = 0.0f;
};
