#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <FFGLSDK.h>
#include <ffglex/FFGLScopedFBOBinding.h>

// IMAG Blob: live blob tracking. Finds the moving / keyed / bright regions of the camera and draws
// tracking squares on them, connecting lines between them and a number above each square, over a
// restyled feed (normal, monochrome, dual tone, ASCII or halftone).
//
// Per frame:
//   1. Copy     input        -> current frame (previous kept for motion detection)
//   2. Mask     frames       -> subject mask (half resolution)
//   3. Moments  mask         -> 256x256 moments, mipmapped; one small level is read back (a few KB)
//   4. CPU      grid         -> blobs (cells or connected regions), matched to last frame's blobs so each
//                               keeps its identity and number; lines; triangle list for squares/lines/text
//   5. Feed     frame        -> host (one small shader per feed mode, compiled on first use)
//   6. Overlay  triangles    -> host, blended
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

	struct Blob
	{
		float x = 0, y = 0;     // centre, 0..1 (y up)
		float w = 0, h = 0;     // extent, 0..1
		float mass = 0;
		int id = 0;
		uint32_t number = 0;
		float nextChange = 0.0f;// time when a random number next changes
		bool matched = false;
	};

private:
	enum class ShaderState
	{
		NotCompiled,
		Ready,
		Failed
	};

	bool EnsureTargets( GLsizei width, GLsizei height );
	void ReleaseTargets();
	void SetStatus( const std::string& text );
	ffglex::FFGLShader* FeedShader( int mode );
	void DetectBlobs( float dt );
	void BuildOverlay( float width, float height );
	uint32_t Random();

	ffglex::FFGLShader copyShader;
	ffglex::FFGLShader maskShader;
	ffglex::FFGLShader momentsShader;
	ffglex::FFGLShader overlayShader;
	std::vector< ffglex::FFGLShader > feedShaders;
	std::vector< ShaderState > feedStates;
	std::vector< std::string > feedErrors;
	ffglex::FFGLScreenQuad quad;

	ffglex::FFGLFBO frames[ 2 ];
	ffglex::FFGLFBO mask;
	GLuint momentsFBO          = 0;
	GLuint momentTextures[ 2 ] = { 0, 0 };
	GLuint atlasTexture        = 0;
	GLuint overlayVAO          = 0;
	GLuint overlayVBO          = 0;

	int currentFrame  = 0;
	bool primeHistory = true;
	GLsizei targetWidth  = 0;
	GLsizei targetHeight = 0;
	GLsizei maskWidth    = 0;
	GLsizei maskHeight   = 0;

	std::vector< float > gridData;
	std::vector< Blob > blobs;
	std::vector< std::pair< int, int > > links;
	std::vector< float > vertices;
	int nextBlobId     = 1;
	uint32_t rngState  = 0x9E3779B9u;
	double lastHostTime = -1.0;
	float time          = 0.0f;
	float lastBeat      = 0.0f;
	bool beatHit        = false;

	std::vector< float > params;
	std::string status;
};
