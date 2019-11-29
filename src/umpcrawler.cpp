#include "umpcrawler.h"

#include <QTime>
#include <QDebug>

BytesAndOffset FindInHeap(Il2CppManagedMemorySnapshot* snapshot, std::uint64_t addr) {
    BytesAndOffset ba;
    for (std::uint32_t i = 0; i < snapshot->heap.sectionCount; i++) {
        auto section = snapshot->heap.sections[i];
        if (addr >= section.sectionStartAddress && addr < (section.sectionStartAddress + static_cast<std::uint64_t>(section.sectionSize))) {
            ba.bytes_ = section.sectionBytes;
            ba.offset_ = addr - section.sectionStartAddress;
            ba.pointerSize_ = snapshot->runtimeInformation.pointerSize;
            break;
        }
    }
    return ba;
}

int ReadArrayLength(Il2CppManagedMemorySnapshot* snapshot, std::uint64_t address, Il2CppMetadataType* arrayType) {
    auto bo = FindInHeap(snapshot, address);
    auto bounds = bo.Add(snapshot->runtimeInformation.arrayBoundsOffsetInHeader).ReadPointer();
    if (bounds == 0)
        return bo.Add(snapshot->runtimeInformation.arraySizeOffsetInHeader).ReadInt32();
    auto cursor = FindInHeap(snapshot, bounds);
    int length = 1;
    int arrayRank = static_cast<int>(arrayType->flags & Il2CppMetadataTypeFlags::kArrayRankMask) >> 16;
    for (int i = 0; i < arrayRank; i++) {
        length *= cursor.ReadInt32();
        cursor = cursor.Add(8);
    }
    return length;
}

int ReadArrayObjectSizeInBytes(Il2CppManagedMemorySnapshot* snapshot, std::uint64_t address, Il2CppMetadataType* arrayType,
                               const std::vector<Il2CppMetadataType*>& typeDescriptions) {
    auto arrayLength = ReadArrayLength(snapshot, address, arrayType);
    auto elementType = typeDescriptions[arrayType->baseOrElementTypeIndex];
    auto elementSize = ((elementType->flags & Il2CppMetadataTypeFlags::kValueType) != 0) ? elementType->size : snapshot->runtimeInformation.pointerSize;
    return static_cast<int>(snapshot->runtimeInformation.arrayHeaderSize + elementSize * static_cast<unsigned int>(arrayLength));
}

int ReadStringObjectSizeInBytes(BytesAndOffset& bo, Il2CppManagedMemorySnapshot* snapshot) {
    auto lengthPointer = bo.Add(snapshot->runtimeInformation.objectHeaderSize);
    auto length = lengthPointer.ReadInt32();
    return static_cast<std::int32_t>(snapshot->runtimeInformation.objectHeaderSize) + 1 + (length + 2) + 2;
}

void Crawler::Crawl(PackedCrawlerData& result, Il2CppManagedMemorySnapshot* snapshot) {
    std::vector<PackedManagedObject> managedObjects;
    std::vector<Connection> connections;
    for (std::uint32_t i = 0; i < snapshot->metadata.typeCount; i++) {
        auto type = &snapshot->metadata.types[i];
        type->typeIndex = i;
        typeInfoToTypeDescription_.emplace(type->typeInfoAddress, type);
        typeDescriptions_.push_back(type);
    }
    // crawl pointers
    for (std::uint32_t i = 0; i < snapshot->gcHandles.trackedObjectCount; i++) {
        auto gcHandle = snapshot->gcHandles.pointersToObjects[i];
        CrawlPointer(snapshot, result.startIndices_, gcHandle, result.startIndices_.OfFirstGCHandle() + i, connections, managedObjects);
    }
    // crawl raw object data
    for (std::size_t i = 0; i < result.typesWithStaticFields_.size(); i++) {
        auto typeDescription = result.typesWithStaticFields_[i];
        BytesAndOffset ba;
        ba.bytes_ = typeDescription->statics;
        ba.offset_ = 0;
        ba.pointerSize_ = snapshot->runtimeInformation.pointerSize;
        CrawlRawObjectData(snapshot, result.startIndices_, ba, typeDescription,
                           true, result.startIndices_.OfFirstStaticFields() + static_cast<std::uint32_t>(i), connections, managedObjects);
    }
    result.managedObjects_ = std::move(managedObjects);
    result.connections_ = std::move(connections);
    result.typeDescriptions_ = std::move(typeDescriptions_);
}

void Crawler::CrawlPointer(Il2CppManagedMemorySnapshot* snapshot, StartIndices startIndices, std::uint64_t pointer, std::uint32_t indexOfFrom,
                           std::vector<Connection>& outConnections, std::vector<PackedManagedObject>& outManagedObjects) {
    auto bo = FindInHeap(snapshot, pointer);
    if (!bo.IsValid())
        return;
    std::uint64_t typeInfoAddress;
    std::uint32_t indexOfObject;
    bool wasAlreadyCrawled;

    ParseObjectHeader(startIndices, snapshot, pointer, typeInfoAddress, indexOfObject, wasAlreadyCrawled, outManagedObjects);
    outConnections.push_back(Connection(indexOfFrom, indexOfObject));

    if (wasAlreadyCrawled)
        return;

    auto typeDescription = typeInfoToTypeDescription_[typeInfoAddress];
    if ((typeDescription->flags & Il2CppMetadataTypeFlags::kArray) == 0) {
        auto bo2 = bo.Add(snapshot->runtimeInformation.objectHeaderSize);
        CrawlRawObjectData(snapshot, startIndices, bo2, typeDescription, false, indexOfObject, outConnections, outManagedObjects);
        return;
    }
    auto arrayLen = ReadArrayLength(snapshot, pointer, typeDescription);
    auto elementType = typeDescriptions_[typeDescription->baseOrElementTypeIndex];
    auto cursor = bo.Add(snapshot->runtimeInformation.arrayHeaderSize);
    for (int i = 0; i != arrayLen; i++) {
        if ((elementType->flags & Il2CppMetadataTypeFlags::kValueType) != 0) {
            CrawlRawObjectData(snapshot, startIndices, cursor, elementType, false, indexOfObject, outConnections, outManagedObjects);
            cursor = cursor.Add(elementType->size);
        } else {
            CrawlPointer(snapshot, startIndices, cursor.ReadPointer(), indexOfObject, outConnections, outManagedObjects);
            cursor = cursor.NextPointer();
        }
    }
}

void Crawler::ParseObjectHeader(StartIndices& startIndices, Il2CppManagedMemorySnapshot* snapshot, std::uint64_t originalHeapAddress, std::uint64_t& typeInfoAddress,
                                std::uint32_t& indexOfObject, bool& wasAlreadyCrawled, std::vector<PackedManagedObject>& outManagedObjects) {
    auto bo = FindInHeap(snapshot, originalHeapAddress);
    auto pointer1 = bo.ReadPointer();
    auto pointer2 = bo.NextPointer();
    if ((pointer1 & 1) == 0) {
        wasAlreadyCrawled = false;
        indexOfObject = static_cast<std::uint32_t>(outManagedObjects.size() + startIndices.OfFirstManagedObject());
        typeInfoAddress = pointer1;
        auto typeDescription = typeInfoToTypeDescription_[pointer1];
        auto size = SizeOfObjectInBytes(typeDescription, bo, snapshot, originalHeapAddress);
        PackedManagedObject managedObj;
        managedObj.address_ = originalHeapAddress;
        managedObj.size_ = static_cast<std::uint32_t>(size);
        managedObj.typeIndex_ = typeDescription->typeIndex;
        outManagedObjects.push_back(managedObj);
        bo.WritePointer(pointer1 | 1);
        pointer2.WritePointer(static_cast<std::uint64_t>(indexOfObject));
        return;
    }
    typeInfoAddress = pointer1 & ~static_cast<std::uint64_t>(1);
    wasAlreadyCrawled = true;
    indexOfObject = static_cast<std::uint32_t>(pointer2.ReadPointer());
    return;
}

void AllFieldsOf(Il2CppMetadataType* typeDescription, std::vector<Il2CppMetadataType*>& typeDescriptions,
                 FieldFindOptions options, std::vector<Il2CppMetadataField*>& outFields) {
    std::vector<Il2CppMetadataType*> targetTypes = { typeDescription };
    while (!targetTypes.empty()) {
        auto curType = targetTypes.back();
        targetTypes.pop_back();
        if ((curType->flags & Il2CppMetadataTypeFlags::kArray) != 0)
            continue;
        // baseOrElementTypeIndex is Uint in unity source-code
        if (options != FieldFindOptions::OnlyStatic && curType->baseOrElementTypeIndex != static_cast<std::uint32_t>(-1)) {
            auto baseTypeDescription = typeDescriptions[curType->baseOrElementTypeIndex];
            targetTypes.push_back(baseTypeDescription);
        }
        for (std::uint32_t i = 0; i < curType->fieldCount; i++) {
            auto field = &curType->fields[i];
            if ((field->isStatic && options == FieldFindOptions::OnlyStatic) || (!field->isStatic && options == FieldFindOptions::OnlyInstance))
                outFields.push_back(field);
        }
    }
}

void Crawler::CrawlRawObjectData(Il2CppManagedMemorySnapshot* snapshot, StartIndices startIndices, BytesAndOffset bytesAndOffset,
                        Il2CppMetadataType* typeDescription, bool useStaticFields, std::uint32_t indexOfFrom,
                        std::vector<Connection>& outConnections, std::vector<PackedManagedObject>& outManagedObjects) {
    std::vector<Il2CppMetadataField*> fields;
    AllFieldsOf(typeDescription, typeDescriptions_, useStaticFields ? FieldFindOptions::OnlyStatic : FieldFindOptions::OnlyInstance, fields);
    for (auto& field : fields) {
        if (field->typeIndex == typeDescription->typeIndex && (typeDescription->flags & Il2CppMetadataTypeFlags::kValueType) != 0)
            continue;
        // field.offset is Uint in unity source-code
        if (field->offset == static_cast<std::uint32_t>(-1))
            continue;
        auto fieldType = typeDescriptions_[field->typeIndex];
        auto fieldLocation = bytesAndOffset.Add(field->offset - (useStaticFields ? 0 : snapshot->runtimeInformation.objectHeaderSize));
        if ((fieldType->flags & Il2CppMetadataTypeFlags::kValueType) != 0) {
            CrawlRawObjectData(snapshot, startIndices, fieldLocation, fieldType, false, indexOfFrom, outConnections, outManagedObjects);
            continue;
        }
        // temporary workaround for a bug in 5.3b4 and earlier where we would get literals returned as fields with offset 0. soon we'll be able to remove this code.
        if (fieldLocation.pointerSize_ == 4 || fieldLocation.pointerSize_ == 8) {
            CrawlPointer(snapshot, startIndices, fieldLocation.ReadPointer(), indexOfFrom, outConnections, outManagedObjects);
        }
    }
}

int Crawler::SizeOfObjectInBytes(Il2CppMetadataType* typeDescription, BytesAndOffset bo, Il2CppManagedMemorySnapshot* snapshot, std::uint64_t address) {
    if ((typeDescription->flags & Il2CppMetadataTypeFlags::kArray) != 0) {
        return ReadArrayObjectSizeInBytes(snapshot, address, typeDescription, typeDescriptions_);
    }
    if (QString(typeDescription->name) == "System.String") {
        return ReadStringObjectSizeInBytes(bo, snapshot);
    }
    return static_cast<int>(typeDescription->size);
}

void CrawledMemorySnapshot::Unpack(CrawledMemorySnapshot& result, Il2CppManagedMemorySnapshot* snapshot, PackedCrawlerData& packedCrawlerData) {
    result.runtimeInformation_ = snapshot->runtimeInformation;
    // managed heap
    result.managedHeap_.resize(snapshot->heap.sectionCount);
    for (std::size_t i = 0; i < snapshot->heap.sectionCount; i++) {
        auto section = &snapshot->heap.sections[i];
        auto newSection = &result.managedHeap_[i];
        newSection->sectionSize_ = section->sectionSize;
        newSection->sectionStartAddress_ = section->sectionStartAddress;
        newSection->sectionBytes_ = new std::uint8_t[section->sectionSize];
        memcpy(newSection->sectionBytes_, section->sectionBytes, section->sectionSize);
    }
    // convert typeDescriptions
    result.typeDescriptions_.resize(packedCrawlerData.typeDescriptions_.size());
    for (std::size_t i = 0; i < packedCrawlerData.typeDescriptions_.size(); i++) {
        auto& from = packedCrawlerData.typeDescriptions_[i];
        auto& to = result.typeDescriptions_[i];
        to.flags_ = from->flags;
        if ((to.flags_ & Il2CppMetadataTypeFlags::kArray) == 0) {
            to.fields_.resize(from->fieldCount);
            for (std::uint32_t j = 0; j < from->fieldCount; j++) {
                auto& fromField = from->fields[j];
                auto& toField = to.fields_[j];
                toField.name_ = QString::fromLocal8Bit(fromField.name);
                toField.offset_ = fromField.offset;
                toField.isStatic_ = fromField.isStatic;
                toField.typeIndex_ = fromField.typeIndex;
            }
            to.statics_ = new std::uint8_t[from->staticsSize];
            to.staticsSize_ = from->staticsSize;
            memcpy(to.statics_, from->statics, from->staticsSize);
        }
        to.baseOrElementTypeIndex_ = from->baseOrElementTypeIndex;
        to.name_ = QString::fromLocal8Bit(from->name);
        to.assemblyName_ = QString::fromLocal8Bit(from->assemblyName);
        to.typeInfoAddress_ = from->typeInfoAddress;
        to.size_ = from->size;
        to.typeIndex_ = from->typeIndex;
    }
    // unpack gchandle
    for (std::uint32_t i = 0; i < snapshot->gcHandles.trackedObjectCount; i++) {
        GCHandle handle;
        handle.size_ = snapshot->runtimeInformation.pointerSize;
        handle.caption_ = "gchandle";
        result.gcHandles_.push_back(handle);
    }
    // unpack statics
    for (auto type : packedCrawlerData.typesWithStaticFields_) {
        StaticFields field;
        field.typeDescription_ = &result.typeDescriptions_[type->typeIndex];
        field.caption_ = QString("static field of ") + type->name;
        field.size_ = type->staticsSize;
        result.staticFields_.push_back(field);
    }
    // unpack managed
    for (auto& managed : packedCrawlerData.managedObjects_) {
        ManagedObject mo;
        mo.address_ = managed.address_;
        mo.size_ = managed.size_;
        mo.typeDescription_ = &result.typeDescriptions_[managed.typeIndex_];
        mo.caption_ = mo.typeDescription_->name_;
        result.managedObjects_.push_back(mo);
    }
    // combine
    result.allObjects_.reserve(result.gcHandles_.size() + result.staticFields_.size() + result.managedObjects_.size());
    std::uint32_t index = 0;
    for (auto& obj : result.gcHandles_) {
        obj.index_ = index++;
        result.allObjects_.push_back(&obj);
    }
    for (auto& obj : result.staticFields_) {
        obj.index_ = index++;
        obj.nameHash_ = qHash(obj.typeDescription_->assemblyName_ + obj.caption_);
        result.allObjects_.push_back(&obj);
    }
    for (auto& obj : result.managedObjects_) {
        obj.index_ = index++;
        result.allObjects_.push_back(&obj);
    }
    // connections
    std::vector<std::vector<ThingInMemory*>> referencesLists(result.allObjects_.size());
    std::vector<std::vector<ThingInMemory*>> referencedByLists(result.allObjects_.size());
    for (auto& connection : packedCrawlerData.connections_) {
        referencesLists[connection.from_].push_back(result.allObjects_[connection.to_]);
        referencedByLists[connection.to_].push_back(result.allObjects_[connection.from_]);
    }
    for (std::size_t i = 0; i != result.allObjects_.size(); i++) {
        result.allObjects_[i]->references_ = std::move(referencesLists[i]);
        result.allObjects_[i]->referencedBy_ = std::move(referencedByLists[i]);
    }
}

BytesAndOffset CrawledMemorySnapshot::FindInHeap(const CrawledMemorySnapshot* snapshot, std::uint64_t addr) {
    BytesAndOffset ba;
    for (std::size_t i = 0; i < snapshot->managedHeap_.size(); i++) {
        auto section = snapshot->managedHeap_[i];
        if (addr >= section.sectionStartAddress_ && addr < (section.sectionStartAddress_ + static_cast<std::uint64_t>(section.sectionSize_))) {
            ba.bytes_ = section.sectionBytes_;
            ba.offset_ = addr - section.sectionStartAddress_;
            ba.pointerSize_ = snapshot->runtimeInformation_.pointerSize;
            break;
        }
    }
    return ba;
}

QString CrawledMemorySnapshot::ReadString(const CrawledMemorySnapshot* snapshot, const BytesAndOffset& bo) {
    if (!bo.IsValid())
        return QString();
    auto lengthPointer = bo.Add(snapshot->runtimeInformation_.objectHeaderSize);
    auto length = lengthPointer.ReadInt32();
    auto firstChar = lengthPointer.Add(4);
    return QString::fromUtf16(reinterpret_cast<std::uint16_t*>(firstChar.bytes_ + firstChar.offset_), length);
}

int CrawledMemorySnapshot::ReadArrayLength(const CrawledMemorySnapshot* snapshot, std::uint64_t address, TypeDescription* arrayType) {
    auto bo = FindInHeap(snapshot, address);
    auto bounds = bo.Add(snapshot->runtimeInformation_.arrayBoundsOffsetInHeader).ReadPointer();
    if (bounds == 0)
        return bo.Add(snapshot->runtimeInformation_.arraySizeOffsetInHeader).ReadInt32();
    auto cursor = FindInHeap(snapshot, bounds);
    int length = 1;
    int arrayRank = static_cast<int>(arrayType->flags_ & Il2CppMetadataTypeFlags::kArrayRankMask) >> 16;
    for (int i = 0; i < arrayRank; i++) {
        length *= cursor.ReadInt32();
        cursor = cursor.Add(8);
    }
    return length;
}

void CrawledMemorySnapshot::AllFieldsOf(const CrawledMemorySnapshot* snapshot, const TypeDescription* typeDescription,
                                        FieldFindOptions options, std::vector<const FieldDescription*>& outFields) {
    std::vector<const TypeDescription*> targetTypes = { typeDescription };
    while (!targetTypes.empty()) {
        auto curType = targetTypes.back();
        targetTypes.pop_back();
        if ((curType->flags_ & Il2CppMetadataTypeFlags::kArray) != 0)
            continue;
        // baseOrElementTypeIndex is Uint in unity source-code
        if (options != FieldFindOptions::OnlyStatic && curType->baseOrElementTypeIndex_ != static_cast<std::uint32_t>(-1)) {
            auto baseTypeDescription = &snapshot->typeDescriptions_[curType->baseOrElementTypeIndex_];
            targetTypes.push_back(baseTypeDescription);
        }
        for (std::size_t i = 0; i < curType->fields_.size(); i++) {
            auto field = &curType->fields_[i];
            if ((field->isStatic_ && options == FieldFindOptions::OnlyStatic) || (!field->isStatic_ && options == FieldFindOptions::OnlyInstance))
                outFields.push_back(field);
        }
    }
}

CrawledMemorySnapshot* CrawledMemorySnapshot::Clone(const CrawledMemorySnapshot* src) {
    auto clone = new CrawledMemorySnapshot();
    clone->runtimeInformation_ = src->runtimeInformation_;
    // managed heap
    clone->managedHeap_.resize(src->managedHeap_.size());
    for (std::size_t i = 0; i < src->managedHeap_.size(); i++) {
        auto section = &src->managedHeap_[i];
        auto newSection = &clone->managedHeap_[i];
        newSection->sectionSize_ = section->sectionSize_;
        newSection->sectionStartAddress_ = section->sectionStartAddress_;
        newSection->sectionBytes_ = new std::uint8_t[section->sectionSize_];
        memcpy(newSection->sectionBytes_, section->sectionBytes_, section->sectionSize_);
    }
    // typeDescriptions
    clone->typeDescriptions_.reserve(src->typeDescriptions_.size());
    for (auto& type : src->typeDescriptions_)
        clone->typeDescriptions_.push_back(TypeDescription(type));
    // gchandle
    clone->gcHandles_.reserve(src->gcHandles_.size());
    for (auto& gchandle : src->gcHandles_) {
        clone->gcHandles_.push_back(GCHandle(gchandle));
    }
    // statics
    clone->staticFields_.reserve(src->staticFields_.size());
    for (auto& staticFields : src->staticFields_) {
        clone->staticFields_.push_back(StaticFields(staticFields));
        auto& newStaticFields = clone->staticFields_.back();
        newStaticFields.typeDescription_ = &clone->typeDescriptions_[staticFields.typeDescription_->typeIndex_];
        newStaticFields.nameHash_ = staticFields.nameHash_;
    }
    clone->managedObjects_.reserve(src->managedObjects_.size());
    for (auto& managed : src->managedObjects_) {
        clone->managedObjects_.push_back(ManagedObject(managed));
        auto& newManaged = clone->managedObjects_.back();
        newManaged.address_ = managed.address_;
        newManaged.typeDescription_ = &clone->typeDescriptions_[managed.typeDescription_->typeIndex_];
    }
    // combine
    clone->allObjects_.reserve(src->allObjects_.size());
    std::uint32_t index = 0;
    for (auto& obj : clone->gcHandles_) {
        obj.index_ = index++;
        clone->allObjects_.push_back(&obj);
    }
    for (auto& obj : clone->staticFields_) {
        obj.index_ = index++;
        clone->allObjects_.push_back(&obj);
    }
    for (auto& obj : clone->managedObjects_) {
        obj.index_ = index++;
        clone->allObjects_.push_back(&obj);
    }
    // connections
    for (std::size_t i = 0; i < clone->allObjects_.size(); i++) {
        auto cloneObj = clone->allObjects_[i];
        auto secondObj = src->allObjects_[i];
        cloneObj->references_.reserve(secondObj->references_.size());
        for (auto& ref : secondObj->references_)
            cloneObj->references_.push_back(clone->allObjects_[ref->index_]);
        cloneObj->referencedBy_.reserve(secondObj->referencedBy_.size());
        for (auto& ref : secondObj->referencedBy_)
            cloneObj->referencedBy_.push_back(clone->allObjects_[ref->index_]);
    }
    return clone;
}

CrawledMemorySnapshot* CrawledMemorySnapshot::Diff(const CrawledMemorySnapshot* firstSnapshot, const CrawledMemorySnapshot* secondSnapshot) {
    auto diffed = CrawledMemorySnapshot::Clone(secondSnapshot);
    // managed
    std::unordered_map<std::uint64_t, const ManagedObject*> firstManagedObjects;
    for (auto& managed : firstSnapshot->managedObjects_) {
        firstManagedObjects[managed.address_] = &managed;
    }
    for (auto& managed : diffed->managedObjects_) {
        auto it = firstManagedObjects.find(managed.address_);
        if (it != firstManagedObjects.end()) {
            auto firstManaged = it->second;
            managed.size_ -= firstManaged->size_;
            if (managed.size_ == 0)
                managed.diff_ = CrawledDiffFlags::kSame;
            else if (managed.size_ > 0)
                managed.diff_ = CrawledDiffFlags::kBigger;
            else
                managed.diff_ = CrawledDiffFlags::kSmaller;
        } else {
            managed.diff_ = CrawledDiffFlags::kAdded;
        }
    }
    // statics
    std::unordered_map<std::uint64_t, const StaticFields*> firstStaticFields;
    for (auto& statics : firstSnapshot->staticFields_) {
        firstStaticFields[statics.nameHash_] = &statics;
    }
    for (auto& statics : diffed->staticFields_) {
        auto it = firstStaticFields.find(statics.nameHash_);
        if (it != firstStaticFields.end()) {
            auto firstStatics = it->second;
            statics.size_ -= firstStatics->size_;
            if (statics.size_ == 0)
                statics.diff_ = CrawledDiffFlags::kSame;
            else if (statics.size_ > 0)
                statics.diff_ = CrawledDiffFlags::kBigger;
            else
                statics.diff_ = CrawledDiffFlags::kSmaller;
        } else {
            statics.diff_ = CrawledDiffFlags::kAdded;
        }
    }
    diffed->name_ = "Diff_" + QTime::currentTime().toString("H_m_s");
    diffed->isDiff_ = true;
    return diffed;
}

void CrawledMemorySnapshot::Free(CrawledMemorySnapshot* snapshot) {
    for (auto& section : snapshot->managedHeap_) {
        if (section.sectionSize_ > 0)
            delete[] section.sectionBytes_;
    }
    for (auto& type : snapshot->typeDescriptions_) {
        if (type.staticsSize_ > 0)
            delete[] type.statics_;
    }
}
