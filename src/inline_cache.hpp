#ifndef TOY_INLINE_CACHE_HPP
#define TOY_INLINE_CACHE_HPP

#include <string>
#include <cstdint>

struct Class;
struct Method;

struct InlineCache {
    uint32_t ic_id = 0;
    std::string method_name;
    Class* cached_class = nullptr;
    Method* cached_method = nullptr;
    uint32_t hit_count = 0;
    uint32_t miss_count = 0;
    bool is_megamorphic = false;
};

#endif // TOY_INLINE_CACHE_HPP
