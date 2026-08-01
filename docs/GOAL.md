In this project we are trying to implement a virtual machine for a toy language using the Eclipse OMR technology. The goal is to implement a bytecode compiler, a bytecode interpreter, and a JIT compiler using the OMR technology. The virtual machine should further make use of the OMR garbage collector. The bytecode interpreter should use inline caches and this should also be exploited in the JIT interpreter.
The toy language being implemented has the following properties: The language is dynamically typed. There is a single primitive type which is a small integer that should be implemented using tagging to not need any allocation. Small integers understand the following operators (+, -, *, /, %, <, <=, >, >=, ==, !=; comparisons return integers 0/1). One can also define classes that are a fixed collection of fields. Classes can also have associated methods. All class fields are private and can only be accessed from the classes methods. There are also global functions. Classes must be allocated and garbage collected. By default, classes understand only the === operator (pointer equality). However, special methods can be implemented to allow operator overloading (__add(other), __sub(other), __mul(other), __div(other), __rem(other), __lt(other), etc.). There are two control flow constructs, a while-loop and if-else. The conditions are true if it is an non-null object or a non-zero integer. The following is a test programs in this toy language.

```
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

    // Basic arithmetic
    assert_true((a + b) == 52);
    assert_true((a - b) == 32);
    assert_true((a * b) == 420);
    assert_true((a / b) == 4);
    assert_true((a % b) == 2);

    // Comparisons (must evaluate to 1 or 0)
    assert_true((a > b) == 1);
    assert_true((a >= b) == 1);
    assert_true((a < b) == 0);
    assert_true((a <= b) == 0);
    assert_true((a == b) == 0);
    assert_true((a != b) == 1);

    // --------------------------------------
    // 2. Control Flow & Truthiness Rules
    // Condition truthiness: non-zero int or non-null object
    // --------------------------------------
    
    // Non-zero integer is truthy
    if 100 { assert_true(1); } else { assert_false(1); }
    
    // Zero is falsy
    if 0 { assert_false(1); } else { assert_true(1); }

    // Object is truthy
    let p_truthy = Point.new().init(1, 1);
    if p_truthy { assert_true(1); } else { assert_false(1); }

    // Null/0 is falsy in loops
    let counter = 5;
    let sum = 0;
    while counter {
        sum = sum + counter;
        counter = counter - 1;
    }
    assert_true(sum == 15);

    // Recursion test with global function
    assert_true(fibonacci(7) == 13);

    // --------------------------------------
    // 3. Pointer Equality (===) vs Overloaded Equality (==)
    // --------------------------------------
    let p1 = Point.new().init(3, 4);
    let p2 = Point.new().init(3, 4);
    let p3 = p1; // Copy reference

    // Default pointer equality (===)
    assert_true(p1 === p3);
    assert_false(p1 === p2);

    // Overloaded equality (== via __eq)
    assert_true(p1 == p2);
    assert_true(p1 == p3);

    // Overloaded addition (__add)
    let p4 = p1 + p2;
    assert_true(p4.get_x() == 6);
    assert_true(p4.get_y() == 8);

    // Overloaded less-than (__lt)
    let p_small = Point.new().init(1, 1);
    assert_true(p_small < p1);
    assert_false(p1 < p_small);

    // --------------------------------------
    // 4. Object Allocation & Garbage Collection
    // Allocates a linked list chain to trigger GC allocations
    // --------------------------------------
    let i = 0;
    let head = null;

    while i < 1000 {
        // Continually allocates new Node objects
        // Unreferenced previous iterations should be collected
        head = Node.new().init(i, head);
        i = i + 1;
    }

    assert_true(head.get_value() == 999);

    return 0;
}
```
