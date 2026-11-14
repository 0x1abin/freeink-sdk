"""Private vendor include paths, scoped to this library's compilation."""
from pathlib import Path

Import("env", "pio_lib_builder")

root = Path(pio_lib_builder.path) / "src/vendor"
env.Append(CPPPATH=[str(root / folder) for folder in (
    ".", "bus", "periph", "display", "display/output_common", "display/output_lcd", "display/waveforms")])
env.Append(CCFLAGS=["-O2", "-Wno-error=format", "-Wno-error=unused-function", "-Wno-error=maybe-uninitialized"])
