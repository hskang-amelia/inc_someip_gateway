# *******************************************************************************
# Copyright (c) 2026 Contributors to the Eclipse Foundation
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License Version 2.0 which is available at
# https://www.apache.org/licenses/LICENSE-2.0
#
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************
"""Generates C++ types and SOME/IP codecs from a JSON type description.

Each named type becomes one Codec with type_name "<namespace>::<Name>", selectable via
TypedSerializerConfig. See score/serializer/docs/index.rst for the input format.
"""

import argparse
import json
import re
import sys

PRIMITIVES = {
    "bool": ("bool", 1),
    "uint8": ("std::uint8_t", 1),
    "uint16": ("std::uint16_t", 2),
    "uint32": ("std::uint32_t", 4),
    "uint64": ("std::uint64_t", 8),
    "int8": ("std::int8_t", 1),
    "int16": ("std::int16_t", 2),
    "int32": ("std::int32_t", 4),
    "int64": ("std::int64_t", 8),
    "float32": ("float", 4),
    "float64": ("double", 8),
}
LENGTH_WIDTHS = (0, 8, 16, 32)
IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
NAMESPACE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*$")


class CodegenError(Exception):
    pass


class Type:
    def __init__(self, kind, **attributes):
        self.kind = kind
        self.__dict__.update(attributes)


class Model:
    def __init__(self, description):
        self.namespace = description.get("namespace", "")
        if not NAMESPACE.match(self.namespace):
            raise CodegenError(f"invalid namespace '{self.namespace}'")
        byte_order = description.get("byte_order", "big")
        if byte_order not in ("big", "little"):
            raise CodegenError("byte_order must be 'big' or 'little'")
        self.big_endian = byte_order == "big"
        self._raw = description.get("types") or {}
        if not self._raw:
            raise CodegenError("no types")
        self.named = {}
        self.order = []
        self._resolving = set()
        for name in self._raw:
            self._named(name)

    def _named(self, name):
        if name in self.named:
            return self.named[name]
        if name not in self._raw:
            raise CodegenError(f"unknown type '{name}'")
        if not IDENTIFIER.match(name):
            raise CodegenError(f"invalid type name '{name}'")
        if name in self._resolving:
            raise CodegenError(f"recursive type '{name}'")
        self._resolving.add(name)
        definition = self._raw[name]
        if "enum" in definition:
            base = definition["enum"]
            if base not in PRIMITIVES or base in ("bool", "float32", "float64"):
                raise CodegenError(f"{name}: enum base must be an integer type")
            values = definition.get("values") or {}
            if not values or not all(IDENTIFIER.match(k) for k in values):
                raise CodegenError(f"{name}: enum needs values with valid names")
            result = Type("enum", name=name, base=base, values=values)
        elif "struct" in definition:
            fields = []
            for field in definition["struct"]:
                if not IDENTIFIER.match(field.get("name", "")):
                    raise CodegenError(f"{name}: invalid field name '{field.get('name')}'")
                where = f"{name}.{field['name']}"
                fields.append((field["name"], self._parse(field["type"], where, field.get("length_field"))))
            if not fields:
                raise CodegenError(f"{name}: struct has no fields")
            length_field = definition.get("length_field", 0)
            if length_field not in LENGTH_WIDTHS:
                raise CodegenError(f"{name}: length_field must be one of {LENGTH_WIDTHS}")
            result = Type("struct", name=name, fields=fields, length_field=length_field)
        else:
            raise CodegenError(f"{name}: expected 'enum' or 'struct'")
        self._resolving.discard(name)
        self.named[name] = result
        self.order.append(name)
        _check_length_fields(result, name)
        return result

    def _parse(self, expression, where, length_field):
        expression = str(expression).replace(" ", "")
        width = 32 if length_field is None else length_field
        match = re.match(r"^string<=(\d+)$", expression)
        if match:
            _check_width(width, where)
            return Type("string", capacity=_positive(match.group(1), where), width=width)
        match = re.match(r"^(.+)\[(<=)?(\d+)\]$", expression)
        if match:
            element = self._parse(match.group(1), where, None)
            count = _positive(match.group(3), where)
            if match.group(2):
                _check_width(width, where)
                return Type("vector", element=element, capacity=count, width=width)
            if length_field is not None:
                raise CodegenError(f"{where}: length_field is not allowed on fixed-size arrays")
            return Type("array", element=element, count=count)
        if length_field is not None:
            raise CodegenError(f"{where}: length_field is only allowed on dynamic arrays and strings")
        if expression in PRIMITIVES:
            return Type("primitive", name=expression)
        return self._named(expression)


def _positive(text, where):
    value = int(text)
    if value <= 0:
        raise CodegenError(f"{where}: size must be greater than 0")
    return value


def _check_width(width, where):
    if width not in (8, 16, 32):
        raise CodegenError(f"{where}: length_field must be 8, 16 or 32")


def _check_length_fields(t, where):
    if t.kind == "struct":
        if t.length_field and max_size(t) - t.length_field // 8 >= 1 << t.length_field:
            raise CodegenError(f"{where}: content does not fit the {t.length_field}-bit length field")
        for name, field in t.fields:
            _check_length_fields(field, f"{where}.{name}")
    elif t.kind in ("vector", "string"):
        if max_size(t) - t.width // 8 >= 1 << t.width:
            raise CodegenError(f"{where}: content does not fit the {t.width}-bit length field")


def max_size(t):
    if t.kind == "primitive":
        return PRIMITIVES[t.name][1]
    if t.kind == "enum":
        return PRIMITIVES[t.base][1]
    if t.kind == "struct":
        return t.length_field // 8 + sum(max_size(f) for _, f in t.fields)
    if t.kind == "array":
        return t.count * max_size(t.element)
    if t.kind == "vector":
        return t.width // 8 + t.capacity * max_size(t.element)
    return t.width // 8 + 3 + t.capacity + 1  # string: BOM + content + terminator


def cpp_type(t):
    if t.kind == "primitive":
        return PRIMITIVES[t.name][0]
    if t.kind in ("enum", "struct"):
        return t.name
    if t.kind == "array":
        return f"std::array<{cpp_type(t.element)}, {t.count}>"
    if t.kind == "vector":
        return f"BoundedVector<{cpp_type(t.element)}, {t.capacity}>"
    return f"BoundedString<{t.capacity}>"


def _unsigned(name):
    return "u" + name if name.startswith("int") else name


def _block(body, indent):
    pad = " " * indent
    return f"{pad}{{\n{body}{pad}}}\n"


def encode(t, value, indent, depth=0):
    pad = " " * indent
    if t.kind == "primitive":
        width = PRIMITIVES[t.name][1]
        if t.name == "bool":
            return f"{pad}if (!wire::PutBool(w, {value})) {{\n{pad}    return false;\n{pad}}}\n"
        if t.name == "float32":
            raw = f"wire::BitCast<std::uint32_t>({value})"
        elif t.name == "float64":
            raw = f"wire::BitCast<std::uint64_t>({value})"
        else:
            raw = f"static_cast<{PRIMITIVES[_unsigned(t.name)][0]}>({value})"
        return f"{pad}if (!w.PutUint({raw}, {width}U)) {{\n{pad}    return false;\n{pad}}}\n"
    if t.kind in ("enum", "struct"):
        return f"{pad}if (!Encode{t.name}(w, {value})) {{\n{pad}    return false;\n{pad}}}\n"
    if t.kind == "array":
        item = f"e{depth}"
        return (
            f"{pad}for (const auto& {item} : {value}) {{\n"
            + encode(t.element, item, indent + 4, depth + 1)
            + f"{pad}}}\n"
        )
    if t.kind == "vector":
        at, index = f"at{depth}", f"i{depth}"
        pad = " " * (indent + 4)
        return _block(
            f"{pad}if ({value}.size > {t.capacity}U) {{\n{pad}    return false;\n{pad}}}\n"
            f"{pad}const std::size_t {at} = w.Position();\n"
            f"{pad}if (!w.Reserve({t.width // 8}U)) {{\n{pad}    return false;\n{pad}}}\n"
            f"{pad}for (std::uint32_t {index} = 0U; {index} < {value}.size; ++{index}) {{\n"
            + encode(t.element, f"{value}.data[{index}]", indent + 8, depth + 1)
            + f"{pad}}}\n{pad}w.PatchLength({at}, {t.width // 8}U);\n",
            indent,
        )
    return (
        f"{pad}if (!wire::PutString(w, {value}.data.data(), {value}.size, {t.capacity}U, "
        f"{t.width // 8}U)) {{\n{pad}    return false;\n{pad}}}\n"
    )


def decode(t, value, indent, reader="r", depth=0):
    pad = " " * indent
    if t.kind == "primitive":
        return _block(_decode_primitive(t, value, indent + 4, reader, depth), indent)
    if t.kind in ("enum", "struct"):
        return f"{pad}if (!Decode{t.name}({reader}, {value})) {{\n{pad}    return false;\n{pad}}}\n"
    if t.kind == "array":
        item = f"e{depth}"
        return (
            f"{pad}for (auto& {item} : {value}) {{\n"
            + decode(t.element, item, indent + 4, reader, depth + 1)
            + f"{pad}}}\n"
        )
    if t.kind == "vector":
        sub = f"sub{depth}"
        pad = " " * (indent + 4)
        return _block(
            f"{pad}wire::Reader {sub}{{nullptr, 0U, true}};\n"
            f"{pad}if (!{reader}.SubReader({t.width // 8}U, {sub})) {{\n{pad}    return false;\n{pad}}}\n"
            f"{pad}{value}.size = 0U;\n"
            f"{pad}while ({sub}.Remaining() > 0U) {{\n"
            f"{pad}    if ({value}.size >= {t.capacity}U) {{\n{pad}        return false;\n{pad}    }}\n"
            + decode(t.element, f"{value}.data[{value}.size]", indent + 8, sub, depth + 1)
            + f"{pad}    ++{value}.size;\n{pad}}}\n",
            indent,
        )
    return (
        f"{pad}if (!wire::GetString({reader}, {value}.data.data(), {value}.size, {t.capacity}U, "
        f"{t.width // 8}U)) {{\n{pad}    return false;\n{pad}}}\n"
    )


def _decode_primitive(t, value, indent, reader, depth):
    pad = " " * indent
    width = PRIMITIVES[t.name][1]
    raw = f"raw{depth}"
    head = f"{pad}std::uint64_t {raw} = 0U;\n{pad}if (!{reader}.GetUint({raw}, {width}U)"
    if t.name == "bool":
        return f"{head} || {raw} > 1U) {{\n{pad}    return false;\n{pad}}}\n{pad}{value} = {raw} != 0U;\n"
    if t.name == "float32":
        conv = f"wire::BitCast<float>(static_cast<std::uint32_t>({raw}))"
    elif t.name == "float64":
        conv = f"wire::BitCast<double>({raw})"
    else:
        conv = f"static_cast<{PRIMITIVES[_unsigned(t.name)][0]}>({raw})"
        if t.name.startswith("int"):
            conv = f"static_cast<{PRIMITIVES[t.name][0]}>({conv})"
    return f"{head}) {{\n{pad}    return false;\n{pad}}}\n{pad}{value} = {conv};\n"


LICENSE = """/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

// Generated by someip_codegen.py. Do not edit.
"""


def generate_header(model, guard):
    out = [
        LICENSE,
        f"#ifndef {guard}\n#define {guard}\n\n#include <array>\n#include <cstdint>\n#include <type_traits>\n\n",
        f"namespace {model.namespace} {{\n\n",
        (
            "template <typename T, std::uint32_t N>\nstruct BoundedVector {\n"
            "    std::uint32_t size{};\n    std::array<T, N> data{};\n"
            "    bool operator==(const BoundedVector& o) const { return size == o.size && data == o.data; }\n"
            "    bool operator!=(const BoundedVector& o) const { return !(*this == o); }\n};\n\n"
            "/// UTF-8 content without BOM and terminator.\n"
            "template <std::uint32_t N>\nstruct BoundedString {\n"
            "    std::uint32_t size{};\n    std::array<char, N> data{};\n"
            "    bool operator==(const BoundedString& o) const { return size == o.size && data == o.data; }\n"
            "    bool operator!=(const BoundedString& o) const { return !(*this == o); }\n};\n\n"
        ),
    ]
    for name in model.order:
        t = model.named[name]
        if t.kind == "enum":
            out.append(f"enum class {name} : {PRIMITIVES[t.base][0]} {{\n")
            out += [f"    {key} = {value},\n" for key, value in t.values.items()]
            out.append("};\n\n")
        else:
            out.append(f"struct {name} {{\n")
            out += [f"    {cpp_type(f)} {fname}{{}};\n" for fname, f in t.fields]
            cmp = " && ".join(f"{fname} == o.{fname}" for fname, _ in t.fields) or "true"
            out.append(f"    bool operator==(const {name}& o) const {{ return {cmp}; }}\n")
            out.append(f"    bool operator!=(const {name}& o) const {{ return !(*this == o); }}\n")
            out.append(f"}};\nstatic_assert(std::is_trivially_copyable_v<{name}>);\n\n")
    out.append(f"}}  // namespace {model.namespace}\n\n#endif  // {guard}\n")
    return "".join(out)


def generate_source(model, header_include):
    big = "true" if model.big_endian else "false"
    out = [
        LICENSE,
        f'#include "{header_include}"\n\n',
        "#include <cstddef>\n#include <cstdint>\n#include <new>\n\n",
        '#include "score/serializer/codec.h"\n#include "score/serializer/wire.h"\n\n',
        "namespace {\n\nnamespace wire = score::someip_gateway::serializer::wire;\n",
        f"constexpr bool kBigEndian = {big};\n\n",
    ]
    for name in model.order:
        q = f"{model.namespace}::{name}"
        out.append(f"bool Encode{name}(wire::Writer& w, const {q}& v);\nbool Decode{name}(wire::Reader& r, {q}& v);\n")
    for name in model.order:
        t = model.named[name]
        q = f"{model.namespace}::{name}"
        out.append(f"\nbool Encode{name}(wire::Writer& w, const {q}& v) {{\n")
        if t.kind == "enum":
            out.append(encode(Type("primitive", name=t.base), f"static_cast<{PRIMITIVES[t.base][0]}>(v)", 4))
        else:
            width = t.length_field // 8
            if width:
                out.append(
                    f"    const std::size_t at = w.Position();\n    if (!w.Reserve({width}U)) {{\n        return false;\n    }}\n"
                )
            for fname, f in t.fields:
                out.append(encode(f, f"v.{fname}", 4))
            if width:
                out.append(f"    w.PatchLength(at, {width}U);\n")
        out.append("    return true;\n}\n")
        if t.kind == "enum":
            base = PRIMITIVES[t.base][0]
            out.append(
                f"\nbool Decode{name}(wire::Reader& r, {q}& v) {{\n    {base} value{{}};\n"
                + decode(Type("primitive", name=t.base), "value", 4)
                + f"    v = static_cast<{q}>(value);\n    return true;\n}}\n"
            )
            continue
        width = t.length_field // 8
        out.append(f"\nbool Decode{name}(wire::Reader& {'outer' if width else 'r'}, {q}& v) {{\n")
        if width:
            out.append(
                f"    wire::Reader r{{nullptr, 0U, true}};\n    if (!outer.SubReader({width}U, r)) {{\n        return false;\n    }}\n"
            )
        for fname, f in t.fields:
            out.append(decode(f, f"v.{fname}", 4))
        out.append("    return true;\n}\n")
    for name in model.order:
        q = f"{model.namespace}::{name}"
        out.append(
            f"\nbool Serialize{name}(const void* object, std::uint8_t* buffer, std::size_t size, std::size_t& written) {{\n"
            f"    wire::Writer w{{buffer, size, kBigEndian}};\n"
            f"    if (!Encode{name}(w, *static_cast<const {q}*>(object))) {{\n        return false;\n    }}\n"
            f"    written = w.Position();\n    return true;\n}}\n"
            f"\nbool Deserialize{name}(const std::uint8_t* buffer, std::size_t size, void* object) {{\n"
            f"    wire::Reader r{{buffer, size, kBigEndian}};\n"
            f"    return Decode{name}(r, *::new (object) {q}{{}});\n}}\n"
        )
    out.append("\nconstexpr score::someip_gateway::serializer::Codec kCodecs[] = {\n")
    for name in model.order:
        q = f"{model.namespace}::{name}"
        out.append(
            f'    {{"{q}", {max_size(model.named[name])}U, sizeof({q}), alignof({q}), '
            f"&Serialize{name}, &Deserialize{name}}},\n"
        )
    out.append(
        "};\n\n}  // namespace\n\nnamespace score::someip_gateway::serializer {\n\n"
        "CodecTable GetCodecs() { return {kCodecs, sizeof(kCodecs) / sizeof(kCodecs[0])}; }\n\n"
        "}  // namespace score::someip_gateway::serializer\n"
    )
    return "".join(out)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--types", required=True, help="JSON type description")
    parser.add_argument("--header", required=True, help="output header path")
    parser.add_argument("--source", required=True, help="output source path")
    parser.add_argument("--include", required=True, help="include path of the header in the source")
    args = parser.parse_args(argv)
    try:
        with open(args.types, encoding="utf-8") as file:
            model = Model(json.load(file))
    except (CodegenError, KeyError, ValueError, TypeError) as error:
        sys.exit(f"someip_codegen: {args.types}: {error}")
    guard = re.sub(r"\W", "_", args.include).upper()
    with open(args.header, "w", encoding="utf-8") as file:
        file.write(generate_header(model, guard))
    with open(args.source, "w", encoding="utf-8") as file:
        file.write(generate_source(model, args.include))


if __name__ == "__main__":
    main()
