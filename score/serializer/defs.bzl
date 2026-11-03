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
"""Macros to build serializer libraries with generated SOME/IP codecs."""

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

_CODEGEN = Label("//score/serializer/codegen:someip_codegen")
_CODEC = Label("//score/serializer:codec")
_SERIALIZER = Label("//score/serializer:null_serializer_impl")
_EXPORTED_SYMBOLS = Label("//score/serializer:exported_symbols.lds")

def someip_codecs(name, types, visibility = None, **kwargs):
    """Generates C++ types and SOME/IP codecs from a JSON type description.

    Creates :<name>_types (header <name>_types.h for applications) and :<name> (the codecs).

    Args:
        name: target name.
        types: JSON type description.
        visibility: visibility of the generated targets.
        **kwargs: common attributes.
    """
    header = name + "_types.h"
    source = name + "_codecs.cpp"
    package = native.package_name()
    include = package + "/" + header if package else header

    native.genrule(
        name = name + "_gen",
        srcs = [types],
        outs = [header, source],
        cmd = "$(location {codegen}) --types $(location {types}) --header $(location {header}) --source $(location {source}) --include {include}".format(
            codegen = _CODEGEN,
            types = types,
            header = header,
            source = source,
            include = include,
        ),
        tools = [_CODEGEN],
        **kwargs
    )
    cc_library(
        name = name + "_types",
        hdrs = [header],
        visibility = visibility,
        **kwargs
    )
    cc_library(
        name = name,
        srcs = [source],
        deps = [":" + name + "_types", _CODEC],
        visibility = visibility,
        **kwargs
    )

def someip_serializer(name, codecs, visibility = None, **kwargs):
    """Builds score_com_serializer.so with the null serializer and the given codecs.

    Args:
        name: target name.
        codecs: a someip_codecs target.
        visibility: visibility of the shared library.
        **kwargs: common attributes.
    """
    cc_shared_library(
        name = name,
        additional_linker_inputs = [_EXPORTED_SYMBOLS],
        shared_lib_name = "score_com_serializer.so",
        user_link_flags = ["-Wl,--version-script=$(location {})".format(_EXPORTED_SYMBOLS)],
        visibility = visibility,
        deps = [_SERIALIZER, codecs],
        **kwargs
    )
