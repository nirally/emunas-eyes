import pathlib, subprocess, sys, tempfile
from PIL import Image
OUT = pathlib.Path(sys.argv[1]).resolve(); OUT.mkdir(exist_ok=True)
EDGE = r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
RED = "#E1191E"
MARK = f'''<g fill="{RED}">
    <path d="M10,104 Q80,36 186,86 L188,92 Q96,62 30,104 Z"/>
    <path d="M26,110 Q100,164 182,98 Q110,150 26,110 Z"/>
    <path d="M100,80 L124,104 L100,128 L76,104 Z"/>
    <path d="M190,90 Q176,150 120,174 Q90,186 64,196 Q116,168 140,150 Q172,124 190,90 Z"/>
  </g>'''
def lockup(bg, fg, sub):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 900 260" width="900" height="260">
  <rect width="900" height="260" fill="{bg}"/>
  <g transform="translate(36,22)">{MARK}</g>
  <g font-family="Bahnschrift">
    <text x="262" y="136" font-size="74" font-weight="300" letter-spacing="10" fill="{fg}">EMUNA’S EYES</text>
    <rect x="265" y="158" width="40" height="4" fill="{RED}"/>
    <text x="320" y="167" font-size="20" letter-spacing="9" fill="{sub}">MIRROR’S EDGE CATALYST · VR</text>
  </g>
</svg>'''
def icon(bg):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <rect width="256" height="256" rx="28" fill="{bg}"/>
  <g transform="translate(28,12)">{MARK}</g>
</svg>'''
files = {"lockup-dark": (lockup("#0B0B0C", "#F4F4F2", "#9A9A98"), 900, 260),
         "lockup-light": (lockup("#F4F4F2", "#141414", "#6A6A68"), 900, 260),
         "icon": (icon("#0B0B0C"), 256, 256)}
for name, (svg, w, h) in files.items():
    p = OUT / f"{name}.svg"; p.write_text(svg, encoding="utf-8")
    with tempfile.TemporaryDirectory() as prof:
        subprocess.run([EDGE, "--headless", "--disable-gpu", "--no-first-run", f"--user-data-dir={prof}",
                        f"--screenshot={OUT / (name + '.png')}", f"--window-size={w},{h}", p.as_uri()],
                       capture_output=True, timeout=60)
ims = [Image.open(OUT / f"{n}.png") for n in files]
sheet = Image.new("RGB", (900 + 256 + 20, 540), "#444")
sheet.paste(ims[0], (0, 0)); sheet.paste(ims[1], (0, 280)); sheet.paste(ims[2], (920, 0))
sheet.save(OUT / "sheet.png"); print("ok")
