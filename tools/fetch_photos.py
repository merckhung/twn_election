#!/usr/bin/env python3
"""Fetches missing candidate portraits from Wikipedia / Wikimedia Commons.

For every candidate in data/election/2026/candidates.json without
assets/photos/<id>.jpg, looks up the zh.wikipedia article (by the Chinese
name, or the optional "wiki" field of the candidate), downloads its lead
image from Commons, downsizes it (longest side 400 px, needs Pillow) and
records author/licence in assets/photos/CREDITS.json.

Only images whose Commons licence is free (CC-BY*, CC0, PD, GFDL) are kept.
Always double-check that the article is about the right person: several
candidates share names with other public figures. Use --dry-run first.

  python3 tools/fetch_photos.py --dry-run
  python3 tools/fetch_photos.py --only 65000-01,66000-03
"""
import argparse
import io
import json
import os
import re
import sys
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UA = "twn_election/0.1 (https://github.com/; candidate photo fetcher)"
FREE = re.compile(r"^(cc[- ]by|cc0|public domain|pd|gfdl)", re.I)


def get_json(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def get_bytes(url):
    req = urllib.request.Request(url, headers={"User-Agent": UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def lead_image(title):
    q = urllib.parse.urlencode({
        "action": "query", "format": "json", "prop": "pageimages|pageprops",
        "piprop": "name", "titles": title, "redirects": 1})
    pages = get_json("https://zh.wikipedia.org/w/api.php?" + q)["query"]["pages"]
    for page in pages.values():
        if "missing" in page or "disambiguation" in page.get("pageprops", {}):
            return None, None
        return page.get("title"), page.get("pageimage")
    return None, None


def commons_info(filename):
    q = urllib.parse.urlencode({
        "action": "query", "format": "json", "prop": "imageinfo",
        "iiprop": "url|extmetadata", "iiurlwidth": 400, "titles": "File:" + filename})
    pages = get_json("https://commons.wikimedia.org/w/api.php?" + q)["query"]["pages"]
    for page in pages.values():
        info = page.get("imageinfo", [{}])[0]
        meta = info.get("extmetadata", {})
        strip = lambda k: re.sub("<[^>]+>", "", meta.get(k, {}).get("value", "")).strip()
        return {
            "thumb": info.get("thumburl") or info.get("url"),
            "page": info.get("descriptionurl"),
            "license": strip("LicenseShortName"),
            "author": strip("Artist"),
        }
    return None


def save_jpeg(data, path):
    try:
        from PIL import Image
    except ImportError:
        with open(path, "wb") as f:
            f.write(data)
        return
    img = Image.open(io.BytesIO(data)).convert("RGB")
    img.thumbnail((400, 400))
    img.save(path, "JPEG", quality=88)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--only", default="", help="comma-separated candidate ids")
    ap.add_argument("--force", action="store_true", help="replace existing photos")
    args = ap.parse_args()

    cands = json.load(open(os.path.join(ROOT, "data/election/2026/candidates.json")))
    credits_path = os.path.join(ROOT, "assets/photos/CREDITS.json")
    credits = json.load(open(credits_path)) if os.path.exists(credits_path) else {}
    only = set(filter(None, args.only.split(",")))

    for race in cands["races"]:
        for c in race["candidates"]:
            cid = c["id"]
            out = os.path.join(ROOT, "assets/photos", cid + ".jpg")
            if only and cid not in only:
                continue
            if os.path.exists(out) and not args.force:
                continue
            try:
                title, image = lead_image(c.get("wiki") or c["name_zh"])
            except Exception as e:  # network errors etc.
                print(f"{cid} {c['name_zh']}: lookup failed: {e}")
                continue
            if not image:
                print(f"{cid} {c['name_zh']}: no article/lead image")
                continue
            info = commons_info(image)
            if not info or not info["thumb"]:
                print(f"{cid} {c['name_zh']}: image {image} not on Commons")
                continue
            if not FREE.match(info["license"] or ""):
                print(f"{cid} {c['name_zh']}: skipped non-free licence {info['license']!r}")
                continue
            print(f"{cid} {c['name_zh']}: {title} -> {image} [{info['license']}] {info['author']}")
            if args.dry_run:
                continue
            save_jpeg(get_bytes(info["thumb"]), out)
            credits[cid] = {
                "name_zh": c["name_zh"],
                "source": info["page"],
                "original_origin": f"Wikimedia Commons lead image of zh.wikipedia article {title!r}",
                "author": info["author"],
                "license_note": info["license"],
            }
    if not args.dry_run:
        with open(credits_path, "w") as f:
            json.dump(credits, f, ensure_ascii=False, indent=2)


if __name__ == "__main__":
    sys.exit(main())
