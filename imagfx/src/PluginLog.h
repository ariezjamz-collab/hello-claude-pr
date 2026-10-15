#pragma once
#include <string>
#include <vector>
#include <FFGLSDK.h>

// Compiles a shader program and, if the driver rejects it, writes the driver's own error message to the
// host's log (Resolume's log), so a failure on a user's GPU can be diagnosed instead of failing silently.
// If `errorOut` is given, the same message is stored there too.
inline bool CompileOrLog( ffglex::FFGLShader& shader, const char* vertex, const char* fragment, const char* plugin, const char* pass,
						  std::string* errorOut = nullptr )
{
	if( shader.Compile( vertex, fragment ) )
		return true;

	std::string message = std::string( plugin ) + ": " + pass + " shader failed. ";
	const char* sources[]   = { vertex, fragment };
	const GLenum stages[]   = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
	const char* stageNames[] = { "vertex", "fragment" };
	for( int i = 0; i < 2; ++i )
	{
		GLuint id = glCreateShader( stages[ i ] );
		glShaderSource( id, 1, &sources[ i ], nullptr );
		glCompileShader( id );
		GLint ok = GL_FALSE, length = 0;
		glGetShaderiv( id, GL_COMPILE_STATUS, &ok );
		glGetShaderiv( id, GL_INFO_LOG_LENGTH, &length );
		if( ok != GL_TRUE && length > 1 )
		{
			std::vector< GLchar > log( length + 1, 0 );
			glGetShaderInfoLog( id, length, nullptr, log.data() );
			message += std::string( stageNames[ i ] ) + ": " + log.data();
		}
		glDeleteShader( id );
	}
	const char* version = (const char*)glGetString( GL_VERSION );
	const char* renderer = (const char*)glGetString( GL_RENDERER );
	message += std::string( " [GL " ) + ( version ? version : "?" ) + ", " + ( renderer ? renderer : "?" ) + "]";
	FFGLLog::LogToHost( message.c_str() );
	if( errorOut )
		*errorOut = message;
	return false;
}
