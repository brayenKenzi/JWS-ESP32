import os
from PIL import Image, ImageDraw, ImageFont

artifact_dir = r"C:\Users\Kenzi\.gemini\antigravity\brain\83616428-70ec-450c-9bf6-d47aac89da41"
workspace_dir = r"C:\Users\Kenzi\Desktop\MASTER WEBSITE\JWS ESP32\PREVIEWS"
os.makedirs(artifact_dir, exist_ok=True)
os.makedirs(workspace_dir, exist_ok=True)

# TFT ILI9341 resolution is 320x240. We will render at 640x480 (2x scale) for crisp presentation.
W, H = 640, 480

font_path = r"C:\Windows\Fonts\segoeui.ttf"
font_bold_path = r"C:\Windows\Fonts\segoeuib.ttf"
font_mono_path = r"C:\Windows\Fonts\consola.ttf"

def get_font(size, bold=False, mono=False):
    p = font_mono_path if mono else (font_bold_path if bold and os.path.exists(font_bold_path) else font_path)
    return ImageFont.truetype(p, size)

# -------------------------------------------------------------
# SCREEN 1: JWS DESKTOP IDLE (JAM & JADWAL SHOLAT KOTAMOBAGU)
# -------------------------------------------------------------
img1 = Image.new("RGB", (W, H), (10, 16, 26))
draw1 = ImageDraw.Draw(img1)

# Subtle background gradient/mesh
for y in range(H):
    r = int(10 + (y / H) * 12)
    g = int(16 + (y / H) * 20)
    b = int(26 + (y / H) * 35)
    draw1.line([(0, y), (W, y)], fill=(r, g, b))

# Top Status Bar
draw1.rectangle([(0, 0), (W, 44)], fill=(15, 23, 42))
draw1.line([(0, 44), (W, 44)], fill=(0, 200, 150))
draw1.text((20, 10), "MASJID KOTAMOBAGU", font=get_font(20, bold=True), fill=(240, 240, 240))
draw1.text((420, 12), "Senin, 09 Nov 2026", font=get_font(18), fill=(148, 163, 184))
draw1.text((580, 12), "[WiFi]", font=get_font(16, bold=True), fill=(34, 197, 94))

# Big Digital Clock
draw1.text((120, 60), "15:24", font=get_font(84, bold=True, mono=True), fill=(255, 255, 255))
draw1.text((395, 80), ":38", font=get_font(42, bold=True, mono=True), fill=(0, 220, 160))
draw1.text((475, 95), "WITA", font=get_font(22, bold=True), fill=(148, 163, 184))

# Hijri Date Pill
draw1.rounded_rectangle([(140, 165), (500, 205)], radius=15, fill=(30, 41, 59), outline=(51, 65, 85))
draw1.text((170, 172), "29 Jumadil Ula 1448 H  |  Fase: Ashar", font=get_font(18, bold=True), fill=(251, 191, 36))

# Prayer Timetable Grid (6 Cards)
prayers = [
    ("IMSAK", "04:12"),
    ("SUBUH", "04:22"),
    ("TERBIT", "05:38"),
    ("DZUHUR", "11:42"),
    ("ASHAR", "15:03"),
    ("MAGHRIB", "17:48"),
    ("ISYA", "18:59"),
]

card_w = 82
gap = 8
start_x = 10
y_card = 225

for i, (name, tm) in enumerate(prayers):
    x = start_x + i * (card_w + gap)
    is_active = (name == "ASHAR")
    border = (0, 220, 160) if is_active else (51, 65, 85)
    bg = (13, 71, 50) if is_active else (23, 32, 48)
    txt_col = (255, 255, 255) if is_active else (203, 213, 225)
    tm_col = (0, 240, 180) if is_active else (241, 245, 249)
    
    draw1.rounded_rectangle([(x, y_card), (x + card_w, y_card + 90)], radius=8, fill=bg, outline=border, width=2 if is_active else 1)
    draw1.text((x + 12, y_card + 8), name, font=get_font(15, bold=True), fill=txt_col)
    draw1.text((x + 14, y_card + 38), tm, font=get_font(20, bold=True, mono=True), fill=tm_col)
    if is_active:
        draw1.text((x + 14, y_card + 68), "NOW", font=get_font(12, bold=True), fill=(250, 204, 21))

# Bottom Running Text Bar & AI Status
draw1.rectangle([(0, 435), (W, H)], fill=(15, 23, 42))
draw1.line([(0, 435), (W, 435)], fill=(30, 41, 59))
draw1.text((15, 445), "AI STANDBY", font=get_font(15, bold=True), fill=(56, 189, 248))
draw1.ellipse([(108, 452), (118, 462)], fill=(34, 197, 94))
draw1.text((140, 445), "Mari menjaga kekhusyukan dan ketertiban sholat jamaah di masjid.", font=get_font(16), fill=(226, 232, 240))

# -------------------------------------------------------------
# SCREEN 2: XIAOZHI AI VOICE MODE (EXPRESSION & CHAT)
# -------------------------------------------------------------
img2 = Image.new("RGB", (W, H), (8, 12, 20))
draw2 = ImageDraw.Draw(img2)

# Glow background
for y in range(H):
    val = int(8 + (1 - abs(y - 200) / 200) * 25)
    draw2.line([(0, y), (W, y)], fill=(8, val, int(val * 1.4)))

# Top Bar (Mini Clock & Status)
draw2.rectangle([(0, 0), (W, 40)], fill=(15, 23, 42))
draw2.line([(0, 40), (W, 40)], fill=(56, 189, 248))
draw2.text((20, 8), "XIAOZHI ISLAMI AI", font=get_font(18, bold=True), fill=(56, 189, 248))
draw2.text((450, 8), "Ashar 15:03  |  15:25", font=get_font(16, mono=True), fill=(148, 163, 184))

# Cute Robot / Cyber-Islamic Eyes
# Left Eye
draw2.rounded_rectangle([(180, 80), (280, 160)], radius=35, fill=(0, 230, 180), outline=(100, 255, 220), width=3)
draw2.ellipse([(205, 95), (235, 125)], fill=(255, 255, 255))
# Right Eye
draw2.rounded_rectangle([(360, 80), (460, 160)], radius=35, fill=(0, 230, 180), outline=(100, 255, 220), width=3)
draw2.ellipse([(385, 95), (415, 125)], fill=(255, 255, 255))
# Friendly Smile
draw2.arc([(270, 155), (370, 195)], start=20, end=160, fill=(0, 230, 180), width=6)

# Sound Wave / Equalizer Visualizer (Dynamic audio response)
bars = [15, 30, 60, 95, 120, 75, 45, 80, 110, 65, 30, 15]
bx_start = 140
for idx, b_h in enumerate(bars):
    bx = bx_start + idx * 30
    draw2.rounded_rectangle([(bx, 240 - b_h // 2), (bx + 14, 240 + b_h // 2)], radius=7, fill=(56, 189, 248))

# Speech Bubble / Conversation Transcript
draw2.rounded_rectangle([(40, 290), (600, 440)], radius=18, fill=(20, 30, 48), outline=(56, 189, 248), width=2)
draw2.text((60, 305), "User: \"Kapan waktu sholat Maghrib hari ini?\"", font=get_font(18, bold=True), fill=(253, 224, 71))
draw2.text((60, 345), "Xiaozhi: \"Untuk Kota Kotamobagu, waktu Maghrib", font=get_font(20, bold=True), fill=(255, 255, 255))
draw2.text((60, 380), "jatuh pada pukul 17:48 WITA (sisa 2 jam 23 menit lagi).\"", font=get_font(20, bold=True), fill=(0, 240, 180))

# Bottom mode hint
draw2.text((180, 452), "[Tekan Mic atau Panggil 'Halo Xiaozhi']", font=get_font(15), fill=(148, 163, 184))

# -------------------------------------------------------------
# SCREEN 3: IQOMAH COUNTDOWN MODE (PERINGATAN SEBELUM SHOLAT)
# -------------------------------------------------------------
img3 = Image.new("RGB", (W, H), (15, 23, 42))
draw3 = ImageDraw.Draw(img3)

# Pulsing amber/emerald header
draw3.rectangle([(0, 0), (W, 70)], fill=(180, 83, 9))
draw3.text((150, 15), "HITUNG MUNDUR IQOMAH", font=get_font(28, bold=True), fill=(255, 255, 255))

draw3.text((190, 85), "SHOLAT ASHAR", font=get_font(32, bold=True), fill=(251, 191, 36))

# Big Countdown Digits
draw3.rounded_rectangle([(110, 135), (530, 285)], radius=25, fill=(30, 41, 59), outline=(245, 158, 11), width=4)
draw3.text((140, 140), "04:58", font=get_font(110, bold=True, mono=True), fill=(255, 255, 255))

# Progress Bar
draw3.rounded_rectangle([(80, 315), (560, 340)], radius=12, fill=(51, 65, 85))
draw3.rounded_rectangle([(80, 315), (320, 340)], radius=12, fill=(245, 158, 11))
draw3.text((275, 348), "Jeda Iqomah: 10 Menit", font=get_font(16), fill=(148, 163, 184))

# Reminder footer
draw3.rounded_rectangle([(40, 385), (600, 455)], radius=15, fill=(15, 35, 30), outline=(16, 185, 129))
draw3.text((80, 398), "Luruskan dan rapatkan shaf. Nonaktifkan nada dering HP.", font=get_font(18, bold=True), fill=(110, 231, 183))
draw3.text((160, 426), "Speaker akan berbunyi BEEP saat waktu habis.", font=get_font(16), fill=(209, 250, 229))

# Save all images to artifact and workspace folders
out1_art = os.path.join(artifact_dir, "preview_1_jws_standby.png")
out2_art = os.path.join(artifact_dir, "preview_2_xiaozhi_expression.png")
out3_art = os.path.join(artifact_dir, "preview_3_iqomah_alert.png")

out1_ws = os.path.join(workspace_dir, "preview_1_jws_standby.png")
out2_ws = os.path.join(workspace_dir, "preview_2_xiaozhi_expression.png")
out3_ws = os.path.join(workspace_dir, "preview_3_iqomah_alert.png")

img1.save(out1_art)
img2.save(out2_art)
img3.save(out3_art)

img1.save(out1_ws)
img2.save(out2_ws)
img3.save(out3_ws)

print("Images generated successfully!")
print("Artifact paths:", out1_art, out2_art, out3_art)
