#pragma once
#include <FFGLSDK.h>

namespace imag
{
// Draws `input` unchanged into the bound framebuffer: used when an effect is switched off with its
// "Effect On" toggle. `copyShader` is built from kVertexShader + kCopyFragmentShader (ShaderCommon.h).
inline void PassThrough( ffglex::FFGLShader& copyShader, ffglex::FFGLScreenQuad& quad, const FFGLTextureStruct& input,
						 const FFGLViewportStruct& viewport )
{
	glViewport( viewport.x, viewport.y, viewport.width, viewport.height );
	const FFGLTexCoords maxCoords = GetMaxGLTexCoords( input );
	ffglex::ScopedShaderBinding binding( copyShader.GetGLID() );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, input.Handle );
	copyShader.Set( "InputTexture", 0 );
	copyShader.Set( "MaxUV", maxCoords.s, maxCoords.t );
	quad.Draw();
	glBindTexture( GL_TEXTURE_2D, 0 );
}
}// namespace imag
