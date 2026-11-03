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
import pytest
from someip_codegen import CodegenError, Model, max_size


def model(types, **extra):
    return Model({"namespace": "ns", "types": types, **extra})


def test_max_size_covers_length_fields_bom_and_terminator():
    m = model(
        {
            "Inner": {"length_field": 16, "struct": [{"name": "a", "type": "uint8"}]},
            "Outer": {
                "struct": [
                    {"name": "s", "type": "string<=4"},
                    {"name": "v", "type": "Inner[<=2]", "length_field": 8},
                    {"name": "f", "type": "uint32[3]"},
                ]
            },
        }
    )
    assert max_size(m.named["Inner"]) == 3
    assert max_size(m.named["Outer"]) == (4 + 3 + 4 + 1) + (1 + 2 * 3) + 12


def test_types_are_ordered_dependencies_first():
    m = model({"Outer": {"struct": [{"name": "i", "type": "Inner"}]}, "Inner": {"enum": "uint8", "values": {"X": 0}}})
    assert m.order == ["Inner", "Outer"]


@pytest.mark.parametrize(
    ("types", "message"),
    [
        ({"A": {"enum": "float32", "values": {"X": 1}}}, "enum base"),
        ({"A": {"struct": [{"name": "a", "type": "B"}]}}, "unknown type"),
        ({"A": {"struct": [{"name": "a", "type": "A"}]}}, "recursive"),
        ({"A": {"struct": [{"name": "a", "type": "string<=300", "length_field": 8}]}}, "does not fit"),
        ({"A": {"struct": [{"name": "a", "type": "uint8[2]", "length_field": 8}]}}, "fixed-size"),
        ({"A": {"struct": [{"name": "a", "type": "uint8", "length_field": 8}]}}, "only allowed"),
        ({"A": {"struct": [{"name": "a", "type": "uint8[<=0]"}]}}, "greater than 0"),
        ({"A": {"struct": []}}, "no fields"),
        ({"A": {"union": []}}, "expected"),
    ],
)
def test_invalid_descriptions_are_rejected(types, message):
    with pytest.raises(CodegenError, match=message):
        model(types)


def test_invalid_namespace_is_rejected():
    with pytest.raises(CodegenError, match="namespace"):
        Model({"namespace": "a::", "types": {"A": {"enum": "uint8", "values": {"X": 0}}}})
