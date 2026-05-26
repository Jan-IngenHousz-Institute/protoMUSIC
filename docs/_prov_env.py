"""Re-export shim — canonical implementation lives in tools/_prov_env.py."""

import os
import sys

_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_REPO_ROOT, "tools"))

from _prov_env import load_dotenv, resolve_cert_bundle  # noqa: E402,F401
