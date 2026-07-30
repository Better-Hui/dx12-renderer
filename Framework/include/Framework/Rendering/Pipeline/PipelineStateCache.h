//Modify Begin:2026-07-27 by BestHui
#pragma once

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

template<typename Key, typename Value, typename Hasher = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
class PipelineStateCache
{
public:
    template<typename CreateFunc>
    Value GetOrCreate(const Key& key, CreateFunc&& createFunc)
    {
        const auto findResult = m_Cache.find(key);
        if (findResult != m_Cache.end())
        {
            return findResult->second;
        }

        Value value = createFunc();
        m_Cache.emplace(key, value);
        return value;
    }

    void Clear()
    {
        m_Cache.clear();
    }

    size_t Size() const
    {
        return m_Cache.size();
    }

private:
    std::unordered_map<Key, Value, Hasher, KeyEqual> m_Cache;
};
//Modify End
