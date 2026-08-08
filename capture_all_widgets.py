#!/usr/bin/env python3
"""Capture looping GIFs for every approved prima widget."""
import asyncio, pathlib
from playwright.async_api import async_playwright

BASE = pathlib.Path(__file__).resolve().parent
OUT = BASE / "assets"
OUT.mkdir(exist_ok=True)
W, H, FRAMES = 1280, 720, 36

WIDGETS = [
    ("as-apple.html",                     "as_apple",            "scroll"),
    ("as-parallax.html",                  "as_parallax",         "scroll"),
    ("bebop-clock.html",                  "bebop_clock",         "sweep"),
    ("flow-field.html",                   "flow_field",          "sweep"),
    ("outputs/aether_material.html",      "aether_material",     "scroll"),
    ("outputs/aurascope.html",            "aurascope",           "sweep"),
    ("outputs/constraint_ripple.html",    "constraint_ripple",   "sweep"),
    ("outputs/lacquer_frame.html",        "lacquer_frame",       "scroll"),
    ("outputs/prima-mechanism.html",      "prima_mechanism",     "scroll"),
    ("outputs/prima-metamorph-field.html","prima_metamorph_field","scroll"),
    ("outputs/prima-observe.html",        "prima_observe",       "scroll"),
    ("outputs/prima-shape.html",          "prima_shape",         "scroll"),
    ("outputs/prima_conductor.html",      "prima_conductor",     "scroll"),
    ("outputs/prima_css_native.html",     "prima_css_native",    "scroll"),
    ("outputs/prima_decrypter.html",      "prima_decrypter",     "sweep"),
    ("outputs/prima_filter_stack.html",   "prima_filter_stack",  "scroll"),
    ("outputs/prima_landing.html",        "prima_landing",       "scroll"),
    ("outputs/prima_launcher.html",       "prima_launcher",      "sweep"),
    ("outputs/prima_membrane.html",       "prima_membrane",      "sweep"),
    ("outputs/prima_mono_ledger.html",    "prima_mono_ledger",   "sweep"),
    ("outputs/prima_transposition.html",  "prima_transposition", "scroll"),
    ("outputs/rotor_gallery.html",        "rotor_gallery",       "sweep"),
    ("outputs/solstice.html",             "solstice",            "sweep"),
    ("outputs/taskflow_material3.html",   "taskflow_material3",  "scroll"),
    ("outputs/thinkpipe_ai_brain.html",   "thinkpipe_ai_brain",  "sweep"),
    ("outputs/vantage_saas_prima.html",   "vantage_saas_prima",  "scroll"),
    ("outputs/verb_chain_benchmark.html", "verb_chain_benchmark","scroll"),
    ("outputs/waveraft_compiled.html",    "waveraft_compiled",   "sweep"),
    ("prima-constraint-bench.html",       "prima_constraint_bench","scroll"),
    ("prima-constraint-field.html",       "prima_constraint_field","sweep"),
    ("prima-crm.html",                    "prima_crm",           "sweep"),
    ("prima-crypto.html",                 "prima_crypto",        "sweep"),
    ("prima-forensic.html",               "prima_forensic",      "sweep"),
    ("prima-index.html",                  "prima_index",         "sweep"),
    ("prima-landing.html",                "prima_landing_root",  "scroll"),
    ("prima-mechanism.html",              "prima_mechanism_root","scroll"),
]

async def run():
    async with async_playwright() as p:
        browser = await p.chromium.launch(channel="chrome")
        for html, name, mode in WIDGETS:
            page = await browser.new_page(viewport={"width": W, "height": H})
            await page.goto((BASE / html).as_uri())
            await page.wait_for_timeout(800)
            maxh = await page.evaluate("Math.max(document.body.scrollHeight, document.documentElement.scrollHeight)")
            if mode == "scroll":
                span = max(1, maxh - H)
                for i in range(FRAMES):
                    await page.evaluate(f"window.scrollTo(0, {int(span * i / (FRAMES - 1))})")
                    await page.wait_for_timeout(60)
                    (OUT / f"_{name}_{i:03d}.jpg").write_bytes(await page.screenshot(type="jpeg", quality=88))
            else:
                for i in range(FRAMES):
                    await page.mouse.move(int(W * i / (FRAMES - 1)), int(H * 0.5))
                    await page.wait_for_timeout(40)
                    (OUT / f"_{name}_{i:03d}.jpg").write_bytes(await page.screenshot(type="jpeg", quality=88))
            await page.close()
            print(f"captured {name} ({mode})")
        await browser.close()

asyncio.run(run())
