import asyncio, pathlib
from playwright.async_api import async_playwright

BASE = pathlib.Path(__file__).resolve().parent
OUT = BASE / "assets"
W, H, FRAMES = 1280, 720, 40

async def run():
    async with async_playwright() as p:
        browser = await p.chromium.launch(channel="chrome")
        page = await browser.new_page(viewport={"width": W, "height": H})
        await page.goto((BASE / "outputs" / "prima_one_page.html").as_uri())
        await page.wait_for_timeout(800)
        for i in range(FRAMES):
            await page.evaluate(f"window.scrollTo(0, {H * 3 * i / (FRAMES - 1)})")
            await page.wait_for_timeout(50)
            (OUT / f"_one_page_{i:03d}.jpg").write_bytes(await page.screenshot(type="jpeg", quality=88))
        await browser.close()

asyncio.run(run())
