#ifndef TOY_OBJECT_HPP
#define TOY_OBJECT_HPP

#include "value.hpp"
#include "Object.hpp"

struct Class;

struct ToyObject {
    ObjectHeader header; // OMR GC ObjectHeader
    Class* klass;        // Pointer to class metadata
    Value fields[0];     // Flexible array member of field values

    static size_t allocSize(size_t numFields) {
        return sizeof(ObjectHeader) + sizeof(Class*) + numFields * sizeof(Value);
    }

    size_t numFields() const {
        return (header.sizeInBytes() - (sizeof(ObjectHeader) + sizeof(Class*))) / sizeof(Value);
    }
};

#endif // TOY_OBJECT_HPP
