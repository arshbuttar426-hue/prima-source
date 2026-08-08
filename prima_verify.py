#!/usr/bin/env python3
"""prima_verify — headless canvas rendering validator.

Screenshots an HTML file via Playwright, clips to the canvas element,
counts non-black pixels. Returns PASSED or FAILED.

Usage:
  python3 prima_verify.py path/to/experiment.html
  python3 prima_verify.py path/to/experiment.html --threshold 3.0

Frame: experiment as renders_visible not_as blank_canvas
"""

import sys
import os
from pathlib import Path

try:
    from playwright.sync_api import sync_playwright
except ImportError:
    print("FAILED: playwright not installed. Run: pip install playwright && playwright install chromium")
    sys.exit(1)

THRESHOLD_PCT = float(os.environ.get("PRIMA_VERIFY_THRESHOLD", "2.0"))
VIEWPORT_WIDTH = 1280
VIEWPORT_HEIGHT = 800


def count_nonblack(image_bytes):
    """Returns percentage of non-black pixels in a PNG byte buffer."""
    # PNG signature check then raw pixel scan
    import struct
    import zlib

    # Simple approach: use PIL if available, raw pixel count otherwise
    try:
        from PIL import Image
        import io
        img = Image.open(io.BytesIO(image_bytes))
        pixels = list(img.getdata())
        total = len(pixels)
        if total == 0:
            return 0.0
        nonblack = sum(1 for p in pixels if isinstance(p, int) and p > 10 or isinstance(p, tuple) and sum(p[:3]) > 30)
        return (nonblack / total) * 100
    except ImportError:
        pass

    # Fallback: count pixels with any channel > 10
    # PNG IHDR gives width/height at fixed offset
    if len(image_bytes) < 33 or image_bytes[1:4] != b'PNG':
        return None
    w = int.from_bytes(image_bytes[16:20], 'big')
    h = int.from_bytes(image_bytes[20:24], 'big')
    if w == 0 or h == 0:
        return 0.0
    # Too complex to parse raw PNG without PIL — require PIL
    return None


def verify(filepath):
    if not os.path.isfile(filepath):
        return {"passed": False, "error": f"file not found: {filepath}"}

    abs_path = os.path.abspath(filepath)

    try:
        with sync_playwright() as p:
            browser = p.chromium.launch(headless=True)
            page = browser.new_page(viewport={"width": VIEWPORT_WIDTH, "height": VIEWPORT_HEIGHT})
            page.goto(f"file://{abs_path}", wait_until="networkidle", timeout=10000)

            # Wait for canvas to render
            page.wait_for_timeout(1500)

            # Find canvas element
            canvas = page.query_selector("canvas")
            if not canvas:
                browser.close()
                return {"passed": False, "error": "no canvas element found"}

            # Screenshot just the canvas
            screenshot = canvas.screenshot()

            # Also check page errors
            errors = []
            page.on("pageerror", lambda err: errors.append(str(err)))

            browser.close()

            # Count non-black pixels
            pct = count_nonblack(screenshot)
            if pct is None:
                return {"passed": False, "error": "cannot parse screenshot (install Pillow: pip install Pillow)"}

            passed = pct >= THRESHOLD_PCT
            return {
                "passed": passed,
                "nonblack_pct": round(pct, 1),
                "threshold": THRESHOLD_PCT,
                "canvas_size": f"{VIEWPORT_WIDTH}x{VIEWPORT_HEIGHT}",
                "errors": errors,
            }

    except Exception as e:
        return {"passed": False, "error": str(e)}


def main():
    if len(sys.argv) < 2:
        print("usage: python3 prima_verify.py <file.html> [--threshold N]")
        sys.exit(2)

    global THRESHOLD_PCT
    for i, arg in enumerate(sys.argv):
        if arg == "--threshold" and i + 1 < len(sys.argv):
            THRESHOLD_PCT = float(sys.argv[i + 1])

    result = verify(sys.argv[1])

    if result["passed"]:
        print(f"PASSED — {result['nonblack_pct']}% non-black pixels (threshold {result['threshold']}%)")
        if result.get("errors"):
            print(f"  console errors: {result['errors']}")
        sys.exit(0)
    else:
        error = result.get("error", "too few visible pixels")
        print(f"FAILED — {error}")
        if "nonblack_pct" in result:
            print(f"  {result['nonblack_pct']}% non-black pixels (threshold {result['threshold']}%)")
        sys.exit(1)


if __name__ == "__main__":
    main()
