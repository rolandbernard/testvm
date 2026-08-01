#ifndef TOY_CLASS_HPP
#define TOY_CLASS_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "value.hpp"

struct InlineCache;

typedef Value (*JitFunctionPtr)(void* vm_ptr, Value receiver, const Value* args);

struct Method {
    std::string name;
    std::vector<std::string> params;
    uint32_t num_locals = 0;
    std::vector<uint8_t> bytecode;
    std::vector<Value> constants;
    std::vector<InlineCache> inline_caches;
    uint32_t invocation_count = 0;
    JitFunctionPtr jit_entry = nullptr;
    bool is_compiling = false;
};

struct Class {
    std::string name;
    uint32_t class_id = 0;
    std::vector<std::string> field_names;
    std::unordered_map<std::string, uint32_t> field_indices;
    std::unordered_map<std::string, Method*> methods;

    int32_t get_field_index(const std::string& fieldName) const {
        auto it = field_indices.find(fieldName);
        if (it != field_indices.end()) {
            return static_cast<int32_t>(it->second);
        }
        return -1;
    }

    Method* lookup_method(const std::string& methodName) const {
        auto it = methods.find(methodName);
        if (it != methods.end()) {
            return it->second;
        }
        return nullptr;
    }
};

#endif // TOY_CLASS_HPP
