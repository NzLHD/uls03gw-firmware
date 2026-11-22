#!/usr/bin/env python3
"""Build docs/ULS-03GW_manual.pdf.

    uv run --with reportlab --with pillow python docs/manual/make_manual.py

setup_page.png is a screenshot of the real setup page (firmware/apps/uls03gw/web/index.html) served by
mock_server.py with example settings. Retake it with headless Firefox if the page changes:

    python3 docs/manual/mock_server.py firmware/apps/uls03gw/web/index.html &
    firefox --headless --window-size=460,2900 --screenshot docs/manual/setup_page.png http://127.0.0.1:8765/
"""
import os

from PIL import Image
from reportlab.graphics.shapes import Drawing, Group, Line, Polygon, Rect, String
from reportlab.lib import colors
from reportlab.lib.enums import TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (Image as RLImage, KeepTogether, PageBreak, Paragraph, SimpleDocTemplate,
                                Spacer, Table, TableStyle)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, 'docs', 'ULS-03GW_manual.pdf')
FW = '0.2.2'

FONT_DIR = '/usr/share/fonts/truetype/dejavu'
pdfmetrics.registerFont(TTFont('DV', os.path.join(FONT_DIR, 'DejaVuSans.ttf')))
pdfmetrics.registerFont(TTFont('DV-B', os.path.join(FONT_DIR, 'DejaVuSans-Bold.ttf')))
pdfmetrics.registerFont(TTFont('DV-M', os.path.join(FONT_DIR, 'DejaVuSansMono.ttf')))
pdfmetrics.registerFontFamily('DV', normal='DV', bold='DV-B', italic='DV', boldItalic='DV-B')

INK = colors.HexColor('#1d2329')
MUTED = colors.HexColor('#5b6670')
ACCENT = colors.HexColor('#1f6feb')
LINE = colors.HexColor('#c9d1d9')
WATER = colors.HexColor('#cfe3fb')
WATER_EDGE = colors.HexColor('#5b9bd5')
ALARM = colors.HexColor('#c62828')
SOFT = colors.HexColor('#f3f5f7')

S = {
    'title': ParagraphStyle('title', fontName='DV-B', fontSize=22, leading=27, textColor=INK, spaceAfter=4),
    'sub': ParagraphStyle('sub', fontName='DV', fontSize=11, leading=15, textColor=MUTED, spaceAfter=14),
    'h1': ParagraphStyle('h1', fontName='DV-B', fontSize=15, leading=19, textColor=INK, spaceBefore=10,
                         spaceAfter=6),
    'h2': ParagraphStyle('h2', fontName='DV-B', fontSize=11.5, leading=15, textColor=INK, spaceBefore=8,
                         spaceAfter=3),
    'body': ParagraphStyle('body', fontName='DV', fontSize=9.6, leading=13.6, textColor=INK, spaceAfter=5,
                           alignment=TA_LEFT),
    'small': ParagraphStyle('small', fontName='DV', fontSize=8.4, leading=11.2, textColor=MUTED),
    'cell': ParagraphStyle('cell', fontName='DV', fontSize=8.4, leading=11, textColor=INK),
    'cellb': ParagraphStyle('cellb', fontName='DV-B', fontSize=8.4, leading=11, textColor=INK),
    'head': ParagraphStyle('head', fontName='DV-B', fontSize=8.4, leading=11, textColor=colors.white),
    'li': ParagraphStyle('li', fontName='DV', fontSize=9.6, leading=13.6, textColor=INK, leftIndent=14,
                         bulletIndent=3, spaceAfter=2),
    'note': ParagraphStyle('note', fontName='DV', fontSize=9, leading=12.6, textColor=INK, backColor=SOFT,
                           borderPadding=(6, 8, 6, 8), spaceBefore=4, spaceAfter=8),
}


def P(text, style='body'):
    return Paragraph(text, S[style])


def bullets(items, style='li'):
    return [Paragraph(t, S[style], bulletText='•') for t in items]


def numbered(items):
    return [Paragraph(t, S['li'], bulletText='%d.' % (i + 1)) for i, t in enumerate(items)]


def table(rows, widths, head=True, zebra=True):
    data = []
    for r, row in enumerate(rows):
        style = 'head' if head and r == 0 else 'cell'
        data.append([Paragraph(str(c), S[style]) for c in row])
    t = Table(data, colWidths=widths, repeatRows=1 if head else 0)
    st = [('VALIGN', (0, 0), (-1, -1), 'TOP'),
          ('LINEBELOW', (0, 0), (-1, -1), 0.4, LINE),
          ('TOPPADDING', (0, 0), (-1, -1), 3.5), ('BOTTOMPADDING', (0, 0), (-1, -1), 3.5),
          ('LEFTPADDING', (0, 0), (-1, -1), 5), ('RIGHTPADDING', (0, 0), (-1, -1), 5)]
    if head:
        st.append(('BACKGROUND', (0, 0), (-1, 0), INK))
    if zebra:
        for r in range(1 if head else 0, len(rows)):
            if r % 2 == 0:
                st.append(('BACKGROUND', (0, r), (-1, r), SOFT))
    t.setStyle(TableStyle(st))
    return t


# --------------------------------------------------------------------------- tank diagram

def tank_diagram():
    """Side view of a tank with the measurements the settings refer to (example values)."""
    W, Hh = 470, 340
    d = Drawing(W, Hh)
    H_CM, FULL_CM, DIST_CM = 180.0, 25.0, 66.0         # example: matches the screenshot
    UP, LOW = 0.90, 0.20
    top, bottom = 300.0, 40.0                           # y of the sensor face and the tank bottom
    def y(cm_from_sensor):
        return top - cm_from_sensor / H_CM * (top - bottom)
    span = H_CM - FULL_CM
    y_full, y_surf = y(FULL_CM), y(DIST_CM)
    y_up = y(H_CM - UP * span)
    y_low = y(H_CM - LOW * span)
    tx0, tx1 = 150, 320

    # tank body + liquid
    d.add(Rect(tx0, bottom, tx1 - tx0, y_surf - bottom, fillColor=WATER, strokeColor=None))
    d.add(Line(tx0, y_surf, tx1, y_surf, strokeColor=WATER_EDGE, strokeWidth=1.4))
    d.add(Rect(tx0, bottom, tx1 - tx0, top + 4 - bottom, fillColor=None, strokeColor=INK, strokeWidth=1.6))
    # blind zone (first 10 cm below the sensor)
    y_blind = y(10)
    d.add(Rect(tx0 + 1, y_blind, tx1 - tx0 - 2, top - y_blind, fillColor=colors.HexColor('#eeeeee'),
               strokeColor=None))
    d.add(String((tx0 + tx1) / 2, y_blind + 3, 'blind zone (10 cm)', fontName='DV', fontSize=6.5,
                 fillColor=MUTED, textAnchor='middle'))
    # sensor on the lid
    sx = (tx0 + tx1) / 2
    d.add(Rect(sx - 22, top, 44, 18, fillColor=INK, strokeColor=None))
    d.add(String(sx, top + 22, 'sensor', fontName='DV-B', fontSize=8, fillColor=INK, textAnchor='middle'))
    # ultrasonic path
    for dx in (-6, 6):
        d.add(Line(sx + dx, top, sx + dx, y_surf, strokeColor=ACCENT, strokeWidth=0.6, strokeDashArray=[2, 2]))

    def hline(yy, color, label, dash=None, x_label=tx1 + 6, anchor='start'):
        d.add(Line(tx0, yy, tx1, yy, strokeColor=color, strokeWidth=0.9, strokeDashArray=dash))
        d.add(String(x_label, yy - 3, label, fontName='DV', fontSize=7.2, fillColor=color, textAnchor=anchor))

    hline(y_full, INK, '', dash=[4, 2])
    d.add(String(tx0 + 4, y_full + 3, 'full line', fontName='DV', fontSize=7, fillColor=INK))
    hline(y_up, ALARM, '', dash=[3, 3])
    d.add(String(tx0 + 4, y_up + 3, 'upper alarm 90 %', fontName='DV', fontSize=7, fillColor=ALARM))
    hline(y_low, ALARM, '', dash=[3, 3])
    d.add(String(tx0 + 4, y_low + 3, 'lower alarm 20 %', fontName='DV', fontSize=7, fillColor=ALARM))
    d.add(String(tx0 + 4, y_surf - 10, 'liquid surface', fontName='DV', fontSize=7, fillColor=WATER_EDGE))
    d.add(String(tx0 + 4, bottom + 4, 'tank bottom', fontName='DV', fontSize=7, fillColor=INK))

    def arrow(x, y0, y1, label, sub, color=INK, side='left'):
        g = Group()
        g.add(Line(x, y0, x, y1, strokeColor=color, strokeWidth=1))
        for yy, s in ((y0, 1), (y1, -1)):
            g.add(Polygon([x, yy, x - 3, yy + 6 * s, x + 3, yy + 6 * s], fillColor=color, strokeColor=None))
        my = (y0 + y1) / 2
        if side == 'left':
            g.add(String(x - 6, my + 3, label, fontName='DV-B', fontSize=8, fillColor=color, textAnchor='end'))
            g.add(String(x - 6, my - 7, sub, fontName='DV', fontSize=7, fillColor=MUTED, textAnchor='end'))
        else:
            g.add(String(x + 6, my + 3, label, fontName='DV-B', fontSize=8, fillColor=color))
            g.add(String(x + 6, my - 7, sub, fontName='DV', fontSize=7, fillColor=MUTED))
        d.add(g)
        for yy in (y0, y1):    # extension lines to the nearer tank wall
            near = tx0 if x < tx0 else tx1
            d.add(Line(min(x, near), yy, max(x, near), yy, strokeColor=LINE, strokeWidth=0.5))

    arrow(40, top, bottom, 'Install height', '180 cm (setting)', side='right')
    arrow(135, top, y_full, 'Full distance', '25 cm (setting)', side='left')
    arrow(345, top, y_surf, 'Distance', '66 cm (measured)', color=ACCENT, side='right')
    arrow(410, y_surf, bottom, 'Depth', '114 cm', color=WATER_EDGE, side='right')
    return d


# --------------------------------------------------------------------------- screenshot

def screenshot_pair(width_total, max_h):
    """The (tall) setup page screenshot, split at a gap between cards into two columns."""
    src = os.path.join(HERE, 'setup_page.png')
    im = Image.open(src).convert('RGB')
    w, h = im.size
    bg = im.getpixel((5, h - 5))
    rows = [y for y in range(h) if any(im.getpixel((x, y)) != bg for x in range(0, w, 3))]
    end = rows[-1] + 12
    # split at the blank gap between cards closest to the middle
    blank = [y for y in range(int(end * 0.4), int(end * 0.6))
             if all(im.getpixel((x, y)) == bg for x in range(0, w, 2))]
    mid = min(blank, key=lambda yy: abs(yy - end / 2)) if blank else end // 2
    parts = []
    for i, (a, b) in enumerate(((0, mid), (mid, end))):
        p = os.path.join(HERE, '_part%d.png' % i)
        im.crop((0, a, w, b)).save(p)
        parts.append((p, b - a))
    col_w = (width_total - 8 * mm) / 2
    scale = min(col_w / w, max_h / max(hh for _, hh in parts))
    imgs = [RLImage(p, width=w * scale, height=hh * scale) for p, hh in parts]
    t = Table([imgs], colWidths=[col_w + 4 * mm, col_w + 4 * mm])
    t.setStyle(TableStyle([('VALIGN', (0, 0), (-1, -1), 'TOP'), ('LEFTPADDING', (0, 0), (-1, -1), 0),
                           ('RIGHTPADDING', (0, 0), (-1, -1), 0), ('BOX', (0, 0), (0, 0), 0.5, LINE),
                           ('BOX', (1, 0), (1, 0), 0.5, LINE)]))
    return t, [p for p, _ in parts]


# --------------------------------------------------------------------------- content

def build():
    doc = SimpleDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm,
                            bottomMargin=16 * mm, title='ULS-03GW manual', author='ULS-03GW clean firmware',
                            subject='User manual, firmware %s' % FW)
    full_w = A4[0] - 36 * mm
    st = []

    st += [P('ULS-03GW tank level sensor', 'title'),
           P('User manual for the clean (cloud-free) firmware %s' % FW, 'sub')]
    st += [P('This firmware replaces the Tuya software on the ULS-03GW ultrasonic tank sensor. The sensor '
             'measures the distance down to the liquid, works out the depth and fill level, and sends them '
             'to your own MQTT broker instead of the Tuya cloud. Home Assistant finds it automatically.'),
           P('The README in the project repository explains how to install the firmware (wiring, backup and '
             'flashing). Keep your backup of the original firmware: writing it back restores the stock Tuya '
             'software.', 'small'),
           P('The firmware was built by Claude (Anthropic\'s AI model). The developer tested and verified it on '
             'hardware.', 'small'),
           Spacer(1, 6)]

    st.append(P('1. At a glance', 'h1'))
    st.append(table([
        ['', ''],
        ['Measures', 'Distance from the sensor to the liquid, 10 cm to 3 m'],
        ['Reports', 'Distance, depth, fill level (%), volume, alarm, Wi-Fi signal, power source'],
        ['Sends to', 'Your MQTT broker, with TLS or without; Home Assistant discovery'],
        ['On battery', 'Wakes every 30 min to read with Wi-Fi off, and sends only when something changed, an '
                       'alarm changed, the button was pressed, or every 6 h as a heartbeat. Asleep it draws '
                       'about 52 µA, so 3× 18650 cells should last around 5 years (estimate).'],
        ['On USB power', 'Stays connected, reads every 10 s and sends on change.'],
        ['Setup', 'Through its own Wi-Fi network and web page. You don\'t need an app or an account.'],
    ][1:], [32 * mm, full_w - 32 * mm], head=False))

    st.append(P('Button and lights', 'h2'))
    st.append(table([
        ['Do this', 'What happens'],
        ['Short press', 'Takes a reading and sends it now. Blue flash: sent. Red flashes ×3: it failed.'],
        ['Hold 4 s', 'The blue light stays on. Release to open the setup network again.'],
        ['Hold 15 s', 'Blue and red blink fast. Release to erase all settings (factory reset).'],
        ['Press while in setup', 'Leaves setup.'],
    ], [32 * mm, full_w - 32 * mm]))
    st.append(Spacer(1, 3))
    st.append(table([
        ['Light', 'Meaning'],
        ['Blue, slow blink', 'Setup network is open'],
        ['Blue, fast blink', 'Connecting (after a button press)'],
        ['Red, steady', 'Running on USB power'],
        ['Both off', 'Normal on battery: the sensor sleeps between readings'],
    ], [32 * mm, full_w - 32 * mm]))

    # ---- install and tank settings
    st.append(PageBreak())
    st.append(P('2. Installing and measuring your tank', 'h1'))
    st += bullets([
        'Mount the sensor on top of the tank, pointing straight down at the liquid. Keep it away from the '
        'inlet, ladders and side walls: anything in the beam gives false echoes.',
        'The sensor cannot see anything in the first 10 cm below its face, so the full line must be at least '
        '10 cm below the sensor.',
        'Measure everything from the <b>face of the sensor</b>, not from the tank lid.',
    ])
    st.append(Spacer(1, 4))
    st.append(KeepTogether([tank_diagram()]))
    st.append(P('Example values, the same as in the screenshot in section 3.', 'small'))
    st.append(P('The two settings you have to enter', 'h2'))
    st.append(table([
        ['Setting', 'Measure from the sensor face to…'],
        ['Install height', 'the bottom of the tank (empty level). This is the 0 % point.'],
        ['Full distance', 'the level you call full, for example the overflow. This is the 100 % point. '
                                  'Tip: fill the tank, press <i>Read sensor now</i> and then <i>Use current '
                                  'distance as full</i>.'],
    ], [35 * mm, full_w - 35 * mm]))
    st.append(P('How the numbers are worked out', 'h2'))
    st.append(table([
        ['Value', 'Formula', 'Example'],
        ['depth', 'install height − distance', '180 − 66 = 114 cm'],
        ['level', 'depth ÷ (install height − full distance) × 100', '114 ÷ 155 = 73 %'],
        ['volume', 'level × tank volume', '73 % of 1000 L = 730 L'],
        ['alarm', '<i>upper</i> when level ≥ upper alarm; <i>lower</i> when level < lower alarm', 'normal'],
    ], [22 * mm, 82 * mm, full_w - 104 * mm]))
    st.append(P('Until install height is larger than full distance, the sensor reports '
                '<font name="DV-M">configured: false</font> and no level. Both are 10 cm out of the box. '
                'If the liquid is out of reach (distance larger than the install height), depth reads 0 with '
                'the error <font name="DV-M">distance_gt_height</font>.', 'note'))

    # ---- setup
    st.append(PageBreak())
    st.append(P('3. Setting it up', 'h1'))
    st += numbered([
        'Power the sensor. If it has never been set up, it opens its own Wi-Fi network '
        '<b>ULS-03GW-XXXXXX</b> and the blue light blinks slowly. To open it later, hold the button for 4 s.',
        'Join that network with your phone or laptop. Password: <b>uls03gw-setup</b>. The setup page should '
        'open by itself. If it doesn\'t, go to <b>http://192.168.4.1/</b>.',
        'Fill in Wi-Fi, MQTT and the tank values (see section 4), then press <b>Test Wi-Fi + MQTT</b>. It '
        'checks each step and tells you which one failed and why.',
        'Press <b>Save</b>. Then press the button, or wait 5 minutes, to leave setup. The sensor restarts and '
        'starts reporting.',
    ])
    st.append(P('Change the setup network password (Device section) during the first setup.', 'note'))
    st.append(P('The status card', 'h2'))
    st.append(P('The top of the setup page shows what the sensor knows right now:'))
    st.append(table([
        ['Line', 'Meaning'],
        ['fw, mac', 'Firmware version and the sensor\'s Wi-Fi address.'],
        ['power', '<i>battery</i> or <i>usb</i>, from the power-detect pin.'],
        ['rssi_last', 'Wi-Fi signal at the last connection, in dBm. Above −70 is fine; below −80 is weak.'],
        ['distance_mm', 'Last measured distance from the sensor face to the liquid.'],
        ['depth_cm, level_pct', 'Depth and fill level worked out from the tank settings (section 2).'],
        ['alarm', '<i>normal</i>, <i>lower</i> or <i>upper</i>.'],
        ['temp_c', 'Temperature reported by the sensor head.'],
        ['last_send, last_error', 'Whether the last report reached the broker, and the reason if not.'],
        ['boot_count', 'Number of starts, including every wake from sleep.'],
    ], [38 * mm, full_w - 38 * mm]))
    st.append(PageBreak())
    st.append(P('The setup page', 'h2'))
    shots, parts = screenshot_pair(full_w, A4[1] - 32 * mm - 60)
    st.append(shots)
    st.append(P('The setup page (example settings). Passwords and the certificate are never shown again '
                'after saving.', 'small'))

    # ---- settings reference
    st.append(PageBreak())
    st.append(P('4. Settings', 'h1'))
    st.append(P('Every setting is on the setup page. Leave a password field empty to keep the stored one; tick '
                '<i>clear</i> in the Device section to remove it.'))
    W1, W2, W3 = 33 * mm, 28 * mm, 25 * mm
    W4 = full_w - W1 - W2 - W3

    def section(title, rows):
        st.append(P(title, 'h2'))
        st.append(table([['Setting', 'Default', 'Range', 'What it does']] + rows, [W1, W2, W3, W4]))

    section('Wi-Fi', [
        ['Network', '—', 'up to 32 chars', 'Your Wi-Fi name. <i>Scan</i> lists the networks it can see.'],
        ['Password', '—', '8–63 chars', 'Your Wi-Fi password. Leave empty for an open network.'],
        ['Host name', 'uls03gw-xxxxxx', 'letters, digits, -', 'The name it gives your router.'],
        ['Static IP', 'off', '', 'Use a fixed address instead of DHCP (IP, netmask, gateway, DNS). Saves about '
                                 '1 s per send on battery.'],
    ])
    section('MQTT', [
        ['Broker host / IP', '—', '', 'Your MQTT broker. Leave empty to only measure and show readings.'],
        ['Port', '8883', '1–65535', '8883 for TLS, 1883 without. Changes with the TLS box.'],
        ['TLS', 'on', '', 'Encrypts the connection. Without TLS the password and data travel in the clear.'],
        ['CA certificate', '—', 'PEM, ≤ 4 KB', 'The certificate of the authority that signed your broker\'s '
                                                'certificate (your <font name="DV-M">ca.crt</font>), never a '
                                                'private key. Paste it or upload the file.'],
        ['Check broker name', 'on', '', 'The broker\'s certificate must match the host name or IP you entered.'],
        ['User / Password', '—', '', 'Your broker login, if it needs one.'],
        ['Client ID', 'uls03gw-xxxxxx', '', 'Must be unique on your broker.'],
        ['Base topic', 'uls03gw/xxxxxx', '', 'Start of every topic this sensor uses (see section 5).'],
        ['HA discovery', 'on', '', 'Announces the sensor to Home Assistant automatically.'],
        ['Discovery prefix', 'homeassistant', '', 'Leave as is unless you changed it in Home Assistant.'],
    ])
    section('Tank', [
        ['Install height', '10 cm', '1–1000 cm', 'Sensor face to the tank bottom (section 2).'],
        ['Full distance', '10 cm', '0–999 cm', 'Sensor face to the full line (section 2).'],
        ['Upper alarm %', '0 (off)', '0–100', 'Alarm <i>upper</i> when the level reaches this.'],
        ['Lower alarm %', '0 (off)', '0–100', 'Alarm <i>lower</i> when the level drops below this. Must be below '
                                              'the upper alarm.'],
        ['Tank volume', '0 (off)', '0–100000 L', 'Litres at 100 %. Adds a volume reading.'],
    ])
    section('Schedule', [
        ['Battery: read every', '30 min', '5–1440 min', 'How often it wakes up to measure.'],
        ['Battery: heartbeat', '6 h', '1–24 h', 'Sends even when nothing changed, so you know it is alive.'],
        ['USB: read every', '10 s', '2–3600 s', 'Reading interval on USB power.'],
        ['USB: heartbeat', '30 min', '5–1440 min', 'Heartbeat on USB power.'],
        ['Change threshold', '2 cm', '1–50 cm', 'Sends when the depth moved by more than this.'],
    ])
    section('Sensor (advanced)', [
        ['Warm-up', '500 ms', '100–5000 ms', 'Wait after switching the sensor head on before the first request. '
                                             'Below about 500 ms that first measurement is invalid, so the '
                                             'reading takes longer.'],
        ['Poll', '500 ms', '450–2000 ms', 'Time between requests to the head. It needs at least about 450 ms '
                                          'per measurement.'],
        ['Samples', '1', '1–15', 'Readings averaged per measurement. The head already smooths its readings.'],
    ])
    section('Device', [
        ['Setup AP password', 'uls03gw-setup', '8–63 chars or empty', 'Password of the setup network. Leaving '
                                                                      'it empty makes the network open, which '
                                                                      'is not recommended.'],
        ['Setup AP timeout', '5 min', '1–30 min', 'The setup network closes after this long without use.'],
    ])
    st.append(P('Buttons on the page', 'h2'))
    st.append(table([
        ['Button', 'What it does'],
        ['Save', 'Checks and stores the settings. Nothing is stored if any value is invalid.'],
        ['Test Wi-Fi + MQTT', 'Tries the settings on the page (saved or not): Wi-Fi, time, TLS, login, and a '
                              'test message to <font name="DV-M">&lt;base&gt;/test</font>.'],
        ['Read sensor now', 'Takes one reading and shows the result.'],
        ['Reboot', 'Restarts the sensor.'],
        ['Factory reset', 'Erases all settings (asks first).'],
    ], [35 * mm, full_w - 35 * mm]))

    # ---- MQTT / HA
    st.append(PageBreak())
    st.append(P('5. Home Assistant and MQTT', 'h1'))
    st.append(P('With discovery on, the sensor appears in Home Assistant as <b>Tank level</b> with Level, Depth, '
                'Volume, Alarm, Problem, Wi-Fi signal, Power source and Distance. It also has number boxes to '
                'change the install height, full distance, alarms, heartbeat and read interval.'))
    st.append(table([
        ['Topic', 'Content'],
        ['<font name="DV-M">&lt;base&gt;/state</font>', 'Latest reading as JSON (retained).'],
        ['<font name="DV-M">&lt;base&gt;/config</font>', 'Current tank, schedule and sensor settings (retained).'],
        ['<font name="DV-M">&lt;base&gt;/set</font>', 'Send settings to the sensor (see below).'],
        ['<font name="DV-M">&lt;base&gt;/status</font>', '<i>online</i> / <i>offline</i>, on USB power only.'],
    ], [38 * mm, full_w - 38 * mm]))
    st.append(Spacer(1, 4))
    st.append(P('Example state message', 'h2'))
    st.append(P('<font name="DV-M" size="8">{"distance_mm":655,"depth_cm":114,"level_pct":73,"alarm":"normal",'
                '"volume_l":730,"configured":true,"sensor_ok":true,"err":null,"power":"battery",'
                '"wake":"timer","rssi":-62,"reads_since_send":3,"boot_count":412,"fw":"%s"}</font>' % FW, 'note'))
    st.append(P('Changing settings over MQTT', 'h2'))
    st.append(P('On battery the sensor is only online for a few seconds at a time. Publish your change as a '
                '<b>retained</b> message to <font name="DV-M">&lt;base&gt;/set</font>; the sensor applies it at '
                'its next send and then clears it. Press the button to apply it straight away.'))
    st.append(P('<font name="DV-M" size="8">{"install_height_cm": 180, "full_distance_cm": 25, "heartbeat_h": 3}'
                '</font>', 'note'))
    st.append(P('Only tank, schedule and sensor settings can be changed this way. Wi-Fi, MQTT and the setup '
                'password can only be changed on the setup page, so a wrong message cannot lock you out. '
                '<font name="DV-M">{"cmd":"report"}</font> asks for a full report, '
                '<font name="DV-M">{"cmd":"reboot"}</font> restarts it.'))

    st.append(P('6. Power', 'h1'))
    st += bullets([
        'On battery (battery connector, switch SW2 on) it measures every 30 min with Wi-Fi off and connects '
        'only to send. It draws 52 µA asleep and 16 mA for about 1 s per reading; a send takes a few seconds.',
        'On USB power it stays connected. If you unplug USB with a battery fitted, it switches to battery mode '
        'by itself.',
        'For solar, use one 18650 with a small (1 W) panel and a charger board connected to the battery '
        'connector. Don\'t use the USB input: USB power keeps the sensor awake.',
    ])

    st.append(P('7. Troubleshooting', 'h1'))
    st.append(table([
        ['What you see', 'What to do'],
        ['Setup page does not open', 'Browse to http://192.168.4.1/ while connected to ULS-03GW-XXXXXX.'],
        ['<i>configured: false</i>, no level', 'Set install height and full distance (section 2).'],
        ['<i>distance_gt_height</i>', 'The distance is larger than the install height: check the install height '
                                     'and that the sensor points at the liquid.'],
        ['<i>sensor_timeout</i>', 'No echo: obstruction in the beam, liquid closer than 10 cm, or the sensor '
                                 'head is unplugged.'],
        ['Test: certificate rejected', 'The message says why: not signed by your CA, name/IP does not match '
                                       '(use the name in the certificate), or expired.'],
        ['Test: broker refused', 'Wrong user/password or not authorised on the broker.'],
        ['Red light flashes 3×', 'The last send or reading failed. Open setup and press Test.'],
        ['Values look stuck on battery', 'This is normal: it only sends on change or every heartbeat (6 h).'],
    ], [45 * mm, full_w - 45 * mm]))

    def footer(canvas, doc_):
        canvas.saveState()
        canvas.setFont('DV', 7.5)
        canvas.setFillColor(MUTED)
        canvas.drawString(18 * mm, 9 * mm, 'ULS-03GW clean firmware %s' % FW)
        canvas.drawRightString(A4[0] - 18 * mm, 9 * mm, 'page %d' % doc_.page)
        canvas.restoreState()

    doc.build(st, onFirstPage=footer, onLaterPages=footer)
    for p in parts:
        os.remove(p)
    print('wrote', OUT)


if __name__ == '__main__':
    build()
