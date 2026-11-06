#!/usr/bin/env python3

"""
M5Burner Firmware Publish Script (burner.m5stack.com API v1)

Uploads a new version of every mapped firmware to M5Burner. With the v1 API
an upload sent with visibility=PUBLIC is already published (it enters the
PENDING review state), so there is no separate publish step.

Usage:
    python3 m5burner_publish.py --tag <version> [--notes "text" | --notes-file notes.md]

Arguments:
    --user / --password: M5Burner credentials (default: $M5BURNER_USER / $M5BURNER_PWD)
    --tag: Version name shown on M5Burner (e.g., 2.9.1)
    --notes / --notes-file: Patch notes (markdown) for versionDescription (default: the tag)
    --artifacts-dir: Directory containing .bin files (default: current directory)
    --only: Restrict to these firmware IDs (repeatable), useful for testing
    --force: Upload even if the firmware's current version already equals --tag
    --output: Write full HTTP request/response log to this file for debugging

Requirements:
    - Python 3.6+
    - requests library (pip install requests)
"""

import argparse
import json
import os
import sys
from pathlib import Path

import requests

API_BASE_URL = "https://burner.m5stack.com/api/v1"

# Internal firmware mapping: firmwareId -> binary filename
FIRMWARE_MAP = {
    "1897200000000002282": "Launcher-m5stack-cplus2.bin",
    # StickC and StickC Plus are one binary now, but both keep their own
    # M5Burner listing, so both ids point at the same file.
    "1897200000000003263": "Launcher-m5stack-plus.bin",
    "1897200000000002741": "Launcher-m5stack-plus.bin",
    "1897200000000001991": "Launcher-m5stack-cardputer.bin",
    "1897200000000001510": "Launcher-m5stack-core.bin",
    "1897200000000002752": "Launcher-m5stack-core2.bin",
    "1897200000000002205": "Launcher-m5stack-cores3.bin",
    "1897200000000001838": "Launcher-m5stack-tab5.bin",
    "2099249772015865858": "Launcher-m5stack-paper-mono.bin",
    "2099250485743800321": "Launcher-m5stack-paper-color.bin",
    "1897200000000002080": "Launcher-m5stack-paper-s3.bin",
    "2118648143728517121": "Launcher-m5stack-paper.bin",
    "2118647594891255809": "Launcher-m5stack-paper-diy.bin",
    "1897200000000002608": "Launcher-m5stack-sticks3.bin",
    "1897200000000001205": "Launcher-arduino-nesso-n1.bin",
    "1897200000000002556": "Launcher-m5stack-dinmeter.bin",
}


def _format_headers(headers):
    return "\n".join(f"  {k}: {v}" for k, v in headers.items())


def _safe_body(response):
    try:
        return json.dumps(response.json(), indent=2, ensure_ascii=False)
    except Exception:
        pass
    text = response.text
    return text if len(text) <= 4096 else text[:4096] + "\n... (truncated)"


class RequestLogger:
    """Hooks into a requests.Session to log every request/response pair."""

    def __init__(self, output_path):
        self._file = open(output_path, "w", encoding="utf-8")
        self._counter = 0

    def attach(self, session):
        session.hooks["response"].append(self._on_response)

    def _on_response(self, response, *args, **kwargs):
        self._counter += 1
        req = response.request

        safe_req_headers = dict(req.headers)
        for h in safe_req_headers:
            if h.lower() in ("authorization", "cookie"):
                safe_req_headers[h] = "***"
        safe_resp_headers = dict(response.headers)
        for h in safe_resp_headers:
            if h.lower() == "set-cookie":
                safe_resp_headers[h] = "***"

        if not req.body:
            req_body = "(empty)"
        elif "/auth/login" in req.url:
            req_body = "*** (credentials)"
        elif isinstance(req.body, bytes) and b"binFile" in req.body:
            req_body = f"<multipart {len(req.body)} bytes>"
        else:
            body = req.body if isinstance(req.body, str) else req.body.decode("utf-8", errors="replace")
            req_body = body if len(body) <= 4096 else body[:4096] + "\n... (truncated)"

        # Never log the access token returned by login
        resp_body = "*** (token)" if "/auth/login" in req.url else _safe_body(response)

        self._file.write(
            f"\n{'=' * 72}\n"
            f"[{self._counter}] {req.method} {req.url}\n"
            f"--- REQUEST HEADERS ---\n{_format_headers(safe_req_headers)}\n"
            f"--- REQUEST BODY ---\n{req_body}\n"
            f"--- RESPONSE {response.status_code} ---\n"
            f"--- RESPONSE HEADERS ---\n{_format_headers(safe_resp_headers)}\n"
            f"--- RESPONSE BODY ---\n{resp_body}\n"
        )
        self._file.flush()

    def close(self):
        self._file.close()


class M5BurnerClient:
    def __init__(self, logger=None):
        self.session = requests.Session()
        if logger:
            logger.attach(self.session)

    def _check(self, response, what, ok_codes=(200,)):
        try:
            body = response.json()
        except ValueError:
            body = None
        if response.status_code not in (200, 201) or not body or body.get("code") not in ok_codes:
            raise Exception(f"{what} failed: HTTP {response.status_code} - {response.text[:1000]}")
        return body

    def login(self, email, password):
        print(f"Logging in as {email}...")
        response = self.session.post(
            f"{API_BASE_URL}/auth/login",
            json={"email": email, "password": password, "rememberMe": False, "deviceName": "Launcher CI"},
        )
        data = self._check(response, "Login").get("data") or {}
        token = data.get("accessToken")
        if not token:
            raise Exception("Login succeeded but no access token received")
        self.session.headers["Authorization"] = f"{data.get('tokenType', 'Bearer')} {token}"
        print("Login successful")

    def get_firmware(self, firmware_id):
        response = self.session.get(f"{API_BASE_URL}/firmwares/{firmware_id}")
        return self._check(response, f"Get firmware {firmware_id}")["data"]

    def upload_version(self, info, version, notes, binary_path):
        data = {
            "firmwareName": info["firmwareName"],
            "versionName": version,
            "sourceUrl": info.get("sourceUrl") or "",
            "categoryIds": json.dumps([c["categoryId"] for c in info.get("categories") or []]),
            "versionDescription": json.dumps({"format": "markdown", "content": notes}),
            "deviceIds": json.dumps([d["deviceId"] for d in info.get("supportedDevices") or []]),
            "visibility": "PUBLIC",
        }
        with open(binary_path, "rb") as f:
            files = {"binFile": (binary_path.name, f, "application/octet-stream")}
            response = self.session.post(f"{API_BASE_URL}/firmwares", data=data, files=files)
        return self._check(response, "Upload", ok_codes=(200, 201))["data"]


def main():
    # The API returns Chinese status messages; don't crash on cp1252 consoles.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")

    parser = argparse.ArgumentParser(description="Upload and publish firmware to M5Burner (API v1)")
    parser.add_argument("--user", default=os.environ.get("M5BURNER_USER"), help="M5Burner e-mail")
    parser.add_argument("--password", default=os.environ.get("M5BURNER_PWD"), help="M5Burner password")
    parser.add_argument("--tag", required=True, help="Version name (e.g., 2.9.1)")
    notes_group = parser.add_mutually_exclusive_group()
    notes_group.add_argument("--notes", help="Patch notes (markdown)")
    notes_group.add_argument("--notes-file", help="File with patch notes (markdown)")
    parser.add_argument("--artifacts-dir", default=".", help="Directory containing .bin files")
    parser.add_argument("--only", action="append", metavar="FIRMWARE_ID", help="Only process these firmware IDs")
    parser.add_argument("--force", action="store_true", help="Upload even if the version already exists")
    parser.add_argument("--output", metavar="filename", help="Write HTTP request/response log to this file")
    args = parser.parse_args()

    if not args.user or not args.password:
        parser.error("credentials missing: pass --user/--password or set M5BURNER_USER/M5BURNER_PWD")

    notes = args.notes
    if args.notes_file:
        notes = Path(args.notes_file).read_text(encoding="utf-8").strip()
    if not notes:
        notes = args.tag

    targets = FIRMWARE_MAP
    if args.only:
        unknown = [fid for fid in args.only if fid not in FIRMWARE_MAP]
        if unknown:
            parser.error(f"unknown firmware IDs: {unknown}")
        targets = {fid: FIRMWARE_MAP[fid] for fid in args.only}

    artifacts_dir = Path(args.artifacts_dir)
    existing = {}
    for fid, filename in targets.items():
        bin_path = artifacts_dir / filename
        if bin_path.exists():
            existing[fid] = bin_path
        else:
            print(f"Warning: {filename} not found, skipping {fid}")
    if not existing:
        print("Error: No firmware files found")
        sys.exit(1)
    print(f"Found {len(existing)} firmware files to process")

    logger = RequestLogger(args.output) if args.output else None
    client = M5BurnerClient(logger=logger)
    failed = []
    try:
        client.login(args.user, args.password)
        for fid, bin_path in existing.items():
            try:
                info = client.get_firmware(fid)
                name = info["firmwareName"]
                current = (info.get("currentVersion") or {}).get("versionName")
                if current == args.tag and not args.force:
                    print(f"Skipping {name}: version {args.tag} already published")
                    continue
                print(f"Uploading {bin_path.name} as {name} {args.tag} (current: {current})...")
                result = client.upload_version(info, args.tag, notes, bin_path)
                print(
                    f"  -> {result.get('operationType')} versionId={result.get('versionId')} "
                    f"visibility={result.get('visibility')} status={result.get('status')}"
                )
            except Exception as e:
                print(f"Error processing {fid}: {e}")
                failed.append(fid)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
    finally:
        if logger:
            logger.close()

    if failed:
        print(f"Failed: {failed}")
        sys.exit(1)
    print("All firmwares processed successfully")


if __name__ == "__main__":
    main()
