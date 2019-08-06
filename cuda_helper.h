#pragma once

#include <cstdio>
#include <cstdint>
#include <cstdlib>



// Platform defines
#if defined(_WIN32) || defined(_WIN64)
#   define CUDAHPlatform_Windows
#   if defined(__MINGW32__) // Defined for both 32 bit/64 bit MinGW
#       define CUDAHPlatform_Windows_MinGW
#   elif defined(_MSC_VER)
#       define CUDAHPlatform_Windows_MSVC
#   endif
#elif defined(__linux__)
#   define CUDAHPlatform_Linux
#elif defined(__APPLE__)
#   define CUDAHPlatform_macOS
#elif defined(__OpenBSD__)
#   define CUDAHPlatform_OpenBSD
#endif

#ifdef _DEBUG
#   define CUDAH_ENABLE_ASSERT
#endif

#ifdef CUDAH_ENABLE_ASSERT
#   define CUDAHAssert(expr, fmt, ...) \
    if (!(expr)) { \
        CUDAHelper::devPrintf("%s @%s: %u:\n", #expr, __FILE__, __LINE__); \
        CUDAHelper::devPrintf(fmt"\n", ##__VA_ARGS__); \
        abort(); \
    } 0
#else
#   define CUDAHAssert(expr, fmt, ...)
#endif

#define CUDAHAssert_ShouldNotBeCalled() CUDAHAssert(false, "Should not be called!")
#define CUDAHAssert_NotImplemented() CUDAHAssert(false, "Not implemented yet!")



#include <sstream>



#include <GL/gl3w.h>

#include <cuda.h>
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>



namespace CUDAHelper {
    void devPrintf(const char* fmt, ...);



    enum class BufferType {
        Device = 0,     // not preferred, typically slower than ZERO_COPY
        GL_Interop = 1, // single device only, preferred for single device
        ZeroCopy = 2,   // general case, preferred for multi-gpu if not fully nvlink connected
        P2P = 3         // fully connected only, preferred for fully nvlink connected
    };

    class Buffer {
        BufferType m_type;

        int32_t m_width;
        int32_t m_height;
        int32_t m_dimension;
        int32_t m_stride;

        void* m_hostPointer;
        void* m_devicePointer;

        GLuint m_GLBufferID;
        cudaGraphicsResource* m_cudaGfxResource;

        CUstream m_stream;
        int32_t m_deviceIndex;

        void makeCurrent();

    public:
        Buffer();
        ~Buffer();

        void initialize(BufferType type, int32_t width, int32_t height, int32_t stride, uint32_t glBufferID);
    };
}
