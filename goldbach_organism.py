import math

def is_prime(n):
    if n < 2:
        return 0
    for i in range(2, int(math.isqrt(n)) + 1):
        if n % i == 0:
            return 0
    return n

def is_even_check(n):
    return n if n > 2 and n % 2 == 0 else 0

def prime_pair_count(n):
    if n <= 2 or n % 2 != 0:
        return 0
    count = 0
    for p in range(2, n // 2 + 1):
        q = n - p
        if is_prime(p) and is_prime(q):
            count += 1
    return count

def counterexample_flag(n):
    if n <= 2 or n % 2 != 0:
        return 0
    has_pair = 0
    for p in range(2, n // 2 + 1):
        q = n - p
        if is_prime(p) and is_prime(q):
            has_pair = 1
            break
    return 0 if has_pair else -n


def evaluate(n, *pipeline):
    result = 0
    op = "+"
    for node in pipeline:
        if node == "+":
            op = "+"
        elif node == "-":
            op = "-"
        else:
            if op == "+":
                result += node(n)
            else:
                result -= node(n)
    return result


show = lambda label, n, v: print(f"  {label}({n:>4}) = {v:>6}")

v1 = [is_even_check, "+", prime_pair_count, "-", counterexample_flag]
v2 = [is_even_check, "+", prime_pair_count, "+", counterexample_flag]
v3 = [prime_pair_count, "+", counterexample_flag, "-", is_even_check]

for n in [10, 28, 97, 100, 2]:
    print(f"\nn = {n}:")
    print(f"  v1 (verify):   {evaluate(n, *v1):>6}")
    print(f"  v2 (analyze):  {evaluate(n, *v2):>6}")
    print(f"  v3 (reversed): {evaluate(n, *v3):>6}")
