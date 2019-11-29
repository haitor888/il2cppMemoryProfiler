#ifndef UMPCRAWLER_H
#define UMPCRAWLER_H

#include <QString>
#include <QDataStream>

#include <cstdint>
#include <vector>
#include <unordered_map>

#include "umpmemory.h"

struct Connection {
    std::uint32_t from_;
    std::uint32_t to_;
    Connection(std::uint32_t from, std::uint32_t to) : from_(from), to_(to) {}
};

struct PackedManagedObject {
    std::uint64_t address_;
    std::uint32_t typeIndex_;
    std::uint32_t size_;
};

struct StartIndices {
    std::uint32_t gcHandleCount_;
    std::uint32_t staticFieldsCount_;
    StartIndices(std::uint32_t gcHandleCount = 0, std::uint32_t staticFieldsCount = 0)
        : gcHandleCount_(gcHandleCount), staticFieldsCount_(staticFieldsCount) {}
    std::uint32_t OfFirstGCHandle() const { return 0; }
    std::uint32_t OfFirstStaticFields() const { return OfFirstGCHandle() + gcHandleCount_; }
    std::uint32_t OfFirstManagedObject() const { return OfFirstStaticFields() + staticFieldsCount_; }
};

struct PackedCrawlerData {
    bool valid_;
    Il2CppManagedMemorySnapshot* snapshot_;
    StartIndices startIndices_;
    std::vector<PackedManagedObject> managedObjects_;
    std::vector<Il2CppMetadataType*> typesWithStaticFields_;
    std::vector<Connection> connections_;
    std::vector<Il2CppMetadataType*> typeDescriptions_;
    PackedCrawlerData(Il2CppManagedMemorySnapshot* snapshot) {
        valid_ = true;
        snapshot_ = snapshot;
        for (std::uint32_t i = 0; i < snapshot->metadata.typeCount; i++) {
            auto type = &snapshot->metadata.types[i];
            if (type->statics != nullptr && type->staticsSize > 0)
                typesWithStaticFields_.push_back(type);
        }
        startIndices_.gcHandleCount_ = snapshot->gcHandles.trackedObjectCount;
        startIndices_.staticFieldsCount_ = static_cast<std::uint32_t>(typesWithStaticFields_.size());
    }
};

struct BytesAndOffset {
    std::uint8_t* bytes_ = nullptr;
    std::uint64_t offset_ = 0;
    std::uint32_t pointerSize_ = 0;
    bool IsValid() const { return bytes_ != nullptr; }
    std::uint64_t ReadPointer() const {
        if (pointerSize_ == 4) {
            std::uint32_t ptr;
            memcpy(&ptr, bytes_ + offset_, 4);
            return ptr;
        }
        std::uint64_t ptr;
        memcpy(&ptr, bytes_ + offset_, 8);
        return ptr;
    }
    std::int32_t ReadInt32() const {
        std::int32_t ptr;
        memcpy(&ptr, bytes_ + offset_, 4);
        return ptr;
    }
    std::int64_t ReadInt64() const {
        std::int64_t ptr;
        memcpy(&ptr, bytes_ + offset_, 8);
        return ptr;
    }
    BytesAndOffset Add(std::uint32_t add) const {
        BytesAndOffset ba;
        ba.bytes_ = bytes_;
        ba.offset_ = offset_ + add;
        ba.pointerSize_ = pointerSize_;
        return ba;
    }
    void WritePointer(std::uint64_t value) {
        for (std::uint32_t i = 0; i < pointerSize_; i++) {
            bytes_[i + offset_] = static_cast<std::uint8_t>(value);
            value >>= 8;
        }
    }
    BytesAndOffset NextPointer() const {
        return Add(pointerSize_);
    }
};

class Crawler {
public:
    void Crawl(PackedCrawlerData& result, Il2CppManagedMemorySnapshot* snapshot);
    void CrawlPointer(Il2CppManagedMemorySnapshot* snapshot, StartIndices startIndices, std::uint64_t pointer, std::uint32_t indexOfFrom,
                      std::vector<Connection>& oConnections, std::vector<PackedManagedObject>& outManagedObjects);
    void ParseObjectHeader(StartIndices& startIndices, Il2CppManagedMemorySnapshot* snapshot, std::uint64_t originalHeapAddress, std::uint64_t& typeInfoAddress,
                           std::uint32_t& indexOfObject, bool& wasAlreadyCrawled, std::vector<PackedManagedObject>& outManagedObjects);
    void CrawlRawObjectData(Il2CppManagedMemorySnapshot* packedMemorySnapshot, StartIndices startIndices, BytesAndOffset bytesAndOffset,
                            Il2CppMetadataType* typeDescription, bool useStaticFields, std::uint32_t indexOfFrom,
                            std::vector<Connection>& out_connections, std::vector<PackedManagedObject>& out_managedObjects);
    int SizeOfObjectInBytes(Il2CppMetadataType* typeDescription, BytesAndOffset bo, Il2CppManagedMemorySnapshot* snapshot, std::uint64_t address);
private:
    std::unordered_map<std::uint64_t, Il2CppMetadataType*> typeInfoToTypeDescription_;
    std::vector<Il2CppMetadataType*> typeDescriptions_;
};

struct FieldDescription {
    std::uint32_t offset_;
    std::uint32_t typeIndex_;
    QString name_;
    bool isStatic_;
};

enum class CrawledDiffFlags : std::uint8_t {
    kNone = 0,
    kAdded,
    kBigger,
    kSame,
    kSmaller,
};

struct TypeDescription {
    Il2CppMetadataTypeFlags flags_;
    std::vector<FieldDescription> fields_;
    std::uint8_t* statics_ = nullptr;
    std::uint32_t staticsSize_ = 0;
    std::uint32_t baseOrElementTypeIndex_;
    QString name_;
    QString assemblyName_;
    std::uint64_t typeInfoAddress_;
    std::int64_t size_;
    std::uint32_t typeIndex_;
    TypeDescription() = default;
    TypeDescription(const TypeDescription& other) {
        flags_ = other.flags_;
        fields_ = other.fields_;
        baseOrElementTypeIndex_ = other.baseOrElementTypeIndex_;
        name_ = other.name_;
        assemblyName_ = other.assemblyName_;
        typeInfoAddress_ = other.typeInfoAddress_;
        size_ = other.size_;
        typeIndex_ = other.typeIndex_;
        if (statics_ != nullptr) {
            delete statics_;
            statics_ = nullptr;
        }
        staticsSize_ = other.staticsSize_;
        if (staticsSize_ > 0) {
            statics_ = new std::uint8_t[staticsSize_];
            memcpy(statics_, other.statics_, staticsSize_);
        }
    }
    bool IsArray() const {
        return (flags_ & Il2CppMetadataTypeFlags::kArray) != 0;
    }
    bool IsValueType() const {
        return (flags_ & Il2CppMetadataTypeFlags::kValueType) != 0;
    }
    int ArrayRank() const {
        return static_cast<int>(flags_ & Il2CppMetadataTypeFlags::kArrayRankMask) >> 16;
    }
};

enum class ThingType {
    NONE = 0,
    MANAGED,
    GCHANDLE,
    STATIC
};

struct ThingInMemory {
    std::uint32_t index_{0};
    std::int64_t size_{0};
    CrawledDiffFlags diff_ = CrawledDiffFlags::kNone;
    QString caption_{};
    std::vector<ThingInMemory*> references_{};
    std::vector<ThingInMemory*> referencedBy_{};
    ThingInMemory() = default;
    ThingInMemory(const ThingInMemory& other) {
        index_ = other.index_;
        size_ = other.size_;
        diff_ = other.diff_;
        caption_ = other.caption_;
    }
    virtual ~ThingInMemory() = default;
    virtual ThingType type() const { return ThingType::NONE; }
};

struct ManagedObject : public ThingInMemory {
    std::uint64_t address_;
    TypeDescription* typeDescription_;
    ThingType type() const override { return ThingType::MANAGED; }
};

struct GCHandle : public ThingInMemory {
    ThingType type() const override { return ThingType::GCHANDLE; }
};

struct StaticFields : public ThingInMemory {
    TypeDescription* typeDescription_;
    std::uint64_t nameHash_;
    ThingType type() const override { return ThingType::STATIC; }
};

struct CrawledManagedMemorySection {
    std::uint64_t sectionStartAddress_ = 0;
    std::uint32_t sectionSize_ = 0;
    std::uint8_t* sectionBytes_ = nullptr;
};

enum class FieldFindOptions {
    OnlyInstance,
    OnlyStatic
};

struct CrawledMemorySnapshot {
    std::vector<GCHandle> gcHandles_{};
    std::vector<ManagedObject> managedObjects_{};
    std::vector<StaticFields> staticFields_{};

    std::vector<ThingInMemory*> allObjects_{};

    std::vector<CrawledManagedMemorySection> managedHeap_;
    std::vector<TypeDescription> typeDescriptions_{};

    Il2CppRuntimeInformation runtimeInformation_;

    bool isDiff_ = false;
    QString name_ = "EmptySnapshot";

    static void Unpack(CrawledMemorySnapshot& result, Il2CppManagedMemorySnapshot* snapshot, PackedCrawlerData& packedCrawlerData);
    static BytesAndOffset FindInHeap(const CrawledMemorySnapshot* snapshot, std::uint64_t addr);
    static QString ReadString(const CrawledMemorySnapshot* snapshot, const BytesAndOffset& bo);
    static int ReadArrayLength(const CrawledMemorySnapshot* snapshot, std::uint64_t address, TypeDescription* arrayType);
    static void AllFieldsOf(const CrawledMemorySnapshot* snapshot, const TypeDescription* typeDescription,
                            FieldFindOptions options, std::vector<const FieldDescription*>& outFields);
    static CrawledMemorySnapshot* Clone(const CrawledMemorySnapshot* src);
    static CrawledMemorySnapshot* Diff(const CrawledMemorySnapshot* firstSnapshot, const CrawledMemorySnapshot* secondSnapshot);
    static void Free(CrawledMemorySnapshot* snapshot);
};

// windows & android runtime are little-endian
class PrimitiveValueReader {
public:
    PrimitiveValueReader(const CrawledMemorySnapshot* snapshot) : snapshot_(snapshot) {}
    template<typename T>
    T ReadInteger(const BytesAndOffset& bo) const {
        T value;
        memcpy(reinterpret_cast<char*>(&value), bo.bytes_ + bo.offset_, sizeof(T));
        return value;
    }
    std::uint8_t ReadByte(const BytesAndOffset& bo) const {
        return bo.bytes_[bo.offset_];
    }
    bool ReadBool(const BytesAndOffset& bo) const {
        return ReadByte(bo) != 0;
    }
    std::uint64_t ReadPointer(const BytesAndOffset& bo) const {
        if (snapshot_->runtimeInformation_.pointerSize == 4)
            return ReadInteger<std::uint32_t>(bo);
        else
            return ReadInteger<std::uint64_t>(bo);
    }
    std::uint64_t ReadPointer(std::uint64_t address) const {
        return ReadPointer(CrawledMemorySnapshot::FindInHeap(snapshot_, address));
    }
private:
    const CrawledMemorySnapshot* snapshot_;
};

#endif // UMPCRAWLER_H
