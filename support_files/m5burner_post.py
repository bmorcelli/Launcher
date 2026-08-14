#!/usr/bin/env python3

"""
M5Burner Firmware Post Script

This script uploads and publishes firmware binaries to the M5Burner platform.
It uses the M5Burner API to authenticate, upload firmware versions, and publish them.

Usage:
    python3 m5burner_post.py --user <username> --password <password> --tag <version_tag>

Arguments:
    --user: M5Burner username (from M5BURNER_USER secret)
    --password: M5Burner password (from M5BURNER_PWD secret)
    --tag: Version tag (e.g., v1.0.0)
    --api-base-url: Optional API base URL (default: http://m5burner-api.m5stack.com)
    --artifacts-dir: Optional directory containing .bin files (default: current directory)
    --output: Optional file to write full HTTP request/response log for debugging

The script maintains an internal mapping of firmware IDs (fid) to binary filenames.
Each mapped firmware will be uploaded as a new version and published.

Requirements:
    - Python 3.6+
    - requests library (pip install requests)
"""

import argparse
import json
import os
import sys
import requests
from pathlib import Path


# Internal firmware mapping: fid -> binary filename
FIRMWARE_MAP = {
    "a9e853ff6386480a3a99ff390832a442": "Launcher-m5stack-cplus2.bin",
    # StickC and StickC Plus are one binary now, but both keep their own
    # M5Burner listing, so both fids point at the same file.
    "f033e5024b10cd8a7f10ddfb43c0479c": "Launcher-m5stack-plus.bin",
    "cb0e3f37a54eee95752076dd2d532acf": "Launcher-m5stack-plus.bin",
    "967e0377b9889c7b82f059fb8a30adda": "Launcher-m5stack-cardputer.bin",
    "731e504d1a02296dd206785cf47a9582": "Launcher-m5stack-core.bin",
    "cbf409503727620eac2066351f9962ff": "Launcher-m5stack-core2.bin",
    "a5880a61a2f9466111e320ea78f91ded": "Launcher-m5stack-cores3.bin",
    "8bd30a7ef1dcce06dbbf04bda8318bf8": "Launcher-m5stack-tab5.bin",
    "9ce0bed47c008f578cf099168b530c72": "Launcher-m5stack-paper-s3.bin",
    "c0e83b10a5b368fbe27fdd6598518ebd": "Launcher-m5stack-sticks3.bin",
    "5ac021007628e2e75fa8575470a59fdd": "Launcher-arduino-nesso-n1.bin",
    "bccbefef5284357a2a7c3c7cc196fd98": "Launcher-m5stack-dinmeter.bin",
}


def _format_headers(headers):
    return "\n".join(f"  {k}: {v}" for k, v in headers.items())


def _safe_body(response):
    content_type = response.headers.get("Content-Type", "")
    if "application/json" in content_type:
        try:
            return json.dumps(response.json(), indent=2)
        except Exception:
            pass
    try:
        text = response.text
        return text if len(text) <= 4096 else text[:4096] + "\n... (truncated)"
    except Exception:
        return f"<binary {len(response.content)} bytes>"


class RequestLogger:
    """Hooks into a requests.Session to log every request/response pair."""

    def __init__(self, output_path):
        self._file = open(output_path, "w", encoding="utf-8")
        self._counter = 0

    def _write(self, text):
        self._file.write(text + "\n")
        self._file.flush()

    def attach(self, session):
        session.hooks["response"].append(self._on_response)

    def _on_response(self, response, *args, **kwargs):
        self._counter += 1
        req = response.request

        # Sanitize Authorization / password headers before logging
        safe_req_headers = dict(req.headers)
        for h in list(safe_req_headers):
            if h.lower() in ("authorization", "m5_auth_token", "cookie"):
                safe_req_headers[h] = "***"

        safe_resp_headers = dict(response.headers)
        for h in list(safe_resp_headers):
            if h.lower() == "set-cookie":
                safe_resp_headers[h] = "***"

        # Request body (skip binary multipart payloads)
        req_body = ""
        if req.body:
            if isinstance(req.body, bytes):
                req_body = f"<binary {len(req.body)} bytes>"
            else:
                body_str = req.body if isinstance(req.body, str) else req.body.decode("utf-8", errors="replace")
                req_body = body_str if len(body_str) <= 4096 else body_str[:4096] + "\n... (truncated)"

        entry = (
            f"\n{'='*72}\n"
            f"[{self._counter}] {req.method} {req.url}\n"
            f"--- REQUEST HEADERS ---\n{_format_headers(safe_req_headers)}\n"
            f"--- REQUEST BODY ---\n{req_body or '(empty)'}\n"
            f"--- RESPONSE {response.status_code} ---\n"
            f"--- RESPONSE HEADERS ---\n{_format_headers(safe_resp_headers)}\n"
            f"--- RESPONSE BODY ---\n{_safe_body(response)}"
        )
        self._write(entry)

    def close(self):
        self._file.close()


class M5BurnerClient:
    def __init__(self, api_base_url="http://m5burner-api.m5stack.com", logger=None):
        self.api_base_url = api_base_url.rstrip('/')
        self.session = requests.Session()
        if logger:
            logger.attach(self.session)

    def login(self, username, password):
        """Login to M5Burner and get auth token."""
        login_url = "https://uiflow2.m5stack.com/api/v1/account/login"
        payload = {
            "email": username,
            "password": password
        }

        print(f"Logging in as {username}...")
        response = self.session.post(login_url, json=payload)

        if response.status_code != 200:
            raise Exception(f"Login failed: {response.status_code} - {response.text}")

        # Check if we got the auth token cookie
        token = self.session.cookies.get('m5_auth_token')
        if not token:
            raise Exception("Login succeeded but no auth token received")

        # Add token to headers for admin API requests on the M5Burner API domain
        self.session.headers.update({'m5_auth_token': token})

        print("Login successful")
        return True

    def get_firmware_info(self, firmware_id):
        """Get firmware information from M5Burner API."""
        url = f"{self.api_base_url}/api/admin/firmware"

        print(f"Getting firmware info for {firmware_id}...")
        response = self.session.get(url)

        if response.status_code != 200:
            raise Exception(f"Failed to get firmware info: {response.status_code} - {response.text}")

        firmwares = response.json()
        # Find the firmware with the matching ID
        for firmware in firmwares:
            if firmware.get('fid') == firmware_id:
                print(f"Found firmware: {firmware.get('name')}")
                return firmware

        raise Exception(f"Firmware with ID {firmware_id} not found")

    def upload_firmware_version(self, firmware_id, version, binary_path):
        """Upload a firmware version."""
        # Get existing firmware information
        firmware_info = self.get_firmware_info(firmware_id)

        url = f"{self.api_base_url}/api/admin/firmware"

        print(f"Uploading {binary_path} for firmware {firmware_id} version {version}...")

        # Prepare multipart form data
        data = {
            'name': firmware_info.get('name', ''),
            'description': firmware_info.get('description', ''),
            'category': firmware_info.get('category', ''),
            'author': firmware_info.get('author', ''),
            'version': version,
            'github': firmware_info.get('github', ''),
            'cover': 'null'
        }

        # Add the firmware binary file
        with open(binary_path, 'rb') as f:
            files = {'firmware': f}
            response = self.session.post(url, data=data, files=files)

        if response.status_code != 200:
            raise Exception(f"Upload failed: {response.status_code} - {response.text}")

        print(f"Upload successful for {firmware_id}")
        return response.json()

    def publish_firmware_version(self, firmware_id, version):
        """Publish a firmware version.

        The publish endpoint expects the value of the 'file' field from the
        version entry whose 'version' matches the given tag, not the tag itself.
        """
        firmware_info = self.get_firmware_info(firmware_id)

        # Locate the version entry matching the tag to get the 'file' identifier
        versions = firmware_info.get('versions') or firmware_info.get('version_list') or []
        file_id = None
        for v in versions:
            if v.get('version') == version:
                file_id = v.get('file')
                break

        if not file_id:
            raise Exception(
                f"Could not find 'file' field for version {version} of firmware {firmware_id}. "
                f"Available versions: {[v.get('version') for v in versions]}"
            )

        url = f"{self.api_base_url}/api/admin/firmware/{firmware_id}/publish/{file_id}/1"

        print(f"Publishing firmware {firmware_id} version {version} (file={file_id})...")

        response = self.session.put(url)

        if response.status_code != 200:
            raise Exception(f"Publish failed: {response.status_code} - {response.text}")

        result = response.json()
        if result.get('status') != 1:
            raise Exception(f"Publish failed: {result}")

        print(f"Publish successful for {firmware_id} version {version}")
        return result


def main():
    parser = argparse.ArgumentParser(description="Upload and publish firmware to M5Burner")
    parser.add_argument('--user', required=True, help='M5Burner username')
    parser.add_argument('--password', required=True, help='M5Burner password')
    parser.add_argument('--tag', required=True, help='Version tag (e.g., v1.0.0)')
    parser.add_argument('--api-base-url', default='http://m5burner-api.m5stack.com',
                       help='API base URL (default: http://m5burner-api.m5stack.com)')
    parser.add_argument('--artifacts-dir', default='.',
                       help='Directory containing .bin files (default: current directory)')
    parser.add_argument('--output', metavar='filename',
                       help='Write full HTTP request/response log to this file for debugging')

    args = parser.parse_args()

    # Validate artifacts directory
    artifacts_dir = Path(args.artifacts_dir)
    if not artifacts_dir.exists():
        print(f"Error: Artifacts directory {artifacts_dir} does not exist")
        sys.exit(1)

    # Check which firmware files exist
    existing_firmwares = {}
    for fid, filename in FIRMWARE_MAP.items():
        bin_path = artifacts_dir / filename
        if bin_path.exists():
            existing_firmwares[fid] = bin_path
        else:
            print(f"Warning: {filename} not found, skipping {fid}")

    if not existing_firmwares:
        print("Error: No firmware files found")
        sys.exit(1)

    print(f"Found {len(existing_firmwares)} firmware files to process")

    # Set up optional request/response logger
    logger = None
    if args.output:
        logger = RequestLogger(args.output)
        print(f"HTTP traffic will be logged to: {args.output}")

    # Initialize client
    client = M5BurnerClient(args.api_base_url, logger=logger)

    try:
        # Login
        client.login(args.user, args.password)

        # Upload all firmware versions first
        uploaded = []
        for fid, bin_path in existing_firmwares.items():
            try:
                client.upload_firmware_version(fid, args.tag, bin_path)
                uploaded.append(fid)
                print(f"Upload done for {fid}")
            except Exception as e:
                print(f"Error uploading {fid}: {e}")

        # Publish all successfully uploaded versions
        for fid in uploaded:
            try:
                client.publish_firmware_version(fid, args.tag)
                print(f"Successfully processed {fid}")
            except Exception as e:
                print(f"Error publishing {fid}: {e}")

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)
    finally:
        if logger:
            logger.close()

    print("All firmwares processed successfully")


if __name__ == "__main__":
    main()
