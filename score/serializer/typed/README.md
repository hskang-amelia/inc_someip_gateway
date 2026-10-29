<!--
*******************************************************************************
Copyright (c) 2026 Contributors to the Eclipse Foundation

See the NOTICE file(s) distributed with this work for additional
information regarding copyright ownership.

This program and the accompanying materials are made available under the
terms of the Apache License Version 2.0 which is available at
https://www.apache.org/licenses/LICENSE-2.0

SPDX-License-Identifier: Apache-2.0
*******************************************************************************
-->

# Typed serializer (prototype)

Generates a `score_com_serializer.so` for concrete application data types, as an
alternative to the null serializer. gatewayd then offers/consumes trivially copyable
structs over `mw::com` instead of `PreSerializedData` byte blobs. The gateway itself
is unchanged; only the plug-in behind `score/serializer/serializer.h` is swapped.

- `serializer_gen.py`: JSON type spec → `<name>_types.h` + `<name>_serializer.cpp`
- `someip_wire.h`: bounds-checked SOME/IP encoding (big-endian, IEEE 754, 1-byte bool)
- `typed_serializer.bzl`: `score_com_typed_serializer(name, spec)`
- `example/`: car window service with a nested struct array

Supported: bool, (u)int8–64, float32/64, fixed-size arrays and structs without
length fields. Not supported yet: strings, dynamic arrays, length fields, methods.
Deserialization ignores trailing bytes beyond the known size.
