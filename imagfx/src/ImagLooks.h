#pragma once
#include <string>
#include <vector>
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>

// IMAG Looks: an effects panel. One "Look" dropdown picks a camera look (night vision, fire map, gold
// edge, analog TV, glitter, comic, halftone, paper, animated cartoon, ...), and a shared set of controls
// shapes whichever look is active. Two passes: copy the input, then one stylise pass to the host.
//
// Each look is its own small shader program, compiled the first time that look is selected. If a
// driver rejects one, that look passes the image through unchanged and the Status field shows why.
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

	ffglex::FFGLShader copyShader;
	std::vector< ffglex::FFGLShader > lookShaders;
	std::vector< ShaderState > lookStates;
	std::vector< std::string > lookErrors;
	ffglex::FFGLScreenQuad quad;
	ffglex::FFGLFBO frame;

	std::vector< float > params;
	std::string status;
	double lastHostTime = -1.0;
	float time          = 0.0f;
	float freePhase     = 0.0f;
	float lastPhase     = 0.0f;
	float beatIndex     = 0.0f;
};
