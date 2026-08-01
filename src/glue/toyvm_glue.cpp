#include "toyvm_glue.hpp"

#include <cstring>

static uintptr_t hash_name(const char *name) {
    uintptr_t hash = 0;
    for (; *name; ++name) hash = (hash << 5) - hash + static_cast<unsigned char>(*name);
    return hash;
}

uintptr_t toyRootTableHashFn(void *entry, void *) {
    return hash_name(((ToyRootEntry *)entry)->name);
}

uintptr_t toyRootTableHashEqualFn(void *left, void *right, void *) {
    return std::strcmp(((ToyRootEntry *)left)->name, ((ToyRootEntry *)right)->name) == 0;
}

uintptr_t toyObjectTableHashFn(void *entry, void *) {
    return hash_name(((ToyObjectEntry *)entry)->name);
}

uintptr_t toyObjectTableHashEqualFn(void *left, void *right, void *) {
    return std::strcmp(((ToyObjectEntry *)left)->name, ((ToyObjectEntry *)right)->name) == 0;
}

uintptr_t toyObjectTableFreeFn(void *, void *) { return 0; }
