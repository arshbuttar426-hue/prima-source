class as_:
    def __init__(self, kind):
        self.kind = kind  # "+" for coupling, "-" for decoupling

AS_PLUS = as_("+")
AS_MINUS = as_("-")
AS_NIL = as_("nil")


def pipeline(expression, input_val):
    result = 0
    current_op = "+"
    for node in expression:
        if isinstance(node, as_):
            if node.kind != "nil":
                current_op = node.kind
        else:
            val = node(input_val)
            if current_op == "+":
                result += val
            else:
                result -= val
    return result

import math

def is_prime(n):
    if n < 2:
        return 0
    for i in range(2, int(math.isqrt(n)) + 1):
        if n % i == 0:
            return 0
    return n

def pair_count(n):
    if n <= 2 or n % 2 != 0:
        return 0
    count = 0
    for p in range(2, n // 2 + 1):
        q = n - p
        if is_prime(p) and is_prime(q):
            count += 1
    return count


verify = [
    is_prime,    AS_PLUS,
    pair_count,  AS_MINUS,
]

analyze = [
    pair_count,  AS_PLUS,
    is_prime,    AS_MINUS,
]

for n in [10, 28, 100]:
    v = pipeline(verify, n)
    a = pipeline(analyze, n)
    print(f"n={n:>4}  verify={v:>6}  analyze={a:>6}  (same functions, different as)")
