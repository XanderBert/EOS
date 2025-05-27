#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "defines.h"
#include "handle.h"

//TODO: Make objects split up in Hot and Cold code path
//So we don't need to pass around the whole object when we only need half of it.
//We have function to only get the hot / cold object or both with the same handle
//std::vector<PoolEntry> HotObjects;
//std::vector<PoolEntry> ColdObjects;

//TODO: this will perform runtime copies and move if we create things at runtime with this pool

//TODO: The current free list uses a linked list, For very large pools use a stack-based free list (std::vector<uint32_t> FreeIndices) for O(1) access.
namespace EOS
{
    template<typename ObjectType, typename ObjectType_Impl>
    class Pool final
    {
    private:
        struct PoolEntry
        {
            explicit PoolEntry(ObjectType_Impl& object)
            : Object(std::move(object)) {}

            ObjectType_Impl Object{};
            uint32_t Generation     = 1;
            uint32_t NextFree       = ListEnd;
        };

    public:
        explicit Pool(uint32_t initialReserve = 10);
        ~Pool() = default;
        DELETE_COPY_MOVE(Pool)

        //Create object of the templated ObjectType
        [[nodiscard]] Handle<ObjectType> Create(ObjectType_Impl&& object);

        // Batch-create objects, returning handles for all created objects
        template<typename Iterator>
        [[nodiscard]] std::vector<Handle<ObjectType>> CreateBatch(Iterator first, Iterator last);

        //Destroy the given object
        void Destroy(Handle<ObjectType> handle);

        //Get the given implementation
        [[nodiscard]] ObjectType_Impl* Get(const Handle<ObjectType> handle);
        [[nodiscard]] const ObjectType_Impl* Get(const Handle<ObjectType> handle) const;

        //Get a handle to the object at position index
        [[nodiscard]] Handle<ObjectType> GetHandle(uint32_t index) const;

        //search for the handle of the object based on the pointer
        [[nodiscard]] Handle<ObjectType> FindObject(const ObjectType_Impl* object);

        //Clear the pool. All handles to objects become stale
        void Clear();

        //Returns the number of objects
        [[nodiscard]] uint32_t NumObjects() const;

        //Tries to reserve a the amount.
        void Reserve(uint32_t capacity);



        std::vector<PoolEntry> Objects;
        
    private:
        //It’s an invalid index (since Objects_size() can’t reach 2^32 - 1), making it a safe "end" marker.
        static constexpr uint32_t ListEnd = 0xFFFFFFFF;
        uint32_t FreeListHead = ListEnd;
        uint32_t FreeList{};
        uint32_t NumberOfObjects{};
    };


    template<typename ObjectType, typename ObjectType_Impl>
    Pool<ObjectType, ObjectType_Impl>::Pool(const uint32_t initialReserve)
    {
        Reserve(initialReserve);
    }

    template<typename ObjectType, typename ObjectType_Impl>
    Handle<ObjectType> Pool<ObjectType, ObjectType_Impl>::Create(ObjectType_Impl &&object)
    {
        uint32_t index{};

        //If the pool has a free slot
        if (FreeListHead != ListEnd)
        {
            index = FreeListHead;
            FreeListHead = Objects[index].NextFree;
            Objects[index].Object = std::move(object);
        }

        //Else if the pool doesn't have a free slot

        else
        {
#if defined(EOS_DEBUG)
            size_t oldCapacity = Objects.capacity();
#endif
            index = static_cast<uint32_t>(Objects.size());
            Objects.emplace_back(object);



#if defined(EOS_DEBUG)
            //Log only in debug, whenever we do reallocations,
            //This can be interesting to tweak the initial pool size to avoid as much runtime reallocations as possible
            if (Objects.capacity() != oldCapacity)
            {
                //TODO: What we can do is: write to a file in the destructor what the biggest capacity was of this pool.
                //Next time we try to read from that file and initialize our pool with that size.
                EOS::Logger->warn("Pool did reallocation, Old Capacity:{} , New Capacity:{}", oldCapacity, Objects.capacity());
            }
#endif
        }

        //increase the objects and return a handle to the Object in the pool
        ++NumberOfObjects;
        return Handle<ObjectType>(index, Objects[index].Generation);
    }

    template<typename ObjectType, typename ObjectType_Impl>
    template<typename Iterator>
    std::vector<Handle<ObjectType>> Pool<ObjectType, ObjectType_Impl>::CreateBatch(Iterator first, Iterator last)
    {
        std::vector<Handle<ObjectType>> handles;
        const size_t batchSize = std::distance(first, last);
        if (batchSize == 0) return handles;

        //reserve the amount
        handles.reserve(batchSize);

        // Reuse free slots first
        size_t reused = 0;
        while (FreeListHead != ListEnd && reused < batchSize)
        {
            const uint32_t index = FreeListHead;
            FreeListHead = Objects[index].NextFree;
            Objects[index].Object = std::move(*first++);
            handles.emplace_back(Handle<ObjectType>(index, Objects[index].Generation));
            ++NumberOfObjects;
            ++reused;
        }

        // Allocate new entries for remaining objects
        const size_t remaining = batchSize - reused;
        if (remaining > 0)
        {
            const size_t currentSize = Objects.size();
            Reserve(currentSize + remaining); // Reserve in one shot

            for (size_t i{}; i < remaining; ++i, ++first)
            {
                Objects.emplace_back(PoolEntry(std::move(*first)));
                handles.emplace_back(Handle<ObjectType>(static_cast<uint32_t>(currentSize + i),Objects[currentSize + i].Generation));
                ++NumberOfObjects;
            }
        }

        return handles;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    void Pool<ObjectType, ObjectType_Impl>::Destroy(Handle<ObjectType> handle)
    {
        if (handle.Empty()) { return; }

        // (this one could already be deleted)
        CHECK(NumberOfObjects > 0, "There are no objects left in the pool");

        const uint32_t index = handle.Index();
        CHECK(index < Objects.size(), "The index is bigger then the amount of objects in the pool");

        //Check if the version in the pool is the same as the version we are referencing
        CHECK(handle.Gen() == Objects[index].Generation, "The generation of the handle is not the same as the one in the pool");

        //Reset to a default state
        Objects[index].Object = ObjectType_Impl{};

        //Increase the amount it has been reused (generation)
        ++Objects[index].Generation;

        //Update the next free pool object in this object
        Objects[index].NextFree = FreeListHead;

        //markt this object as free
        FreeListHead = index;

        //reduce the number of in use objects
        --NumberOfObjects;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    ObjectType_Impl* Pool<ObjectType, ObjectType_Impl>::Get(const Handle<ObjectType> handle)
    {
        if (handle.Empty()) { return nullptr; }

        const uint32_t index = handle.Index();
        CHECK(index < Objects.size(), "The index is bigger then the amount of objects in the pool");

        //Check if the version in the pool is the same as the version we are referencing
        CHECK(handle.Gen() == Objects[index].Generation, "The generation of the handle is not the same as the one in the pool");

        return &Objects[index].Object;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    const ObjectType_Impl* Pool<ObjectType, ObjectType_Impl>::Get(const Handle<ObjectType> handle) const
    {
        if (handle.Empty()) { return nullptr; }

        const uint32_t index = handle.Index();
        CHECK(index < Objects.size(), "The index is bigger then the amount of objects in the pool");

        //Check if the version in the pool is the same as the version we are referencing
        CHECK(handle.Gen() == Objects[index].Generation, "The generation of the handle is not the same as the one in the pool");

        return &Objects[index].Object;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    Handle<ObjectType> Pool<ObjectType, ObjectType_Impl>::GetHandle(uint32_t index) const
    {
        CHECK(index < Objects.size(), "The index is bigger then the amount of objects in the pool");
        if (index >= Objects.size()) { return {}; }

        return Handle<ObjectType>(index, Objects[index].Generation);
    }

    template<typename ObjectType, typename ObjectType_Impl>
    Handle<ObjectType> Pool<ObjectType, ObjectType_Impl>::FindObject(const ObjectType_Impl *object)
    {
        if (!object) { return {}; }

        for (size_t idx{}; idx != Objects.size(); ++idx)
        {
            if (Objects[idx].Object == *object)
            {
                return Handle<ObjectType>(static_cast<uint32_t>(idx), Objects[idx].Generation);
            }
        }

        return {};
    }

    template<typename ObjectType, typename ObjectType_Impl>
    void Pool<ObjectType, ObjectType_Impl>::Clear()
    {
        Objects.clear();
        FreeListHead = ListEnd;
        NumberOfObjects = 0;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    uint32_t Pool<ObjectType, ObjectType_Impl>::NumObjects() const
    {
        return NumberOfObjects;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    void Pool<ObjectType, ObjectType_Impl>::Reserve(uint32_t capacity)
    {
        Objects.reserve(capacity);
    }
}