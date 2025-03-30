#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "defines.h"
#include "handle.h"

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
        Pool() = default;
        ~Pool() = default;
        DELETE_COPY_MOVE(Pool)

        std::vector<PoolEntry> Objects;

        //Create object of the templated ObjectType
        [[nodiscard]] inline Handle<ObjectType> Create(ObjectType_Impl&& object)
        {
            uint32_t index{};

            //If the pool is already in use
            if (FreeListHead != ListEnd)
            {
                index = FreeListHead;
                FreeListHead = Objects[index].nextFree_;
                Objects[index].Object = std::move(object);
            }

            //Else if the pool has never been used
            else
            {
                index = static_cast<uint32_t>(Objects.size());
                Objects.emplace_back(object);
            }

            //increase the objects and return a handle to the Object in the pool
            NumberOfObjects++;
            return Handle<ObjectType>(index, Objects[index].gen_);
        }


    private:
        //It’s an invalid index (since Objects_size() can’t reach 2^32 - 1), making it a safe "end" marker.
        static constexpr uint32_t ListEnd = 0xFFFFFFFF;
        uint32_t FreeListHead = ListEnd;
        uint32_t FreeList{};
        uint32_t NumberOfObjects{};
    };
}
