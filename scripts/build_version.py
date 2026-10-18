from pathlib import Path
import re

Import("env")

version = (Path(env["PROJECT_DIR"]) / "VERSION").read_text().strip()
if not re.fullmatch(r"[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}", version):
    raise ValueError("VERSION must be a stable major.minor.patch version")
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])

