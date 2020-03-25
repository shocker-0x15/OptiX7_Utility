#pragma once

/*

現状ではあらゆるAPIに破壊的変更が入る可能性が非常に高い。

TODO:
- SBTのダブルバッファリングの運用方法を考える。
  => これもレイアウトとフィル処理だけUtilが行ってバッファーはユーザー管理にする？
- Assertとexceptionの整理。
- IASのインスタンスを保持するバッファーはユーザー管理にすべき？
- GAS/IASに関してユーザーが気にするところはAS云々ではなくグループ化なので
  名前を変えるべき？GeometryGroup/InstanceGroup
- 途中で各オブジェクトのパラメターを変更した際の処理。

*/

// Platform defines
#if defined(_WIN32) || defined(_WIN64)
#    define OPTIX_Platform_Windows
#    if defined(_MSC_VER)
#        define OPTIX_Platform_Windows_MSVC
#    endif
#elif defined(__APPLE__)
#    define OPTIX_Platform_macOS
#endif

#include <optix.h>
#include <cstdint>

#if !defined(__CUDA_ARCH__)
#include <optix_stubs.h>
#include "cuda_helper.h"
#endif

#if defined(__CUDA_ARCH__)
#   define RT_FUNCTION __forceinline__ __device__
#   define RT_PROGRAM extern "C" __global__
#else
#   define RT_FUNCTION
#endif



namespace optix {

#ifdef _DEBUG
#   define OPTIX_ENABLE_ASSERT
#endif

#if defined(OPTIX_Platform_Windows_MSVC)
    void devPrintf(const char* fmt, ...);
#else
#   define devPrintf(fmt, ...) printf(fmt, ##__VA_ARGS__);
#endif

#if 1
#   define optixPrintf(fmt, ...) do { devPrintf(fmt, ##__VA_ARGS__); printf(fmt, ##__VA_ARGS__); } while (0)
#else
#   define optixPrintf(fmt, ...) printf(fmt, ##__VA_ARGS__)
#endif

#if defined(OPTIX_ENABLE_ASSERT)
#   if defined(__CUDA_ARCH__)
#   define optixAssert(expr, fmt, ...) if (!(expr)) { devPrintf("%s @%s: %u:\n", #expr, __FILE__, __LINE__); devPrintf(fmt"\n", ##__VA_ARGS__); } 0
#   else
#   define optixAssert(expr, fmt, ...) if (!(expr)) { devPrintf("%s @%s: %u:\n", #expr, __FILE__, __LINE__); devPrintf(fmt"\n", ##__VA_ARGS__); abort(); } 0
#   endif
#else
#   define optixAssert(expr, fmt, ...)
#endif

#define optixAssert_ShouldNotBeCalled() optixAssert(false, "Should not be called!")
#define optixAssert_NotImplemented() optixAssert(false, "Not implemented yet!")



    struct HitGroupSBTRecordData {
        uint32_t materialData;
        uint32_t geomInstData;
    };

#if defined(__CUDA_ARCH__)
    RT_FUNCTION HitGroupSBTRecordData getHitGroupSBTRecordData() {
        return *reinterpret_cast<HitGroupSBTRecordData*>(optixGetSbtDataPointer());
    }
#endif



#if !defined(__CUDA_ARCH__)
    using namespace CUDAHelper;

    /*

    Context --+-- Pipeline --+-- Module
              |              |
              |              +-- ProgramGroup
              |
              +-- Material
              |
              |
              |
              +-- Scene    --+-- IAS
                             |
                             +-- Instance
                             |
                             +-- GAS
                             |
                             +-- GeomInst

    JP: 
    EN: 

    */

    class Context;
    class Material;
    class Scene;
    class GeometryInstance;
    class GeometryAccelerationStructure;
    class Instance;
    class InstanceAccelerationStructure;
    class Pipeline;
    class Module;
    class ProgramGroup;

#define OPTIX_PIMPL() \
public: \
    class Priv; \
private: \
    Priv* m = nullptr



    class Context {
        OPTIX_PIMPL();

    public:
        static Context create(CUcontext cudaContext);
        void destroy();

        Material createMaterial() const;
        Scene createScene() const;

        Pipeline createPipeline() const;
    };



    class Material {
        OPTIX_PIMPL();

    public:
        void destroy();

        void setHitGroup(uint32_t rayType, const ProgramGroup &hitGroup);
        void setUserData(uint32_t data) const;
    };



    class Scene {
        OPTIX_PIMPL();

    public:
        void destroy();

        GeometryInstance createGeometryInstance() const;
        GeometryAccelerationStructure createGeometryAccelerationStructure() const;
        Instance createInstance() const;
        InstanceAccelerationStructure createInstanceAccelerationStructure() const;
    };



    class GeometryInstance {
        OPTIX_PIMPL();

    public:
        void destroy();

        void setVertexBuffer(Buffer* vertexBuffer) const;
        void setTriangleBuffer(Buffer* triangleBuffer) const;
        void setNumMaterials(uint32_t numMaterials, TypedBuffer<uint32_t>* matIdxOffsetBuffer) const;

        void setUserData(uint32_t data) const;

        void setGeometryFlags(uint32_t matIdx, OptixGeometryFlags flags) const;
        void setMaterial(uint32_t matSetIdx, uint32_t matIdx, const Material &mat) const;
    };



    class GeometryAccelerationStructure {
        OPTIX_PIMPL();

    public:
        void destroy();

        void setConfiguration(bool preferFastTrace, bool allowUpdate, bool allowCompaction) const;
        void setNumMaterialSets(uint32_t numMatSets) const;
        void setNumRayTypes(uint32_t matSetIdx, uint32_t numRayTypes) const;

        void addChild(const GeometryInstance &geomInst) const;

        void prepareForBuild(OptixAccelBufferSizes* memoryRequirement) const;
        OptixTraversableHandle rebuild(CUstream stream, const Buffer &accelBuffer, const Buffer &scratchBuffer) const;
        void prepareForCompact(CUstream rebuildOrUpdateStream, size_t* compactedAccelBufferSize) const;
        OptixTraversableHandle compact(CUstream stream, const Buffer &compactedAccelBuffer) const;
        void removeUncompacted(CUstream compactionStream) const;
        OptixTraversableHandle update(CUstream stream, const Buffer &scratchBuffer) const;

        bool isReady() const;

        void markDirty() const;
    };



    class Instance {
        OPTIX_PIMPL();

    public:
        void destroy();

        void setGAS(const GeometryAccelerationStructure &gas, uint32_t matSetIdx = 0) const;
        void setTransform(const float transform[12]) const;
    };



    class InstanceAccelerationStructure {
        OPTIX_PIMPL();

    public:
        void destroy();

        void setConfiguration(bool preferFastTrace, bool allowUpdate, bool allowCompaction) const;

        void addChild(const Instance &instance) const;

        void prepareForBuild(OptixAccelBufferSizes* memoryRequirement) const;
        OptixTraversableHandle rebuild(CUstream stream, const Buffer &accelBuffer, const Buffer &scratchBuffer) const;
        void prepareForCompact(CUstream rebuildOrUpdateStream, size_t* compactedAccelBufferSize) const;
        OptixTraversableHandle compact(CUstream stream, const Buffer &compactedAccelBuffer) const;
        void removeUncompacted(CUstream compactionStream) const;
        OptixTraversableHandle update(CUstream stream, const Buffer &scratchBuffer) const;

        bool isReady() const;

        void markDirty() const;
    };



    class Pipeline {
        OPTIX_PIMPL();

    public:
        void destroy();

        void setMaxTraceDepth(uint32_t maxTraceDepth) const;
        void setPipelineOptions(uint32_t numPayloadValues, uint32_t numAttributeValues, const char* launchParamsVariableName, size_t sizeOfLaunchParams,
                                bool useMotionBlur, uint32_t traversableGraphFlags, uint32_t exceptionFlags) const;

        Module createModuleFromPTXString(const std::string &ptxString, int32_t maxRegisterCount, OptixCompileOptimizationLevel optLevel, OptixCompileDebugLevel debugLevel) const;

        ProgramGroup createRayGenProgram(Module module, const char* entryFunctionName) const;
        ProgramGroup createExceptionProgram(Module module, const char* entryFunctionName) const;
        ProgramGroup createMissProgram(Module module, const char* entryFunctionName) const;
        ProgramGroup createHitProgramGroup(Module module_CH, const char* entryFunctionNameCH,
                                           Module module_AH, const char* entryFunctionNameAH,
                                           Module module_IS, const char* entryFunctionNameIS) const;
        ProgramGroup createCallableGroup(Module module_DC, const char* entryFunctionNameDC,
                                         Module module_CC, const char* entryFunctionNameCC) const;

        void link(OptixCompileDebugLevel debugLevel, bool overrideUseMotionBlur) const;

        void setScene(Scene scene) const;
        void setNumMissRayTypes(uint32_t numMissRayTypes) const;

        void setRayGenerationProgram(ProgramGroup program) const;
        void setExceptionProgram(ProgramGroup program) const;
        void setMissProgram(uint32_t rayType, ProgramGroup program) const;

        void launch(CUstream stream, CUdeviceptr plpOnDevice, uint32_t dimX, uint32_t dimY, uint32_t dimZ) const;
    };



    class Module {
        OPTIX_PIMPL();

    public:
        void destroy();
    };



    class ProgramGroup {
        OPTIX_PIMPL();

    public:
        void destroy();
    };

#endif // #if !defined(__CUDA_ARCH__)
}
