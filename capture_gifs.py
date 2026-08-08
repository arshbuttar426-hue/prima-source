#!/usr/bin/env python3
"""Capture looping GIFs of the prima widgets via headless Chrome."""
import asyncio, io, pathlib, sys
from playwright.async_api import async_playwright

BASE = pathlib.Path(__file__).resolve().parent
OUT = BASE / "assets"
OUT.mkdir(exist_ok=True)

WIDGETS = [
    ("prima_one_page.html",    "one_page",    None),
    ("prima_frame.html",       "frame",       None),
    ("prima-metamorph.html",   "metamorph",   None),
    ("prima_gravity.html",     "gravity",     ["click_wells"]),
    ("prima_mirror.html",      "mirror",      ["drag_mirror"]),
    ("prima_fracture.html",    "fracture",    ["drag_fault"]),
    ("prima_resonance.html",   "resonance",   ["click_pulses"]),
    ("prima_seam.html",        "seam",        None),
]

W, H = 1280, 720
FRAMES = 40
SWEEP_MS = 2400

async def run():
    async with async_playwright() as p:
        browser = await p.chromium.launch(channel="chrome")
        for html, name, acts in WIDGETS:
            page = await browser.new_page(viewport={"width": W, "height": H})
            await page.goto((BASE / "outputs" / html).as_uri())
            await page.wait_for_timeout(600)
            if acts:
                box = await page.evaluate("() => ({w: innerWidth, h: innerHeight})")
                cx, cy = box["w"] / 2, box["h"] / 2
                if "click_wells" in acts:
                    for dx, dy in [(0.30, 0.42), (0.62, 0.30), (0.80, 0.60)]:
                        await page.mouse.click(box["w"] * dx, box["h"] * dy)
                        await page.wait_for_timeout(250)
                if "click_pulses" in acts:
                    for dx, dy in [(0.35, 0.40), (0.60, 0.55), (0.75, 0.30)]:
                        await page.mouse.click(box["w"] * dx, box["h"] * dy)
                        await page.wait_for_timeout(300)
                if "drag_mirror" in acts:
                    await page.mouse.move(box["w"] * 0.30, box["h"] * 0.25)
                    await page.mouse.down()
                    for i in range(1, 25):
                        await page.mouse.move(
                            box["w"] * (0.30 + 0.35 * i / 24),
                            box["h"] * (0.25 + 0.45 * i / 24))
                        await page.wait_for_timeout(12)
                    await page.mouse.up()
                if "drag_fault" in acts:
                    await page.mouse.move(box["w"] * 0.35, box["h"] * 0.20)
                    await page.mouse.down()
                    for i in range(1, 25):
                        await page.mouse.move(
                            box["w"] * (0.35 + 0.30 * i / 24),
                            box["h"] * (0.20 + 0.55 * i / 24))
                        await page.wait_for_timeout(12)
                    await page.mouse.up()
                await page.wait_for_timeout(500)
            # sweep cursor across → conducts phase 0→1 (wave→lattice→burst)
            for i in range(FRAMES):
                await page.mouse.move(int(W * i / (FRAMES - 1)), int(H * 0.5))
                (OUT / f"_{name}_{i:03d}.jpg").write_bytes(await page.screenshot(type="jpeg", quality=88))
            await page.close()
            print(f"captured {name}")
        await browser.close()

asyncio.run(run())
