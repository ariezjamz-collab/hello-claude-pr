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

	// Redo the compile and link by hand to collect the driver's messages. Some drivers (NVIDIA) compile
	// each stage happily and only report problems when linking, so the link log matters as much.
	std::string message = std::string( plugin ) + ": " + pass + " shader failed. ";
	const char* sources[]    = { vertex, fragment };
	const GLenum stages[]    = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
	const char* stageNames[] = { "vertex", "fragment" };
	GLuint ids[ 2 ]          = { 0, 0 };
	bool compiled            = true;
	for( int i = 0; i < 2; ++i )
	{
		ids[ i ] = glCreateShader( stages[ i ] );
		glShaderSource( ids[ i ], 1, &sources[ i ], nullptr );
		glCompileShader( ids[ i ] );
		GLint ok = GL_FALSE, length = 0;
		glGetShaderiv( ids[ i ], GL_COMPILE_STATUS, &ok );
		glGetShaderiv( ids[ i ], GL_INFO_LOG_LENGTH, &length );
		if( ok != GL_TRUE )
		{
			compiled = false;
			if( length > 1 )
			{
				std::vector< GLchar > log( length + 1, 0 );
				glGetShaderInfoLog( ids[ i ], length, nullptr, log.data() );
				message += std::string( stageNames[ i ] ) + ": " + log.data();
			}
		}
	}
	if( compiled )
	{
		GLuint program = glCreateProgram();
		glAttachShader( program, ids[ 0 ] );
		glAttachShader( program, ids[ 1 ] );
		glLinkProgram( program );
		GLint ok = GL_FALSE, length = 0;
		glGetProgramiv( program, GL_LINK_STATUS, &ok );
		glGetProgramiv( program, GL_INFO_LOG_LENGTH, &length );
		if( length > 1 )
		{
			std::vector< GLchar > log( length + 1, 0 );
			glGetProgramInfoLog( program, length, nullptr, log.data() );
			message += std::string( "link: " ) + log.data();
		}
		else if( ok != GL_TRUE )
			message += "link failed without a message from the driver";
		glDeleteProgram( program );
	}
	for( GLuint id : ids )
		glDeleteShader( id );
	const char* version = (const char*)glGetString( GL_VERSION );
	const char* renderer = (const char*)glGetString( GL_RENDERER );
	message += std::string( " [GL " ) + ( version ? version : "?" ) + ", " + ( renderer ? renderer : "?" ) + "]";
	FFGLLog::LogToHost( message.c_str() );
	if( errorOut )
		*errorOut = message;
	return false;
}
