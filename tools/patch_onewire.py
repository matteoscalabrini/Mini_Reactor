"""
patch_onewire.py — PlatformIO pre-build patch for paulstoffregen/OneWire 2.3.8.

Why: OneWire's directModeOutput() (util/OneWire_direct_gpio.h) gates output-mode
configuration behind `pin <= 33`, a leftover assumption from the original ESP32
where GPIO34-39 are input-only. The ESP32-S3 has NO input-only pins, so our
DS18B20 on GPIO42 (hardwired on the board) could never be driven low: no 1-Wire
reset pulse -> no presence pulse -> every read returns -127 (DEVICE_DISCONNECTED).

The rest of the same file already handles pins up to 45 (`pin < 46`) for read and
write; only directModeOutput was missed. This patch relaxes that one guard to
match, restoring open-drain output on GPIO42.

Idempotent: re-running is a no-op once patched. Runs on every build so a clean
.pio (which re-installs the pristine library) is re-patched automatically.
"""

import glob
import os

Import("env")  # noqa: F821  (injected by PlatformIO/SCons)

OLD = "if ( digitalPinIsValid(pin) && pin <= 33 ) // pins above 33 can be only inputs"
NEW = (
    "if ( digitalPinIsValid(pin) && pin < 46 ) "
    "// S3 patch: ESP32-S3 has no input-only pins; GPIO34-45 are output-capable"
)


def patch_onewire(*_args, **_kwargs):
    libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")  # noqa: F821
    pioenv = env.subst("$PIOENV")  # noqa: F821
    pattern = os.path.join(libdeps_dir, pioenv, "**", "OneWire_direct_gpio.h")
    matches = glob.glob(pattern, recursive=True)

    if not matches:
        print("[patch_onewire] OneWire_direct_gpio.h not found (skipping)")
        return

    for path in matches:
        with open(path, "r", encoding="utf-8") as f:
            src = f.read()

        if NEW in src:
            print("[patch_onewire] already patched: %s" % path)
            continue
        if OLD not in src:
            print("[patch_onewire] WARNING: expected guard not found in %s "
                  "(library version changed?) — leaving untouched" % path)
            continue

        with open(path, "w", encoding="utf-8") as f:
            f.write(src.replace(OLD, NEW))
        print("[patch_onewire] patched directModeOutput pin guard in %s" % path)


# Patch now (scripts are executed during the build, after lib install).
patch_onewire()
