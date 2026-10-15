#pragma once
// Stands in for the export header libfacedetection's own CMake build generates. The library is compiled
// straight into IMAG Face, so nothing is exported.
#define FACEDETECTION_EXPORT

// Pick the SIMD path from the flags the file was compiled with (-mavx2 / /arch:AVX2 on x64, NEON on
// Apple Silicon), so a universal macOS build gets the right one for each architecture.
#if !defined( _ENABLE_AVX2 ) && !defined( _ENABLE_NEON ) && !defined( _ENABLE_AVX512 )
#	if defined( __AVX2__ )
#		define _ENABLE_AVX2
#	elif defined( __aarch64__ ) || defined( _M_ARM64 )
#		define _ENABLE_NEON
#	endif
#endif
