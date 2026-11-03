#!/usr/bin/env python3
"""Compatibility entry point for the connection-owned wolfSSL record tests.

Set WOLFSSL_INTERNAL_SOURCE to the consumer's patched internal.c. Global
allocator loans were removed; request timing/cancellation is tested by run.sh.
"""
import runpy
from pathlib import Path

runpy.run_path(str(Path(__file__).with_name("test_record_reservation.py")), run_name="__main__")
