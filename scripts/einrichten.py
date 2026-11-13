"""Richtet game/ für ts2.exe von Grund auf ein (docs/einrichten.md).

Aufruf (aus dem Repo-Ordner, nach scripts\\build.cmd):

  python -I scripts/einrichten.py --homefront "<Steam>\\steamapps\\common\\Homefront_The_Revolution"
         [--xbox "<TimeSplitters 2 (Europe) (En,Fr,De,Es,It).iso>"]
         [--privat <Ordner>]            private Eingänge (privat/redux_hd, privat/schriftzug, privat/re_out)
         [--redux <ts2_redux.pak>]      Redux-HD-Texturen aus einer vorhandenen Kopie
         [--schriftzug <PNG>]           eigener Schriftzug (sonst privat/schriftzug/…)
         [--ohne-redux] [--spiel <game-Ordner>] [--bin <Ordner mit ts2.exe/hfpak.exe>]
         [--referenz]                   zusätzlich reference/homefront (+ re/out aus --privat) für die Analyse
  python -I scripts/einrichten.py --pruefen [--spiel <game-Ordner>]   nur vergleichen

Schritte, in dieser Reihenfolge (jeder ist wiederholbar):

 1. Homefront prüfen (Steam-Build, SHA-256 von EXE und Paks)
 2. ts2.exe --einrichten: game/ts2.bin und game/ts2/ (1,7 GB)
 3. TS2-Redux-HD-Texturen nach game/hd: aus --privat (redux_hd/), --redux
    oder aus der festen Quelle (REDUX_URL, Commit und SHA-256 unten)
 4. eigene HD-Texturen (tools/hd/make_hd.py, Auswahl EIGENE_HD installiert),
    Schriften (tools/hd/make_fonts.py), Logo-Export (tools/hd/logo.py)
 5. Schriftzug des Nutzers (tools/hd/logo.py --installieren …), wenn vorhanden
 6. aus dem Xbox-Image: game/sprache/*.bin (tools/re/xbox_text.py) und
    game/xbox_alpha.bin (tools/re/xbox_alpha.py), wenn angegeben
 7. Vergleich aller Ergebnisse mit scripts/pruefsummen.tsv

Erwartete Prüfsummen (Eingänge und Ergebnisse) stehen in scripts/pruefsummen.tsv.
Alles unter game/ (und reference/, re/out) sind Spieldaten: nie committen.
"""
import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOLL_DATEI = os.path.join(REPO, 'scripts', 'pruefsummen.tsv')
TOOLS = os.path.join(REPO, 'tools')

STEAM_APP = '223100'
STEAM_BUILD = '1733788'
# TS2 Redux (github.com/HFTSRedux/TS2Redux), Datei Install/Texture-Mods/ts2_redux.pak
# auf Commit 015685e9 (2023-09-07, letzte Änderung der Datei in 60036e29, 2021-11-05)
REDUX_URL = ('https://github.com/HFTSRedux/TS2Redux/raw/015685e95cfa2d360d8630bd1e5e1b340c961c40/'
             'Install/Texture-Mods/ts2_redux.pak')
# eigene HD-Texturen, die game/hd bekommt (die übrigen aus make_hd.py bleiben in hd_eigen)
EIGENE_HD = ['crosshair_*', 'arc_*', 'ts2banner1b', 'ts2smallbanner*', 'ts2loading', 'gmod_escort']
# Schriftzug des Nutzers (1500x527 RGBA) wie in docs/hd-texturen.md
SCHRIFTZUG = ['--lage', '112,40,1772,390', '--relief', '0.6', '--menueschein']
SCHRIFTZUG_NAME = 'neu_schriftzug_vorlage.png'
# Teilkopie von Homefront für die Analyse (tools/re, Ghidra), relativ zur Installation;
# dazu Bin64/steam_appid.txt mit "223100" (am Entwicklungsrechner außerdem ein Game.log)
REFERENZ = (
    [f'Bin64/{n}' for n in (
        'CUESDK.x64_2013.dll', 'Homefront2_Release.exe', 'Txaa.win64.dll', 'bink2w64.dll', 'd3dcompiler_47.dll',
        'd3dx11_42.dll', 'fmod_event64.dll', 'fmodex64.dll', 'libcurl.dll', 'libeay32.dll', 'msvcp100.dll',
        'msvcp120.dll', 'msvcr100.dll', 'msvcr120.dll', 'nvDXTLibrary.dll', 'ssleay32.dll', 'steam_api64.dll',
        'zlibwapi.dll')]
    + ['cdr.pak', 'system.cfg']
    + [f'engine/{n}.pak' for n in ('ShaderCache', 'ShaderCacheStartup', 'ShadersBin', 'ShadersSerialise', 'engine')]
    + [f'gamehf2/{n}.pak' for n in (
        '_fastload/engineboot', '_fastload/frontend', '_fastload/intromovies', 'bundle', 'chunkmap', 'core_patch',
        'gamedata', 'levels/philly/level', 'levels/philly/levelmm', 'lsao_cached', 'prefabs', 'scripts',
        'texturesui')]
    + [f'localization/{n}_xml.pak' for n in (
        'brazilian_portuguese', 'czech', 'english', 'french', 'german', 'italian', 'japanese', 'mexican_spanish',
        'polish', 'russian', 'spanish')])

fehler = []


def meldung(text):
    print(text, flush=True)


def schritt(text):
    meldung('\n== ' + text)


def sha256(pfad):
    h = hashlib.sha256()
    with open(pfad, 'rb') as f:
        for b in iter(lambda: f.read(1 << 22), b''):
            h.update(b)
    return h.hexdigest()


def ordner_summe(wurzel, praefix):
    """Gesamtprüfsumme eines Ordners: SHA-256 über die sortierten Zeilen
    "<praefix><pfad>\\t<größe>\\t<sha256>\\n" (Pfade mit /). Liefert (Anzahl, Summe)."""
    zeilen = []
    for w, _, dateien in os.walk(wurzel):
        for d in dateien:
            p = os.path.join(w, d)
            rel = praefix + os.path.relpath(p, wurzel).replace(os.sep, '/')
            zeilen.append(f'{rel}\t{os.path.getsize(p)}\t{sha256(p)}\n')
    zeilen.sort()
    return len(zeilen), hashlib.sha256(''.join(zeilen).encode('utf-8')).hexdigest()


def soll_lesen():
    """scripts/pruefsummen.tsv: art, quelle, pfad, größe, sha256"""
    eintraege = []
    with open(SOLL_DATEI, encoding='utf-8') as f:
        for z in f:
            if z.startswith('#') or not z.strip():
                continue
            art, quelle, pfad, groesse, summe = z.rstrip('\n').split('\t')
            eintraege.append(dict(art=art, quelle=quelle, pfad=pfad, groesse=int(groesse), sha256=summe))
    return eintraege


def soll(art, quelle=None, pfad=None):
    return [e for e in SOLL if e['art'] == art and (quelle is None or e['quelle'] == quelle)
            and (pfad is None or e['pfad'] == pfad)]


def pruefe_datei(pfad, eintrag, was, pflicht=True):
    """Datei gegen einen Soll-Eintrag; Abweichung: Fehler (pflicht) oder Hinweis"""
    ist = sha256(pfad)
    if ist == eintrag['sha256']:
        meldung(f'  ok   {was}  {ist[:16]}…')
        return True
    text = f'{was}: SHA-256 {ist} statt {eintrag["sha256"]}'
    if pflicht and not ARGS.ohne_pruefung:
        fehler.append(text)
        sys.exit(f'FEHLER {text}\n(andere Fassung? Mit --ohne-pruefung trotzdem weiter, Ergebnis weicht dann ab)')
    meldung(f'  HINWEIS {text}')
    return False


def python_tool(*args, still=False):
    """Werkzeug aus tools/ mit demselben Python (-I) ausführen"""
    cmd = [sys.executable, '-I', *args]
    if still:
        r = subprocess.run(cmd, capture_output=True)
        out = r.stdout.decode('utf-8', 'replace')
        if r.returncode:
            sys.stdout.write(out + r.stderr.decode('utf-8', 'replace'))
            sys.exit(f'FEHLER: {os.path.basename(args[0])} beendet mit {r.returncode}')
        return out
    r = subprocess.run(cmd)
    if r.returncode:
        sys.exit(f'FEHLER: {os.path.basename(args[0])} beendet mit {r.returncode}')
    return ''


# ---------------------------------------------------------------- Schritte

def pakete_pruefen():
    try:
        from importlib.metadata import version
    except ImportError:
        return
    with open(os.path.join(REPO, 'requirements.txt'), encoding='utf-8') as f:
        for z in f:
            m = re.match(r'([A-Za-z0-9_.-]+)==(\S+)', z.strip())
            if not m:
                continue
            try:
                ist = version(m.group(1))
            except Exception:  # noqa: BLE001
                sys.exit(f'FEHLER: Python-Paket {m.group(1)} fehlt (python -m pip install -r requirements.txt)')
            if ist != m.group(2):
                meldung(f'  HINWEIS: {m.group(1)} {ist} statt {m.group(2)} (Ergebnisse können abweichen)')
    if sys.version_info[:2] != (3, 14):
        meldung(f'  HINWEIS: Python {sys.version.split()[0]} statt 3.14')


def homefront_pruefen(hf):
    schritt('1. Homefront: ' + hf)
    exe = os.path.join(hf, 'Bin64', 'Homefront2_Release.exe')
    if not os.path.exists(exe):
        sys.exit(f'FEHLER: keine Bin64\\Homefront2_Release.exe in {hf}')
    acf = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(hf))), f'appmanifest_{STEAM_APP}.acf')
    if os.path.exists(acf):
        m = re.search(r'"buildid"\s+"(\d+)"', open(acf, encoding='utf-8', errors='replace').read())
        b = m.group(1) if m else '?'
        meldung(f'  {"ok  " if b == STEAM_BUILD else "HINWEIS"} Steam-App {STEAM_APP}, Build {b}'
                f'{"" if b == STEAM_BUILD else " (geprüft: " + STEAM_BUILD + ")"}')
    else:
        meldung(f'  (kein {os.path.basename(acf)} gefunden, Build-ID nicht geprüft)')
    meldung('  prüfe EXE und Paks (2,1 GB) …')
    for e in soll('eingang', 'homefront'):
        pruefe_datei(os.path.join(hf, *e['pfad'].split('/')), e, e['pfad'])


def grundlage(hf, spiel, bin_dir):
    schritt('2. ts2.bin und ts2/ (ts2.exe --einrichten)')
    e_bin = soll('ergebnis', 'homefront', 'ts2.bin')[0]
    ziel = os.path.join(spiel, 'ts2.bin')
    fertig = (os.path.exists(ziel) and sha256(ziel) == e_bin['sha256']
              and os.path.isdir(os.path.join(spiel, 'ts2', 'data')))
    if fertig:
        meldung('  schon eingerichtet')
    else:
        ts2 = os.path.join(bin_dir, 'ts2.exe')
        for n in ('ts2.exe', 'hfpak.exe'):
            if not os.path.exists(os.path.join(bin_dir, n)):
                sys.exit(f'FEHLER: {n} fehlt in {bin_dir} (zuerst scripts\\build.cmd, oder --bin angeben)')
        os.makedirs(spiel, exist_ok=True)
        meldung(f'  {ts2} --spiel {spiel} --einrichten {hf}  (einige Minuten)')
        r = subprocess.run([ts2, '--spiel', spiel, '--einrichten', hf])
        if r.returncode:
            sys.exit(f'FEHLER: ts2.exe --einrichten beendet mit {r.returncode} (siehe ts2.log neben ts2.exe)')
    pruefe_datei(ziel, e_bin, 'ts2.bin')
    e_ts2 = soll('ergebnis', 'homefront', 'ts2/')[0]
    meldung('  prüfe ts2/ (1,7 GB) …')
    n, s = ordner_summe(os.path.join(spiel, 'ts2'), 'ts2/')
    if (n, s) == (e_ts2['groesse'], e_ts2['sha256']):
        meldung(f'  ok   ts2/  {n} Dateien  {s[:16]}…')
    else:
        fehler.append(f'ts2/: {n} Dateien, Summe {s} statt {e_ts2["groesse"]} / {e_ts2["sha256"]}')
        meldung('  ABWEICHUNG ' + fehler[-1])
        meldung(f'  (abgebrochenes Entpacken? {os.path.join(spiel, "ts2")} löschen und neu starten)')


def redux_vorhanden(spiel):
    """alle Redux-Texturen liegen schon in hd/ (oder, von eigenen ersetzt, in hd_backup/)"""
    for e in soll('eingang', 'redux_hd'):
        rel = e['pfad'][len('redux_hd/'):]
        if not any(os.path.exists(p) and sha256(p) == e['sha256']
                   for p in (os.path.join(spiel, 'hd', rel), os.path.join(spiel, 'hd_backup', rel))):
            return False
    return True


def redux(spiel, privat, pak):
    schritt('3. TS2-Redux-HD-Texturen nach game/hd')
    if ARGS.ohne_redux:
        meldung('  übersprungen (--ohne-redux): game/hd bekommt nur die eigenen Texturen')
        return False
    quelle = os.path.join(privat, 'redux_hd') if privat else None
    if not pak and not (quelle and os.path.isdir(quelle)) and redux_vorhanden(spiel):
        meldung('  schon vorhanden')
        return True
    if quelle and os.path.isdir(quelle):
        meldung(f'  aus {quelle}')
        for e in soll('eingang', 'redux_hd'):
            rel = e['pfad'][len('redux_hd/'):]
            q = os.path.join(quelle, *rel.split('/'))
            if not os.path.exists(q):
                sys.exit(f'FEHLER: {q} fehlt')
            pruefe_datei(q, e, 'privat/' + e['pfad'])
        anzahl = 0
        for w, _, dateien in os.walk(quelle):
            for d in dateien:
                rel = os.path.relpath(os.path.join(w, d), quelle)
                z = os.path.join(spiel, 'hd', rel)
                os.makedirs(os.path.dirname(z), exist_ok=True)
                shutil.copy2(os.path.join(w, d), z)
                anzahl += 1
        meldung(f'  {anzahl} Texturen nach {os.path.join(spiel, "hd")}')
        return True
    e_pak = soll('eingang', 'redux', 'ts2_redux.pak')[0]
    tmp = None
    if not pak:
        tmp = tempfile.mkdtemp(prefix='ts2_redux_')
        pak = os.path.join(tmp, 'ts2_redux.pak')
        meldung(f'  lade {REDUX_URL}')
        with urllib.request.urlopen(REDUX_URL, timeout=120) as r, open(pak, 'wb') as o:
            shutil.copyfileobj(r, o)
    try:
        pruefe_datei(pak, e_pak, 'ts2_redux.pak')
        python_tool(os.path.join(TOOLS, 're', 'hd_textures.py'), pak, spiel)
    finally:
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)
    return True


def eigene_hd(spiel):
    schritt('4. eigene HD-Texturen, Schriften, Logo-Export')
    hd = os.path.join(TOOLS, 'hd')
    meldung('  make_hd.py: alle Ziele nach hd_eigen')
    python_tool(os.path.join(hd, 'make_hd.py'), '--spiel', spiel, still=True)
    meldung('  make_hd.py --installieren --nur ' + ' '.join(EIGENE_HD))
    python_tool(os.path.join(hd, 'make_hd.py'), '--spiel', spiel, '--installieren', '--nur', *EIGENE_HD)
    meldung('  make_fonts.py (etwa eine halbe Minute)')
    python_tool(os.path.join(hd, 'make_fonts.py'), '--spiel', spiel, still=True)
    python_tool(os.path.join(hd, 'make_fonts.py'), '--spiel', spiel, '--installieren')
    # Export nur einmal und vor dem Schriftzug (sonst kämen *_hd_*.png dazu)
    if not os.path.exists(os.path.join(spiel, 'hd_eigen', 'logo', 'ts2newlogo_512x256.png')):
        meldung('  logo.py --export')
        python_tool(os.path.join(hd, 'logo.py'), '--spiel', spiel, '--export', still=True)


def schriftzug(spiel, png):
    schritt('5. Schriftzug im Hauptmenü')
    if not png:
        meldung('  kein Schriftzug angegeben (--schriftzug oder --privat): Hauptmenü zeigt den Original-Schriftzug')
        return False
    if not os.path.exists(png):
        sys.exit(f'FEHLER: {png} fehlt')
    pruefe_datei(png, soll('eingang', 'schriftzug')[0], os.path.basename(png), pflicht=False)
    ziel = os.path.join(spiel, 'hd_eigen', 'logo', SCHRIFTZUG_NAME)
    os.makedirs(os.path.dirname(ziel), exist_ok=True)
    if os.path.abspath(png) != os.path.abspath(ziel):
        shutil.copy2(png, ziel)
    python_tool(os.path.join(TOOLS, 'hd', 'logo.py'), '--spiel', spiel, '--installieren', ziel, *SCHRIFTZUG)
    return True


def xbox_inhalt(iso):
    """Gesamtprüfsumme aller Dateien im Xbox-Image (unabhängig von XISO/Redump/gekürzt)"""
    sys.path.insert(0, os.path.join(TOOLS, 're'))
    import struct
    import xiso
    f, base = xiso.open_image(iso)
    f.seek(base + 32 * xiso.SECTOR + 20)
    root_sec, root_size = struct.unpack('<II', f.read(8))
    zeilen = []
    for p, sec, sz in xiso.walk_tree(f, base, root_sec, root_size):
        f.seek(base + sec * xiso.SECTOR)
        h = hashlib.sha256()
        rest = sz
        while rest:
            b = f.read(min(rest, 1 << 22))
            if not b:
                break
            h.update(b)
            rest -= len(b)
        zeilen.append((p.lower(), f'{p}\t{sz}\t{h.hexdigest()}\n'))
    zeilen.sort()
    return len(zeilen), hashlib.sha256(''.join(z for _, z in zeilen).encode('utf-8')).hexdigest()


def xbox(spiel, iso):
    schritt('6. Xbox-Image: Sprachen und Xbox-Alpha')
    if not iso:
        meldung('  kein Xbox-Image (--xbox): es fehlen game/sprache/ (deutsche, französische, italienische,\n'
                '  spanische Texte und Untertitel; das Spiel bleibt englisch) und game/xbox_alpha.bin\n'
                '  (Xbox-Alpha für 18 Leucht-Texturen)')
        return False
    if not os.path.exists(iso):
        sys.exit(f'FEHLER: {iso} fehlt')
    meldung('  prüfe den Inhalt des Images (1,8 GB) …')
    e = soll('eingang', 'xbox', 'xbox:inhalt')[0]
    n, s = xbox_inhalt(iso)
    if (n, s) == (e['groesse'], e['sha256']):
        meldung(f'  ok   Inhalt: {n} Dateien  {s[:16]}…')
    elif ARGS.ohne_pruefung:
        meldung(f'  HINWEIS Inhalt: {n} Dateien, {s} statt {e["groesse"]} / {e["sha256"]}')
    else:
        sys.exit(f'FEHLER: Inhalt des Images: {n} Dateien, {s} statt {e["groesse"]} / {e["sha256"]}\n'
                 '(andere Fassung? Mit --ohne-pruefung trotzdem weiter)')
    python_tool(os.path.join(TOOLS, 're', 'xbox_text.py'), iso, spiel)
    out = python_tool(os.path.join(TOOLS, 're', 'xbox_alpha.py'), iso, spiel,
                      '--bin', os.path.join(spiel, 'xbox_alpha.bin'), still=True)
    meldung('  xbox_alpha.py: ' + out.splitlines()[0] if out else '')
    return True


def referenz(hf, privat):
    schritt('Analyse: reference/homefront und re/out')
    ziel = os.path.join(REPO, 'reference', 'homefront')
    for rel in REFERENZ:
        q = os.path.join(hf, *rel.split('/'))
        z = os.path.join(ziel, *rel.split('/'))
        if not os.path.exists(q):
            sys.exit(f'FEHLER: {q} fehlt')
        if not (os.path.exists(z) and os.path.getsize(z) == os.path.getsize(q)):
            os.makedirs(os.path.dirname(z), exist_ok=True)
            shutil.copy2(q, z)
    with open(os.path.join(ziel, 'Bin64', 'steam_appid.txt'), 'w', newline='\n') as f:
        f.write(STEAM_APP + '\n')
    meldung(f'  {len(REFERENZ) + 1} Dateien in {ziel}')
    pruefe_datei(os.path.join(ziel, 'Bin64', 'Homefront2_Release.exe'),
                 soll('eingang', 'homefront', 'Bin64/Homefront2_Release.exe')[0], 'reference/…/Homefront2_Release.exe')
    q = os.path.join(privat, 're_out') if privat else None
    out = os.path.join(REPO, 're', 'out')
    if q and os.path.isdir(q):
        if os.path.exists(out):
            meldung(f'  {out} gibt es schon, nicht überschrieben')
        else:
            shutil.copytree(q, out)
            meldung(f'  {q} -> {out}')
    else:
        meldung('  kein privat/re_out: re/out neu mit Ghidra erzeugen (docs/einrichten.md, "Analyse")')


def vergleich(spiel, quellen):
    schritt('7. Vergleich mit scripts/pruefsummen.tsv')
    ok = abw = fehlt = 0
    for e in soll('ergebnis'):
        if e['quelle'] not in quellen or e['pfad'] == 'ts2/':
            continue
        p = os.path.join(spiel, *e['pfad'].split('/'))
        if not os.path.exists(p):
            fehlt += 1
            fehler.append(f'fehlt: {e["pfad"]}')
            meldung(f'  FEHLT {e["pfad"]}')
        elif sha256(p) != e['sha256']:
            abw += 1
            fehler.append(f'weicht ab: {e["pfad"]}')
            meldung(f'  ABWEICHUNG {e["pfad"]}')
        else:
            ok += 1
    meldung(f'  {ok} Dateien gleich, {abw} abweichend, {fehlt} fehlen '
            f'(geprüft: {", ".join(sorted(quellen))})')


def main():
    global ARGS, SOLL
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument('--homefront', help='Homefront-Installation (Steam), z. B. ...\\common\\Homefront_The_Revolution')
    ap.add_argument('--xbox', help='eigenes Xbox-Image der PAL-Fassung (XISO oder Redump)')
    ap.add_argument('--privat', help='Ordner mit privaten Eingängen: redux_hd/, schriftzug/, re_out/')
    ap.add_argument('--redux', help='ts2_redux.pak (sonst aus --privat oder von REDUX_URL geladen)')
    ap.add_argument('--ohne-redux', action='store_true', help='keine Redux-Texturen')
    ap.add_argument('--schriftzug', help=f'eigener Schriftzug (PNG; sonst <privat>/schriftzug/{SCHRIFTZUG_NAME})')
    ap.add_argument('--spiel', default=os.path.join(REPO, 'game'), help='game-Ordner (Standard: <Repo>/game)')
    ap.add_argument('--bin', default=os.path.join(REPO, 'build', 'bin'), help='Ordner mit ts2.exe und hfpak.exe')
    ap.add_argument('--referenz', action='store_true', help='auch reference/homefront und re/out anlegen')
    ap.add_argument('--pruefen', action='store_true', help='nur den game-Ordner mit pruefsummen.tsv vergleichen')
    ap.add_argument('--ohne-pruefung', action='store_true', help='abweichende Eingänge nur melden')
    ARGS = ap.parse_args()
    SOLL = soll_lesen()
    spiel = os.path.abspath(ARGS.spiel)
    privat = os.path.abspath(ARGS.privat) if ARGS.privat else None
    if privat and not os.path.isdir(privat):
        sys.exit(f'FEHLER: {privat} ist kein Ordner')

    if ARGS.pruefen:
        quellen = {'homefront', 'eigen'}
        if os.path.exists(os.path.join(spiel, 'hd', 'ts2', 'textures', 'misc', 'gmod_dm.rgb')):
            quellen.add('redux')
        if os.path.exists(os.path.join(spiel, 'hd', 'ts2', 'textures', 'misc', 'ts2newlogo.rgb')):
            quellen.add('schriftzug')
        if os.path.exists(os.path.join(spiel, 'xbox_alpha.bin')):
            quellen.add('xbox')
        vergleich(spiel, quellen)
        e_ts2 = soll('ergebnis', 'homefront', 'ts2/')[0]
        n, s = ordner_summe(os.path.join(spiel, 'ts2'), 'ts2/')
        gleich = (n, s) == (e_ts2['groesse'], e_ts2['sha256'])
        meldung(f'  ts2/: {n} Dateien, {"gleich" if gleich else "ABWEICHUNG"}')
        sys.exit(1 if fehler or not gleich else 0)

    if not ARGS.homefront:
        ap.error('--homefront fehlt')
    hf = os.path.abspath(ARGS.homefront)
    meldung(f'Zeitbrecher Zwo: game einrichten in {spiel}')
    pakete_pruefen()
    homefront_pruefen(hf)
    grundlage(hf, spiel, os.path.abspath(ARGS.bin))
    quellen = {'homefront', 'eigen'}
    if redux(spiel, privat, ARGS.redux):
        quellen.add('redux')
    eigene_hd(spiel)
    png = ARGS.schriftzug or (os.path.join(privat, 'schriftzug', SCHRIFTZUG_NAME) if privat else None)
    if png and not ARGS.schriftzug and not os.path.exists(png):
        png = None
    if schriftzug(spiel, png):
        quellen.add('schriftzug')
    if xbox(spiel, ARGS.xbox):
        quellen.add('xbox')
    if ARGS.referenz:
        referenz(hf, privat)
    vergleich(spiel, quellen)
    fehlend = {'redux': 'Redux-HD-Texturen', 'schriftzug': 'eigener Schriftzug',
               'xbox': 'Sprachen/Untertitel und Xbox-Alpha'}
    ohne = [t for q, t in fehlend.items() if q not in quellen]
    if fehler:
        meldung(f'\nFertig mit {len(fehler)} Abweichung(en):')
        for f in fehler:
            meldung('  ' + f)
        sys.exit(1)
    meldung('\nFertig: game/ entspricht dem Stand in scripts/pruefsummen.tsv'
            + (f' (ohne: {", ".join(ohne)})' if ohne else '') + '.')


if __name__ == '__main__':
    main()
