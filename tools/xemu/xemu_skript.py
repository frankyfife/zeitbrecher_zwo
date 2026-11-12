"""TimeSplitters 2 (Xbox, PAL) in xemu starten, per Skript steuern und Bilder aufnehmen.

Referenzbilder der Xbox-Fassung für Vergleiche mit dem PC-Port. Bedienung: tools/xemu/README.md.

Grundsätze
- Die Konfiguration des Nutzers (%APPDATA%\\xemu\\xemu\\xemu.toml) und seine Festplatte werden nie
  geändert. Das Werkzeug arbeitet in einem eigenen Ordner (Vorgabe <repo>\\game\\xemu): eigene
  xemu.toml (Fenster, Port 1 = Tastatur), Kopie der Festplatte (hdd_basis.qcow2, einmalig) und
  EEPROM, je Lauf eine frische Overlay-Datei hdd_lauf.qcow2 (qemu-img, Rückgriff auf hdd_basis).
- Eingaben per SendInput mit Scancodes (SDL liest Scancodes); xemu muss den Fokus haben. Vor
  jeder Eingabe wird geprüft, ob das xemu-Fenster vorn ist, sonst wird es nach vorn geholt; gelingt
  das nicht, wird die Eingabe ausgelassen (nie Tasten in fremde Fenster).
- Esc (echte Taste) bricht den Lauf ab. xemu wird immer beendet (QMP quit, WM_CLOSE, notfalls kill).
- Zustände (Snapshots) per QMP savevm/loadvm in der Overlay-Datei; gesicherte Zustände liegen als
  zustand_<name>.qcow2 im Arbeitsordner und werden vor dem Laden kopiert (bleiben unverändert).

Skriptdatei (eine Anweisung je Zeile, '#' Kommentar; Zeit in Sekunden seit Start bzw. seit dem
Laden des Zustands, '+x' = x Sekunden nach der vorigen Zeile):
  2.0   tippe A            Taste kurz drücken (Vorgabe 0.15 s), mehrere mit '+': tippe LS_HOCH+RT 0.5
  3.0   halte LS_HOCH      gedrückt halten          4.0 lasse LS_HOCH   (lasse alle)
  5.0   bild name          Einzelbild name.png
  5.0   serie 0.5 9.0      ab hier alle 0.5 s ein Bild bis 9.0 s (serie aus = beenden)
  6.0   speichere siberia  Zustand sichern (QMP savevm; danach zustand_siberia.qcow2)
  +0.3  warte ref/x.png 100,100,700,160 max=5 frist=30
                           warten, bis der Fensterausschnitt (Koordinaten im 1280x960-Fenster) dem
                           Vergleichsbild gleicht (mittlerer Grauwertabstand <= max); danach zählen
                           die Zeiten ab dem Treffer neu. Noch ausstehende Tastendrücke von vorher
                           verfallen beim Treffer (Loslassen nicht). Ohne Treffer: Abbruch (--weiter).
                           Vergleichsbilder anlegen mit --ausschnitt, Schwelle prüfen mit --abstand.
  10.0  ende               Lauf beenden
Zustand laden: --laden NAME (startet xemu normal, nach --lade-verzoegerung s QMP loadvm, Zeit 0 danach).
Tasten: A B X Y WEISS SCHWARZ START BACK HOCH RUNTER LINKS RECHTS (Steuerkreuz) LT RT
        LS_HOCH LS_RUNTER LS_LINKS LS_RECHTS RS_HOCH RS_RUNTER RS_LINKS RS_RECHTS LS RS (Stickknöpfe)
"""
import argparse
import ctypes
import ctypes.wintypes as wt
import heapq
import json
import os
import queue
import shutil
import socket
import subprocess
import sys
import threading
import time
import tomllib
from pathlib import Path

from PIL import Image, ImageGrab, ImageStat

REPO = Path(__file__).resolve().parents[2]
ARBEIT = REPO / "game" / "xemu"
AUSGABE = REPO / "game" / "xemu_ref"
XEMU = Path(r"F:\emulatoren\xemu-win-release\xemu.exe")
NUTZER_TOML = Path(os.environ.get("APPDATA", "")) / "xemu" / "xemu" / "xemu.toml"
QMP_PORT = 47770

# Xbox-Knopf -> (Schlüssel in [input.keyboard_controller_scancode_map], SDL-Scancode, Set-1-Scancode,
# erweitert). Die SDL-Werte sind die Vorgaben von xemu 0.8 (werden trotzdem ausdrücklich geschrieben).
TASTEN = {
    "A": ("a", 4, 0x1E, False),               # Taste A
    "B": ("b", 5, 0x30, False),               # Taste B
    "X": ("x", 27, 0x2D, False),              # Taste X
    "Y": ("y", 28, 0x15, False),              # Taste Y (US-Lage; deutsche Tastatur: Z)
    "LINKS": ("dpad_left", 80, 0x4B, True),   # Pfeil links
    "HOCH": ("dpad_up", 82, 0x48, True),      # Pfeil hoch
    "RECHTS": ("dpad_right", 79, 0x4D, True), # Pfeil rechts
    "RUNTER": ("dpad_down", 81, 0x50, True),  # Pfeil runter
    "BACK": ("back", 42, 0x0E, False),        # Rücktaste
    "START": ("start", 40, 0x1C, False),      # Eingabe
    "WEISS": ("white", 30, 0x02, False),      # 1
    "SCHWARZ": ("black", 31, 0x03, False),    # 2
    "LS": ("lstick_btn", 32, 0x04, False),    # 3
    "RS": ("rstick_btn", 33, 0x05, False),    # 4
    "GUIDE": ("guide", 34, 0x06, False),      # 5
    "LS_HOCH": ("lstick_up", 8, 0x12, False),     # E
    "LS_LINKS": ("lstick_left", 22, 0x1F, False), # S
    "LS_RECHTS": ("lstick_right", 9, 0x21, False),# F
    "LS_RUNTER": ("lstick_down", 7, 0x20, False), # D
    "LT": ("ltrigger", 26, 0x11, False),          # W
    "RS_HOCH": ("rstick_up", 12, 0x17, False),    # I
    "RS_LINKS": ("rstick_left", 13, 0x24, False), # J
    "RS_RECHTS": ("rstick_right", 15, 0x26, False),# L
    "RS_RUNTER": ("rstick_down", 14, 0x25, False),# K
    "RT": ("rtrigger", 18, 0x18, False),          # O
}
ALIAS = {"UP": "HOCH", "DOWN": "RUNTER", "LEFT": "LINKS", "RIGHT": "RECHTS", "WHITE": "WEISS",
         "BLACK": "SCHWARZ", "OK": "A", "ZURUECK": "B"}

user32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32")
kernel32 = ctypes.WinDLL("kernel32")
user32.SetProcessDPIAware()
ctypes.WinDLL("winmm").timeBeginPeriod(1)

WM_CLOSE = 0x0010
VK_ESCAPE = 0x1B
INPUT_KEYBOARD = 1
KEYEVENTF_EXTENDEDKEY = 0x1
KEYEVENTF_KEYUP = 0x2
KEYEVENTF_SCANCODE = 0x8


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD),
                ("dwExtraInfo", ctypes.c_size_t)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
                ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]


def taste(name):
    n = name.strip().upper().replace("Ä", "AE").replace("Ö", "OE").replace("Ü", "UE")
    n = ALIAS.get(n, n)
    if n not in TASTEN:
        raise ValueError(f"unbekannte Taste '{name}' (bekannt: {' '.join(TASTEN)})")
    return n


# ---------------------------------------------------------------- Einrichtung / Konfiguration

def toml_wert(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v)
    return "'" + str(v) + "'"   # Literal-String (Backslashes bleiben)


def schreibe_toml(pfad, daten):
    zeilen = []
    for abschnitt, werte in daten.items():
        zeilen.append(f"[{abschnitt}]")
        zeilen += [f"{k} = {toml_wert(v)}" for k, v in werte.items()]
        zeilen.append("")
    Path(pfad).write_text("\n".join(zeilen), encoding="utf-8")


def qemu_img(xemu):
    return Path(xemu).with_name("qemu-img.exe")


def einrichten(arbeit, nutzer_toml, neu=False):
    """Kopiert Festplatte und EEPROM des Nutzers einmalig in den Arbeitsordner."""
    arbeit.mkdir(parents=True, exist_ok=True)
    nutzer = tomllib.loads(Path(nutzer_toml).read_text(encoding="utf-8"))
    dateien = nutzer.get("sys", {}).get("files", {})
    for schluessel, ziel in (("hdd_path", "hdd_basis.qcow2"), ("eeprom_path", "eeprom.bin")):
        quelle, zielpfad = Path(dateien[schluessel]), arbeit / ziel
        if neu or not zielpfad.exists():
            print(f"kopiere {quelle} -> {zielpfad}")
            shutil.copyfile(quelle, zielpfad)
    return nutzer


def neues_overlay(xemu, arbeit, ziel):
    if ziel.exists():
        ziel.unlink()
    subprocess.run([str(qemu_img(xemu)), "create", "-q", "-f", "qcow2", "-o",
                    "backing_file=hdd_basis.qcow2,backing_fmt=qcow2", ziel.name],
                   cwd=arbeit, check=True)


def schreibe_konfiguration(arbeit, nutzer, args):
    dateien = nutzer.get("sys", {}).get("files", {})
    anzeige = nutzer.get("display", {})
    breite, hoehe = (int(x) for x in args.fenster.lower().split("x"))
    daten = {
        "general": {"show_welcome": False, "skip_boot_anim": True},
        "general.updates": {"check": False},
        "input": {"background_input_capture": False, "auto_bind": False, "allow_vibration": False},
        "input.bindings": {"port1_driver": "usb-xbox-gamepad", "port1": "keyboard"},
        "input.keyboard_controller_scancode_map": {s: sdl for s, sdl, _, _ in TASTEN.values()},
        "display": {"renderer": anzeige.get("renderer", "VULKAN"), "filtering": "linear"},
        "display.quality": {"surface_scale": args.skala},
        "display.window": {"fullscreen_on_startup": False, "fullscreen_exclusive": False,
                           "startup_size": "last_used", "last_width": breite, "last_height": hoehe,
                           "vsync": True},
        "display.ui": {"show_menubar": False, "show_notifications": False, "hide_cursor": True,
                       "fit": "scale", "aspect_ratio": "native", "use_animations": False},
        # use_dsp wie in der Konfiguration des Nutzers: ohne DSP kommen 3D-Geräusche
        # (Schüsse) etwa 7 dB zu leise und mit anderem Klang (Pegelmessung 11/2026)
        "audio": {"volume_limit": 0.0 if args.stumm else 1.0, "use_dsp": True},
        "sys": {"mem_limit": nutzer.get("sys", {}).get("mem_limit", "64")},
        "sys.files": {
            "bootrom_path": dateien["bootrom_path"],
            "flashrom_path": dateien["flashrom_path"],
            "eeprom_path": str(arbeit / "eeprom.bin"),
            "hdd_path": str(arbeit / "hdd_lauf.qcow2"),
            "dvd_path": args.dvd or dateien["dvd_path"],
        },
    }
    if "vulkan" in anzeige and "preferred_physical_device" in anzeige["vulkan"]:
        daten["display.vulkan"] = {"preferred_physical_device": anzeige["vulkan"]["preferred_physical_device"]}
    pfad = arbeit / "xemu.toml"
    schreibe_toml(pfad, daten)
    return pfad


# ---------------------------------------------------------------- QMP

class Qmp:
    def __init__(self, port, frist=20.0):
        ende = time.monotonic() + frist
        while True:
            try:
                self.sock = socket.create_connection(("127.0.0.1", port), timeout=2)
                break
            except OSError:
                if time.monotonic() > ende:
                    raise
                time.sleep(0.2)
        self.sock.settimeout(120)
        self.f = self.sock.makefile("rw", encoding="utf-8", newline="\n")
        self.f.readline()
        self.cmd("qmp_capabilities")

    def cmd(self, execute, **arguments):
        msg = {"execute": execute}
        if arguments:
            msg["arguments"] = arguments
        self.f.write(json.dumps(msg) + "\n")
        self.f.flush()
        while True:
            zeile = self.f.readline()
            if not zeile:
                raise ConnectionError("QMP geschlossen")
            antwort = json.loads(zeile)
            if "return" in antwort:
                return antwort["return"]
            if "error" in antwort:
                raise RuntimeError(f"QMP {execute}: {antwort['error']}")

    def hmp(self, befehl):
        text = self.cmd("human-monitor-command", **{"command-line": befehl})
        return text.strip()

    def schliessen(self):
        try:
            self.sock.close()
        except OSError:
            pass


# ---------------------------------------------------------------- Fenster, Fokus, Eingabe, Bild

def suche_fenster(pid):
    gefunden = []
    proc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def cb(hwnd, _):
        besitzer = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(besitzer))
        if besitzer.value == pid and user32.IsWindowVisible(hwnd):
            r = wt.RECT()
            user32.GetClientRect(hwnd, ctypes.byref(r))
            if r.right > 200 and r.bottom > 200:
                gefunden.append((r.right * r.bottom, hwnd))
        return True

    user32.EnumWindows(proc(cb), 0)
    return max(gefunden)[1] if gefunden else None


def fokus(hwnd):
    if user32.GetForegroundWindow() == hwnd:
        return True
    vorn = user32.GetForegroundWindow()
    fremd = user32.GetWindowThreadProcessId(vorn, None)
    eigen = kernel32.GetCurrentThreadId()
    if user32.IsIconic(hwnd):
        user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.AttachThreadInput(eigen, fremd, True)
    user32.BringWindowToTop(hwnd)
    user32.SetForegroundWindow(hwnd)
    user32.AttachThreadInput(eigen, fremd, False)
    time.sleep(0.05)
    return user32.GetForegroundWindow() == hwnd


def sende(namen, los):
    n = len(namen)
    feld = (INPUT * n)()
    for i, name in enumerate(namen):
        _, _, scan, erw = TASTEN[name]
        flags = KEYEVENTF_SCANCODE | (KEYEVENTF_EXTENDEDKEY if erw else 0) | (KEYEVENTF_KEYUP if los else 0)
        feld[i].type = INPUT_KEYBOARD
        feld[i].ki = KEYBDINPUT(0, scan, flags, 0, 0)
    user32.SendInput(n, feld, ctypes.sizeof(INPUT))


def print_window(hwnd):
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    w, h = r.right, r.bottom
    hdc = user32.GetDC(hwnd)
    mdc = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    alt = gdi32.SelectObject(mdc, bmp)
    user32.PrintWindow(hwnd, mdc, 0x3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
    bih = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mdc, bmp, 0, h, buf, ctypes.byref(bih), 0)
    gdi32.SelectObject(mdc, alt)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mdc)
    user32.ReleaseDC(hwnd, hdc)
    return Image.frombuffer("RGB", (w, h), buf, "raw", "BGRX", 0, 1)


def bildschirm(hwnd):
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    pt = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(pt))
    return ImageGrab.grab(bbox=(pt.x, pt.y, pt.x + r.right, pt.y + r.bottom), all_screens=True)


def schwarz(img):
    return max(ImageStat.Stat(img.convert("L")).extrema[0]) < 8


def bereich_lesen(text):
    return tuple(int(v) for v in text.split(","))


def vergleichsbild(img, bereich=None):
    """Ausschnitt (Koordinaten im 1280x960-Fenster, wird auf die echte Größe umgerechnet) als kleines Graubild."""
    if bereich:
        sx, sy = img.width / 1280, img.height / 960
        x0, y0, x1, y1 = bereich
        img = img.crop((round(x0 * sx), round(y0 * sy), round(x1 * sx), round(y1 * sy)))
    b = min(160, img.width)
    return img.convert("L").resize((b, max(1, round(img.height * b / img.width))), Image.BILINEAR)


def abstand(a, b):
    """mittlerer Grauwertabstand 0..255 zweier Vergleichsbilder"""
    if a.size != b.size:
        a = a.resize(b.size, Image.BILINEAR)
    pa, pb = a.tobytes(), b.tobytes()
    return sum(abs(x - y) for x, y in zip(pa, pb)) / len(pa)


# ---------------------------------------------------------------- Skript

def lies_skript(text):
    """-> Liste von Abschnitten; jeder Abschnitt ist eine Liste (zeit, rang, befehl, argumente).

    Ein 'warte' beendet seinen Abschnitt; die Zeiten des nächsten Abschnitts zählen ab dem Treffer.
    'tippe' wird zu halte/lasse aufgelöst.
    """
    abschnitte, aktuell, vorher = [], [], 0.0
    for nr, zeile in enumerate(text.splitlines(), 1):
        zeile = zeile.split("#", 1)[0].strip()
        if not zeile:
            continue
        teile = zeile.split()
        if len(teile) < 2:
            raise ValueError(f"Zeile {nr}: '{zeile}' (erwartet: zeit befehl ...)")
        z = teile[0]
        t = vorher + float(z[1:]) if z.startswith("+") else float(z)
        vorher = t
        befehl, rest = teile[1].lower(), teile[2:]
        if befehl in ("tippe", "halte", "lasse"):
            if not rest:
                raise ValueError(f"Zeile {nr}: Taste fehlt")
            if not (befehl == "lasse" and rest[0].lower() == "alle"):
                rest[0] = "+".join(taste(n) for n in rest[0].split("+"))
        elif befehl not in ("bild", "serie", "speichere", "ende", "fokus", "warte"):
            raise ValueError(f"Zeile {nr}: unbekannter Befehl '{befehl}'")
        if befehl == "tippe":
            dauer = float(rest[1]) if len(rest) > 1 else 0.15
            aktuell.append((t, 2, "halte", [rest[0]]))
            aktuell.append((t + dauer, 0, "lasse", [rest[0]]))
        else:
            rang = {"lasse": 0, "bild": 1, "serie": 1, "speichere": 3, "warte": 8, "ende": 9}.get(befehl, 2)
            aktuell.append((t, rang, befehl, rest))
        if befehl == "warte":
            abschnitte.append(aktuell)
            aktuell, vorher = [], 0.0
    abschnitte.append(aktuell)
    return abschnitte


def eingabe_kurz(text):
    """'t:TASTE[+TASTE][:dauer],...' -> Skriptzeilen."""
    zeilen = []
    for teil in filter(None, (x.strip() for x in text.split(","))):
        f = teil.split(":")
        zeilen.append(f"{f[0]} tippe {f[1]}" + (f" {f[2]}" if len(f) > 2 else ""))
    return "\n".join(zeilen)


# ---------------------------------------------------------------- Lauf

class Lauf:
    def __init__(self, args):
        self.args = args
        self.arbeit = Path(args.arbeit)
        self.aus = Path(args.aus)
        self.aus.mkdir(parents=True, exist_ok=True)
        self.proc = self.hwnd = self.qmp = None
        self.gehalten = set()
        self.protokoll = []
        self.index = []
        self.speicher = queue.Queue()
        self.gesichert = []
        self.schreiber = threading.Thread(target=self._schreibe, daemon=True)
        self.schreiber.start()

    def log(self, t, text):
        zeile = f"{t:8.3f}\t{text}"
        self.protokoll.append(zeile)
        if not self.args.leise:
            print(zeile, flush=True)

    def _schreibe(self):
        while True:
            auftrag = self.speicher.get()
            if auftrag is None:
                return
            img, pfad = auftrag
            img.save(pfad)

    def starte(self):
        a = self.args
        xemu = Path(a.xemu)
        nutzer = einrichten(self.arbeit, a.nutzer_toml)
        lauf_hdd = self.arbeit / "hdd_lauf.qcow2"
        if a.laden:
            quelle = self.arbeit / f"zustand_{a.laden}.qcow2"
            if not quelle.exists():
                sys.exit(f"Zustand fehlt: {quelle}")
            shutil.copyfile(quelle, lauf_hdd)
        elif not a.hdd_behalten or not lauf_hdd.exists():
            neues_overlay(xemu, self.arbeit, lauf_hdd)
        toml = schreibe_konfiguration(self.arbeit, nutzer, a)
        cmd = [str(xemu), "-config_path", str(toml), "-qmp", f"tcp:127.0.0.1:{a.qmp_port},server,nowait"]
        self.xlog = open(self.aus / "xemu_ausgabe.txt", "w", encoding="utf-8", errors="replace")
        self.proc = subprocess.Popen(cmd, cwd=self.arbeit, stdout=self.xlog, stderr=subprocess.STDOUT)
        t0 = time.monotonic()
        while self.hwnd is None and time.monotonic() - t0 < 30 and self.proc.poll() is None:
            time.sleep(0.1)
            self.hwnd = suche_fenster(self.proc.pid)
        if self.hwnd is None:
            raise RuntimeError("xemu-Fenster nicht gefunden (siehe xemu_ausgabe.txt)")
        self.qmp = Qmp(a.qmp_port)
        if not fokus(self.hwnd):
            print("Warnung: xemu-Fenster konnte nicht nach vorn geholt werden", flush=True)
        if a.laden:
            # -loadvm auf der Befehlszeile scheitert: xemu steckt den Controller (usb-hub/Port 1) erst nach
            # dem Start an, der Zustand enthält ihn aber. Daher warten, dann loadvm. (Mit -S angehalten
            # gestartet stürzt xemu beim loadvm im DSP ab, daher läuft die Maschine bis dahin normal.)
            time.sleep(a.lade_verzoegerung)
            antwort = self.qmp.hmp(f"loadvm {a.laden}")
            if antwort:
                raise RuntimeError(f"loadvm {a.laden}: {antwort}")
            if not self.qmp.cmd("query-status").get("running"):
                self.qmp.cmd("cont")

    def bild(self, t, name=None):
        img = print_window(self.hwnd)
        art = "printwindow"
        if schwarz(img) and user32.GetForegroundWindow() == self.hwnd:
            img, art = bildschirm(self.hwnd), "bildschirm"
        datei = f"{name}.png" if name else f"b_{t:07.2f}s.png"
        self.speicher.put((img, self.aus / datei))
        self.index.append(f"{t:.3f}\t{datei}\t{img.width}x{img.height}\t{art}\t{'schwarz' if schwarz(img) else ''}")

    def warte(self, arg):
        """warte REF.png [x0,y0,x1,y1] [max=12] [frist=30]: bis der Fensterausschnitt dem Bild gleicht."""
        ref_pfad = Path(arg[0])
        if not ref_pfad.is_absolute():
            ref_pfad = self.arbeit / ref_pfad
        bereich, grenze, frist = None, 12.0, 30.0
        for x in arg[1:]:
            if x.startswith("max="):
                grenze = float(x[4:])
            elif x.startswith("frist="):
                frist = float(x[6:])
            else:
                bereich = bereich_lesen(x)
        ref = vergleichsbild(Image.open(ref_pfad).convert("RGB"))
        t0 = time.perf_counter()
        d = 255.0
        while time.perf_counter() - t0 < frist:
            if not self.bedienen():
                return None, d
            d = abstand(vergleichsbild(print_window(self.hwnd), bereich), ref)
            if d <= grenze:
                return True, d
            time.sleep(0.03)
        return False, d

    def tasten(self, t, namen, los):
        if not los and not fokus(self.hwnd):
            self.log(t, f"FOKUS FEHLT, ausgelassen: halte {'+'.join(namen)}")
            return
        sende(namen, los)
        (self.gehalten.difference_update if los else self.gehalten.update)(namen)

    def jetzt(self):
        return time.perf_counter() - self.t_start

    def bedienen(self):
        """fällige Ereignisse und Serienbilder abarbeiten; False bei Abbruch oder Ende"""
        if user32.GetAsyncKeyState(VK_ESCAPE) & 0x8000:
            self.log(self.jetzt(), "Abbruch (Esc)")
            return False
        if self.proc.poll() is not None:
            self.log(self.jetzt(), "xemu beendet")
            return False
        while self.heap and self.heap[0][0] <= time.perf_counter():
            soll, _, _, befehl, arg = heapq.heappop(self.heap)
            if not self.ausfuehren(soll, befehl, arg):
                return False
        if self.serie:
            schritt, bis, naechstes = self.serie
            jetzt = time.perf_counter()
            if bis is not None and naechstes > bis + 1e-6:
                self.serie = None
            elif jetzt >= naechstes:
                self.bild(naechstes - self.t_start)
                naechstes += schritt * (int((jetzt - naechstes) / schritt) + 1)
                self.serie = (schritt, bis, naechstes)
        return True

    def ausfuehren(self, soll, befehl, arg):
        a, ist = self.args, self.jetzt()
        if befehl in ("halte", "lasse"):
            namen = sorted(self.gehalten) if arg[0].lower() == "alle" else arg[0].split("+")
            if namen:
                self.tasten(ist, namen, befehl == "lasse")
            if befehl == "halte" or a.alles_protokollieren:
                self.log(ist, f"{befehl} {'+'.join(namen)}")
        elif befehl == "bild":
            self.bild(ist, arg[0] if arg else None)
        elif befehl == "serie":
            if not arg or arg[0].lower() == "aus":
                self.serie = None
            else:
                bis = self.ursprung + float(arg[1]) if len(arg) > 1 else None
                self.serie = (float(arg[0]), bis, soll)
        elif befehl == "fokus":
            self.log(ist, f"fokus {fokus(self.hwnd)}")
        elif befehl == "speichere":
            vorher = time.perf_counter()
            antwort = self.qmp.hmp(f"savevm {arg[0]}")
            dauer = time.perf_counter() - vorher
            self.gesichert.append(arg[0])
            self.log(ist, f"savevm {arg[0]} ({dauer:.2f} s) {antwort}")
            self.verschieben(dauer)  # Sicherungsdauer (Maschine angehalten) nicht mitzählen
        elif befehl == "ende":
            self.log(ist, "ende")
            return False
        return True

    def verschieben(self, dauer):
        """Pause (Maschine angehalten) aus der Zeitachse nehmen"""
        self.t_start += dauer
        self.ursprung += dauer
        self.heap = [(z + dauer, r, n, b, x) for z, r, n, b, x in self.heap]
        heapq.heapify(self.heap)
        if self.serie:
            schritt, bis, naechstes = self.serie
            self.serie = (schritt, None if bis is None else bis + dauer, naechstes + dauer)

    def lauf(self):
        a = self.args
        text = Path(a.skript).read_text(encoding="utf-8") if a.skript else ""
        if a.eingabe:
            text += "\n" + eingabe_kurz(a.eingabe)
        if a.serie:
            f = a.serie.split(":")
            text += f"\n{f[0]} serie {f[1]}" + (f" {f[2]}" if len(f) > 2 else "")
        for z in filter(None, (a.bilder or "").split(",")):
            text += f"\n{z} bild"
        if a.dauer is not None:
            text += f"\n{a.dauer} ende"
        abschnitte = lies_skript(text)
        self.heap, self.serie, nr = [], None, 0
        self.t_start = self.ursprung = time.perf_counter()
        self.log(0.0, "start" + (f" (Zustand {a.laden})" if a.laden else ""))
        for k, abschnitt in enumerate(abschnitte):
            schluss = abschnitt[-1] if abschnitt and abschnitt[-1][2] == "warte" else None
            for t, rang, befehl, arg in (abschnitt[:-1] if schluss else abschnitt):
                nr += 1
                heapq.heappush(self.heap, (self.ursprung + t, rang, nr, befehl, arg))
            if schluss:
                ziel = self.ursprung + schluss[0]
                while time.perf_counter() < ziel:
                    if not self.bedienen():
                        return
                    time.sleep(0.002)
                if not self.bedienen():
                    return
                ok, d = self.warte(schluss[3])
                if ok is None:
                    return
                ist = self.jetzt()
                self.log(ist, f"warte {schluss[3][0]}: {'gefunden' if ok else 'FRIST ABGELAUFEN'}"
                              f" (Abstand {d:.1f})")
                if not ok and not a.weiter:
                    self.bild(ist, f"fehler_warte_{Path(schluss[3][0]).stem}")
                    raise RuntimeError(f"warte {schluss[3][0]}: nicht erreicht (Abstand {d:.1f})")
                # noch ausstehende Ereignisse vor dem Treffer verfallen (außer Loslassen und Serie)
                verfallen = [e for e in self.heap if e[3] not in ("lasse", "serie")]
                if verfallen:
                    self.heap = [e for e in self.heap if e[3] in ("lasse", "serie")]
                    heapq.heapify(self.heap)
                    self.log(ist, f"{len(verfallen)} ausstehende Ereignisse verfallen")
                self.ursprung = time.perf_counter()
            elif k == len(abschnitte) - 1:
                if not any(b == "ende" for _, _, _, b, _ in self.heap):
                    letzte = max((z for z, *_ in self.heap), default=self.ursprung)
                    heapq.heappush(self.heap, (letzte + 1.0, 9, nr + 1, "ende", []))
                while self.bedienen():
                    time.sleep(0.002)

    def beenden(self):
        if self.gehalten:
            sende(sorted(self.gehalten), True)
            self.gehalten.clear()
        if self.proc and self.proc.poll() is None:
            try:
                if not self.qmp:
                    raise ConnectionError
                self.qmp.cmd("quit")
            except Exception:
                if self.hwnd:
                    user32.PostMessageW(self.hwnd, WM_CLOSE, 0, 0)
            try:
                self.proc.wait(15)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(5)
                print("xemu musste beendet werden (kill)", flush=True)
        if self.qmp:
            self.qmp.schliessen()
        self.speicher.put(None)
        self.schreiber.join()
        if hasattr(self, "xlog"):
            self.xlog.close()
        for name in self.gesichert:
            ziel = self.arbeit / f"zustand_{name}.qcow2"
            shutil.copyfile(self.arbeit / "hdd_lauf.qcow2", ziel)
            print(f"Zustand gesichert: {ziel}", flush=True)
        (self.aus / "index.tsv").write_text("sekunden\tdatei\tgroesse\tart\tnotiz\n" + "\n".join(self.index) + "\n",
                                            encoding="utf-8")
        (self.aus / "protokoll.txt").write_text("\n".join(self.protokoll) + "\n", encoding="utf-8")
        print(f"{len(self.index)} Bilder in {self.aus}", flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("skript", nargs="?", help="Skriptdatei (siehe oben)")
    ap.add_argument("--eingabe", help="Kurzform 't:TASTE[+TASTE][:dauer],...'")
    ap.add_argument("--serie", help="start:intervall[:ende] in Sekunden")
    ap.add_argument("--bilder", help="Einzelbilder zu Zeitpunkten 't1,t2,...'")
    ap.add_argument("--dauer", type=float, help="Lauf nach so vielen Sekunden beenden")
    ap.add_argument("--aus", help="Ausgabeordner (Vorgabe game/xemu_ref/<skriptname>)")
    ap.add_argument("--laden", help="Zustand laden (zustand_<name>.qcow2 im Arbeitsordner)")
    ap.add_argument("--lade-verzoegerung", type=float, default=3.0,
                    help="Sekunden nach dem Start bis loadvm (Controller muss angesteckt sein)")
    ap.add_argument("--hdd-behalten", action="store_true", help="Overlay des letzten Laufs weiterverwenden")
    ap.add_argument("--fenster", default="1280x960", help="Fenstergröße (Vorgabe 1280x960)")
    ap.add_argument("--skala", type=int, default=1, help="interne Auflösung (surface_scale, Vorgabe 1 = 640x480)")
    ap.add_argument("--ton", dest="stumm", action="store_false", help="Ton an (Vorgabe stumm)")
    ap.add_argument("--dvd", help="anderes DVD-Abbild")
    ap.add_argument("--arbeit", default=str(ARBEIT), help="Arbeitsordner (Konfiguration, HDD-Kopie)")
    ap.add_argument("--xemu", default=str(XEMU))
    ap.add_argument("--nutzer-toml", default=str(NUTZER_TOML), help="xemu.toml des Nutzers (nur gelesen)")
    ap.add_argument("--qmp-port", type=int, default=QMP_PORT)
    ap.add_argument("--einrichten", action="store_true", help="HDD/EEPROM neu kopieren und beenden")
    ap.add_argument("--alles-protokollieren", action="store_true", help="auch Loslassen protokollieren")
    ap.add_argument("--leise", action="store_true")
    ap.add_argument("--weiter", action="store_true", help="bei abgelaufener warte-Frist nicht abbrechen")
    ap.add_argument("--ausschnitt", nargs=3, metavar=("BILD", "BEREICH", "ZIEL"),
                    help="Vergleichsbild für 'warte' anlegen: Ausschnitt x0,y0,x1,y1 (1280x960) aus BILD")
    ap.add_argument("--abstand", nargs="+", metavar="DATEI",
                    help="REF.png BEREICH BILD...: Abstände ausgeben (zum Einstellen von max=)")
    args = ap.parse_args()

    if args.ausschnitt:
        bild, bereich, ziel = args.ausschnitt
        Path(ziel).parent.mkdir(parents=True, exist_ok=True)
        x0, y0, x1, y1 = bereich_lesen(bereich)
        img = Image.open(bild).convert("RGB")
        sx, sy = img.width / 1280, img.height / 960
        img.crop((round(x0 * sx), round(y0 * sy), round(x1 * sx), round(y1 * sy))).save(ziel)
        return
    if args.abstand:
        ref = vergleichsbild(Image.open(args.abstand[0]).convert("RGB"))
        bereich = bereich_lesen(args.abstand[1])
        for f in args.abstand[2:]:
            print(f"{abstand(vergleichsbild(Image.open(f).convert('RGB'), bereich), ref):6.1f}  {f}")
        return
    if args.einrichten:
        einrichten(Path(args.arbeit), args.nutzer_toml, neu=True)
        return
    if not args.aus:
        name = Path(args.skript).stem if args.skript else time.strftime("lauf_%Y%m%d_%H%M%S")
        args.aus = str(AUSGABE / name)
    laufend = subprocess.run(["tasklist", "/FI", "IMAGENAME eq xemu.exe", "/NH"], capture_output=True,
                             encoding="oem", errors="replace").stdout or ""
    if "xemu.exe" in laufend.lower():
        sys.exit("xemu läuft bereits - nie zwei gleichzeitig starten")
    lauf = Lauf(args)
    try:
        lauf.starte()
        lauf.lauf()
    finally:
        lauf.beenden()


if __name__ == "__main__":
    main()
