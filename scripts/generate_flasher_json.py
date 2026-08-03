#!/usr/bin/env python3
"""
Generates src/flasher.json by combining manifest.json with GitHub release data.

For each release (excluding "beta" and "last" tags), checks which
Launcher-<device-id>.bin files exist and records the CDN URL via jsDelivr.

Output shape:
{
  "releases": [
    {
      "tag": "v1.2.3",
      "name": "v1.2.3",
      "published_at": "2025-01-01T00:00:00Z",
      "prerelease": false,
      "devices": {
        "<device-id>": "https://cdn.jsdelivr.net/gh/bmorcelli/Launcher@v1.2.3/Launcher-<device-id>.bin"
      }
    },
    ...
  ],
  "beta": {
    "tag": "beta",
    "devices": { "<device-id>": "https://cdn.jsdelivr.net/gh/bmorcelli/Launcher@beta/Launcher-<device-id>.bin" }
  }
}

Releases are ordered newest-first. Only devices whose .bin is present in
the release assets are included.
"""

import json
import os
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request

# Local dev: msys64 Python may lack system CA bundle — bypass SSL verification.
# In CI (ubuntu-latest) the system CAs are present so this context is not used.
_ssl_ctx = ssl.create_default_context()
if os.environ.get("CI") != "true":
    _ssl_ctx.check_hostname = False
    _ssl_ctx.verify_mode = ssl.CERT_NONE

REPO = "bmorcelli/Launcher"
ASSET_LINK = "https://github.com/bmorcelli/Launcher/releases/download"
# CORS proxy, whitelisted server-side to only accept URLs matching
# ASSET_LINK (see Launcher-API's /flasherProxy route).
JSDELIVR_BASE = "https://api.launcherhub.net/flasherProxy?url="
GITHUB_API = "https://api.github.com"

def github_get(path: str, token: str | None) -> list | dict:
    url = f"{GITHUB_API}{path}"
    req = urllib.request.Request(url)
    req.add_header("Accept", "application/vnd.github+json")
    req.add_header("X-GitHub-Api-Version", "2022-11-28")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, context=_ssl_ctx) as resp:
        return json.loads(resp.read().decode())


def get_all_releases(token: str | None) -> list[dict]:
    releases = []
    page = 1
    while True:
        page_data = github_get(f"/repos/{REPO}/releases?per_page=100&page={page}", token)
        if not page_data:
            break
        releases.extend(page_data)
        if len(page_data) < 100:
            break
        page += 1
    return releases


def asset_names(release: dict) -> set[str]:
    return {a["name"] for a in release.get("assets", [])}


def build_devices_map(
    all_devices: list[dict], tag: str, names: set[str], beta: bool = False
) -> dict[str, str]:
    release_ver = semver_tuple(tag)
    result: dict[str, str] = {}
    for device in all_devices:
        device_id = device["id"]
        if not beta:
            if not device.get("display", True):
                continue
            released_at = device.get("released_at")
            if released_at and semver_tuple(released_at) > release_ver:
                continue
        bin_name = f"Launcher-{device_id}.bin"
        if bin_name in names:
            gh_bin_url = f"{ASSET_LINK}/{tag}/{bin_name}"
            encoded_url = urllib.parse.quote(gh_bin_url, safe="")
            result[device_id] = f"{JSDELIVR_BASE}{encoded_url}"
    return result


def semver_tuple(tag: str) -> tuple[int, ...]:
    """Parse 'vX.Y.Z' or 'X.Y.Z' into a comparable int tuple."""
    clean = tag.lstrip("v")
    try:
        return tuple(int(x) for x in clean.split("."))
    except ValueError:
        return (0,)


def load_manifest(path: str) -> list[dict]:
    with open(path, encoding="utf-8") as f:
        manifest: dict = json.load(f)
    devices: list[dict] = []
    for group in manifest.values():
        for d in group:
            devices.append(d)
    return devices


def main():
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    manifest_path = os.path.join(os.path.dirname(__file__), "..", "src", "manifest.json")
    output_path = os.path.join(os.path.dirname(__file__), "..", "src", "flasher.json")

    print("Loading manifest.json...")
    all_devices = load_manifest(manifest_path)
    print(f"  {len(all_devices)} devices found.")

    print("Fetching releases from GitHub API...")
    all_releases = get_all_releases(token)
    print(f"  {len(all_releases)} releases fetched.")

    # Separate beta and last from versioned releases
    beta_release = None
    versioned_releases = []

    for rel in all_releases:
        tag = rel["tag_name"]
        if tag in ("beta", "last"):
            if tag == "beta":
                beta_release = rel
        else:
            versioned_releases.append(rel)

    # Sort versioned releases newest-first by published_at
    versioned_releases.sort(key=lambda r: r.get("published_at", ""), reverse=True)

    releases_out = []
    for rel in versioned_releases:
        tag = rel["tag_name"]
        names = asset_names(rel)
        devices = build_devices_map(all_devices, tag, names)
        if not devices:
            continue
        releases_out.append({
            "tag": tag,
            "name": rel.get("name") or tag,
            "published_at": rel.get("published_at", ""),
            "prerelease": rel.get("prerelease", False),
            "devices": devices,
        })

    beta_out = None
    if beta_release:
        tag = beta_release["tag_name"]
        names = asset_names(beta_release)
        devices = build_devices_map(all_devices, tag, names, beta=True)
        beta_out = {
            "tag": tag,
            "name": beta_release.get("name") or tag,
            "published_at": beta_release.get("published_at", ""),
            "devices": devices,
        }

    output = {
        "releases": releases_out,
        "beta": beta_out,
    }

    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=2)

    print(f"Written: {output_path}")
    print(f"  {len(releases_out)} versioned releases, beta={'yes' if beta_out else 'no'}")


if __name__ == "__main__":
    try:
        main()
    except urllib.error.HTTPError as e:
        print(f"GitHub API error: {e.code} {e.reason}", file=sys.stderr)
        sys.exit(1)
