import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

BOARD = "waveshare-esp32-s3-zero-4mb"
REPOSITORY = "Pawel-sp9pw/Launchpad_mqtt"
ROOT = Path(__file__).resolve().parents[1]


def validate_version(version):
    if not re.fullmatch(r"[0-9]{1,4}\.[0-9]{1,4}\.[0-9]{1,4}", version):
        raise ValueError("Only stable major.minor.patch releases are supported")
    return version


def create_manifest(binary, version):
    validate_version(version)
    return {
        "schema": 1,
        "board": BOARD,
        "version": version,
        "size": len(binary),
        "sha256": hashlib.sha256(binary).hexdigest(),
        "url": f"https://github.com/{REPOSITORY}/releases/download/v{version}/firmware.bin",
    }


def verify_release(directory, public_key):
    body = (directory / "manifest.json").read_bytes()
    signature = bytes.fromhex((directory / "manifest.sig").read_text().strip())
    key = serialization.load_pem_public_key(public_key)
    if not isinstance(key, ec.EllipticCurvePublicKey) or not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("Expected an ECDSA P-256 public key")
    key.verify(signature, body, ec.ECDSA(hashes.SHA256()))
    manifest = json.loads(body)
    binary = (directory / "firmware.bin").read_bytes()
    if manifest != create_manifest(binary, manifest["version"]):
        raise ValueError("Manifest metadata or firmware SHA256 mismatch")
    if len(binary) > 0x140000 or binary[0] != 0xE9 or int.from_bytes(binary[12:14], "little") != 9:
        raise ValueError("Firmware does not fit the standard OTA slot or targets another chip")
    if binary[48:80].split(b"\0", 1)[0].decode("ascii") != manifest["version"]:
        raise ValueError("Firmware application descriptor version mismatch")
    return manifest


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--key", type=Path)
    parser.add_argument("--verify-only", action="store_true")
    arguments = parser.parse_args()
    public_key = (ROOT / "keys/release-public.pem").read_bytes()
    if arguments.verify_only:
        print(json.dumps(verify_release(arguments.output, public_key), indent=2))
        return
    version = validate_version((ROOT / "VERSION").read_text().strip())
    binary = arguments.firmware.read_bytes()
    pem = arguments.key.read_bytes() if arguments.key else os.environ["OTA_SIGNING_KEY_PEM"].encode()
    private_key = serialization.load_pem_private_key(pem, password=None)
    if not isinstance(private_key, ec.EllipticCurvePrivateKey) or not isinstance(private_key.curve, ec.SECP256R1):
        raise ValueError("Expected ECDSA P-256 private key")
    trusted_key = serialization.load_pem_public_key(public_key)
    if private_key.public_key().public_bytes(serialization.Encoding.DER,
            serialization.PublicFormat.SubjectPublicKeyInfo) != trusted_key.public_bytes(
            serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo):
        raise ValueError("Signing key does not match firmware trust anchor")
    body = (json.dumps(create_manifest(binary, version), sort_keys=True, separators=(",", ":")) + "\n").encode()
    signature = private_key.sign(body, ec.ECDSA(hashes.SHA256()))
    arguments.output.mkdir(parents=True, exist_ok=True)
    (arguments.output / "firmware.bin").write_bytes(binary)
    (arguments.output / "manifest.json").write_bytes(body)
    (arguments.output / "manifest.sig").write_text(signature.hex() + "\n", encoding="ascii")
    checksums = []
    for name in ("firmware.bin", "manifest.json", "manifest.sig"):
        checksums.append(f"{hashlib.sha256((arguments.output / name).read_bytes()).hexdigest()}  {name}\n")
    (arguments.output / "SHA256SUMS").write_text("".join(checksums), encoding="ascii")
    shutil.copy2(ROOT / "keys/release-public.pem", arguments.output / "release-public.pem")
    print(json.dumps(verify_release(arguments.output, public_key), indent=2))


if __name__ == "__main__":
    main()
