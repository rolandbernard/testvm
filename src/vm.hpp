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
#include <deque>
#include <vector>
#include <unordered_map>
#include <string>
#include <memory>

struct StackFrame {
    Method* method = nullptr;
    std::vector<Value> locals;
    std::vector<Value> eval_stack;
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
    static const uint32_t JIT_THRESHOLD = 2000000000;
    void maybe_jit_compile(Method* method);

    // Accessors
    std::deque<StackFrame>& getCallStack() { return call_stack; }
    OMR_VMThread* getOMRThread() const { return omrVMThread; }

private:
    OMR_VM_Example exampleVM;
    OMR_VMThread* omrVMThread = nullptr;

    std::vector<std::unique_ptr<Class>> classes;
    std::vector<std::unique_ptr<Method>> functions;
    std::unordered_map<std::string, Class*> class_map;
    std::unordered_map<std::string, Method*> func_map;

    // Note: std::deque guarantees that push_back/pop_back do not invalidate
    // references to other elements. The interpreter holds references to the
    // current frame and its eval_stack while making nested calls, so a plain
    // std::vector here would reallocate and dangle those references.
    std::deque<StackFrame> call_stack;
    std::vector<Value> temp_roots;
};

#endif // TOY_VM_HPP
