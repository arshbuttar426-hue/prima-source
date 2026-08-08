"""
prima — thorough test of the As/NotAs constraint model.

Three primitives:
  as X        — positive constraint: value counts as X
  not_as Y    — negative constraint: value absolutely does NOT count as Y
  fn[n]       — categorical computation, selected by index from a function space

A frame declares both what it counts as (positive) and what it avoids (negative).
Pipeline resolves steps sequentially: each step constrains or transforms.
"""


class Frame:
    def __init__(self, name, positive, negative):
        self.name = name
        self.positive = positive   # predicate: what counts as X
        self.negative = negative   # predicate: what X avoids
        self.fns = {}

    def set_fn(self, n, callable_):
        self.fns[n] = callable_

    def check_as(self, value):
        """value counts as this frame → satisfies positive AND avoids negative"""
        if callable(self.positive) and not self.positive(value):
            return False
        if callable(self.negative) and self.negative(value):
            return False  # value is in the exclusion zone
        return True

    def check_not_as(self, value):
        """value does NOT count as this frame → fails positive"""
        if callable(self.positive) and self.positive(value):
            return False  # value IS what we're avoiding
        return True


class Environment:
    def __init__(self):
        self.frames = {}

    def declare(self, name, pos, neg):
        self.frames[name] = Frame(name, pos, neg)

    def resolve(self, name):
        if name not in self.frames:
            raise RuntimeError(f"unknown frame: {name}")
        return self.frames[name]


def pipeline(inputs, steps, env):
    bindings = dict(inputs)
    result = None

    for kind, name, fn_idx, arg_names in steps:
        frame = env.resolve(name)

        if kind == 'as':
            # all named values must satisfy the frame
            targets = arg_names if arg_names else list(bindings.keys())
            for key in targets:
                if key in bindings:
                    val = bindings[key]
                    if not frame.check_as(val):
                        raise RuntimeError(
                            f"as '{name}' failed: {key}={val}"
                            f" (does not count as {name})")

        elif kind == 'not_as':
            # all named values must NOT satisfy the frame's positive
            targets = arg_names if arg_names else list(bindings.keys())
            for key in targets:
                if key in bindings:
                    val = bindings[key]
                    if not frame.check_not_as(val):
                        raise RuntimeError(
                            f"not_as '{name}' failed: {key}={val}"
                            f" (counts as {name}, should not)")

        elif kind == 'fn':
            fn = frame.fns.get(fn_idx)
            if fn is None:
                raise RuntimeError(f"fn[{fn_idx}] not defined on '{name}'")
            args = [bindings[k] for k in (arg_names or [])]
            result = fn(*args)
            bindings['result'] = result

    return bindings, result


# ========== FRAME DEFINITIONS ==========
env = Environment()

env.declare("INT",
    lambda v: isinstance(v, int),
    lambda v: False)

env.declare("letter",
    lambda v: isinstance(v, str) and v.isalpha(),
    lambda v: False)

env.declare("name",
    lambda v: isinstance(v, str) and v.isalpha(),
    lambda v: isinstance(v, int))  # avoids INT

env.declare("CHAR<10",
    lambda v: isinstance(v, str) and len(v) < 10,
    lambda v: False)

env.declare("CHAR>=10",
    lambda v: isinstance(v, str) and len(v) >= 10,
    lambda v: False)

env.declare("operand",
    lambda v: isinstance(v, (int, float)) and v != 0,
    lambda v: isinstance(v, str))

env.declare("positive",
    lambda v: isinstance(v, (int, float)) and v > 0,
    lambda v: v <= 0)

env.declare("even",
    lambda v: isinstance(v, int) and v % 2 == 0,
    lambda v: False)

env.declare("empty",
    lambda v: v is None or v == "" or v == 0 or v == [],
    lambda v: False)

env.declare("not_empty",
    lambda v: v is not None and v != "" and v != 0 and v != [],
    lambda v: False)

env.declare("BETWEEN_1_100",
    lambda v: isinstance(v, (int, float)) and 1 <= v <= 100,
    lambda v: v < 1 or v > 100)

# categorical function space: binary_op
env.declare("binary_op",
    lambda v: callable(v),  # placeholder, fns are checked by index
    lambda v: False)
ops = env.resolve("binary_op")
ops.set_fn(0, lambda a, b: a + b)
ops.set_fn(1, lambda a, b: a - b)
ops.set_fn(2, lambda a, b: a * b)
ops.set_fn(3, lambda a, b: a / b if b != 0 else float('inf'))

# categorical: string_transform
env.declare("string_transform",
    lambda v: callable(v),
    lambda v: False)
str_ops = env.resolve("string_transform")
str_ops.set_fn(0, lambda s: s.upper())
str_ops.set_fn(1, lambda s: s.lower())
str_ops.set_fn(2, lambda s: s[::-1])
str_ops.set_fn(3, lambda s: ''.join(sorted(set(s.lower()))))

# categorical: numeric_transform
env.declare("numeric_transform",
    lambda v: callable(v),
    lambda v: False)
num_ops = env.resolve("numeric_transform")
num_ops.set_fn(0, lambda n: n * 2)
num_ops.set_fn(1, lambda n: n ** 2)
num_ops.set_fn(2, lambda n: abs(n))
num_ops.set_fn(3, lambda n: n % 10)


# ========== HELPERS ==========
passed = 0
failed = 0

def test(label, ok):
    global passed, failed
    if ok:
        passed += 1
        print(f"  ✓ {label}")
    else:
        failed += 1
        print(f"  ✗ {label}")

def check(label, fn):
    try:
        result = fn()
        test(label, result)
    except Exception as e:
        test(label, False)
        print(f"    unexpected error: {e}")

def expect_pass(label, inputs, steps):
    def fn():
        pipeline(inputs, steps, env)
        return True
    check(label, fn)

def expect_fail(label, inputs, steps):
    def fn():
        try:
            pipeline(inputs, steps, env)
            return False  # should have failed
        except RuntimeError:
            return True  # correctly failed
    check(label, fn)


# ========== TEST SUITE ==========
print("=== PRIMA TEST SUITE ===")
print()

# --- 1. Frame constraint basics ---
print("1. Frame constraint basics")

test("letter 'abc' as letter",
    env.resolve("letter").check_as("abc"))
test("letter 'A' as letter",
    env.resolve("letter").check_as("A"))
test("letter 123 not as letter",
    not env.resolve("letter").check_as(123))
test("letter '' not as letter",
    not env.resolve("letter").check_as(""))
test("letter '123' not as letter",
    not env.resolve("letter").check_as("123"))

test("INT 42 as INT",
    env.resolve("INT").check_as(42))
test("INT -5 as INT",
    env.resolve("INT").check_as(-5))
test("INT 0 as INT",
    env.resolve("INT").check_as(0))
test("INT 'abc' not as INT",
    not env.resolve("INT").check_as("abc"))

test("name 'Alice' as name",
    env.resolve("name").check_as("Alice"))
test("name 42 not as name",
    not env.resolve("name").check_as(42))
test("name '123' not as name",
    not env.resolve("name").check_as("123"))

test("CHAR<10 'hi' as CHAR<10",
    env.resolve("CHAR<10").check_as("hi"))
test("CHAR<10 '' as CHAR<10",
    env.resolve("CHAR<10").check_as(""))
test("CHAR<10 '1234567890' not as CHAR<10",
    not env.resolve("CHAR<10").check_as("1234567890"))

test("NOT_INT: 'abc' not_as INT = OK",
    env.resolve("INT").check_not_as("abc"))
test("NOT_INT: 42 not_as INT = FAIL",
    not env.resolve("INT").check_not_as(42))

test("NOT_CHAR<10: 'longstringhere' not_as CHAR<10 = OK",
    env.resolve("CHAR<10").check_not_as("longstringhere"))
test("NOT_CHAR<10: 'hi' not_as CHAR<10 = FAIL",
    not env.resolve("CHAR<10").check_not_as("hi"))

print()

# --- 2. Pipeline: single constraints ---
print("2. Pipeline: single constraints")

expect_pass("value is name",
    {'x': "Bob"},
    [("as", "name", None, ['x'])])

expect_fail("value is not name (number)",
    {'x': 42},
    [("as", "name", None, ['x'])])

expect_pass("value not_as INT",
    {'x': "hello"},
    [("not_as", "INT", None, ['x'])])

expect_fail("value fails not_as INT",
    {'x': 42},
    [("not_as", "INT", None, ['x'])])

expect_pass("value is name and not CHAR>=10",
    {'x': "Bob"},
    [("as", "name", None, ['x']),
     ("not_as", "CHAR>=10", None, ['x'])])

expect_pass("value is name and not under 10 chars (passes not_as CHAR<10)",
    {'x': "verylongnamehere"},
    [("as", "name", None, ['x']),
     ("not_as", "CHAR<10", None, ['x'])])

expect_pass("value is name and under 10 chars",
    {'x': "Alice"},
    [("as", "name", None, ['x']),
     ("as", "CHAR<10", None, ['x'])])

print()

# --- 3. Pipeline: categorical functions ---
print("3. Categorical function selection")

expect_pass("fn[0] add 3+5=8",
    {'a': 3, 'b': 5},
    [("as", "operand", None, ['a', 'b']),
     ("fn", "binary_op", 0, ['a', 'b'])])

def check_result(label, inputs, steps, expected_key, expected_val):
    def fn():
        bindings, res = pipeline(inputs, steps, env)
        return bindings.get(expected_key) == expected_val
    check(label, fn)

check_result("fn[0] add 3+5 = 8",
    {'a': 3, 'b': 5},
    [("fn", "binary_op", 0, ['a', 'b'])],
    'result', 8)

check_result("fn[1] sub 10-4 = 6",
    {'a': 10, 'b': 4},
    [("fn", "binary_op", 1, ['a', 'b'])],
    'result', 6)

check_result("fn[2] mul 6*7 = 42",
    {'a': 6, 'b': 7},
    [("fn", "binary_op", 2, ['a', 'b'])],
    'result', 42)

check_result("fn[3] div 20/4 = 5",
    {'a': 20, 'b': 4},
    [("fn", "binary_op", 3, ['a', 'b'])],
    'result', 5.0)

check_result("fn[0] upper 'hello' -> 'HELLO'",
    {'s': "hello"},
    [("fn", "string_transform", 0, ['s'])],
    'result', "HELLO")

check_result("fn[2] reverse 'abc' -> 'cba'",
    {'s': "abc"},
    [("fn", "string_transform", 2, ['s'])],
    'result', "cba")

check_result("fn[1] square 7^2 = 49",
    {'n': 7},
    [("fn", "numeric_transform", 1, ['n'])],
    'result', 49)

check_result("fn[0] double 21*2 = 42",
    {'n': 21},
    [("fn", "numeric_transform", 0, ['n'])],
    'result', 42)

print()

# --- 4. Pipeline: constraints + function ---
print("4. Constraints + categorical function")

check_result("calc: 3 fn[0] 5 = 8 with operand check",
    {'a': 3, 'b': 5},
    [("as", "operand", None, ['a', 'b']),
     ("fn", "binary_op", 0, ['a', 'b']),
     ("as", "operand", None, ['result']),
     ("as", "positive", None, ['result'])],
    'result', 8)

check_result("calc: 20 fn[2] 4 = 80 with operand check",
    {'a': 20, 'b': 4},
    [("as", "operand", None, ['a', 'b']),
     ("fn", "binary_op", 2, ['a', 'b']),
     ("as", "operand", None, ['result']),
     ("as", "positive", None, ['result'])],
    'result', 80)

expect_fail("calc: operand 0 rejected",
    {'a': 0, 'b': 5},
    [("as", "operand", None, ['a', 'b'])])

expect_pass("string: upper + as name",
    {'s': "hello"},
    [("fn", "string_transform", 0, ['s']),
     ("as", "name", None, ['result'])])

check_result("string: reverse + upper 'abc' -> 'CBA'",
    {'s': "abc"},
    [("fn", "string_transform", 2, ['s']),
     ("fn", "string_transform", 0, ['result'])],
    'result', "CBA")

print()

# --- 5. Combined as/not_as like user examples ---
print("5. User examples: as/not_as patterns")

# name as letter not_as INT
check_result("'Alice' as name not_as INT",
    {'x': "Alice"},
    [("as", "name", None, ['x']),
     ("not_as", "INT", None, ['x'])],
    'x', "Alice")

# 42 as INT not_as letter
check_result("42 as INT not_as letter",
    {'x': 42},
    [("as", "INT", None, ['x']),
     ("not_as", "letter", None, ['x'])],
    'x', 42)

# input as name not_as CHAR<10
# "not_as CHAR<10" = value must NOT be under 10 chars
expect_pass("long name: not_as CHAR<10 passes (name is not under 10)",
    {'x': "verylongnamehere"},
    [("as", "name", None, ['x']),
     ("not_as", "CHAR<10", None, ['x'])])

expect_fail("short name: not_as CHAR<10 fails (Bob is under 10)",
    {'x': "Bob"},
    [("as", "name", None, ['x']),
     ("not_as", "CHAR<10", None, ['x'])])

expect_fail("long name: not_as CHAR>=10 fails (name is >= 10)",
    {'x': "verylongnamehere"},
    [("as", "name", None, ['x']),
     ("not_as", "CHAR>=10", None, ['x'])])

check_result("pipeline: raw -> upper -> name check",
    {'raw': "hello"},
    [("fn", "string_transform", 0, ['raw']),
     ("as", "name", None, ['result']),
     ("as", "CHAR<10", None, ['result'])],
    'result', "HELLO")

print()

# --- 6. Constraint violations ---
print("6. Constraint violation detection")

expect_fail("not_as INT with integer value",
    {'x': 10},
    [("not_as", "INT", None, ['x'])])

expect_fail("as letter with number",
    {'x': 100},
    [("as", "letter", None, ['x'])])

expect_fail("as positive with negative number",
    {'x': -5},
    [("as", "positive", None, ['x'])])

expect_fail("as operand with zero",
    {'x': 0},
    [("as", "operand", None, ['x'])])

expect_fail("as BETWEEN_1_100 with 101",
    {'x': 101},
    [("as", "BETWEEN_1_100", None, ['x'])])

expect_pass("as BETWEEN_1_100 with 50",
    {'x': 50},
    [("as", "BETWEEN_1_100", None, ['x'])])

expect_fail("as empty with non-empty string",
    {'x': "hello"},
    [("as", "empty", None, ['x'])])

expect_pass("as empty with None",
    {'x': None},
    [("as", "empty", None, ['x'])])

expect_pass("as empty with 0",
    {'x': 0},
    [("as", "empty", None, ['x'])])

print()

# --- 7. Multiple inputs and chaining ---
print("7. Multiple inputs and chaining")

check_result("two inputs: a fn[0] b",
    {'a': 15, 'b': 7},
    [("fn", "binary_op", 0, ['a', 'b'])],
    'result', 22)

check_result("chain: fn[0] then fn[1] on result",
    {'a': 10, 'b': 3},
    [("fn", "binary_op", 0, ['a', 'b']),   # 10+3 = 13
     ("fn", "binary_op", 1, ['result', 'a'])],  # 13-10 = 3
    'result', 3)

check_result("complex: (a+b)*c",
    {'a': 2, 'b': 3, 'c': 4},
    [("fn", "binary_op", 0, ['a', 'b']),   # 2+3 = 5
     ("fn", "binary_op", 2, ['result', 'c'])],  # 5*4 = 20
    'result', 20)

check_result("string chain: upper + reverse",
    {'s': "abc"},
    [("fn", "string_transform", 0, ['s']),
     ("fn", "string_transform", 2, ['result'])],
    'result', "CBA")

check_result("numeric chain: double then square",
    {'n': 5},
    [("fn", "numeric_transform", 0, ['n']),  # 5*2 = 10
     ("fn", "numeric_transform", 1, ['result'])],  # 10^2 = 100
    'result', 100)

print()

# --- 8. Edge cases ---
print("8. Edge cases")

# unknown frame
expect_fail("unknown frame rejected",
    {'x': 1},
    [("as", "nonexistent", None, ['x'])])

# missing fn index
expect_fail("missing fn index rejected",
    {'a': 1, 'b': 2},
    [("fn", "binary_op", 99, ['a', 'b'])])

# empty pipeline
check_result("empty pipeline returns inputs",
    {'x': 42},
    [],
    'x', 42)

# not_as with value that partially matches
expect_fail("not_as even with even number",
    {'x': 4},
    [("not_as", "even", None, ['x'])])

expect_pass("not_as even with odd number",
    {'x': 7},
    [("not_as", "even", None, ['x'])])

# multiple constraints applying to different values
check_result("multi-value: a as operand, b as operand, a fn[add] b",
    {'a': 10, 'b': 20},
    [("as", "operand", None, ['a', 'b']),
     ("fn", "binary_op", 0, ['a', 'b'])],
    'result', 30)

print()

# ========== SUMMARY ==========
print(f"=== RESULTS: {passed} passed, {failed} failed ===")
if failed == 0:
    print("ALL TESTS PASSED")
else:
    print(f"SOME TESTS FAILED")
