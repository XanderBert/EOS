#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <vector>

#include "defines.h"
#include "handle.h"

//TODO: Make objects split up in Hot and Cold code path
//So we don't need to pass around the whole object when we only need half of it.
//We have function to only get the hot / cold object or both with the same handle
//std::vector<PoolEntry> HotObjects;
//std::vector<PoolEntry> ColdObjects;

//TODO: The current free list uses a linked list, For very large pools use a stack-based free list (std::vector<uint32_t> FreeIndices) for O(1) access.
namespace EOS
{
    // A handle based object pool with stable object addresses.
    //
    // The backing store is a paged array rather than a std::vector on purpose. Get() hands out a pointer
    // straight into the storage, and callers keep that pointer alive across other pool calls.
    // A vector reallocates when it grows past its capacity, which would turn every outstanding pointer into
    // a dangling one, so growing the pool would silently break unrelated code. Growing here allocates a new
    // page instead and never touches the pages that already exist, so a pointer stays valid until that
    // object is destroyed or the pool is cleared.
    template<typename ObjectType, typename ObjectType_Impl>
    class Pool final
    {
    private:
        struct PoolEntry
        {
            PoolEntry() = default;

            explicit PoolEntry(ObjectType_Impl& object)
            : Object(std::move(object)) {}

            ObjectType_Impl Object{};
            uint32_t Generation     = 1;
            uint32_t NextFree       = ListEnd;
        };

        //Round down so that the page index and the offset inside the page are a shift and a mask.
        static constexpr uint32_t RoundDownToPowerOfTwo(uint32_t value)
        {
            uint32_t result = 1;
            while (result <= value / 2) { result *= 2; }
            return result;
        }

        //Aim for a page of roughly this many bytes, but never fewer than MinEntriesPerPage entries, so that
        //pools of large objects still get pages worth walking rather than one entry each.
        static constexpr uint32_t TargetPageBytes   = 16 * 1024;
        static constexpr uint32_t MinEntriesPerPage = 16;
        static constexpr uint32_t EntriesPerPage    = RoundDownToPowerOfTwo(std::max(static_cast<uint32_t>(TargetPageBytes / sizeof(PoolEntry)), MinEntriesPerPage));
        static constexpr uint32_t PageIndexShift    = [] { uint32_t shift = 0; while ((1u << shift) != EntriesPerPage) { ++shift; } return shift; }();
        static constexpr uint32_t PageOffsetMask    = EntriesPerPage - 1;

        using Page = std::array<PoolEntry, EntriesPerPage>;

        // Iterates the objects themselves instead of the entries wrapping them, so that PoolEntry can stay
        // an implementation detail while the pool is still usable in a range based for loop.
        template<typename PoolPointer, typename ValueType>
        class ObjectIterator final
        {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = ValueType;
            using difference_type   = std::ptrdiff_t;
            using pointer           = ValueType*;
            using reference         = ValueType&;

            ObjectIterator() = default;
            ObjectIterator(PoolPointer owner, uint32_t index) : Owner(owner), Index(index) {}

            reference operator*() const     { return Owner->At(Index); }
            pointer operator->() const      { return &Owner->At(Index); }

            ObjectIterator& operator++()    { ++Index; return *this; }
            ObjectIterator operator++(int)  { ObjectIterator previous = *this; ++Index; return previous; }

            bool operator==(const ObjectIterator& other) const { return Owner == other.Owner && Index == other.Index; }
            bool operator!=(const ObjectIterator& other) const { return !(*this == other); }

        private:
            PoolPointer Owner = nullptr;
            uint32_t Index    = 0;
        };

    public:
        using Iterator      = ObjectIterator<Pool*, ObjectType_Impl>;
        using ConstIterator = ObjectIterator<const Pool*, const ObjectType_Impl>;

        Pool() = default;
        ~Pool() = default;
        DELETE_COPY_MOVE(Pool)

        //Create object of the templated ObjectType
        [[nodiscard]] Handle<ObjectType> Create(ObjectType_Impl&& object);

        // Batch-create objects, returning handles for all created objects
        template<typename InputIterator>
        [[nodiscard]] std::vector<Handle<ObjectType>> CreateBatch(InputIterator first, InputIterator last);

        //Destroy the given object
        void Destroy(Handle<ObjectType> handle);

        //Get the given implementation. The pointer stays valid until this object is destroyed or the pool is cleared.
        [[nodiscard]] ObjectType_Impl* Get(const Handle<ObjectType> handle);
        [[nodiscard]] const ObjectType_Impl* Get(const Handle<ObjectType> handle) const;

        //Get a handle to the object at position index
        [[nodiscard]] Handle<ObjectType> GetHandle(uint32_t index) const;

        //search for the handle of the object based on the pointer
        [[nodiscard]] Handle<ObjectType> FindObject(const ObjectType_Impl* object);

        //Clear the pool. All handles to objects become stale
        void Clear();

        //Returns the number of objects that are currently alive
        [[nodiscard]] uint32_t NumObjects() const;

        //Returns the number of slots, which includes slots whose object has been destroyed.
        //Handles index into this range, so this is the size that an array indexed by handle needs to have.
        [[nodiscard]] size_t NumSlots() const;

        //Access a slot by its raw index instead of by handle. A slot whose object was destroyed holds a
        //default constructed object, so iterating or indexing the pool can hand back objects that are not alive.
        [[nodiscard]] ObjectType_Impl& At(uint32_t index);
        [[nodiscard]] const ObjectType_Impl& At(uint32_t index) const;

        //Iterate every slot in index order, destroyed slots included. See At() for what those slots hold.
        [[nodiscard]] Iterator begin()              { return Iterator(this, 0); }
        [[nodiscard]] Iterator end()                { return Iterator(this, NumberOfSlots); }
        [[nodiscard]] ConstIterator begin() const   { return ConstIterator(this, 0); }
        [[nodiscard]] ConstIterator end() const     { return ConstIterator(this, NumberOfSlots); }

    private:
        //It’s an invalid index (since the slot count can’t reach 2^32 - 1), making it a safe "end" marker.
        static constexpr uint32_t ListEnd = 0xFFFFFFFF;

        [[nodiscard]] PoolEntry& EntryAt(uint32_t index)                { return (*Pages[index >> PageIndexShift])[index & PageOffsetMask]; }
        [[nodiscard]] const PoolEntry& EntryAt(uint32_t index) const    { return (*Pages[index >> PageIndexShift])[index & PageOffsetMask]; }

        //Append a slot, allocating a page when the last one is full. Existing pages are never touched, which
        //is what keeps the pointers handed out by Get() valid.
        [[nodiscard]] uint32_t AppendSlot(ObjectType_Impl&& object);

        std::vector<std::unique_ptr<Page>> Pages;
        uint32_t NumberOfSlots{};
        uint32_t FreeListHead = ListEnd;
        uint32_t NumberOfObjects{};
    };

    template<typename ObjectType, typename ObjectType_Impl>
    uint32_t Pool<ObjectType, ObjectType_Impl>::AppendSlot(ObjectType_Impl&& object)
    {
        if (NumberOfSlots == Pages.size() * EntriesPerPage)
        {
            Pages.emplace_back(std::make_unique<Page>());
        }

        const uint32_t index = NumberOfSlots++;
        EntryAt(index).Object = std::move(object);
        return index;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    Handle<ObjectType> Pool<ObjectType, ObjectType_Impl>::Create(ObjectType_Impl &&object)
    {
        uint32_t index{};

        //If the pool has a free slot
        if (FreeListHead != ListEnd)
        {
            index = FreeListHead;
            FreeListHead = EntryAt(index).NextFree;
            EntryAt(index).Object = std::move(object);
        }

        //Else if the pool doesn't have a free slot
        else
        {
            index = AppendSlot(std::move(object));
        }

        //increase the objects and return a handle to the Object in the pool
        ++NumberOfObjects;
        return Handle<ObjectType>(index, EntryAt(index).Generation);
    }

    template<typename ObjectType, typename ObjectType_Impl>
    template<typename InputIterator>
    std::vector<Handle<ObjectType>> Pool<ObjectType, ObjectType_Impl>::CreateBatch(InputIterator first, InputIterator last)
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
            FreeListHead = EntryAt(index).NextFree;
            EntryAt(index).Object = std::move(*first++);
            handles.emplace_back(Handle<ObjectType>(index, EntryAt(index).Generation));
            ++NumberOfObjects;
            ++reused;
        }

        // Allocate new entries for remaining objects
        for (size_t remaining = batchSize - reused; remaining > 0; --remaining, ++first)
        {
            const uint32_t index = AppendSlot(std::move(*first));
            handles.emplace_back(Handle<ObjectType>(index, EntryAt(index).Generation));
            ++NumberOfObjects;
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
        CHECK(index < NumberOfSlots, "The index is bigger then the amount of objects in the pool");

        //Check if the version in the pool is the same as the version we are referencing
        CHECK(handle.Gen() == EntryAt(index).Generation, "The generation of the handle is not the same as the one in the pool");

        //Reset to a default state
        EntryAt(index).Object = ObjectType_Impl{};

        //Increase the amount it has been reused (generation)
        ++EntryAt(index).Generation;

        //Update the next free pool object in this object
        EntryAt(index).NextFree = FreeListHead;

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
        CHECK(index < NumberOfSlots, "The index: {} is bigger then the amount of objects in the pool: {}", index, NumberOfSlots);

        //Check if the version in the pool is the same as the version we are referencing
        CHECK(handle.Gen() == EntryAt(index).Generation, "The generation of the handle is not the same as the one in the pool");

        return &EntryAt(index).Object;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    const ObjectType_Impl* Pool<ObjectType, ObjectType_Impl>::Get(const Handle<ObjectType> handle) const
    {
        if (handle.Empty()) { return nullptr; }

        const uint32_t index = handle.Index();
        CHECK(index < NumberOfSlots, "The index is bigger then the amount of objects in the pool");

        //Check if the version in the pool is the same as the version we are referencing
        CHECK(handle.Gen() == EntryAt(index).Generation, "The generation of the handle is not the same as the one in the pool");

        return &EntryAt(index).Object;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    Handle<ObjectType> Pool<ObjectType, ObjectType_Impl>::GetHandle(uint32_t index) const
    {
        CHECK(index < NumberOfSlots, "The index is bigger then the amount of objects in the pool");
        if (index >= NumberOfSlots) { return {}; }

        return Handle<ObjectType>(index, EntryAt(index).Generation);
    }

    template<typename ObjectType, typename ObjectType_Impl>
    Handle<ObjectType> Pool<ObjectType, ObjectType_Impl>::FindObject(const ObjectType_Impl *object)
    {
        if (!object) { return {}; }

        for (uint32_t idx{}; idx != NumberOfSlots; ++idx)
        {
            if (EntryAt(idx).Object == *object)
            {
                return Handle<ObjectType>(idx, EntryAt(idx).Generation);
            }
        }

        return {};
    }

    template<typename ObjectType, typename ObjectType_Impl>
    void Pool<ObjectType, ObjectType_Impl>::Clear()
    {
        Pages.clear();
        NumberOfSlots = 0;
        FreeListHead = ListEnd;
        NumberOfObjects = 0;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    uint32_t Pool<ObjectType, ObjectType_Impl>::NumObjects() const
    {
        return NumberOfObjects;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    size_t Pool<ObjectType, ObjectType_Impl>::NumSlots() const
    {
        return NumberOfSlots;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    ObjectType_Impl& Pool<ObjectType, ObjectType_Impl>::At(uint32_t index)
    {
        CHECK(index < NumberOfSlots, "The index: {} is bigger then the amount of objects in the pool: {}", index, NumberOfSlots);
        return EntryAt(index).Object;
    }

    template<typename ObjectType, typename ObjectType_Impl>
    const ObjectType_Impl& Pool<ObjectType, ObjectType_Impl>::At(uint32_t index) const
    {
        CHECK(index < NumberOfSlots, "The index: {} is bigger then the amount of objects in the pool: {}", index, NumberOfSlots);
        return EntryAt(index).Object;
    }
}
