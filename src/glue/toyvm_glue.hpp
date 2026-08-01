#ifndef TOYVM_GC_GLUE_HPP
#define TOYVM_GC_GLUE_HPP

#include "omr.h"
#include "hashtable_api.h"
#include "objectdescription.h"

// This is deliberately owned by the language rather than imported from the
// OMR example.  A root is a slot in the VM's flat Value stack, so a moving GC
// updates the real language reference instead of a copied pointer.
typedef struct ToyVMGlue {
    OMR_VM *_omrVM;
    OMR_VMThread *_omrVMThread;
    J9HashTable *rootTable;
    J9HashTable *objectTable;
    // Published by VM at every allocation safepoint.  Values use a low-bit
    // tag, so the glue can distinguish object slots without VM callbacks.
    volatile uintptr_t *valueStack;
    uintptr_t valueStackSize;
    omrthread_t self;
    omrthread_rwmutex_t _vmAccessMutex;
    volatile uintptr_t _vmExclusiveAccessCount;
} ToyVMGlue;

typedef struct ToyRootEntry {
    const char *name;
    volatile omrobjectptr_t *slot;
} ToyRootEntry;

typedef struct ToyObjectEntry {
    const char *name;
    omrobjectptr_t objPtr;
    int32_t numOfRef;
} ToyObjectEntry;

uintptr_t toyRootTableHashFn(void *entry, void *userData);
uintptr_t toyRootTableHashEqualFn(void *leftEntry, void *rightEntry, void *userData);
uintptr_t toyObjectTableHashFn(void *entry, void *userData);
uintptr_t toyObjectTableHashEqualFn(void *leftEntry, void *rightEntry, void *userData);
uintptr_t toyObjectTableFreeFn(void *entry, void *userData);

#endif
