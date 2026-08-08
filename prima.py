#!/usr/bin/env python3
"""
prima — self-contained parser + interpreter for the As/NotAs language.

Copyright (C) 2026 Arshbuttar.
Licensed under the GNU Affero General Public License v3.
See LICENSE for the full license text.

Usage:
    python3 prima.py test.prima

Grammar:
    program    := (def | exec)+
    def        := frame_def | space_def | pipe_def
    frame_def  := IDENTIFIER 'as' IDENTIFIER 'not_as' IDENTIFIER ';'
    space_def  := 'space' IDENTIFIER '{' arm (',' arm)* '}' ';'
    arm        := 'fn' '[' INT ']' '=' IDENTIFIER
    pipe_def   := 'pipe' IDENTIFIER '(' params ')' ':' body ';'
    params     := IDENTIFIER (',' IDENTIFIER)*
    body       := step (';' step)*
    step       := target 'as' frame
                | target 'not_as' frame
                | '->' 'fn' '[' INT ']' 'from' IDENTIFIER
                | 'output' IDENTIFIER
    exec       := IDENTIFIER '(' args ')' ';'
    args       := value (',' value)*
"""

import sys
import re

# ===== TOKENIZER =====

TOKEN_SPEC = [
    ('COMMENT',  r'--[^\n]*'),
    ('ARROW',    r'->'),
    ('NUMBER',   r'-?\d+'),
    ('STRING',   r'"[^"]*"|\'[^\']*\''),
    ('IDENT',    r'[a-zA-Z_][a-zA-Z_0-9]*'),
    ('LPAREN',   r'\('),
    ('RPAREN',   r'\)'),
    ('LBRACE',   r'\{'),
    ('RBRACE',   r'\}'),
    ('LBRACK',   r'\['),
    ('RBRACK',   r'\]'),
    ('SEMI',     r';'),
    ('COLON',    r':'),
    ('COMMA',    r','),
    ('EQ',       r'='),
    ('SKIP',     r'[ \t\n\r]+'),
    ('UNKNOWN',  r'.'),
]

TOKEN_RE = re.compile('|'.join(f'(?P<{name}>{pattern})' for name, pattern in TOKEN_SPEC))

def tokenize(source):
    tokens = []
    for m in TOKEN_RE.finditer(source):
        kind = m.lastgroup
        val = m.group()
        if kind == 'SKIP' or kind == 'COMMENT':
            continue
        if kind == 'UNKNOWN':
            raise SyntaxError(f"unexpected character: {val!r}")
        if kind == 'STRING':
            val = val[1:-1]
        elif kind == 'NUMBER':
            val = int(val)
        tokens.append((kind, val))
    return tokens


# ===== PARSER =====

class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def peek(self):
        return self.tokens[self.pos] if self.pos < len(self.tokens) else (None, None)

    def advance(self, expected=None):
        kind, val = self.peek()
        if expected and kind != expected:
            raise SyntaxError(f"expected {expected}, got {kind}({val!r})")
        self.pos += 1
        return kind, val

    def expect(self, kind):
        self.advance(kind)

    def parse(self):
        nodes = []
        while self.pos < len(self.tokens):
            kind, val = self.peek()
            if kind == 'IDENT':
                if val == 'space':
                    self.advance()
                    nodes.append(self._parse_space_def())
                elif val == 'pipe':
                    self.advance()
                    nodes.append(self._parse_pipe_def())
                elif val == 'output':
                    raise SyntaxError("'output' outside of pipe body")
                elif self._lookahead_is('as', 1):
                    nodes.append(self._parse_frame_def())
                elif self._lookahead_is('(', 1):
                    nodes.append(self._parse_exec())
                elif self._lookahead_is(':', 1):
                    nodes.append(self._parse_pipe_def())
                else:
                    raise SyntaxError(f"unexpected token after IDENT: {self.tokens[self.pos+1] if self.pos+1 < len(self.tokens) else 'EOF'}")
            elif kind == 'SEMI':
                self.advance()
            else:
                raise SyntaxError(f"unexpected token: {kind}({val!r})")
        return nodes

    def _lookahead_is(self, expected, offset):
        idx = self.pos + offset
        if idx < len(self.tokens):
            k, v = self.tokens[idx]
            if isinstance(expected, str):
                return k == 'IDENT' and v == expected if expected in ('as', 'not_as', 'space', 'pipe', 'output', 'fn', 'from') else k == expected
            return k == expected
        return False

    def _parse_frame_def(self):
        name = self.advance('IDENT')[1]
        as_tok = self.advance('IDENT')
        if as_tok[1] != 'as':
            raise SyntaxError(f"expected 'as', got {as_tok[1]}")
        positive = self.advance('IDENT')[1]
        not_as_tok = self.advance('IDENT')
        if not_as_tok[1] != 'not_as':
            raise SyntaxError(f"expected 'not_as', got {not_as_tok[1]}")
        negative = self.advance('IDENT')[1]
        self.expect('SEMI')
        return ('frame_def', name, positive, negative)

    def _parse_space_def(self):
        name = self.advance('IDENT')[1]
        self.expect('LBRACE')
        arms = []
        while True:
            self.expect('IDENT')  # 'fn'
            self.expect('LBRACK')
            idx = self.advance('NUMBER')[1]
            self.expect('RBRACK')
            self.expect('EQ')
            func_name = self.advance('IDENT')[1]
            arms.append((idx, func_name))
            k, v = self.peek()
            if k == 'COMMA':
                self.advance()
                continue
            break
        self.expect('RBRACE')
        self.expect('SEMI')
        return ('space_def', name, arms)

    def _parse_pipe_def(self):
        # either 'pipe' keyword already consumed, or we're at IDENT name
        kind, val = self.peek()
        if kind == 'IDENT' and val in ('pipe',):
            # already consumed earlier
            pass
        name = self.advance('IDENT')[1]
        self.expect('LPAREN')
        params = []
        while True:
            k, v = self.peek()
            if k == 'RPAREN':
                break
            if params:
                self.expect('COMMA')
            params.append(self.advance('IDENT')[1])
        self.expect('RPAREN')
        self.expect('LBRACE')
        steps = self._parse_body()
        self.expect('RBRACE')
        self.expect('SEMI')
        return ('pipe_def', name, params, steps)

    def _parse_body(self):
        steps = []
        while True:
            k, v = self.peek()
            if k == 'SEMI' or k == None:
                break
            if k == 'IDENT':
                if v == 'output':
                    self.advance()
                    target = self.advance('IDENT')[1]
                    steps.append(('output', target))
                elif v == '->':
                    self.advance()
                    steps.append(self._parse_fn_call_step())
                else:
                    target = self.advance('IDENT')[1]
                    constraint_kind = self.advance('IDENT')[1]
                    if constraint_kind not in ('as', 'not_as'):
                        raise SyntaxError(f"expected 'as' or 'not_as', got {constraint_kind}")
                    frame = self.advance('IDENT')[1]
                    steps.append(('constraint', target, constraint_kind, frame))
            elif k == 'ARROW':  # '->'
                self.advance()
                steps.append(self._parse_fn_call_step())
            else:
                break
            # check for semicolon separator
            k2, v2 = self.peek()
            if k2 == 'SEMI':
                self.advance()
        return steps

    def _parse_fn_call_step(self):
        self.expect('IDENT')  # 'fn'
        self.expect('LBRACK')
        kind, val = self.advance()
        if kind == 'NUMBER':
            idx = ('literal', val)
        elif kind == 'IDENT':
            idx = ('ref', val)
        else:
            raise SyntaxError(f"expected number or identifier in fn index, got {kind}")
        self.expect('RBRACK')
        self.expect('IDENT')  # 'from'
        space = self.advance('IDENT')[1]
        # optional explicit args: (arg1, arg2, ...)
        args = None
        if self.pos < len(self.tokens) and self.tokens[self.pos][0] == 'LPAREN':
            self.advance()  # '('
            args = []
            while True:
                k, v = self.peek()
                if k == 'RPAREN':
                    break
                if args:
                    self.expect('COMMA')
                args.append(self.advance('IDENT')[1])
            self.expect('RPAREN')
        return ('fn_call', idx, space, args)

    def _parse_exec(self):
        name = self.advance('IDENT')[1]
        self.expect('LPAREN')
        args = []
        while True:
            k, v = self.peek()
            if k == 'RPAREN':
                break
            if args:
                self.expect('COMMA')
            k, v = self.advance()
            if k == 'STRING':
                args.append(('string', v))
            elif k == 'NUMBER':
                args.append(('number', v))
            elif k == 'IDENT':
                args.append(('ident', v))
            else:
                raise SyntaxError(f"unexpected argument: {k}({v!r})")
        self.expect('RPAREN')
        self.expect('SEMI')
        return ('exec', name, args)


# ===== INTERPRETER =====

class Frame:
    def __init__(self, name, pos_pred, neg_pred):
        self.name = name
        self.pos_pred = pos_pred
        self.neg_pred = neg_pred

    def check_as(self, value):
        if callable(self.pos_pred) and not self.pos_pred(value):
            return False
        if callable(self.neg_pred) and self.neg_pred(value):
            return False
        return True

    def check_not_as(self, value):
        if callable(self.pos_pred) and self.pos_pred(value):
            return False
        return True


class Space:
    def __init__(self, name):
        self.name = name
        self.fns = {}  # idx -> callable


class Env:
    def __init__(self):
        self.frames = {}
        self.spaces = {}
        self.pipes = {}
        self._init_builtins()

    def _init_builtins(self):
        # unique / nil
        self._def_frame("nil", lambda v: True, lambda v: False)

        # type frames
        self._def_frame("number", lambda v: isinstance(v, (int, float)), lambda v: False)
        self._def_frame("int", lambda v: isinstance(v, int) and not isinstance(v, bool), lambda v: False)
        self._def_frame("string", lambda v: isinstance(v, str), lambda v: False)
        self._def_frame("alpha", lambda v: isinstance(v, str) and v.isalpha(), lambda v: False)
        self._def_frame("alnum", lambda v: isinstance(v, str) and v.isalnum(), lambda v: False)

        # value frames
        self._def_frame("zero", lambda v: v == 0, lambda v: v != 0)
        self._def_frame("positive", lambda v: isinstance(v, (int, float)) and v > 0, lambda v: v <= 0)
        self._def_frame("negative", lambda v: isinstance(v, (int, float)) and v < 0, lambda v: v >= 0)
        self._def_frame("nonzero", lambda v: v != 0, lambda v: v == 0)
        self._def_frame("even", lambda v: isinstance(v, int) and v % 2 == 0, lambda v: False)
        self._def_frame("odd", lambda v: isinstance(v, int) and v % 2 != 0, lambda v: False)
        self._def_frame("empty", lambda v: v is None or v == "" or v == 0 or v == [], lambda v: False)

    def _def_frame(self, name, pos, neg):
        self.frames[name] = Frame(name, pos, neg)

    def def_frame(self, name, pos_name, neg_name):
        pos_f = self.frames.get(pos_name)
        neg_f = self.frames.get(neg_name)
        if not pos_f:
            raise RuntimeError(f"unknown frame: {pos_name}")
        if not neg_f:
            raise RuntimeError(f"unknown frame: {neg_name}")
        self.frames[name] = Frame(name, pos_f.pos_pred, neg_f.pos_pred)

    def get_frame(self, name):
        if name not in self.frames:
            raise RuntimeError(f"unknown frame: {name}")
        return self.frames[name]

    def def_space(self, name, arms):
        sp = Space(name)
        self.spaces[name] = sp
        return sp

    def get_space(self, name):
        if name not in self.spaces:
            raise RuntimeError(f"unknown space: {name}")
        return self.spaces[name]

    def def_pipe(self, name, params, steps):
        self.pipes[name] = (params, steps)

    def get_pipe(self, name):
        if name not in self.pipes:
            raise RuntimeError(f"unknown pipeline: {name}")
        return self.pipes[name]


BUILTIN_FUNCS = {
    "add": lambda a, b: a + b,
    "sub": lambda a, b: a - b,
    "mul": lambda a, b: a * b,
    "div": lambda a, b: a / b if b != 0 else float('inf'),
    "pow": lambda a, b: a ** b,
    "mod": lambda a, b: a % b,
    "upper": lambda s: s.upper() if isinstance(s, str) else s,
    "lower": lambda s: s.lower() if isinstance(s, str) else s,
    "reverse": lambda s: s[::-1] if isinstance(s, str) else s,
    "concat": lambda a, b: str(a) + str(b),
    "length": lambda s: len(s) if hasattr(s, '__len__') else 0,
    "abs": lambda n: abs(n),
    "negate": lambda n: -n,
    "double": lambda n: n * 2,
    "square": lambda n: n ** 2,
    "min": min,
    "max": max,
}


def run(ast, env):
    results = []

    for node in ast:
        kind = node[0]

        if kind == 'frame_def':
            _, name, pos, neg = node
            env.def_frame(name, pos, neg)
            results.append(None)

        elif kind == 'space_def':
            _, name, arms = node
            sp = env.def_space(name, arms)
            for idx, func_name in arms:
                if func_name not in BUILTIN_FUNCS:
                    raise RuntimeError(f"unknown function '{func_name}' in space '{name}'")
                sp.fns[idx] = BUILTIN_FUNCS[func_name]
            results.append(None)

        elif kind == 'pipe_def':
            _, name, params, steps = node
            env.def_pipe(name, params, steps)
            results.append(None)

        elif kind == 'exec':
            _, name, args = node
            pipe = env.get_pipe(name)
            p_params, p_steps = pipe
            if len(args) != len(p_params):
                raise RuntimeError(
                    f"pipeline '{name}' expects {len(p_params)} args, got {len(args)}")

            bindings = {}
            for i, (akind, aval) in enumerate(args):
                bindings[p_params[i]] = aval

            result = None
            for step in p_steps:
                skind = step[0]
                if skind == 'constraint':
                    _, target, ctype, frame_name = step
                    frame = env.get_frame(frame_name)
                    val = bindings.get(target)
                    if val is None:
                        raise RuntimeError(f"unknown binding '{target}' in pipeline '{name}'")
                    if ctype == 'as':
                        if not frame.check_as(val):
                            raise RuntimeError(
                                f"constraint failed: {target} = {val!r} as {frame_name}")
                    elif ctype == 'not_as':
                        if not frame.check_not_as(val):
                            raise RuntimeError(
                                f"constraint failed: {target} = {val!r} not_as {frame_name}")

                elif skind == 'fn_call':
                    _, idx_node, space_name, fn_args_names = step
                    sp = env.get_space(space_name)
                    if idx_node[0] == 'literal':
                        idx = idx_node[1]
                    else:
                        idx = bindings.get(idx_node[1])
                        if idx is None:
                            raise RuntimeError(f"unknown binding '{idx_node[1]}' for fn index")
                    fn = sp.fns.get(idx)
                    if fn is None:
                        raise RuntimeError(f"fn[{idx}] not defined in space '{space_name}'")
                    # resolve arguments from bindings
                    if fn_args_names:
                        fn_args = [bindings.get(k) for k in fn_args_names]
                        if any(a is None for a in fn_args):
                            missing = [k for k in fn_args_names if bindings.get(k) is None]
                            raise RuntimeError(f"missing args for fn call: {missing}")
                    else:
                        # no explicit args: use current value
                        fn_args = [bindings.get('result')] if 'result' in bindings else []
                        if not fn_args:
                            fn_args = [v for k, v in bindings.items() if k != 'result']
                    result = fn(*fn_args)
                    bindings['result'] = result

                elif skind == 'output':
                    _, target = step
                    result = bindings.get(target)
                    if result is None:
                        raise RuntimeError(f"unknown binding '{target}' for output")

            if result is not None:
                results.append(result)
            else:
                results.append(None)

    return results


# ===== MAIN =====

def main():
    if len(sys.argv) < 2:
        print("usage: python3 prima.py <file.prima>")
        sys.exit(1)

    with open(sys.argv[1]) as f:
        source = f.read()

    tokens = tokenize(source)
    parser = Parser(tokens)
    ast = parser.parse()

    env = Env()
    results = run(ast, env)

    for r in results:
        if r is not None:
            print(r)


if __name__ == '__main__':
    main()
