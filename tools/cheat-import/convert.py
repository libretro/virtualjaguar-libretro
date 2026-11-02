#!/usr/bin/env python3
"""Statically translate a conservative subset of community cheat C sources.

Does not compile, import, execute, preprocess includes, or download input code.
No SDK or proprietary emulator implementation is used. See README.md.
"""
import argparse
import ast
import hashlib
import itertools
import functools
import json
import re
import sys
import zlib
from dataclasses import dataclass, replace
from pathlib import Path

from pycparser import c_ast as C, c_parser


class Unsupported(ValueError):
    def __init__(self, message="", call=None):
        super().__init__(message)
        self.call = call  # API call whose name caused the refusal, else None


# Calls this converter handles itself -> the status bigpemu-api.json must give them.
SPECIAL_CALLS = {
    "bigpemu_get_module_handle": "supported", "bigpemu_set_module_usage_flags": "supported",
    "bigpemu_register_setting_category": "supported", "bigpemu_register_setting_bool": "supported",
    "bigpemu_register_setting_int": "setting-omitted", "bigpemu_register_setting_int_full": "supported",
    "bigpemu_register_event_sw_loaded": "supported", "bigpemu_register_event_emu_thread_frame": "supported",
    "bigpemu_get_loaded_fnv1a64": "supported", "bigpemu_get_setting_value": "supported",
    "bigpemu_jag_write8": "supported", "bigpemu_jag_write16": "supported", "bigpemu_jag_write32": "supported",
    "printf_notify": "supported", "printf": "ignored"}
# bigp_init may only make these (the registration calls and handle/flag setup).
INIT_CALLS = frozenset(n for n in SPECIAL_CALLS if n.startswith(("bigpemu_register_", "bigpemu_get_module", "bigpemu_set_module")))
API_STATUSES = ("supported", "setting-omitted", "ignored", "unsupported")
MAX_MESSAGES, MAX_MESSAGE_CHARS, NOTE_CHARS = 4, 200, 60
MAX_INTEGER_VALUES, MAX_SETTING_COMBINATIONS = 64, 4096
MAX_ARRAY_ELEMENTS = 1024  # Total materialized table elements per evaluation.


@functools.lru_cache(maxsize=None)
def gap_call(name):
    """name if it is an API-style call the map lists as unsupported or lacks (a worklist item), else None."""
    if name.startswith("bigpemu_") or name in ("printf", "printf_notify", "memcpy"):
        row = load_api_map().get(name)
        if row is None or row["status"] == "unsupported": return name
    return None


def load_api_map(path=None):
    """The BigPEmu API call -> {status, meaning, ours, todo} map (bigpemu-api.json), validated."""
    path = Path(path) if path else Path(__file__).with_name("bigpemu-api.json")
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("format") != 1 or not isinstance(data.get("calls"), dict):
        raise ValueError(f"{path}: expected {{\"format\": 1, \"calls\": {{...}}}}")
    for name, row in data["calls"].items():
        if (not isinstance(row, dict) or set(row) != {"status", "meaning", "ours", "todo"}
                or row["status"] not in API_STATUSES or not all(isinstance(v, str) for v in row.values())):
            raise ValueError(f"{path}: bad row for {name}")
    return data["calls"]


@dataclass(frozen=True)
class Value:
    n: int
    deps: frozenset = frozenset()
    bits: int = 32
    unsigned: bool = False


@dataclass(frozen=True)
class ConstantArray:
    values: tuple


def integer(value, bits=32, unsigned=False):
    number = value.n
    if unsigned:
        number &= (1 << bits)-1
    elif not -(1 << (bits-1)) <= number < (1 << (bits-1)):
        raise Unsupported("signed integer overflow/conversion requires manual review")
    return Value(number, value.deps, bits, unsigned)


def type_info(node):
    if not isinstance(node, C.TypeDecl) or not isinstance(node.type, C.IdentifierType):
        raise Unsupported("only scalar integer declarations are supported")
    typ = " ".join(node.type.names)
    match = re.fullmatch(r"(u?)int(8|16|32|64)_t", typ)
    if match: return int(match[2]), bool(match[1])
    if typ in ("int", "signed int", "bool"): return 32, False
    if typ in ("unsigned", "unsigned int"): return 32, True
    raise Unsupported("unsupported integer type " + typ)


def walk(node, macros):
    """Inspect syntax and referenced object-like macros without executing either.

    Each macro body is inspected once: alias cycles terminate and shared macro
    graphs cannot turn a small source into exponentially many validation steps.
    """
    pending, expanded = [node], set()
    while pending:
        current = pending.pop()
        yield current
        pending.extend(child for _, child in current.children())
        if isinstance(current, C.ID) and current.name in macros and current.name not in expanded:
            expanded.add(current.name)
            pending.append(macros[current.name])


def promote(value):
    return integer(value) if value.bits < 32 else value


def common(left, right):
    left, right = promote(left), promote(right)
    bits = max(left.bits, right.bits)
    unsigned = any(v.unsigned and v.bits == bits for v in (left, right))
    return integer(left, bits, unsigned), integer(right, bits, unsigned)


TYPES = """
typedef unsigned char uint8_t; typedef unsigned short uint16_t;
typedef unsigned int uint32_t; typedef unsigned long long uint64_t;
typedef signed char int8_t; typedef short int16_t; typedef int int32_t;
typedef long long int64_t; typedef unsigned int size_t; typedef int bool;
"""


def uncomment(text):
    # Keep quoted strings/characters intact, including comment-looking text.
    pattern = r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/'
    return re.sub(pattern, lambda m: "\n" * m[0].count("\n") + " "
                  if m[0].startswith(("//", "/*")) else m[0], text, flags=re.S)


def fnv1a64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def rom_bytes(path):
    if path.suffix.lower() not in (".j64", ".jag", ".rom", ".bin"):
        raise Unsupported("use an uncompressed raw cartridge (.j64/.jag/.rom/.bin)")
    if not 0 < path.stat().st_size <= 16*1024*1024:
        raise Unsupported("raw cartridge size must be 1 byte through 16 MiB")
    data = path.read_bytes()
    if not data or len(data) > 16 * 1024 * 1024:
        raise Unsupported("raw cartridge size must be 1 byte through 16 MiB")
    # Same header detection as src/core/file.c. Refuse instead of guessing which
    # bytes a foreign script's hash referred to. The user supplies a native dump.
    marker = b"\x04\x04\x04\x04"
    if (len(data) >= 0x604 and len(data) % 0x100000 and data[0x600:0x604] == marker
            and ((len(data)-512) % 0x100000 == 0
                 or (len(data)-512 < 0x100000 and data[0x400:0x404] != marker))):
        raise Unsupported("copier-headered ROM: supply the native cartridge payload")
    return data


class Runtime:
    """Bounded abstract evaluation: integer constants and setting dependencies.

    RAM reads and arbitrary calls have no implementation. A write can only be
    emitted if its value/control depend solely on the one selected toggle.
    """
    def __init__(self, module, rom_hash, selected=None):
        self.module = module
        self.rom_hash = rom_hash
        self.selected = selected
        self.scopes = [{}]
        self.types = [{}]
        self.frame = False
        self.path = frozenset()
        self.writes = []
        self.steps = 0
        self.resolving = set()
        self.array_elements = 0

    def tick(self):
        self.steps += 1
        if self.steps > 20000:
            raise Unsupported("evaluation/loop budget exceeded")

    def get(self, name, array=False):
        for scope in reversed(self.scopes):
            if name in scope:
                value = scope[name]
                if value is None: raise Unsupported("read of uninitialised local " + name)
                if isinstance(value, ConstantArray) and not array:
                    raise Unsupported("constant array requires a subscript; pointers are unsupported")
                return value
        if name in self.module.macros:
            if name in self.resolving:
                raise Unsupported("recursive macro " + name)
            self.resolving.add(name)
            try:
                return self.expr(self.module.macros[name])
            finally:
                self.resolving.remove(name)
        raise Unsupported("unknown identifier " + name)

    def put(self, name, value, declare=False, typ=None):
        if value is not None:
            value = replace(value, deps=value.deps | self.path)
        if declare:
            self.types[-1][name] = typ or (32, False)
            self.scopes[-1][name] = integer(value, *self.types[-1][name]) if value else None
            return
        for i in reversed(range(len(self.scopes))):
            scope = self.scopes[i]
            if name in scope:
                if isinstance(scope[name], ConstantArray):
                    raise Unsupported("constant arrays cannot be assigned")
                if self.frame and i == 0:
                    raise Unsupported("frame callback changes persistent state")
                scope[name] = integer(value, *self.types[i][name])
                return
        raise Unsupported("assignment to unknown identifier " + name)

    def expr(self, node):
        self.tick()
        if isinstance(node, C.Constant):
            if node.type == "string":
                raise Unsupported("string used as an integer")
            if node.type == "char":
                number = ord(ast.literal_eval(node.value))
                if number > 127: raise Unsupported("non-ASCII character constant")
                return Value(number)
            token = re.sub(r"[uUlL]+$", "", node.value)
            if node.value.lower().count("l") == 1:
                raise Unsupported("platform-dependent long literal requires manual review")
            try:
                number = int(token, 8 if re.fullmatch(r"0[0-7]+", token) else 0)
                unsigned = "u" in node.value.lower()
                bits = 64 if "ll" in node.value.lower() else 32
                if not unsigned and number >= 1 << (bits-1):
                    if token.lower().startswith("0x"): unsigned = True
                    else: bits = 64
                if number >= 1 << bits: raise Unsupported("integer literal out of range")
                return integer(Value(number), bits, unsigned)
            except ValueError:
                raise Unsupported("non-integer constant " + node.value)
        if isinstance(node, C.ID):
            return self.get(node.name)
        if isinstance(node, C.ArrayRef):
            indices, root = [], node
            while isinstance(root, C.ArrayRef):
                indices.append(root.subscript)
                root = root.name
            if not isinstance(root, C.ID):
                raise Unsupported("subscript requires a named constant array")
            if len(indices) > 1 and any(
                    isinstance(n, (C.Assignment, C.FuncCall)) or
                    (isinstance(n, C.UnaryOp) and n.op in ("++", "--", "p++", "p--"))
                    for index in indices for n in walk(index, self.module.macros)):
                raise Unsupported("nested array subscripts must be side-effect-free")
            value, deps = self.get(root.name, array=True), frozenset()
            for subscript in reversed(indices):
                if not isinstance(value, ConstantArray):
                    raise Unsupported("subscript requires a constant array")
                index = self.expr(subscript)
                if not 0 <= index.n < len(value.values):
                    raise Unsupported("array index out of bounds")
                deps |= index.deps
                value = value.values[index.n]
            if isinstance(value, ConstantArray):
                raise Unsupported("constant array requires all subscripts; pointers are unsupported")
            return replace(value, deps=value.deps | deps)
        if isinstance(node, C.Cast):
            val = self.expr(node.expr)
            return integer(val, *type_info(node.to_type.type))
        if isinstance(node, C.UnaryOp):
            if node.op in ("++", "p++", "--", "p--") and isinstance(node.expr, C.ID):
                old = self.get(node.expr.name)
                new = integer(replace(old, n=old.n + (1 if "+" in node.op else -1)), old.bits, old.unsigned)
                self.put(node.expr.name, new)
                return old if node.op.startswith("p") else new
            val = promote(self.expr(node.expr))
            if node.op == "+": return val
            if node.op == "-": return integer(replace(val, n=-val.n), val.bits, val.unsigned)
            if node.op == "!": return Value(int(not val.n), val.deps)
            if node.op == "~": return integer(replace(val, n=~val.n), val.bits, val.unsigned)
            raise Unsupported("unsupported unary operator " + node.op)
        if isinstance(node, C.BinaryOp):
            left = self.expr(node.left)
            if node.op == "&&" and not left.n: return Value(0, left.deps)
            if node.op == "||" and left.n: return Value(1, left.deps)
            right = self.expr(node.right)
            if node.op in ("<<", ">>"):
                left, right = promote(left), promote(right)
            else:
                left, right = common(left, right)
            a, b = left.n, right.n
            ops = {"+": lambda: a+b, "-": lambda: a-b, "*": lambda: a*b,
                   "&": lambda: a & b, "|": lambda: a | b, "^": lambda: a ^ b,
                   "==": lambda: int(a == b), "!=": lambda: int(a != b),
                   "<": lambda: int(a < b), ">": lambda: int(a > b),
                   "<=": lambda: int(a <= b), ">=": lambda: int(a >= b),
                   "&&": lambda: int(bool(a and b)), "||": lambda: int(bool(a or b))}
            if node.op in ("<<", ">>"):
                if not 0 <= b < left.bits or a < 0:
                    raise Unsupported("unsupported shift")
                number = a << b if node.op == "<<" else a >> b
            elif node.op in ops:
                number = ops[node.op]()
            else:
                raise Unsupported("unsupported binary operator " + node.op)
            result = Value(number, left.deps | right.deps)
            if node.op in ("==", "!=", "<", ">", "<=", ">=", "&&", "||"):
                return result
            return integer(result, left.bits, left.unsigned)
        if isinstance(node, C.TernaryOp):
            # C chooses a common type from both arms, even when only one runs.
            raise Unsupported("conditional expression requires manual review")
        if isinstance(node, C.FuncCall):
            return self.call(node)
        raise Unsupported("unsupported expression " + type(node).__name__)

    def call(self, node):
        name = getattr(node.name, "name", "")
        args = node.args.exprs if node.args else []
        if name in ("printf_notify", "printf"):
            # Output only: arguments are never evaluated (no memory effect), so they must have no side effects.
            for arg in args:
                for sub in walk(arg, self.module.macros):
                    if isinstance(sub, (C.Assignment, C.FuncCall)) or (
                            isinstance(sub, C.UnaryOp) and sub.op in ("++", "--", "p++", "p--")):
                        raise Unsupported("side effect in message arguments requires manual review")
            if name == "printf":
                self.module.ignored_calls.add(name)
            else:
                self.module.notify(args, not self.frame and self.path <= {"rom"})
            return Value(0)
        if name == "bigpemu_get_loaded_fnv1a64" and not args:
            return Value(self.rom_hash, frozenset(["rom"]), 64, True)
        if name == "bigpemu_get_setting_value" and len(args) == 2:
            ptr = args[0]
            if not isinstance(ptr, C.UnaryOp) or ptr.op != "&" or not isinstance(ptr.expr, C.ID):
                raise Unsupported("setting output must be a scalar local variable")
            identifier = getattr(args[1], "name", None)
            if identifier not in self.module.handles or any(identifier in scope for scope in self.scopes[1:]):
                raise Unsupported("setting handle must be its registered global identifier")
            handle = self.get(identifier).n
            if handle != self.module.handles[identifier]:
                raise Unsupported("setting handle was changed")
            value = (self.selected or {}).get(handle, self.module.off_values[handle])
            self.put(ptr.expr.name, Value(value, frozenset([handle])))
            return Value(0)
        write = re.fullmatch(r"bigpemu_jag_write(8|16|32)", name)
        if write and len(args) == 2:
            address, value = map(self.expr, args)
            deps = self.path | address.deps | value.deps
            if not self.selected or "rom" not in deps or not (deps - {"rom"}) or not deps <= set(self.selected) | {"rom"}:
                raise Unsupported("write needs a ROM-hash guard and exactly one independent setting")
            if not address.deps <= self.path:
                raise Unsupported("setting-dependent write address")
            width = int(write[1]) // 8
            if not 0 <= address.n <= 0x200000-width:
                raise Unsupported("write outside 2 MiB main RAM")
            if width > 1 and address.n % 2:
                raise Unsupported("unaligned word/long write requires manual review")
            if len(self.writes) >= 256:
                raise Unsupported("cheat exceeds the core's 256-write limit")
            self.writes.append((address.n, value.n & ((1 << (8*width))-1), width))
            return Value(0)
        if name in SPECIAL_CALLS:
            raise Unsupported("unsupported use of " + name)
        raise Unsupported("unsupported call " + name + " (RAM-dependent operations are not constant cheats)", gap_call(name))

    def stmt(self, node):
        if node is None: return
        self.tick()
        if isinstance(node, C.Compound):
            self.scopes.append({})
            self.types.append({})
            try:
                for child in node.block_items or []: self.stmt(child)
            finally:
                self.scopes.pop()
                self.types.pop()
        elif isinstance(node, C.Decl):
            if isinstance(node.type, C.ArrayDecl):
                self.declare_array(node)
                return
            if isinstance(node.type, C.PtrDecl):
                raise Unsupported("pointer/array declarations require manual review")
            if "static" in node.storage and len(self.scopes) > 1:
                raise Unsupported("static local state requires manual review")
            value = self.expr(node.init) if node.init else (Value(0) if len(self.scopes) == 1 else None)
            self.put(node.name, value, declare=True, typ=type_info(node.type))
        elif isinstance(node, C.DeclList):
            for child in node.decls: self.stmt(child)
        elif isinstance(node, C.ExprList):
            for child in node.exprs: self.stmt(child)
        elif isinstance(node, C.Assignment) and isinstance(node.lvalue, C.ID):
            if node.op == "=": value = self.expr(node.rvalue)
            else:
                value = self.expr(C.BinaryOp(node.op[:-1], node.lvalue, node.rvalue))
            self.put(node.lvalue.name, value)
        elif isinstance(node, C.If):
            cond = self.expr(node.cond)
            old = self.path
            self.path |= cond.deps
            try: self.stmt(node.iftrue if cond.n else node.iffalse)
            finally: self.path = old
        elif isinstance(node, C.For):
            self.scopes.append({})
            self.types.append({})
            try:
                self.stmt(node.init)
                while True:
                    cond = self.expr(node.cond)
                    if not cond.deps <= self.path:
                        raise Unsupported("setting-dependent loop bound")
                    if not cond.n: break
                    self.stmt(node.stmt); self.stmt(node.next)
            finally:
                self.scopes.pop()
                self.types.pop()
        elif isinstance(node, (C.FuncCall, C.UnaryOp)):
            self.expr(node)
        elif isinstance(node, C.Return):
            # Callbacks in this subset must end with a literal return 0. An early
            # return changes execution and is deliberately not approximated.
            raise Unsupported("early return requires manual review")
        elif not isinstance(node, C.EmptyStatement):
            raise Unsupported("unsupported statement " + type(node).__name__)

    def declare_array(self, node):
        if id(node) in self.module.unused_tables:
            self.check_unused_table(node)
            return
        element, dimensions = node.type, []
        while isinstance(element, C.ArrayDecl):
            dimensions.append(element.dim)
            element = element.type
        if (len(self.scopes) != 1 or not isinstance(element, C.TypeDecl)
                or "const" not in element.quals or "volatile" in element.quals
                or not isinstance(node.init, (C.InitList, C.Constant))):
            raise Unsupported("pointer/array declarations require manual review")
        typ = type_info(element)
        # Inspect the full declaration, including macro expansions, before evaluation.
        for expr in [dim for dim in dimensions if dim is not None] + [node.init]:
            if any(isinstance(n, (C.FuncCall, C.Assignment, C.NamedInitializer)) or
                   (isinstance(n, C.UnaryOp) and n.op in ("++", "--", "p++", "p--")) for n in walk(expr, self.module.macros)):
                raise Unsupported("constant array size/initializer requires constant integer expressions")
        sizes, total = [], 1
        for depth, dim in enumerate(dimensions):
            if dim is None:
                if depth or not isinstance(node.init, C.InitList):
                    raise Unsupported("only the outer array size may be inferred from a braced initializer")
                size = Value(len(node.init.exprs or []))
            else:
                size = self.expr(dim)
            total *= size.n
            if size.deps or size.n <= 0 or total > MAX_ARRAY_ELEMENTS - self.array_elements:
                raise Unsupported("constant array size exceeds the 1024-element evaluation limit or is not constant")
            sizes.append(size.n)
        self.array_elements += total
        self.scopes[-1][node.name] = self.array_initializer(node.init, sizes, typ)

    def array_initializer(self, init, sizes, typ):
        self.tick()
        if isinstance(init, C.Constant) and init.type == "string":
            if len(sizes) != 1 or typ[0] != 8:
                raise Unsupported("string initializer requires an 8-bit integer row")
            # Decode C escapes explicitly: Python's hexadecimal escape width differs.
            text = init.value
            if not text.startswith('"') or not text.endswith('"'):
                raise Unsupported("only narrow ASCII string initializers are supported")
            tokens = re.findall(r'[^"\\]|\\(?:[0-7]{1,3}|x[0-9a-fA-F]+|[\'"?\\abfnrtv])', text[1:-1])
            if "".join(tokens) != text[1:-1]:
                raise Unsupported("unsupported C string escape")
            escapes = dict(zip("abfnrtv", (7, 8, 12, 10, 13, 9, 11)))
            numbers = []
            for token in tokens:
                if token.startswith('\\x'):
                    number = int(token[2:], 16)
                elif token.startswith('\\') and token[1] in "01234567":
                    number = int(token[1:], 8)
                elif token.startswith('\\'):
                    number = escapes.get(token[1], ord(token[1]))
                else:
                    number = ord(token)
                if number > 127:
                    raise Unsupported("only ASCII string initializers are supported")
                numbers.append(number)
            if len(numbers) > sizes[0]:
                raise Unsupported("string initializer exceeds row size")
            # C permits exact-fit strings without the trailing NUL; otherwise zero-fill.
            return ConstantArray(tuple(integer(Value(n), *typ) for n in
                                       numbers + [0] * (sizes[0] - len(numbers))))
        if init is not None and not isinstance(init, C.InitList):
            raise Unsupported("nested arrays require explicitly braced row initializers")
        items = (init.exprs or []) if init is not None else []
        if len(items) > sizes[0]:
            raise Unsupported("too many constant array initializers")
        items = items + [None] * (sizes[0] - len(items))
        if len(sizes) > 1:
            return ConstantArray(tuple(self.array_initializer(item, sizes[1:], typ) for item in items))
        values = []
        for item in items:
            value = integer(self.expr(item) if item is not None else Value(0), *typ)
            if value.deps:
                raise Unsupported("constant array initializer depends on runtime state")
            values.append(value)
        return ConstantArray(tuple(values))

    def check_unused_table(self, node):
        # An unreferenced static object cannot contribute to callback writes.
        # Validate its declaration without allocating its rows or zero-fill.
        if any(isinstance(n, (C.FuncCall, C.Assignment, C.NamedInitializer)) or
               (isinstance(n, C.UnaryOp) and n.op in ("++", "--", "p++", "p--"))
               for n in walk(node, self.module.macros)):
            raise Unsupported("unused static table must have a side-effect-free declaration")
        element = node.type
        while isinstance(element, C.ArrayDecl):
            size = self.expr(element.dim)
            if size.deps or size.n <= 0:
                raise Unsupported("unused static table requires positive constant dimensions")
            element = element.type
        typ = type_info(element)
        if not isinstance(node.init, C.InitList) and not (
                isinstance(node.init, C.Constant) and node.init.type == "string"):
            raise Unsupported("unused static table requires an initializer")
        pending = [node.init]
        while pending:
            self.tick()
            item = pending.pop()
            if isinstance(item, C.InitList):
                pending.extend(item.exprs or [])
            elif isinstance(item, C.Constant) and item.type == "string":
                if typ[0] != 8:
                    raise Unsupported("unused string table requires 8-bit integer elements")
            elif integer(self.expr(item), *typ).deps:
                raise Unsupported("unused static table initializer depends on runtime state")

    def run(self, name):
        body = self.module.functions[name].body
        statements = list(body.block_items or [])
        if not statements or not isinstance(statements[-1], C.Return):
            raise Unsupported("callback must end with return 0")
        if not isinstance(statements[-1].expr, C.Constant) or statements[-1].expr.value != "0":
            raise Unsupported("callback must return 0")
        self.scopes.append({})
        self.types.append({})
        self.frame = name == self.module.events["bigpemu_register_event_emu_thread_frame"]
        try:
            for statement in statements[:-1]: self.stmt(statement)
        finally:
            self.scopes.pop()
            self.types.pop()
            self.frame = False


class Module:
    def __init__(self, source):
        if len(source) > 1024*1024: raise Unsupported("source exceeds 1 MiB")
        self.parser = c_parser.CParser()
        self.macros = {}
        clean = uncomment(source.replace("\\\n", ""))
        lines = []
        for line in clean.splitlines():
            if not line.lstrip().startswith("#"):
                lines.append(line); continue
            define = re.fullmatch(r"\s*#\s*define\s+([A-Za-z_]\w*)\s+(.+)", line)
            if define:
                name, expr = define.groups()
                tree = self.parser.parse(TYPES + "void value(void) { return (" + expr + "); }")
                if name in self.macros: raise Unsupported("duplicate macro " + name)
                self.macros[name] = tree.ext[-1].body.block_items[0].expr
            elif not re.fullmatch(r'\s*#\s*include\s*["<]bigpcrt\.h[">]\s*', line):
                raise Unsupported("unsupported preprocessor directive: " + line.strip())
        tree = self.parser.parse(TYPES + "\n" + "\n".join(lines))
        self.functions = {n.decl.name: n for n in tree.ext if isinstance(n, C.FuncDef)}
        if len(self.functions) != sum(isinstance(n, C.FuncDef) for n in tree.ext):
            raise Unsupported("duplicate function definition")
        if "bigp_init" not in self.functions: raise Unsupported("missing bigp_init")
        self.handles, self.settings, self.events = {}, [], {}
        self.messages, self.dropped_messages, self.ignored_calls = [], [], set()  # per convert()
        categories = []
        registered = set()
        parameter_args = {}
        init_constants, parameter_prefixes, init_handles = [], {}, set()
        init_body = self.functions["bigp_init"].body
        init_top_level = {id(node) for node in init_body.block_items or []}

        class Registrations(C.NodeVisitor):
            def visit_FuncCall(visitor, node):
                name = getattr(node.name, "name", "")
                if name not in INIT_CALLS:
                    raise Unsupported("unsupported initialisation call " + name, gap_call(name))
                if name.startswith("bigpemu_register_") and id(node) not in registered:
                    raise Unsupported("registration must assign a handle")
                visitor.generic_visit(node)

            def visit_If(visitor, node):
                raise Unsupported("conditional initialisation requires manual review")

            def visit_For(visitor, node):
                raise Unsupported("initialisation loop requires manual review")

            visit_While = visit_For
            visit_DoWhile = visit_For
            visit_Switch = visit_If

            def visit_Return(visitor, node):
                raise Unsupported("initialisation return requires manual review")

            def visit_Decl(visitor, node):
                if not isinstance(node.init, C.FuncCall):
                    if (not node.name or id(node) not in init_top_level or node.storage
                            or not isinstance(node.type, C.TypeDecl) or "const" not in node.type.quals
                            or node.init is None):
                        raise Unsupported("unsupported initialisation declaration")
                    type_info(node.type)
                    if any(isinstance(n, (C.FuncCall, C.Assignment)) or
                           (isinstance(n, C.UnaryOp) and n.op in ("++", "--", "p++", "p--"))
                           for n in walk(node.init, self.macros)):
                        raise Unsupported("initialisation constant must have a pure integer initializer")
                    init_constants.append(node)
                    return
                scan(node.name, node.init)
                visitor.generic_visit(node)

            def visit_UnaryOp(visitor, node):
                if node.op in ("++", "--", "p++", "p--"):
                    raise Unsupported("initialisation mutation requires manual review")
                visitor.generic_visit(node)

            def visit_Assignment(visitor, node):
                if not (node.op == "=" and isinstance(node.rvalue, C.FuncCall) and isinstance(node.lvalue, C.ID)):
                    raise Unsupported("unsupported initialisation assignment")
                scan(node.lvalue.name, node.rvalue)
                visitor.generic_visit(node)

        def scan(handle, call):
            registered.add(id(call))
            init_handles.add(handle)
            name = getattr(call.name, "name", "")
            args = call.args.exprs if call.args else []
            if name == "bigpemu_register_setting_category" and len(args) == 2:
                categories.append(ast.literal_eval(args[1].value))
            elif name.startswith("bigpemu_register_setting_") and len(args) >= 3:
                if handle in self.handles: raise Unsupported("duplicate setting handle")
                key = len(self.settings)+1
                self.handles[handle] = key
                boolean = name == "bigpemu_register_setting_bool"
                self.settings.append((key, ast.literal_eval(args[1].value), boolean))
                if not boolean:
                    if name not in ("bigpemu_register_setting_int", "bigpemu_register_setting_int_full"):
                        raise Unsupported("unsupported setting registration " + name)
                    if (name.endswith("_full") and len(args) != 6) or (not name.endswith("_full") and len(args) != 3):
                        raise Unsupported("malformed integer setting registration")
                    parameter_args[key] = args[2:]
                    parameter_prefixes[key] = len(init_constants)
            elif name.startswith("bigpemu_register_event_") and len(args) == 2:
                if name in self.events: raise Unsupported("duplicate event registration")
                self.events[name] = args[-1].name

        Registrations().visit(init_body)
        required = {"bigpemu_register_event_sw_loaded", "bigpemu_register_event_emu_thread_frame"}
        if set(self.events) != required or len(set(categories)) != 1 or not self.settings:
            raise Unsupported("expected one title, settings, and load/frame callbacks only")
        if any(name not in self.functions for name in self.events.values()):
            raise Unsupported("missing registered callback body")
        self.title = categories[0]
        labels = [label for _, label, _ in self.settings]
        if len(set(label.casefold() for label in labels)) != len(labels) or any(
                re.search(r" \(part [0-9]+\)$", label, re.I) for label in labels):
            raise Unsupported("setting labels would be ambiguous in the .cht frontend")
        if len(self.settings) > 10:
            raise Unsupported("at most 10 settings supported (exhaustive interaction check)")
        self.globals = []
        for node in tree.ext:
            if isinstance(node, C.Decl) and not isinstance(node.type, C.FuncDecl):
                if isinstance(getattr(node.type, "type", None), C.Enum):
                    enum = node.type.type
                elif isinstance(node.type, C.Enum): enum = node.type
                else: enum = None
                self.globals.append((node, enum))

        # Count all syntactic references, including macros and code that is not
        # reached by conversion. A declaration name itself is not an ID node.
        references = {n.name for n in walk(tree, self.macros) if isinstance(n, C.ID)}
        self.unused_tables = set()
        for node, _ in self.globals:
            element, dimensions = node.type, []
            while isinstance(element, C.ArrayDecl):
                dimensions.append(element.dim)
                element = element.type
            if (dimensions and all(dim is not None for dim in dimensions)
                    and node.storage == ["static"] and node.name not in references
                    and isinstance(element, C.TypeDecl) and "const" in element.quals
                    and "volatile" not in element.quals):
                self.unused_tables.add(id(node))

        self.parameter_args = parameter_args
        self.init_constants, self.parameter_prefixes, self.init_handles = init_constants, parameter_prefixes, init_handles
        reserved = set(self.macros) | init_handles | set(self.functions) | INIT_CALLS
        for node, enum in self.globals:
            if node.name: reserved.add(node.name)
            if enum: reserved.update(item.name for item in enum.values.enumerators)
        for node in init_constants:
            if node.name in reserved:
                raise Unsupported("initialisation constant name is duplicated or shadows another identifier")
            reserved.add(node.name)

    def prepare_settings(self):
        # Defer global evaluation until convert() so failures retain the cartridge
        # identity in the pack generator's per-cartridge error report.
        # Evaluate only scalar constant declarations, never script calls or bigp_init.
        # Capture the locals visible at each integer registration, before later declarations.
        self.off_values = {key: 0 for key, _, _ in self.settings}
        self.parameters = {}
        constants = self.global_runtime(0, {}) if self.parameter_args or self.init_constants else None
        snapshots = {0: {}}
        if constants:
            # Registration handles are synthetic identifiers, not constant integers.
            for name in self.init_handles: constants.scopes[0].pop(name, None)
            constants.scopes.append({})
            constants.types.append({})
            for count, node in enumerate(self.init_constants, 1):
                constants.stmt(node)
                if count in self.parameter_prefixes.values():
                    snapshots[count] = dict(constants.scopes[-1])
        for key, args in self.parameter_args.items():
            constants.scopes[-1] = snapshots[self.parameter_prefixes[key]]
            for arg in args:
                if any(isinstance(n, (C.FuncCall, C.Assignment)) or
                       (isinstance(n, C.UnaryOp) and n.op in ("++", "--", "p++", "p--")) for n in walk(arg, self.macros)):
                    raise Unsupported("integer setting metadata must be constant")
            default = integer(constants.expr(args[0])).n
            try:
                if len(args) != 4:
                    raise Unsupported("parameter setting requires explicit bounds and step")
                low, high, step = [integer(constants.expr(arg)).n for arg in args[1:]]
                if step <= 0 or low > high or not low <= default <= high or (default-low) % step:
                    raise Unsupported("invalid integer setting default/range/step")
                self.off_values[key] = low
                count = (high-low)//step + 1
                if count == 1:
                    raise Unsupported("parameter setting has no selectable values above its inactive minimum")
                if count > MAX_INTEGER_VALUES:
                    raise Unsupported(f"parameter setting exceeds {MAX_INTEGER_VALUES}-value limit")
                self.parameters[key] = tuple(range(low, high+1, step))
            except Unsupported as error:
                self.parameters[key] = str(error)

    def notify(self, args, shown):
        """printf_notify: a lone string literal reached at load for this cartridge becomes a toast message;
        anything else (format arguments, frame callback, setting-dependent) is recorded as not shown."""
        node = args[0] if args else None
        text = None
        if isinstance(node, C.Constant) and node.type == "string":
            try: text = " ".join(re.sub(r"[\x00-\x1f]", " ", ast.literal_eval(node.value)).split())
            except (ValueError, SyntaxError): pass
        if shown and len(args) == 1 and text is not None:
            text = text[:MAX_MESSAGE_CHARS]
            if not text or text in self.messages: return
            if len(self.messages) < MAX_MESSAGES:
                self.messages.append(text)
                return
        note = "script message not shown: " + (text[:NOTE_CHARS] if text else "dynamic")
        if note not in self.dropped_messages: self.dropped_messages.append(note)

    def global_runtime(self, digest, selected):
        runtime = Runtime(self, digest, selected)
        for node, enum in self.globals:
            if enum:
                number = -1
                for item in enum.values.enumerators:
                    number = runtime.expr(item.value).n if item.value else number+1
                    runtime.put(item.name, Value(number), declare=True)
                if node.name: runtime.put(node.name, Value(0), declare=True)
            else: runtime.stmt(node)
        for name, handle in self.handles.items():
            runtime.put(name, Value(handle))
        return runtime

    def runtime(self, digest, selected):
        runtime = self.global_runtime(digest, selected)
        runtime.run(self.events["bigpemu_register_event_sw_loaded"])
        if runtime.writes: raise Unsupported("load-time memory changes are not frame cheats")
        return runtime

    def convert(self, digest):
        self.messages, self.dropped_messages, self.ignored_calls = [], [], set()
        self.prepare_settings()
        baseline = self.runtime(digest, None)
        baseline.run(self.events["bigpemu_register_event_emu_thread_frame"])
        results, groups = [], []
        for key, label, boolean in self.settings:
            options = (1,) if boolean else self.parameters[key]
            if isinstance(options, str):
                results.append({"name": label, "unsupported": options})
                groups.append([])
                continue
            group = []
            for value in options:
                if not boolean and value == self.off_values[key]:
                    continue  # Off uses the range minimum, verified to have no RAM effects.
                result = {"name": label if boolean else f"{label}: {value}"}
                try:
                    runtime = self.runtime(digest, {key: value})
                    runtime.run(self.events["bigpemu_register_event_emu_thread_frame"])
                    if not runtime.writes: raise Unsupported("no writes activated (check ROM revision/hash)")
                    result["code"] = "+".join(f"{a:08X}:{v:0{w*2}X}" for a, v, w in runtime.writes)
                    result["writes"] = len(runtime.writes)
                    group.append((key, value, result, runtime.writes))
                except Unsupported as error:
                    result["unsupported"] = str(error)
                results.append(result)
            groups.append(group)

        labels = [r["name"].casefold() for r in results]
        if len(labels) != len(set(labels)):
            raise Unsupported("expanded setting labels would be ambiguous in the .cht frontend")

        def image(writes):
            out = {}
            for address, value, width in writes:
                for i, byte in enumerate(value.to_bytes(width, "big")):
                    out[address+i] = byte
            return out

        # Several values can be enabled in a .cht. Each must overwrite every byte
        # of the preceding choice so only the last value survives in frontend order.
        for group in groups:
            if len({frozenset(image(writes)) for _, _, _, writes in group}) > 1:
                for _, _, result, _ in group:
                    result.pop("code")
                    result.pop("writes")
                    result["unsupported"] = "integer choices write different bytes; cannot preserve last-choice behaviour"
                group.clear()

        # Exhaust every off/value combination of original settings (not 2**N
        # expanded toggles). Same-setting subsets reduce to their last choice above.
        combinations = 1
        for group in groups:
            combinations *= len(group)+1
        if combinations > MAX_SETTING_COMBINATIONS:
            raise Unsupported(f"expanded settings exceed {MAX_SETTING_COMBINATIONS}-combination interaction limit")
        for combination in itertools.product(*[[None] + group for group in groups]):
            active = [choice for choice in combination if choice is not None]
            if len(active) < 2:
                continue
            selected = {key: value for key, value, _, _ in active}
            runtime = self.runtime(digest, selected)
            runtime.run(self.events["bigpemu_register_event_emu_thread_frame"])
            expected = [write for _, _, _, writes in active for write in writes]
            if image(runtime.writes) != image(expected):
                raise Unsupported("toggles interact; independent freezes cannot preserve combined behaviour")
        return results


def quoted(value):
    if any(c in value for c in ('"', "\\")) or any(ord(c) < 32 for c in value):
        raise Unsupported("quote, backslash or control character in metadata")
    return '"' + value + '"'


def cht_header(crc, comment_lines):
    """Comment lines (prefixed "# ") then the revision guard; no `cheats = N` (see cht_entries)."""
    return ["# " + line for line in comment_lines] + ["vj_rom_crc32 = " + quoted(f"{crc:08X}")]


def cht_entries(entries):
    lines = ["cheats = " + str(len(entries)), ""]
    for i, entry in enumerate(entries):
        lines += [f"cheat{i}_desc = " + quoted(entry["name"]),
                  f"cheat{i}_code = " + quoted(entry["code"]),
                  f"cheat{i}_enable = false", ""]
    return lines


def cht_text(title, crc, entries, source_name, source_sha):
    return "\n".join(cht_header(crc, [
        "Locally converted community cheats; nothing is enabled automatically.",
        "Source: " + source_name.replace("\n", " ").replace("\r", " "),
        "Source SHA256: " + source_sha,
        "Cartridge revision guard is enforced by the Virtual Jaguar app."]) + cht_entries(entries))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="community-authored C source, never a compiled script")
    parser.add_argument("--rom", type=Path, required=True, help="matching native cartridge dump")
    parser.add_argument("--output", type=Path, required=True, help="new .cht file; existing files are refused")
    parser.add_argument("--report", type=Path, required=True, help="new JSON conversion/provenance report")
    parser.add_argument("--allow-partial", action="store_true", help="export complete supported toggles; report every omission")
    args = parser.parse_args(argv)
    try:
        if args.output.exists() or args.report.exists() or args.output.resolve() == args.report.resolve():
            raise Unsupported("output/report must be distinct new files")
        if args.source.suffix.lower() != ".c": raise Unsupported("input must be a .c source file")
        if args.source.stat().st_size > 1024*1024: raise Unsupported("source exceeds 1 MiB")
        raw = args.source.read_bytes()
        data = rom_bytes(args.rom)
        module = Module(raw.decode("utf-8-sig"))
        digest = fnv1a64(data)
        entries = module.convert(digest)
        supported = [entry for entry in entries if "code" in entry]
        report = {"format": 1, "title": module.title, "source": str(args.source),
                  "source_sha256": hashlib.sha256(raw).hexdigest(), "rom_crc32": f"{zlib.crc32(data):08X}",
                  "rom_fnv1a64": f"{digest:016X}", "cheats": entries}
        # Report before refusing an incomplete conversion, so omissions are reviewable.
        with args.report.open("x", encoding="utf-8") as report_file:
            report_file.write(json.dumps(report, indent=2)+"\n")
        if not supported or (len(supported) != len(entries) and not args.allow_partial):
            print("Incomplete conversion; no .cht written. See " + str(args.report), file=sys.stderr)
            return 1
        text = cht_text(module.title, zlib.crc32(data), supported, str(args.source), report["source_sha256"])
        with args.output.open("x", encoding="utf-8") as output_file:
            output_file.write(text)
        print(f"Converted {len(supported)}/{len(entries)} toggles for {module.title}: {args.output}")
        return 0
    except (Unsupported, ValueError, OSError, RecursionError, KeyError, AttributeError, IndexError,
            c_parser.ParseError) as error:
        print("Cannot convert: " + str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
