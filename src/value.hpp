#ifndef TOY_VALUE_HPP
#define TOY_VALUE_HPP

#include <cstdint>
#include <cstddef>

struct ToyObject;

// Tagged Value representation:
// - Small Integer: LSB is 1. (value << 1) | 1. 63-bit signed integer.
// - Object Pointer: LSB is 0, non-zero. Pointing to ToyObject (8-byte aligned).
// - Null: 0.
typedef uintptr_t Value;

inline bool is_int(Value v) {
    return (v & 1) != 0;
}

inline int64_t decode_int(Value v) {
    return static_cast<int64_t>(static_cast<intptr_t>(v) >> 1);
}

inline Value encode_int(int64_t n) {
    return static_cast<Value>((static_cast<uintptr_t>(n) << 1) | 1);
}

inline bool is_obj(Value v) {
    return ((v & 1) == 0) && (v != 0);
}

inline ToyObject* decode_obj(Value v) {
    return reinterpret_cast<ToyObject*>(v);
}

inline Value encode_obj(ToyObject* obj) {
    return reinterpret_cast<Value>(obj);
}

inline bool is_null(Value v) {
    return v == 0;
}

inline Value make_null() {
    return 0;
}

inline bool is_truthy(Value v) {
    if (is_int(v)) {
        return decode_int(v) != 0;
    } else if (is_obj(v)) {
        return decode_obj(v) != nullptr;
    }
    return false;
}

#endif // TOY_VALUE_HPP
