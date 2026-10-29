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

"""Builds a typed score_com_serializer.so from a JSON type spec."""

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")

def score_com_typed_serializer(name, spec, visibility = None):
    """Generates <name>_types (struct header), <name>_impl and the shared library <name>.

    Args:
      name: target prefix; the header is <name>_types.h.
      spec: JSON type spec, see serializer_gen.py.
      visibility: visibility of the generated targets.
    """
    types_h = name + "_types.h"
    serializer_cc = name + "_serializer.cpp"
    gen = "//score/serializer/typed:serializer_gen"
    lds = "//score/serializer:exported_symbols.lds"

    native.genrule(
        name = name + "_gen",
        srcs = [spec],
        outs = [types_h, serializer_cc],
        tools = [gen],
        cmd = ("$(location {gen}) --spec $(location {spec}) --types-out $(location {h}) " +
               "--serializer-out $(location {cc}) --types-include {inc}").format(
                  gen = gen,
                  spec = spec,
                  h = types_h,
                  cc = serializer_cc,
                  inc = native.package_name() + "/" + types_h,
              ),
    )

    cc_library(
        name = name + "_types",
        hdrs = [types_h],
        visibility = visibility,
    )

    cc_library(
        name = name + "_impl",
        srcs = [serializer_cc],
        deps = [
            ":" + name + "_types",
            "//score/serializer:interface",
            "//score/serializer/typed:someip_wire",
        ],
        visibility = visibility,
    )

    cc_shared_library(
        name = name,
        additional_linker_inputs = [lds],
        shared_lib_name = "score_com_serializer.so",
        user_link_flags = ["-Wl,--version-script=$(location {})".format(lds)],
        deps = [":" + name + "_impl"],
        visibility = visibility,
    )
