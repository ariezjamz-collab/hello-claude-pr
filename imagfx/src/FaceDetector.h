#pragma once
// Face detection for IOKI Face, on a worker thread so the render thread never waits for it.
//
// The detector is libfacedetection (https://github.com/ShiqiYu/libfacedetection, BSD 3-Clause), a small
// CNN that finds faces of many sizes and returns a box, a confidence and five landmarks for each. Its
// sources are compiled with AVX2 on x64 (NEON on Apple Silicon); see CMakeLists.txt. Its header is not
// included here on purpose: it defines classes whose layout depends on those flags.
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#if defined( _MSC_VER ) && ( defined( _M_X64 ) || defined( _M_IX86 ) )
#	include <intrin.h>
#endif

int* facedetect_cnn( unsigned char* result_buffer, unsigned char* bgr_image_data, int width, int height, int step );

namespace imag
{
struct FaceResult
{
	float x, y, w, h;      // pixels in the detection image, top-left origin
	float score;           // 0..1
	float landmarks[ 10 ]; // five x,y points: eyes, nose tip, mouth corners
};

// True when this CPU can run the detector as it was compiled.
inline bool FaceDetectorSupported()
{
#if defined( __APPLE__ )
	return true;//macOS builds use NEON (Apple Silicon) or plain C++ (Intel), never AVX2
#elif defined( __x86_64__ ) || defined( _M_X64 ) || defined( __i386__ ) || defined( _M_IX86 )
#	if defined( _MSC_VER )
	int info[ 4 ];
	__cpuid( info, 0 );
	if( info[ 0 ] < 7 )
		return false;
	__cpuid( info, 1 );
	const bool osxsave = ( info[ 2 ] & ( 1 << 27 ) ) != 0, fma = ( info[ 2 ] & ( 1 << 12 ) ) != 0;
	if( !osxsave || !fma || ( _xgetbv( 0 ) & 6 ) != 6 )//OS saves the AVX registers
		return false;
	__cpuidex( info, 7, 0 );
	return ( info[ 1 ] & ( 1 << 5 ) ) != 0;
#	else
	__builtin_cpu_init();
	return __builtin_cpu_supports( "avx2" ) && __builtin_cpu_supports( "fma" );
#	endif
#else
	return true;//ARM: NEON is always there
#endif
}

class FaceDetector
{
public:
	~FaceDetector()
	{
		Stop();
	}

	void Start()
	{
		if( worker.joinable() )
			return;
		quit   = false;
		worker = std::thread( [ this ] { Run(); } );
	}

	void Stop()
	{
		{
			std::lock_guard< std::mutex > lock( mutex );
			quit = true;
		}
		wake.notify_all();
		if( worker.joinable() )
			worker.join();
		hasJob = hasResult = false;
	}

	// True while the worker is free for another image.
	bool Idle()
	{
		std::lock_guard< std::mutex > lock( mutex );
		return !hasJob && !busy;
	}

	// Hands over a BGR image (rows top to bottom). Ignored if the worker is still busy.
	void Submit( std::vector< unsigned char >& bgr, int width, int height )
	{
		{
			std::lock_guard< std::mutex > lock( mutex );
			if( hasJob || busy )
				return;
			job.swap( bgr );
			jobWidth  = width;
			jobHeight = height;
			hasJob    = true;
		}
		wake.notify_one();
	}

	// Takes the latest results, if there are new ones since the last call.
	bool Take( std::vector< FaceResult >& out, int& width, int& height, float& milliseconds )
	{
		std::lock_guard< std::mutex > lock( mutex );
		if( !hasResult )
			return false;
		out.swap( results );
		width        = resultWidth;
		height       = resultHeight;
		milliseconds = resultMilliseconds;
		hasResult    = false;
		return true;
	}

private:
	void Run()
	{
		std::vector< unsigned char > image;
		std::vector< unsigned char > buffer( 0x9000 );//FACEDETECTION_RESULT_BUFFER_SIZE
		std::vector< FaceResult > found;
		for( ;; )
		{
			int width, height;
			{
				std::unique_lock< std::mutex > lock( mutex );
				wake.wait( lock, [ this ] { return quit || hasJob; } );
				if( quit )
					return;
				image.swap( job );
				width  = jobWidth;
				height = jobHeight;
				hasJob = false;
				busy   = true;
			}

			const auto start = std::chrono::steady_clock::now();
			found.clear();
			{
				// The library builds its weights on first use without a lock, so instances take turns.
				std::lock_guard< std::mutex > lock( LibraryMutex() );
				const int* result = facedetect_cnn( buffer.data(), image.data(), width, height, width * 3 );
				const int count   = result ? std::min( *result, 1024 ) : 0;
				for( int i = 0; i < count; ++i )
				{
					const short* p = reinterpret_cast< const short* >( result + 1 ) + 16 * i;
					FaceResult f;
					f.score = p[ 0 ] / 100.0f;
					f.x     = p[ 1 ];
					f.y     = p[ 2 ];
					f.w     = p[ 3 ];
					f.h     = p[ 4 ];
					for( int k = 0; k < 10; ++k )
						f.landmarks[ k ] = p[ 5 + k ];
					found.push_back( f );
				}
			}
			const float ms = std::chrono::duration< float, std::milli >( std::chrono::steady_clock::now() - start ).count();

			std::lock_guard< std::mutex > lock( mutex );
			results.swap( found );
			resultWidth        = width;
			resultHeight       = height;
			resultMilliseconds = ms;
			hasResult          = true;
			busy               = false;
			image.swap( job );//hand the buffer back for reuse
		}
	}

	static std::mutex& LibraryMutex()
	{
		static std::mutex m;
		return m;
	}

	std::thread worker;
	std::mutex mutex;
	std::condition_variable wake;
	bool quit = false, hasJob = false, busy = false, hasResult = false;
	std::vector< unsigned char > job;
	int jobWidth = 0, jobHeight = 0;
	std::vector< FaceResult > results;
	int resultWidth = 0, resultHeight = 0;
	float resultMilliseconds = 0.0f;
};
}// namespace imag
