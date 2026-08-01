#ifndef TOY_VM_HPP
#define TOY_VM_HPP

#include "omr.h"
#include "omrvm.h"
#include "omrgc.h"
#include "omrExampleVM.hpp"
#include "value.hpp"
#include "object.hpp"
#include "class.hpp"
#include "inline_cache.hpp"
#include "ast.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

struct StackFrame {
    Method* method = nullptr;
    // A frame is only metadata.  Locals and operands both live in VM::stack,
    // which makes the complete language stack contiguous and walkable by GC.
    size_t base = 0;
    size_t locals_end = 0;
};

class VM {
public:
    VM();
    ~VM();

    bool init();
    void shutdown();

    ToyObject* allocate_object(Class* klass);
    void register_root(const char* name, ToyObject* obj);
    void unregister_root(const char* name);
    void update_stack_roots();

    Class* getClass(const std::string& name) const;
    Method* getFunction(const std::string& name) const;

    Value execute_program(const ProgramAST* ast);

    // Core execution engine
    Value call_method(Method* method, Value receiver, const std::vector<Value>& args);
    Value call_global(Method* method, const std::vector<Value>& args);

    // JIT compiler compilation trigger threshold
    static const uint32_t JIT_THRESHOLD = 20;
    void maybe_jit_compile(Method* method);

    // The interpreter owns the layout but the VM owns the storage so that GC
    // can scan one precise range rather than a graph of per-frame vectors.
    std::vector<StackFrame>& getCallStack() { return call_stack; }
    std::vector<Value>& getStack() { return stack; }
    OMR_VMThread* getOMRThread() const { return omrVMThread; }

private:
    OMR_VM_Example exampleVM;
    OMR_VMThread* omrVMThread = nullptr;

    std::vector<std::unique_ptr<Class>> classes;
    std::vector<std::unique_ptr<Method>> functions;
    std::unordered_map<std::string, Class*> class_map;
    std::unordered_map<std::string, Method*> func_map;

    std::vector<StackFrame> call_stack;
    std::vector<Value> stack;
    std::vector<Value> temp_roots;
    // RootEntry stores a borrowed char pointer. Keep names stable until the
    // root table is rebuilt on the next allocation.
    std::vector<std::string> stack_root_names;

    // The example OMR glue updates RootEntry::rootPtr after an evacuation.
    // Copy those forwarded pointers back into the actual language stack before
    // execution resumes.
    void synchronize_moved_roots();
};

#endif // TOY_VM_HPP
