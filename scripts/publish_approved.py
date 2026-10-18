import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from release import REPOSITORY, ROOT, verify_release


def command(*arguments):
    return subprocess.check_output(list(arguments), cwd=ROOT, text=True)


def main():
    if os.environ.get("GITHUB_REPOSITORY") != REPOSITORY:
        raise ValueError("Release publication is restricted to the canonical repository")
    approval = json.loads((ROOT / "releases/approved.json").read_text())
    run_id = approval["ci_run_id"]
    commit = approval["commit"]
    if not isinstance(run_id, int) or run_id <= 0 or not re.fullmatch(r"[a-f0-9]{40}", commit):
        raise ValueError("Invalid approval metadata")
    run = json.loads(command("gh", "api", f"repos/{REPOSITORY}/actions/runs/{run_id}"))
    if (run["conclusion"] != "success" or run["head_sha"] != commit or
            run["event"] != "push" or run["head_branch"] != "main" or
            run["path"] != ".github/workflows/ci.yml"):
        raise ValueError("Artifact must come from successful main-branch CI at the approved commit")
    output = ROOT / "dist"
    output.mkdir(exist_ok=True)
    command("gh", "run", "download", str(run_id), "--repo", REPOSITORY,
        "--name", "firmware-ci-not-a-release", "--dir", str(output))
    for name in ("manifest.json", "manifest.sig"):
        shutil.copy2(ROOT / "releases" / name, output / name)
    public = (ROOT / "keys/release-public.pem").read_bytes()
    manifest = verify_release(output, public)
    version = manifest["version"]
    if command("git", "show", f"{commit}:VERSION").strip() != version:
        raise ValueError("Approved commit VERSION does not match the signed artifact")
    existing = subprocess.run(["git", "rev-parse", f"refs/tags/v{version}"],
        cwd=ROOT, capture_output=True)
    if existing.returncode == 0:
        raise ValueError("Version already exists; releases are never overwritten")
    import hashlib
    checksums = "".join(f"{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}\n"
        for name in ("firmware.bin", "manifest.json", "manifest.sig"))
    (output / "SHA256SUMS").write_text(checksums, encoding="ascii")
    shutil.copy2(ROOT / "keys/release-public.pem", output / "release-public.pem")
    notes = ROOT / "dist/RELEASE_NOTES.md"
    notes.write_text(command("git", "show", f"{commit}:docs/RELEASE_NOTES.md"), encoding="utf-8")
    command("gh", "release", "create", f"v{version}", "--repo", REPOSITORY,
        "--target", commit, "--draft", "--title", f"Launchpad MQTT v{version}",
        "--notes-file", str(notes))
    command("gh", "release", "upload", f"v{version}", "--repo", REPOSITORY,
        *[str(output / name) for name in
          ("firmware.bin", "manifest.json", "manifest.sig", "SHA256SUMS", "release-public.pem")])
    command("gh", "release", "edit", f"v{version}", "--repo", REPOSITORY,
        "--draft=false", "--latest")
    print(f"Published signed release v{version} from successful CI run {run_id}")


if __name__ == "__main__":
    main()

