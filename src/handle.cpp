#include "handle.h"

namespace EOS
{
    template<typename ObjectType>
    bool Handle<ObjectType>::operator==(const Handle<ObjectType> &other) const
    {
        return Idx == other.Idx && Generation == other.Generation;
    }

    template<typename ObjectType>
    bool Handle<ObjectType>::operator!=(const Handle<ObjectType> &other) const
    {
        return Idx != other.Idx || Generation != other.Generation;
    }
}