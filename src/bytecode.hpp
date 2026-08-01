#ifndef TOY_BYTECODE_HPP
#define TOY_BYTECODE_HPP

#include <cstdint>

enum Opcode : uint8_t {
    OP_PUSH_CONST = 0, // Operand: u32 const_idx
    OP_PUSH_NULL,      // No operand
    OP_LOAD_LOCAL,     // Operand: u32 local_idx
    OP_STORE_LOCAL,    // Operand: u32 local_idx
    OP_LOAD_FIELD,     // Operand: u32 field_idx
    OP_STORE_FIELD,    // Operand: u32 field_idx
    OP_NEW_OBJECT,     // Operand: u32 class_id
    OP_ADD,            // Operand: u32 ic_idx
    OP_SUB,            // Operand: u32 ic_idx
    OP_MUL,            // Operand: u32 ic_idx
    OP_DIV,            // Operand: u32 ic_idx
    OP_REM,            // Operand: u32 ic_idx
    OP_LT,             // Operand: u32 ic_idx
    OP_LE,             // Operand: u32 ic_idx
    OP_GT,             // Operand: u32 ic_idx
    OP_GE,             // Operand: u32 ic_idx
    OP_EQ,             // Operand: u32 ic_idx
    OP_NE,             // Operand: u32 ic_idx
    OP_REF_EQ,         // No operand (===)
    OP_REF_NE,         // No operand (!==)
    OP_JUMP,           // Operand: i32 offset
    OP_JUMP_IF_FALSE,  // Operand: i32 offset
    OP_CALL_GLOBAL,    // Operand: u32 global_func_idx, u32 arg_count
    OP_CALL_METHOD,    // Operand: u32 ic_idx, u32 arg_count
    OP_RETURN,         // No operand
    OP_POP,            // No operand
    OP_DUP             // No operand
};

#endif // TOY_BYTECODE_HPP
