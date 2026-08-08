import math

class as_:
    def __init__(self, kind): self.kind = kind

AS_PLUS = as_("+")
AS_MINUS = as_("-")


def pipeline(expression, input_val):
    result = 0
    op = "+"
    for node in expression:
        if isinstance(node, as_):
            if node.kind != "nil": op = node.kind
        else:
            val = node(input_val)
            if op == "+": result += val
            else: result -= val
    return result


def collatz_steps(n):
    steps = 0
    x = n
    while x != 1:
        x = x // 2 if x % 2 == 0 else 3 * x + 1
        steps += 1
    return steps

def peak_value(n):
    x, peak = n, n
    while x != 1:
        x = x // 2 if x % 2 == 0 else 3 * x + 1
        if x > peak: peak = x
    return peak

def even_ratio_pct(n):
    x, even, total = n, 0, 0
    while x != 1:
        if x % 2 == 0: even += 1
        x = x // 2 if x % 2 == 0 else 3 * x + 1
        total += 1
    return int((even / total) * 100) if total > 0 else 0

def is_power_of_two_val(n):
    return n if (n & (n - 1) == 0) and n != 0 else 0


chain_a = [collatz_steps, AS_PLUS, peak_value, AS_MINUS, even_ratio_pct]
chain_b = [collatz_steps, AS_MINUS, is_power_of_two_val, AS_PLUS, peak_value]
chain_c = [even_ratio_pct, AS_PLUS, is_power_of_two_val, AS_MINUS, collatz_steps]

print(" n  steps  peak  even%  pow2  chain_a  chain_b  chain_c")
print("---  ----  ----  ----  ----  ------  ------  ------")
for n in [7, 9, 16, 27, 97]:
    s = collatz_steps(n); p = peak_value(n); e = even_ratio_pct(n); w = is_power_of_two_val(n)
    a = pipeline(chain_a, n); b = pipeline(chain_b, n); c = pipeline(chain_c, n)
    print(f"{n:>3}  {s:>4}  {p:>4}  {e:>3}%  {w:>4}  {a:>6}  {b:>6}  {c:>6}")
