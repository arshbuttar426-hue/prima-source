class as_:
    def __init__(self, kind):
        self.kind = kind

AS_PLUS = as_("+")
AS_MINUS = as_("-")


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

def signature(n):
    return sum(int(d) for d in str(n))


v1 = [is_prime, AS_PLUS, pair_count, AS_PLUS, signature]
v2 = [is_prime, AS_PLUS, signature, AS_MINUS, pair_count]
v3 = [pair_count, AS_PLUS, is_prime, AS_MINUS, signature]

for n in [10, 28, 100]:
    p1 = pipeline(v1, n)
    p2 = pipeline(v2, n)
    p3 = pipeline(v3, n)
    print(f"n={n:>4}  v1={p1:>6}  v2={p2:>6}  v3={p3:>6}  (same 3 fns, different as)")
