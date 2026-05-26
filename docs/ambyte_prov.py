#!/usr/bin/env python3
"""DEPRECATED — BLE provisioning has been removed from the firmware.

Provisioning now happens at flash time: tools/extra_script.py generates an
NVS image from .env + device_certs/<bundle>/ and flashes it alongside the
firmware (`pio run -t upload`). See README.md for the new workflow.

This shim exists only so older scripts/CI that still invoke
`docs/ambyte_prov.py` fail loudly with a pointer to the new flow. It will
be removed in a future release.
"""

import sys

print(__doc__, file=sys.stderr)
sys.exit(2)
