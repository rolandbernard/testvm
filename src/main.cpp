#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "lexer.hpp"
#include "parser.hpp"
#include "vm.hpp"

const char* default_test_program = R"PROG(
// ==========================================
// Global Functions
// ==========================================

fn assert_true(cond) {
    if cond {
        return 1;
    } else {
        // Simple trick to force an error or failure signal
        return 1 / 0; 
    }
}

fn assert_false(cond) {
    if cond {
        return 1 / 0;
    } else {
        return 1;
    }
}

// Global function testing recursion and integer operations
fn fibonacci(n) {
    if n <= 1 {
        return n;
    } else {
        return fibonacci(n - 1) + fibonacci(n - 2);
    }
}

// ==========================================
// Class Definitions
// ==========================================

class Point {
    field x;
    field y;

    fn init(x_val, y_val) {
        this.x = x_val;
        this.y = y_val;
        return this;
    }

    // Field accessors (fields are private to class methods)
    fn get_x() { return this.x; }
    fn get_y() { return this.y; }

    // Operator Overloading Methods
    fn __add(other) {
        return Point.new().init(this.x + other.get_x(), this.y + other.get_y());
    }

    fn __eq(other) {
        if this.x == other.get_x() {
            return this.y == other.get_y();
        }
        return 0;
    }

    fn __lt(other) {
        // Compare Manhattan distance from origin
        return (this.x + this.y) < (other.get_x() + other.get_y());
    }
}

class Node {
    field value;
    field next;

    fn init(val, next_node) {
        this.value = val;
        this.next = next_node;
        return this;
    }

    fn get_value() { return this.value; }
    fn get_next() { return this.next; }
}

// ==========================================
// Test Suite Execution
// ==========================================

fn main() {
    // --------------------------------------
    // 1. Tagged Small Integer Operations
    // --------------------------------------
    let a = 42;
    let b = 10;

    assert_true((a + b) == 52);
    assert_true((a - b) == 32);
    assert_true((a * b) == 420);
    assert_true((a / b) == 4);
    assert_true((a % b) == 2);

    assert_true((a > b) == 1);
    assert_true((a >= b) == 1);
    assert_true((a < b) == 0);
    assert_true((a <= b) == 0);
    assert_true((a == b) == 0);
    assert_true((a != b) == 1);

    // --------------------------------------
    // 2. Control Flow & Truthiness Rules
    // --------------------------------------
    if 100 { assert_true(1); } else { assert_false(1); }
    if 0 { assert_false(1); } else { assert_true(1); }

    let p_truthy = Point.new().init(1, 1);
    if p_truthy { assert_true(1); } else { assert_false(1); }

    let counter = 5;
    let sum = 0;
    while counter {
        sum = sum + counter;
        counter = counter - 1;
    }
    assert_true(sum == 15);

    assert_true(fibonacci(7) == 13);

    // --------------------------------------
    // 3. Pointer Equality (===) vs Overloaded Equality (==)
    // --------------------------------------
    let p1 = Point.new().init(3, 4);
    let p2 = Point.new().init(3, 4);
    let p3 = p1;

    assert_true(p1 === p3);
    assert_false(p1 === p2);

    assert_true(p1 == p2);
    assert_true(p1 == p3);

    let p4 = p1 + p2;
    assert_true(p4.get_x() == 6);
    assert_true(p4.get_y() == 8);

    let p_small = Point.new().init(1, 1);
    assert_true(p_small < p1);
    assert_false(p1 < p_small);

    // --------------------------------------
    // 4. Object Allocation & Garbage Collection
    // --------------------------------------
    let i = 0;
    let head = null;

    while i < 1000 {
        head = Node.new().init(i, head);
        i = i + 1;
    }

    assert_true(head.get_value() == 999);

    return 0;
}
)PROG";

extern "C" int omr_main_entry(int argc, char** argv, char** envp) {
    std::string code;

    if (argc > 1) {
        std::ifstream file(argv[1]);
        if (!file.is_open()) {
            std::cerr << "Error: Could not open input file: " << argv[1] << std::endl;
            return 1;
        }
        std::stringstream ss;
        ss << file.rdbuf();
        code = ss.str();
    } else {
        std::cout << "Executing default test suite program..." << std::endl;
        code = default_test_program;
    }

    try {
        Lexer lexer(code);
        Parser parser(lexer);
        auto programAST = parser.parseProgram();

        VM vm;
        if (!vm.init()) {
            std::cerr << "Failed to initialize VM" << std::endl;
            return 1;
        }

        std::cout << "Starting VM execution..." << std::endl;
        Value result = vm.execute_program(programAST.get());

        std::cout << "VM execution completed successfully!" << std::endl;
        if (is_int(result)) {
            std::cout << "Result: " << decode_int(result) << std::endl;
        }
        std::cout << std::flush;

        vm.shutdown();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "VM Error: " << e.what() << std::endl;
        return 1;
    }
}
