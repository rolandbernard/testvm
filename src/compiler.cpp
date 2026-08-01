#include "compiler.hpp"
#include <stdexcept>
#include <iostream>
#include <algorithm>

Compiler::Compiler() {}

uint32_t Compiler::add_constant(Value val) {
    current_method->constants.push_back(val);
    return static_cast<uint32_t>(current_method->constants.size() - 1);
}

uint32_t Compiler::add_inline_cache(const std::string& methodName) {
    InlineCache ic;
    ic.ic_id = static_cast<uint32_t>(current_method->inline_caches.size());
    ic.method_name = methodName;
    current_method->inline_caches.push_back(ic);
    return ic.ic_id;
}

int32_t Compiler::find_or_add_local(const std::string& name) {
    for (size_t i = 0; i < local_names.size(); ++i) {
        if (local_names[i] == name) return static_cast<int32_t>(i);
    }
    local_names.push_back(name);
    current_method->num_locals = static_cast<uint32_t>(local_names.size());
    return static_cast<int32_t>(local_names.size() - 1);
}

void Compiler::compileProgram(const ProgramAST* program,
                              std::vector<std::unique_ptr<Class>>& out_classes,
                              std::vector<std::unique_ptr<Method>>& out_functions,
                              std::unordered_map<std::string, Class*>& out_class_map,
                              std::unordered_map<std::string, Method*>& out_func_map) {
    // First pass: create Class metadata and Method signatures
    uint32_t next_class_id = 0;
    for (const auto& classAST : program->classes) {
        auto klass = std::make_unique<Class>();
        klass->name = classAST->name;
        klass->class_id = next_class_id++;
        klass->field_names = classAST->fields;
        for (size_t i = 0; i < classAST->fields.size(); ++i) {
            klass->field_indices[classAST->fields[i]] = static_cast<uint32_t>(i);
        }
        out_class_map[klass->name] = klass.get();
        out_classes.push_back(std::move(klass));
    }

    // Global functions pass
    for (const auto& funcAST : program->functions) {
        auto method = std::make_unique<Method>();
        method->name = funcAST->name;
        method->params = funcAST->params;
        out_func_map[method->name] = method.get();
        out_functions.push_back(std::move(method));
    }

    // Class methods pass
    for (size_t i = 0; i < program->classes.size(); ++i) {
        const auto& classAST = program->classes[i];
        Class* klass = out_classes[i].get();
        for (const auto& mAST : classAST->methods) {
            auto method = std::make_unique<Method>();
            method->name = mAST->name;
            // Method params include implicit 'this' as param 0
            method->params.push_back("this");
            for (const auto& p : mAST->params) {
                method->params.push_back(p);
            }
            klass->methods[method->name] = method.get();
            out_functions.push_back(std::move(method));
        }
    }

    // Second pass: compile function bodies
    for (const auto& funcAST : program->functions) {
        Method* method = out_func_map[funcAST->name];
        compileFunction(funcAST.get(), method, nullptr);
    }

    for (size_t i = 0; i < program->classes.size(); ++i) {
        const auto& classAST = program->classes[i];
        Class* klass = out_classes[i].get();
        for (const auto& mAST : classAST->methods) {
            Method* method = klass->methods[mAST->name];
            compileFunction(mAST.get(), method, klass);
        }
    }
}

void Compiler::compileFunction(const FunctionAST* funcAST, Method* method, Class* parentClass) {
    current_method = method;
    current_class = parentClass;
    local_names.clear();

    // Add parameters to local_names
    for (const auto& param : method->params) {
        local_names.push_back(param);
    }
    current_method->num_locals = static_cast<uint32_t>(local_names.size());

    // Compile body statements
    for (const auto& stmt : funcAST->body->stmts) {
        compileStatement(stmt.get());
    }

    // Ensure implicit return null at end
    uint32_t nullConst = add_constant(make_null());
    current_method->bytecode.push_back(OP_PUSH_CONST);
    uint32_t cidx = nullConst;
    current_method->bytecode.insert(current_method->bytecode.end(),
                                   reinterpret_cast<uint8_t*>(&cidx),
                                   reinterpret_cast<uint8_t*>(&cidx) + sizeof(uint32_t));
    current_method->bytecode.push_back(OP_RETURN);
}

static void emit_u32(std::vector<uint8_t>& code, uint32_t val) {
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&val), reinterpret_cast<uint8_t*>(&val) + sizeof(uint32_t));
}

static void emit_i32(std::vector<uint8_t>& code, int32_t val) {
    code.insert(code.end(), reinterpret_cast<uint8_t*>(&val), reinterpret_cast<uint8_t*>(&val) + sizeof(int32_t));
}

static void patch_i32(std::vector<uint8_t>& code, size_t pos, int32_t val) {
    *reinterpret_cast<int32_t*>(&code[pos]) = val;
}

void Compiler::compileStatement(const StmtAST* stmt) {
    switch (stmt->getType()) {
        case ASTNodeType::LetStmt: {
            auto letStmt = static_cast<const LetStmtAST*>(stmt);
            compileExpression(letStmt->init_expr.get());
            int32_t localIdx = find_or_add_local(letStmt->name);
            current_method->bytecode.push_back(OP_STORE_LOCAL);
            emit_u32(current_method->bytecode, static_cast<uint32_t>(localIdx));
            break;
        }
        case ASTNodeType::AssignStmt: {
            auto assignStmt = static_cast<const AssignStmtAST*>(stmt);
            compileExpression(assignStmt->value.get());
            if (assignStmt->target->getType() == ASTNodeType::VarExpr) {
                auto varExpr = static_cast<const VarExprAST*>(assignStmt->target.get());
                int32_t localIdx = find_or_add_local(varExpr->name);
                current_method->bytecode.push_back(OP_STORE_LOCAL);
                emit_u32(current_method->bytecode, static_cast<uint32_t>(localIdx));
            } else if (assignStmt->target->getType() == ASTNodeType::FieldAccess) {
                auto fieldAccess = static_cast<const FieldAccessAST*>(assignStmt->target.get());
                if (fieldAccess->target->getType() != ASTNodeType::ThisExpr) {
                    throw std::runtime_error("Class fields are private and can only be accessed on 'this'");
                }
                if (!current_class) {
                    throw std::runtime_error("Field assignment 'this." + fieldAccess->field_name + "' outside class method");
                }
                int32_t fidx = current_class->get_field_index(fieldAccess->field_name);
                if (fidx < 0) {
                    throw std::runtime_error("Unknown field '" + fieldAccess->field_name + "' in class '" + current_class->name + "'");
                }
                current_method->bytecode.push_back(OP_STORE_FIELD);
                emit_u32(current_method->bytecode, static_cast<uint32_t>(fidx));
            } else {
                throw std::runtime_error("Invalid assignment target");
            }
            break;
        }
        case ASTNodeType::IfStmt: {
            auto ifStmt = static_cast<const IfStmtAST*>(stmt);
            compileExpression(ifStmt->condition.get());

            current_method->bytecode.push_back(OP_JUMP_IF_FALSE);
            size_t jumpElsePos = current_method->bytecode.size();
            emit_i32(current_method->bytecode, 0); // Placeholder

            compileStatement(ifStmt->then_branch.get());

            if (ifStmt->else_branch) {
                current_method->bytecode.push_back(OP_JUMP);
                size_t jumpEndPos = current_method->bytecode.size();
                emit_i32(current_method->bytecode, 0); // Placeholder

                size_t elseStartPos = current_method->bytecode.size();
                patch_i32(current_method->bytecode, jumpElsePos, static_cast<int32_t>(elseStartPos - (jumpElsePos + 4)));

                compileStatement(ifStmt->else_branch.get());

                size_t endPos = current_method->bytecode.size();
                patch_i32(current_method->bytecode, jumpEndPos, static_cast<int32_t>(endPos - (jumpEndPos + 4)));
            } else {
                size_t elseStartPos = current_method->bytecode.size();
                patch_i32(current_method->bytecode, jumpElsePos, static_cast<int32_t>(elseStartPos - (jumpElsePos + 4)));
            }
            break;
        }
        case ASTNodeType::WhileStmt: {
            auto whileStmt = static_cast<const WhileStmtAST*>(stmt);
            size_t loopStartPos = current_method->bytecode.size();

            compileExpression(whileStmt->condition.get());
            current_method->bytecode.push_back(OP_JUMP_IF_FALSE);
            size_t jumpEndPos = current_method->bytecode.size();
            emit_i32(current_method->bytecode, 0); // Placeholder

            compileStatement(whileStmt->body.get());

            current_method->bytecode.push_back(OP_JUMP);
            size_t backJumpPos = current_method->bytecode.size();
            emit_i32(current_method->bytecode, static_cast<int32_t>(loopStartPos - (backJumpPos + 4)));

            size_t endPos = current_method->bytecode.size();
            patch_i32(current_method->bytecode, jumpEndPos, static_cast<int32_t>(endPos - (jumpEndPos + 4)));
            break;
        }
        case ASTNodeType::BlockStmt: {
            auto blockStmt = static_cast<const BlockStmtAST*>(stmt);
            for (const auto& s : blockStmt->stmts) {
                compileStatement(s.get());
            }
            break;
        }
        case ASTNodeType::ReturnStmt: {
            auto retStmt = static_cast<const ReturnStmtAST*>(stmt);
            if (retStmt->expr) {
                compileExpression(retStmt->expr.get());
            } else {
                uint32_t cidx = add_constant(make_null());
                current_method->bytecode.push_back(OP_PUSH_CONST);
                emit_u32(current_method->bytecode, cidx);
            }
            current_method->bytecode.push_back(OP_RETURN);
            break;
        }
        case ASTNodeType::ExprStmt: {
            auto exprStmt = static_cast<const ExprStmtAST*>(stmt);
            compileExpression(exprStmt->expr.get());
            current_method->bytecode.push_back(OP_POP);
            break;
        }
        default:
            throw std::runtime_error("Unknown statement type");
    }
}

void Compiler::compileExpression(const ExprAST* expr) {
    switch (expr->getType()) {
        case ASTNodeType::IntLiteral: {
            auto intLit = static_cast<const IntLiteralAST*>(expr);
            uint32_t cidx = add_constant(encode_int(intLit->value));
            current_method->bytecode.push_back(OP_PUSH_CONST);
            emit_u32(current_method->bytecode, cidx);
            break;
        }
        case ASTNodeType::NullLiteral: {
            current_method->bytecode.push_back(OP_PUSH_NULL);
            break;
        }
        case ASTNodeType::VarExpr: {
            auto varExpr = static_cast<const VarExprAST*>(expr);
            int32_t localIdx = find_or_add_local(varExpr->name);
            current_method->bytecode.push_back(OP_LOAD_LOCAL);
            emit_u32(current_method->bytecode, static_cast<uint32_t>(localIdx));
            break;
        }
        case ASTNodeType::ThisExpr: {
            int32_t localIdx = find_or_add_local("this");
            current_method->bytecode.push_back(OP_LOAD_LOCAL);
            emit_u32(current_method->bytecode, static_cast<uint32_t>(localIdx));
            break;
        }
        case ASTNodeType::FieldAccess: {
            auto fieldAccess = static_cast<const FieldAccessAST*>(expr);
            if (fieldAccess->target->getType() != ASTNodeType::ThisExpr) {
                throw std::runtime_error("Class fields are private and can only be accessed on 'this'");
            }
            if (!current_class) {
                throw std::runtime_error("Field access 'this." + fieldAccess->field_name + "' outside class method");
            }
            int32_t fidx = current_class->get_field_index(fieldAccess->field_name);
            if (fidx < 0) {
                throw std::runtime_error("Unknown field '" + fieldAccess->field_name + "' in class '" + current_class->name + "'");
            }
            current_method->bytecode.push_back(OP_LOAD_FIELD);
            emit_u32(current_method->bytecode, static_cast<uint32_t>(fidx));
            break;
        }
        case ASTNodeType::NewExpr: {
            auto newExpr = static_cast<const NewExprAST*>(expr);
            // Constant string for class name or class ID lookup
            uint32_t cidx = add_constant(reinterpret_cast<uintptr_t>(newExpr->class_name.c_str()));
            current_method->bytecode.push_back(OP_NEW_OBJECT);
            emit_u32(current_method->bytecode, cidx);
            break;
        }
        case ASTNodeType::BinaryExpr: {
            auto binExpr = static_cast<const BinaryExprAST*>(expr);
            compileExpression(binExpr->left.get());
            compileExpression(binExpr->right.get());

            if (binExpr->op == "===") {
                current_method->bytecode.push_back(OP_REF_EQ);
            } else if (binExpr->op == "!==") {
                current_method->bytecode.push_back(OP_REF_NE);
            } else {
                std::string methodName;
                Opcode op;
                if (binExpr->op == "+") { op = OP_ADD; methodName = "__add"; }
                else if (binExpr->op == "-") { op = OP_SUB; methodName = "__sub"; }
                else if (binExpr->op == "*") { op = OP_MUL; methodName = "__mul"; }
                else if (binExpr->op == "/") { op = OP_DIV; methodName = "__div"; }
                else if (binExpr->op == "%") { op = OP_REM; methodName = "__rem"; }
                else if (binExpr->op == "<") { op = OP_LT; methodName = "__lt"; }
                else if (binExpr->op == "<=") { op = OP_LE; methodName = "__le"; }
                else if (binExpr->op == ">") { op = OP_GT; methodName = "__gt"; }
                else if (binExpr->op == ">=") { op = OP_GE; methodName = "__ge"; }
                else if (binExpr->op == "==") { op = OP_EQ; methodName = "__eq"; }
                else if (binExpr->op == "!=") { op = OP_NE; methodName = "__ne"; }
                else {
                    throw std::runtime_error("Unknown binary operator '" + binExpr->op + "'");
                }

                uint32_t ic_idx = add_inline_cache(methodName);
                current_method->bytecode.push_back(op);
                emit_u32(current_method->bytecode, ic_idx);
            }
            break;
        }
        case ASTNodeType::CallExpr: {
            auto callExpr = static_cast<const CallExprAST*>(expr);
            if (callExpr->target == nullptr) {
                // Global function call
                for (const auto& arg : callExpr->args) {
                    compileExpression(arg.get());
                }
                uint32_t cidx = add_constant(reinterpret_cast<uintptr_t>(callExpr->func_name.c_str()));
                current_method->bytecode.push_back(OP_CALL_GLOBAL);
                emit_u32(current_method->bytecode, cidx);
                emit_u32(current_method->bytecode, static_cast<uint32_t>(callExpr->args.size()));
            } else {
                // Method call: receiver, then args
                compileExpression(callExpr->target.get());
                for (const auto& arg : callExpr->args) {
                    compileExpression(arg.get());
                }
                uint32_t ic_idx = add_inline_cache(callExpr->func_name);
                current_method->bytecode.push_back(OP_CALL_METHOD);
                emit_u32(current_method->bytecode, ic_idx);
                emit_u32(current_method->bytecode, static_cast<uint32_t>(callExpr->args.size()));
            }
            break;
        }
        default:
            throw std::runtime_error("Unknown expression type");
    }
}
