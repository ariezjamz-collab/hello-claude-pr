#pragma once
#include <algorithm>
#include <cmath>

// Colour controls are exposed as hue / saturation / brightness, which Resolume shows as a rainbow colour
// picker. Shaders still take RGB, so values are converted when uniforms are set. All values are 0..1.

inline void RgbToHsb( const float rgb[ 3 ], float hsb[ 3 ] )
{
	const float maxC  = std::max( rgb[ 0 ], std::max( rgb[ 1 ], rgb[ 2 ] ) );
	const float minC  = std::min( rgb[ 0 ], std::min( rgb[ 1 ], rgb[ 2 ] ) );
	const float delta = maxC - minC;
	float hue         = 0.0f;
	if( delta > 1e-6f )
	{
		if( maxC == rgb[ 0 ] )
			hue = std::fmod( ( rgb[ 1 ] - rgb[ 2 ] ) / delta + 6.0f, 6.0f );
		else if( maxC == rgb[ 1 ] )
			hue = ( rgb[ 2 ] - rgb[ 0 ] ) / delta + 2.0f;
		else
			hue = ( rgb[ 0 ] - rgb[ 1 ] ) / delta + 4.0f;
	}
	hsb[ 0 ] = hue / 6.0f;
	hsb[ 1 ] = maxC > 1e-6f ? delta / maxC : 0.0f;
	hsb[ 2 ] = maxC;
}

inline void HsbToRgb( const float hsb[ 3 ], float rgb[ 3 ] )
{
	const float h = ( hsb[ 0 ] - std::floor( hsb[ 0 ] ) ) * 6.0f;
	const float s = std::min( std::max( hsb[ 1 ], 0.0f ), 1.0f );
	const float v = std::min( std::max( hsb[ 2 ], 0.0f ), 1.0f );
	for( int i = 0; i < 3; ++i )
	{
		// Distance on the hue wheel from each channel's peak (red 0, green 2, blue 4).
		const float k = std::fmod( 5.0f - 2.0f * i + h, 6.0f );
		rgb[ i ]      = v - v * s * std::max( 0.0f, std::min( std::min( k, 4.0f - k ), 1.0f ) );
	}
}
