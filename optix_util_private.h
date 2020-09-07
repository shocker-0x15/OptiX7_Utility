﻿/*

   Copyright 2020 Shin Watanabe

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

*/

#pragma once

#include "optix_util.h"

#if defined(OPTIX_Platform_Windows_MSVC)
#   define _USE_MATH_DEFINES
#   include <Windows.h>
#   undef min
#   undef max
#   undef near
#   undef far
#   undef RGB
#endif

#include <optix_function_table_definition.h>

#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>

#include <intrin.h>

#include <stdexcept>

#define OPTIX_CHECK(call) \
    do { \
        OptixResult error = call; \
        if (error != OPTIX_SUCCESS) { \
            std::stringstream ss; \
            ss << "OptiX call (" << #call << ") failed: " \
               << "(" __FILE__ << ":" << __LINE__ << ")\n"; \
            throw std::runtime_error(ss.str().c_str()); \
        } \
    } while (0)

#define OPTIX_CHECK_LOG(call) \
    do { \
        OptixResult error = call; \
        if (error != OPTIX_SUCCESS) { \
            std::stringstream ss; \
            ss << "OptiX call (" << #call << ") failed: " \
               << "(" __FILE__ << ":" << __LINE__ << ")\n" \
               << "Log: " << log << (logSize > sizeof(log) ? "<TRUNCATED>" : "") \
               << "\n"; \
            throw std::runtime_error(ss.str().c_str()); \
        } \
    } while (0)



namespace optixu {
    static std::runtime_error make_runtime_error(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        char str[4096];
        vsnprintf_s(str, sizeof(str), _TRUNCATE, fmt, args);
        va_end(args);

        return std::runtime_error(str);
    }

#define THROW_RUNTIME_ERROR(expr, fmt, ...) do { if (!(expr)) throw make_runtime_error(fmt, ##__VA_ARGS__); } while (0)

    static void logCallBack(uint32_t level, const char* tag, const char* message, void* cbdata) {
        optixPrintf("[%2u][%12s]: %s\n", level, tag, message);
    }

    static constexpr size_t s_maxMaterialUserDataSize = 512;
    static constexpr size_t s_maxGeometryInstanceUserDataSize = 512;
    static constexpr size_t s_maxGASUserDataSize = 512;



#define OPTIX_ALIAS_PIMPL(Name) using _ ## Name = Name::Priv

    OPTIX_ALIAS_PIMPL(Context);
    OPTIX_ALIAS_PIMPL(Material);
    OPTIX_ALIAS_PIMPL(Scene);
    OPTIX_ALIAS_PIMPL(GeometryInstance);
    OPTIX_ALIAS_PIMPL(GeometryAccelerationStructure);
    OPTIX_ALIAS_PIMPL(Transform);
    OPTIX_ALIAS_PIMPL(Instance);
    OPTIX_ALIAS_PIMPL(InstanceAccelerationStructure);
    OPTIX_ALIAS_PIMPL(Pipeline);
    OPTIX_ALIAS_PIMPL(Module);
    OPTIX_ALIAS_PIMPL(ProgramGroup);
    OPTIX_ALIAS_PIMPL(Denoiser);



#define OPTIX_OPAQUE_BRIDGE(BaseName) \
    friend class BaseName; \
\
    BaseName getPublicType() { \
        BaseName ret; \
        ret.m = this; \
        return ret; \
    } \
\
    static BaseName::Priv* extract(BaseName publicType) { \
        return publicType.m; \
    }

    template <typename PublicType>
    static typename PublicType::Priv* extract(const PublicType &obj) {
        return PublicType::Priv::extract(obj);
    }



    struct SizeAlign {
        uint32_t size;
        uint32_t alignment;

        constexpr SizeAlign() : size(0), alignment(1) {}
        constexpr SizeAlign(uint32_t s, uint32_t a) : size(s), alignment(a) {}

        SizeAlign &add(const SizeAlign &sa, uint32_t* offset) {
            uint32_t mask = sa.alignment - 1;
            alignment = std::max(alignment, sa.alignment);
            size = (size + mask) & ~mask;
            if (offset)
                *offset = size;
            size += sa.size;
            return *this;
        }
        SizeAlign &operator+=(const SizeAlign &sa) {
            return add(sa, nullptr);
        }
        SizeAlign &alignUp() {
            uint32_t mask = alignment - 1;
            size = (size + mask) & ~mask;
            return *this;
        }
    };

    SizeAlign max(const SizeAlign &sa0, const SizeAlign &sa1) {
        return SizeAlign{ std::max(sa0.size, sa1.size), std::max(sa0.alignment, sa1.alignment) };
    }



    class Context::Priv {
        CUcontext cudaContext;
        OptixDeviceContext rawContext;
        uint32_t maxInstanceID;
        uint32_t numVisibilityMaskBits;

    public:
        OPTIX_OPAQUE_BRIDGE(Context);

        Priv(CUcontext cuContext) : cudaContext(cuContext) {
            OPTIX_CHECK(optixInit());

            OptixDeviceContextOptions options = {};
            options.logCallbackFunction = &logCallBack;
            options.logCallbackLevel = 4;
            OPTIX_CHECK(optixDeviceContextCreate(cudaContext, &options, &rawContext));
            OPTIX_CHECK(optixDeviceContextGetProperty(rawContext, OPTIX_DEVICE_PROPERTY_LIMIT_MAX_INSTANCE_ID,
                                                      &maxInstanceID, sizeof(maxInstanceID)));
            OPTIX_CHECK(optixDeviceContextGetProperty(rawContext, OPTIX_DEVICE_PROPERTY_LIMIT_NUM_BITS_INSTANCE_VISIBILITY_MASK,
                                                      &numVisibilityMaskBits, sizeof(numVisibilityMaskBits)));
        }
        ~Priv() {
            optixDeviceContextDestroy(rawContext);
        }

        uint32_t getMaxInstanceID() const {
            return maxInstanceID;
        }
        uint32_t getNumVisibilityMaskBits() const {
            return numVisibilityMaskBits;
        }

        CUcontext getCUDAContext() const {
            return cudaContext;
        }
        OptixDeviceContext getRawContext() const {
            return rawContext;
        }
    };



    class Material::Priv {
        struct Key {
            const _Pipeline* pipeline;
            uint32_t rayType;

            bool operator<(const Key &rKey) const {
                if (pipeline < rKey.pipeline) {
                    return true;
                }
                else if (pipeline == rKey.pipeline) {
                    if (rayType < rKey.rayType)
                        return true;
                }
                return false;
            }

            struct Hash {
                typedef std::size_t result_type;

                std::size_t operator()(const Key& key) const {
                    size_t seed = 0;
                    auto hash0 = std::hash<const _Pipeline*>()(key.pipeline);
                    auto hash1 = std::hash<uint32_t>()(key.rayType);
                    seed ^= hash0 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                    seed ^= hash1 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                    return seed;
                }
            };
            bool operator==(const Key &rKey) const {
                return pipeline == rKey.pipeline && rayType == rKey.rayType;
            }
        };

        _Context* context;
        SizeAlign userDataSizeAlign;
        std::vector<uint8_t> userData;

        std::unordered_map<Key, const _ProgramGroup*, Key::Hash> programs;

    public:
        OPTIX_OPAQUE_BRIDGE(Material);

        Priv(_Context* ctxt) :
            context(ctxt), userData(sizeof(uint32_t)) {}
        ~Priv() {}

        OptixDeviceContext getRawContext() const {
            return context->getRawContext();
        }

        SizeAlign getUserDataSizeAlign() const {
            return userDataSizeAlign;
        }
        void setRecordData(const _Pipeline* pipeline, uint32_t rayType, uint8_t* record, SizeAlign* curSizeAlign) const;
    };


    
    class Scene::Priv {
        struct SBTOffsetKey {
            const _GeometryAccelerationStructure* gas;
            uint32_t matSetIndex;

            bool operator<(const SBTOffsetKey &rKey) const {
                if (gas < rKey.gas) {
                    return true;
                }
                else if (gas == rKey.gas) {
                    if (matSetIndex < rKey.matSetIndex)
                        return true;
                }
                return false;
            }

            struct Hash {
                typedef std::size_t result_type;

                std::size_t operator()(const SBTOffsetKey& key) const {
                    size_t seed = 0;
                    auto hash0 = std::hash<const _GeometryAccelerationStructure*>()(key.gas);
                    auto hash1 = std::hash<uint32_t>()(key.matSetIndex);
                    seed ^= hash0 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                    seed ^= hash1 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                    return seed;
                }
            };
            bool operator==(const SBTOffsetKey &rKey) const {
                return gas == rKey.gas && matSetIndex == rKey.matSetIndex;
            }
        };

        const _Context* context;
        std::unordered_set<_GeometryAccelerationStructure*> geomASs;
        std::unordered_map<SBTOffsetKey, uint32_t, SBTOffsetKey::Hash> sbtOffsets;
        uint32_t singleRecordSize;
        uint32_t numSBTRecords;
        std::unordered_set<_Transform*> transforms;
        std::unordered_set<_InstanceAccelerationStructure*> instASs;
        struct {
            unsigned int sbtLayoutIsUpToDate : 1;
        };

    public:
        OPTIX_OPAQUE_BRIDGE(Scene);

        Priv(const _Context* ctxt) : context(ctxt),
            singleRecordSize(OPTIX_SBT_RECORD_HEADER_SIZE), numSBTRecords(0),
            sbtLayoutIsUpToDate(false) {}
        ~Priv() {}

        const _Context* getContext() const {
            return context;
        }
        CUcontext getCUDAContext() const {
            return context->getCUDAContext();
        }
        OptixDeviceContext getRawContext() const {
            return context->getRawContext();
        }



        void addGAS(_GeometryAccelerationStructure* gas) {
            geomASs.insert(gas);
        }
        void removeGAS(_GeometryAccelerationStructure* gas) {
            geomASs.erase(gas);
        }
        void addTransform(_Transform* tr) {
            transforms.insert(tr);
        }
        void removeTransform(_Transform* tr) {
            transforms.erase(tr);
        }
        void addIAS(_InstanceAccelerationStructure* ias) {
            instASs.insert(ias);
        }
        void removeIAS(_InstanceAccelerationStructure* ias) {
            instASs.erase(ias);
        }

        bool sbtLayoutGenerationDone() const {
            return sbtLayoutIsUpToDate;
        }
        void markSBTLayoutDirty();
        uint32_t getSBTOffset(_GeometryAccelerationStructure* gas, uint32_t matSetIdx) {
            SBTOffsetKey key = SBTOffsetKey{ gas, matSetIdx };
            THROW_RUNTIME_ERROR(sbtOffsets.count(key), "GAS %p: material set index %u is out of bounds.", gas, matSetIdx);
            return sbtOffsets.at(key);
        }

        uint32_t getSingleRecordSize() const {
            return singleRecordSize;
        }
        void setupHitGroupSBT(CUstream stream, const _Pipeline* pipeline, cudau::Buffer* sbt);

        bool isReady(bool* hasMotionAS);
    };



    class GeometryInstance::Priv {
        _Scene* scene;
        SizeAlign userDataSizeAlign;
        std::vector<uint8_t> userData;

        // TODO: support deformation blur (multiple vertex buffers)
        union {
            struct {
                CUdeviceptr* vertexBufferArray;
                BufferView vertexBuffer;
                BufferView triangleBuffer;
                OptixVertexFormat vertexFormat;
                OptixIndicesFormat indexFormat;
            };
            struct {
                CUdeviceptr* primitiveAabbBufferArray;
                BufferView primitiveAABBBuffer;
            };
        };
        uint32_t primitiveIndexOffset;
        uint32_t materialIndexOffsetSize;
        BufferView materialIndexOffsetBuffer;
        std::vector<uint32_t> buildInputFlags; // per SBT record

        std::vector<std::vector<const _Material*>> materials;

        struct {
            const unsigned int forCustomPrimitives : 1;
        };

    public:
        OPTIX_OPAQUE_BRIDGE(GeometryInstance);

        Priv(_Scene* _scene, bool _forCustomPrimitives) :
            scene(_scene),
            userData(sizeof(uint32_t)),
            primitiveIndexOffset(0),
            materialIndexOffsetSize(0),
            forCustomPrimitives(_forCustomPrimitives) {
            if (forCustomPrimitives) {
                primitiveAabbBufferArray = new CUdeviceptr[1];
                primitiveAabbBufferArray[0] = 0;
                primitiveAABBBuffer = BufferView();
            }
            else {
                vertexBufferArray = new CUdeviceptr[1];
                vertexBufferArray[0] = 0;
                vertexBuffer = BufferView();
                triangleBuffer = BufferView();
                vertexFormat = OPTIX_VERTEX_FORMAT_NONE;
                indexFormat = OPTIX_INDICES_FORMAT_NONE;
            }
        }
        ~Priv() {
            if (forCustomPrimitives)
                delete[] primitiveAabbBufferArray;
            else
                delete[] vertexBufferArray;
        }

        const _Scene* getScene() const {
            return scene;
        }
        OptixDeviceContext getRawContext() const {
            return scene->getRawContext();
        }



        bool isCustomPrimitiveInstance() const {
            return forCustomPrimitives;
        }
        void fillBuildInput(OptixBuildInput* input, CUdeviceptr preTransform) const;
        void updateBuildInput(OptixBuildInput* input, CUdeviceptr preTransform) const;

        SizeAlign calcMaxRecordSizeAlign(uint32_t gasMatSetIdx) const;
        uint32_t getNumSBTRecords() const;
        uint32_t fillSBTRecords(const _Pipeline* pipeline, uint32_t gasMatSetIdx,
                                const void* gasUserData, const SizeAlign gasUserDataSizeAlign,
                                uint32_t numRayTypes, uint8_t* records) const;
    };



    class GeometryAccelerationStructure::Priv {
        struct Child {
            _GeometryInstance* geomInst;
            CUdeviceptr preTransform;

            bool operator==(const Child &rChild) const {
                return geomInst == rChild.geomInst && preTransform == rChild.preTransform;
            }
        };

        _Scene* scene;
        SizeAlign userDataSizeAlign;
        std::vector<uint8_t> userData;

        std::vector<uint32_t> numRayTypesPerMaterialSet;

        std::vector<Child> children;
        std::vector<OptixBuildInput> buildInputs;

        OptixAccelBuildOptions buildOptions;
        OptixAccelBufferSizes memoryRequirement;

        CUevent finishEvent;
        cudau::TypedBuffer<size_t> compactedSizeOnDevice;
        size_t compactedSize;
        OptixAccelEmitDesc propertyCompactedSize;

        OptixTraversableHandle handle;
        OptixTraversableHandle compactedHandle;
        BufferView accelBuffer;
        BufferView compactedAccelBuffer;
        ASTradeoff tradeoff;
        struct {
            unsigned int forCustomPrimitives : 1;
            unsigned int allowUpdate : 1;
            unsigned int allowCompaction : 1;
            unsigned int allowRandomVertexAccess : 1;
            unsigned int readyToBuild : 1;
            unsigned int available : 1;
            unsigned int readyToCompact : 1;
            unsigned int compactedAvailable : 1;
        };

    public:
        OPTIX_OPAQUE_BRIDGE(GeometryAccelerationStructure);

        Priv(_Scene* _scene, bool _forCustomPrimitives) :
            scene(_scene),
            userData(sizeof(uint32_t)),
            handle(0), compactedHandle(0),
            tradeoff(ASTradeoff::Default),
            forCustomPrimitives(_forCustomPrimitives),
            allowUpdate(false), allowCompaction(false), allowRandomVertexAccess(false),
            readyToBuild(false), available(false), 
            readyToCompact(false), compactedAvailable(false) {
            scene->addGAS(this);

            CUDADRV_CHECK(cuEventCreate(&finishEvent,
                                        CU_EVENT_BLOCKING_SYNC | CU_EVENT_DISABLE_TIMING));
            compactedSizeOnDevice.initialize(scene->getCUDAContext(), cudau::BufferType::Device, 1);

            propertyCompactedSize = OptixAccelEmitDesc{};
            propertyCompactedSize.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
            propertyCompactedSize.result = compactedSizeOnDevice.getCUdeviceptr();
        }
        ~Priv() {
            compactedSizeOnDevice.finalize();
            cuEventDestroy(finishEvent);

            scene->removeGAS(this);
        }

        const _Scene* getScene() const {
            return scene;
        }
        CUcontext getCUDAContext() const {
            return scene->getCUDAContext();
        }
        OptixDeviceContext getRawContext() const {
            return scene->getRawContext();
        }



        uint32_t getNumMaterialSets() const {
            return static_cast<uint32_t>(numRayTypesPerMaterialSet.size());
        }
        uint32_t getNumRayTypes(uint32_t matSetIdx) const {
            return numRayTypesPerMaterialSet[matSetIdx];
        }

        SizeAlign calcMaxRecordSizeAlign(uint32_t matSetIdx) const;
        uint32_t calcNumSBTRecords(uint32_t matSetIdx) const;
        uint32_t fillSBTRecords(const _Pipeline* pipeline, uint32_t matSetIdx, uint8_t* records) const;
        bool hasMotion() const {
            return false;
        }
        
        void markDirty();
        bool isReady() const {
            return available || compactedAvailable;
        }

        OptixTraversableHandle getHandle() const {
            THROW_RUNTIME_ERROR(isReady(), "Traversable handle is not ready.");
            if (compactedAvailable)
                return compactedHandle;
            if (available)
                return handle;
            return 0;
        }
    };



    enum class ChildType {
        GAS = 0,
        IAS,
        Transform,
        Invalid
    };



    class Transform::Priv {
        _Scene* scene;
        union {
            _GeometryAccelerationStructure* childGas;
            _InstanceAccelerationStructure* childIas;
            _Transform* childXfm;
        };
        ChildType childType;
        uint8_t* data;
        size_t dataSize;
        TransformType type;
        OptixMotionOptions options;

        OptixTraversableHandle handle;
        struct {
            unsigned int available : 1;
        };

    public:
        OPTIX_OPAQUE_BRIDGE(Transform);

        Priv(_Scene* _scene) :
            scene(_scene), type(TransformType::Invalid),
            childGas(nullptr),
            childType(ChildType::Invalid),
            data(nullptr), dataSize(0),
            handle(0),
            available(false) {
            scene->addTransform(this);

            options.numKeys = 2;
            options.timeBegin = 0.0f;
            options.timeEnd = 0.0f;
            options.flags = OPTIX_MOTION_FLAG_NONE;
        }
        ~Priv() {
            if (data)
                delete data;
            data = nullptr;
            scene->removeTransform(this);
        }

        const _Scene* getScene() const {
            return scene;
        }
        CUcontext getCUDAContext() const {
            return scene->getCUDAContext();
        }
        OptixDeviceContext getRawContext() const {
            return scene->getRawContext();
        }



        _GeometryAccelerationStructure* getDescendantGAS() const;

        void markDirty();
        bool isReady() const {
            return available;
        }

        OptixTraversableHandle getHandle() const {
            THROW_RUNTIME_ERROR(isReady(), "Traversable handle is not ready.");
            return handle;
        }
    };



    class Instance::Priv {
        _Scene* scene;
        ChildType type;
        union {
            _GeometryAccelerationStructure* childGas;
            _InstanceAccelerationStructure* childIas;
            _Transform* childXfm;
        };
        uint32_t matSetIndex;
        uint32_t id;
        uint32_t visibilityMask;
        OptixInstanceFlags flags;
        float instTransform[12];

    public:
        OPTIX_OPAQUE_BRIDGE(Instance);

        Priv(_Scene* _scene) :
            scene(_scene),
            type(ChildType::Invalid) {
            childGas = nullptr;
            matSetIndex = 0xFFFFFFFF;
            id = 0;
            visibilityMask = 0xFF;
            flags = OPTIX_INSTANCE_FLAG_NONE;
            float identity[] = {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
            };
            std::copy_n(identity, 12, instTransform);
        }
        ~Priv() {}

        const _Scene* getScene() const {
            return scene;
        }



        void fillInstance(OptixInstance* instance) const;
        void updateInstance(OptixInstance* instance) const;
        bool isMotionAS() const;
        bool isTransform() const;
    };



    class InstanceAccelerationStructure::Priv {
        _Scene* scene;

        std::vector<_Instance*> children;
        OptixBuildInput buildInput;
        std::vector<OptixInstance> instances;

        OptixMotionOptions motionOptions;
        OptixAccelBuildOptions buildOptions;
        OptixAccelBufferSizes memoryRequirement;

        CUevent finishEvent;
        cudau::TypedBuffer<size_t> compactedSizeOnDevice;
        size_t compactedSize;
        OptixAccelEmitDesc propertyCompactedSize;

        OptixTraversableHandle handle;
        OptixTraversableHandle compactedHandle;
        BufferView instanceBuffer;
        BufferView aabbBuffer;
        BufferView accelBuffer;
        BufferView compactedAccelBuffer;
        ASTradeoff tradeoff;
        struct {
            unsigned int allowUpdate : 1;
            unsigned int allowCompaction : 1;
            unsigned int aabbsRequired : 1;
            unsigned int readyToBuild : 1;
            unsigned int available : 1;
            unsigned int readyToCompact : 1;
            unsigned int compactedAvailable : 1;
        };

    public:
        OPTIX_OPAQUE_BRIDGE(InstanceAccelerationStructure);

        Priv(_Scene* _scene) :
            scene(_scene),
            handle(0), compactedHandle(0),
            tradeoff(ASTradeoff::Default),
            allowUpdate(false), allowCompaction(false),
            readyToBuild(false), available(false),
            readyToCompact(false), compactedAvailable(false) {
            scene->addIAS(this);

            motionOptions = {};

            CUDADRV_CHECK(cuEventCreate(&finishEvent,
                                        CU_EVENT_BLOCKING_SYNC | CU_EVENT_DISABLE_TIMING));
            compactedSizeOnDevice.initialize(scene->getCUDAContext(), cudau::BufferType::Device, 1);

            std::memset(&propertyCompactedSize, 0, sizeof(propertyCompactedSize));
            propertyCompactedSize.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
            propertyCompactedSize.result = compactedSizeOnDevice.getCUdeviceptr();
        }
        ~Priv() {
            compactedSizeOnDevice.finalize();
            cuEventDestroy(finishEvent);

            scene->removeIAS(this);
        }

        const _Scene* getScene() const {
            return scene;
        }
        CUcontext getCUDAContext() const {
            return scene->getCUDAContext();
        }
        OptixDeviceContext getRawContext() const {
            return scene->getRawContext();
        }



        bool hasMotion() const {
            return motionOptions.numKeys >= 2;
        }



        void markDirty();
        bool isReady() const {
            return available || compactedAvailable;
        }

        OptixTraversableHandle getHandle() const {
            THROW_RUNTIME_ERROR(isReady(), "Traversable handle is not ready.");
            if (compactedAvailable)
                return compactedHandle;
            if (available)
                return handle;
            optixAssert_ShouldNotBeCalled();
            return 0;
        }
    };



    class Pipeline::Priv {
        const _Context* context;
        OptixPipeline rawPipeline;

        OptixPipelineCompileOptions pipelineCompileOptions;
        size_t sizeOfPipelineLaunchParams;
        std::unordered_set<OptixProgramGroup> programGroups;

        _Scene* scene;
        uint32_t numMissRayTypes;
        uint32_t numCallablePrograms;
        size_t sbtSize;

        _ProgramGroup* rayGenProgram;
        _ProgramGroup* exceptionProgram;
        std::vector<_ProgramGroup*> missPrograms;
        std::vector<_ProgramGroup*> callablePrograms;
        cudau::Buffer* sbt;
        cudau::Buffer* hitGroupSbt;
        OptixShaderBindingTable sbtParams;

        struct {
            unsigned int pipelineLinked : 1;
            unsigned int sbtLayoutIsUpToDate : 1;
            unsigned int sbtIsUpToDate : 1;
            unsigned int hitGroupSbtIsUpToDate : 1;
        };

        void setupShaderBindingTable(CUstream stream);

    public:
        OPTIX_OPAQUE_BRIDGE(Pipeline);

        Priv(const _Context* ctxt) :
            context(ctxt), rawPipeline(nullptr),
            sizeOfPipelineLaunchParams(0),
            scene(nullptr), numMissRayTypes(0), numCallablePrograms(0),
            rayGenProgram(nullptr), exceptionProgram(nullptr), hitGroupSbt(nullptr),
            pipelineLinked(false), sbtLayoutIsUpToDate(false), sbtIsUpToDate(false), hitGroupSbtIsUpToDate(false) {
            sbtParams = {};
        }
        ~Priv() {
            if (pipelineLinked)
                optixPipelineDestroy(rawPipeline);
        }

        CUcontext getCUDAContext() const {
            return context->getCUDAContext();
        }
        OptixDeviceContext getRawContext() const {
            return context->getRawContext();
        }



        void createProgram(const OptixProgramGroupDesc &desc, const OptixProgramGroupOptions &options, OptixProgramGroup* group);
        void destroyProgram(OptixProgramGroup group);
    };



    class Module::Priv {
        const _Pipeline* pipeline;
        OptixModule rawModule;

    public:
        OPTIX_OPAQUE_BRIDGE(Module);

        Priv(const _Pipeline* pl, OptixModule _rawModule) :
            pipeline(pl), rawModule(_rawModule) {}



        const _Pipeline* getPipeline() const {
            return pipeline;
        }

        OptixModule getRawModule() const {
            return rawModule;
        }
    };



    class ProgramGroup::Priv {
        _Pipeline* pipeline;
        OptixProgramGroup rawGroup;

    public:
        OPTIX_OPAQUE_BRIDGE(ProgramGroup);

        Priv(_Pipeline* pl, OptixProgramGroup _rawGroup) :
            pipeline(pl), rawGroup(_rawGroup) {}



        const _Pipeline* getPipeline() const {
            return pipeline;
        }

        OptixProgramGroup getRawProgramGroup() const {
            return rawGroup;
        }

        void packHeader(uint8_t* record) const {
            OPTIX_CHECK(optixSbtRecordPackHeader(rawGroup, record));
        }
    };



    static inline size_t getPixelSize(OptixPixelFormat format) {
        switch (format) {
        case OPTIX_PIXEL_FORMAT_HALF3:
            return 3 * sizeof(uint16_t);
        case OPTIX_PIXEL_FORMAT_HALF4:
            return 4 * sizeof(uint16_t);
        case OPTIX_PIXEL_FORMAT_FLOAT3:
            return 3 * sizeof(float);
        case OPTIX_PIXEL_FORMAT_FLOAT4:
            return 4 * sizeof(float);
        case OPTIX_PIXEL_FORMAT_UCHAR3:
            return 3 * sizeof(uint8_t);
        case OPTIX_PIXEL_FORMAT_UCHAR4:
            return 4 * sizeof(uint8_t);
        default:
            optixAssert_ShouldNotBeCalled();
            break;
        }
        return 0;
    }

    struct _DenoisingTask {
        int32_t inputOffsetX;
        int32_t inputOffsetY;
        int32_t outputOffsetX;
        int32_t outputOffsetY;
        int32_t outputWidth;
        int32_t outputHeight;

        _DenoisingTask() {}
        _DenoisingTask(const DenoisingTask &v) {
            std::memcpy(this, &v, sizeof(v));
        }
        operator DenoisingTask() const {
            DenoisingTask ret;
            std::memcpy(&ret, this, sizeof(ret));
            return ret;
        }
    };
    static_assert(sizeof(DenoisingTask) == sizeof(_DenoisingTask) &&
                  alignof(DenoisingTask) == alignof(_DenoisingTask),
                  "Size/Alignment mismatch: DenoisingTask vs _DenoisingTask");
    
    class Denoiser::Priv {
        const _Context* context;
        OptixDenoiser rawDenoiser;
        OptixDenoiserInputKind inputKind;

        uint32_t imageWidth;
        uint32_t imageHeight;
        uint32_t tileWidth;
        uint32_t tileHeight;
        int32_t overlapWidth;
        uint32_t maxInputWidth;
        uint32_t maxInputHeight;
        size_t stateSize;
        size_t scratchSize;
        size_t scratchSizeForComputeIntensity;

        BufferView stateBuffer;
        BufferView scratchBuffer;
        BufferView colorBuffer;
        BufferView albedoBuffer;
        BufferView normalBuffer;
        BufferView outputBuffer;
        OptixPixelFormat colorFormat;
        OptixPixelFormat albedoFormat;
        OptixPixelFormat normalFormat;
        struct {
            unsigned int modelSet : 1;
            unsigned int useTiling : 1;
            unsigned int imageSizeSet : 1;
            unsigned int imageLayersSet : 1;
            unsigned int stateIsReady : 1;
        };

    public:
        OPTIX_OPAQUE_BRIDGE(Denoiser);

        Priv(const _Context* ctxt, OptixDenoiserInputKind _inputKind) :
            context(ctxt),
            inputKind(_inputKind),
            imageWidth(0), imageHeight(0), tileWidth(0), tileHeight(0),
            overlapWidth(0), maxInputWidth(0), maxInputHeight(0),
            stateSize(0), scratchSize(0), scratchSizeForComputeIntensity(0),
            modelSet(false), useTiling(false), imageSizeSet(false), imageLayersSet(false), stateIsReady(false) {
            OptixDenoiserOptions options = {};
            options.inputKind = inputKind;
            OPTIX_CHECK(optixDenoiserCreate(context->getRawContext(), &options, &rawDenoiser));
        }
        ~Priv() {
            optixDenoiserDestroy(rawDenoiser);
        }

        CUcontext getCUDAContext() const {
            return context->getCUDAContext();
        }
        OptixDeviceContext getRawContext() const {
            return context->getRawContext();
        }
    };
}
