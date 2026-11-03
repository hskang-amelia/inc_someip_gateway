..
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

.. _serializer:

Serializer
##########

Abstract
========

Serialization utilities. Provides a null-passthrough serializer for pre-serialized payloads so the fast path avoids double serialization,
and typed serialization through codecs generated from a type description.


.. comp:: SOME/IP Serializer
   :id: comp__serializer
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :belongs_to: feat__someip_gateway

   Serializer and deserializer for SOME/IP messages. Used by the :need:`comp__gatewayd`.
   The serializer is built or configured especially for the application data types that it handles.


.. comp:: Null Serializer
   :id: comp__null_serializer
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :belongs_to: feat__someip_gateway

   A serializer that does not actually serialize or deserialize data, but only handles messages that are already serialized by the application.


.. comp:: Typed Serializer
   :id: comp__typed_serializer
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :belongs_to: feat__someip_gateway

   Converts IPC samples of a known type to and from SOME/IP payloads.
   Selected per element by ``TypedSerializerConfig.type_name``; the codecs are generated at build time and linked into the serializer library.


Typed serialization
===================

An element configured with ``TypedSerializerConfig`` uses the codec whose name matches ``type_name``.
Elements configured with ``NullSerializerConfig`` keep the null-passthrough behaviour, so both can be mixed in one configuration.
If no codec matches, ``score_com_serializer_get`` returns ``score_com_serializer_result_serializer_nonexistent``.

.. code-block:: json

   {
       "event_name": "window_control",
       "event_id": 1,
       "serialization_config_type": "TypedSerializerConfig",
       "serialization_config": {"type_name": "carwindow::WindowControl"}
   }

Codecs are generated with the ``someip_codecs`` and ``someip_serializer`` macros from ``//score/serializer:defs.bzl``:

.. code-block:: python

   someip_codecs(name = "car_window_codecs", types = "car_window_types.json")
   someip_serializer(name = "car_window_serializer", codecs = ":car_window_codecs")

``someip_codecs`` creates ``:<name>_types`` with the header ``<package>/<name>_types.h``.
Applications publish these types over IPC, so the sample layout always matches the codec.
``someip_serializer`` creates ``score_com_serializer.so``.

Type description
----------------

.. code-block:: json

   {
       "namespace": "carwindow",
       "byte_order": "big",
       "types": {
           "WindowCommand": {"enum": "uint8", "values": {"kStop": 0, "kOpen": 1, "kClose": 2}},
           "WindowControl": {"struct": [{"name": "command", "type": "WindowCommand"}]}
       }
   }

Every named type gets a codec named ``<namespace>::<Name>``. The wire format follows AUTOSAR PRS_SOMEIPProtocol:

.. list-table::
   :header-rows: 1

   * - Type
     - C++
     - Wire format
   * - ``bool``, ``uint8`` … ``uint64``, ``int8`` … ``int64``, ``float32``, ``float64``
     - fixed-width types
     - ``byte_order`` (default ``big``); ``bool`` is 0 or 1
   * - ``{"enum": <integer type>, "values": {...}}``
     - ``enum class``
     - base type
   * - ``{"struct": [...], "length_field": 0|8|16|32}``
     - ``struct``
     - members in order, optional length field (default 0); unknown trailing bytes are skipped
   * - ``T[N]``
     - ``std::array<T, N>``
     - N elements, no length field
   * - ``T[<=N]``
     - ``BoundedVector<T, N>``
     - length field in bytes (member ``length_field``, default 32) and up to N elements
   * - ``string<=N``
     - ``BoundedString<N>``
     - length field (default 32), UTF-8 BOM, up to N bytes, terminating zero

Generated types are trivially copyable, as required for IPC samples.
