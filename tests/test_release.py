import importlib.util
import json
from pathlib import Path

import pytest
from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

spec = importlib.util.spec_from_file_location("release", Path(__file__).parents[1] / "scripts/release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


def signed_fixture(directory):
    key = ec.generate_private_key(ec.SECP256R1())
    binary = bytearray(2048)
    binary[0] = 0xE9
    binary[12] = 9
    binary[48:53] = b"1.0.0"
    body = json.dumps(release.create_manifest(binary, "1.0.0")).encode()
    (directory / "firmware.bin").write_bytes(binary)
    (directory / "manifest.json").write_bytes(body)
    signature = key.sign(body, ec.ECDSA(hashes.SHA256()))
    (directory / "manifest.sig").write_text(signature.hex())
    public = key.public_key().public_bytes(serialization.Encoding.PEM,
        serialization.PublicFormat.SubjectPublicKeyInfo)
    return public, key


def test_signature_and_hash_valid(tmp_path):
    public, _ = signed_fixture(tmp_path)
    assert release.verify_release(tmp_path, public)["version"] == "1.0.0"


@pytest.mark.parametrize("filename", ["manifest.json", "firmware.bin", "manifest.sig"])
def test_tampered_release_rejected(tmp_path, filename):
    public, _ = signed_fixture(tmp_path)
    target = tmp_path / filename
    if filename == "firmware.bin":
        target.write_bytes(target.read_bytes() + b"changed")
    elif filename == "manifest.sig":
        target.write_text("00" * 72)
    else:
        target.write_bytes(target.read_bytes().replace(b"1.0.0", b"9.0.0"))
    with pytest.raises((ValueError, InvalidSignature)):
        release.verify_release(tmp_path, public)


def test_wrong_signer_rejected(tmp_path):
    signed_fixture(tmp_path)
    wrong = ec.generate_private_key(ec.SECP256R1()).public_key().public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo)
    with pytest.raises(InvalidSignature):
        release.verify_release(tmp_path, wrong)


@pytest.mark.parametrize("version", ["v1.0.0", "1.0.1-rc1", "1.0", "1.2.3/../../x", "12345.0.0"])
def test_unstable_or_invalid_versions_rejected(version):
    with pytest.raises(ValueError):
        release.validate_version(version)


def test_signed_wrong_board_rejected(tmp_path):
    public, key = signed_fixture(tmp_path)
    manifest = json.loads((tmp_path / "manifest.json").read_bytes())
    manifest["board"] = "another-board"
    body = json.dumps(manifest).encode()
    (tmp_path / "manifest.json").write_bytes(body)
    (tmp_path / "manifest.sig").write_text(key.sign(body, ec.ECDSA(hashes.SHA256())).hex())
    with pytest.raises(ValueError):
        release.verify_release(tmp_path, public)


def test_signed_external_url_rejected(tmp_path):
    public, key = signed_fixture(tmp_path)
    manifest = json.loads((tmp_path / "manifest.json").read_bytes())
    manifest["url"] = "https://example.org/untrusted.bin"
    body = json.dumps(manifest).encode()
    (tmp_path / "manifest.json").write_bytes(body)
    (tmp_path / "manifest.sig").write_text(key.sign(body, ec.ECDSA(hashes.SHA256())).hex())
    with pytest.raises(ValueError):
        release.verify_release(tmp_path, public)
