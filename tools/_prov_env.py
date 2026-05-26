"""Shared env/bundle helpers for host-side provisioning tooling.

Two jobs:
  1. Auto-load .env from the repo root into os.environ (setdefault — shell env wins).
  2. Resolve a "cert bundle" into AMBYTE_CA_CERT / AMBYTE_DEV_CERT / AMBYTE_DEV_KEY.

Bundle layout:
    device_certs/
        <bundle-name>/
            *RootCA*.pem                       (any; first match used)
            *-certificate.pem.crt              (device cert)
            *-private.pem.key                  (device key)

Bundle selection precedence:
  1. AMBYTE_CERT_BUNDLE env var / .env entry
  2. If exactly one subfolder under device_certs/, pick it silently
  3. Interactive prompt listing available bundles (stdin must be a TTY)

Explicit AMBYTE_CA_CERT / AMBYTE_DEV_CERT / AMBYTE_DEV_KEY always override
bundle auto-resolution for their respective slot.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

REPO_ROOT    = Path(__file__).resolve().parent.parent
BUNDLES_DIR  = REPO_ROOT / "device_certs"
DOTENV_PATH  = REPO_ROOT / ".env"


def load_dotenv() -> None:
    """Populate os.environ with KEY=VALUE lines from `.env`. Existing env wins."""
    if not DOTENV_PATH.exists():
        return
    for raw in DOTENV_PATH.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, _, value = line.partition("=")
        os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


def _pick_bundle_interactively(subs: list[Path]) -> str | None:
    if not sys.stdin.isatty():
        return None
    print("Multiple cert bundles under device_certs/ — pick one:", file=sys.stderr)
    for i, s in enumerate(subs):
        print(f"  [{i}] {s.name}", file=sys.stderr)
    choice = input("bundle (name or index): ").strip()
    if choice.isdigit() and 0 <= int(choice) < len(subs):
        return subs[int(choice)].name
    if any(s.name == choice for s in subs):
        return choice
    print(f"warning: no bundle matches {choice!r}", file=sys.stderr)
    return None


def _find(files: list[Path], predicate) -> str | None:
    for f in files:
        if predicate(f.name):
            return str(f.resolve().relative_to(REPO_ROOT))
    return None


def resolve_cert_bundle() -> None:
    """Fill AMBYTE_CA_CERT/AMBYTE_DEV_CERT/AMBYTE_DEV_KEY from a bundle directory.
    No-op if the three are already all set. Any already-set ones are preserved."""
    if (os.environ.get("AMBYTE_CA_CERT")
        and os.environ.get("AMBYTE_DEV_CERT")
        and os.environ.get("AMBYTE_DEV_KEY")):
        return
    if not BUNDLES_DIR.is_dir():
        return

    bundle = os.environ.get("AMBYTE_CERT_BUNDLE")
    if not bundle:
        subs = sorted(d for d in BUNDLES_DIR.iterdir() if d.is_dir())
        if len(subs) == 1:
            bundle = subs[0].name
        elif len(subs) > 1:
            bundle = _pick_bundle_interactively(subs)
        if not bundle:
            return

    bundle_dir = BUNDLES_DIR / bundle
    if not bundle_dir.is_dir():
        print(f"warning: AMBYTE_CERT_BUNDLE={bundle!r} — {bundle_dir} not found", file=sys.stderr)
        return

    os.environ["AMBYTE_CERT_BUNDLE"] = bundle

    # AWS IoT client_id must match the thing the cert is bound to — the bundle
    # folder name is that thing name. Default client_id to it; warn on mismatch.
    existing_client_id = os.environ.get("AMBYTE_CLIENT_ID")
    if existing_client_id and existing_client_id != bundle:
        print(
            f"warning: AMBYTE_CLIENT_ID={existing_client_id!r} does not match "
            f"AMBYTE_CERT_BUNDLE={bundle!r}. AWS IoT will reject the handshake "
            f"if the cert isn't bound to this client id.",
            file=sys.stderr)
    os.environ.setdefault("AMBYTE_CLIENT_ID", bundle)

    files = sorted(bundle_dir.iterdir())

    ca = _find(files, lambda n: "rootca" in n.lower().replace("_", "").replace("-", ""))
    if not ca:
        ca = _find(files, lambda n: n.lower().endswith(".pem") and "ca" in n.lower())
    cert = _find(files, lambda n: n.endswith("-certificate.pem.crt") or n.endswith(".crt"))
    key  = _find(files, lambda n: n.endswith("-private.pem.key")
                                   or (n.endswith(".key") and "private" in n.lower())
                                   or n.endswith(".key"))

    if ca:   os.environ.setdefault("AMBYTE_CA_CERT",  ca)
    if cert: os.environ.setdefault("AMBYTE_DEV_CERT", cert)
    if key:  os.environ.setdefault("AMBYTE_DEV_KEY",  key)
