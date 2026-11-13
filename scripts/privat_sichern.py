"""Sichert den vollen Stand in das PRIVATE Duplikat-Repo.

Das private Repo ist ein eigenes Git-Repository mit eigenem Git-Ordner
(`.git-privat/`, im öffentlichen Repo ignoriert) über demselben Arbeitsbaum.
Es enthält alles, was das öffentliche Repo enthält (gleiche .gitignore-Regeln),
zusätzlich den Ordner `privat/` (Redux-HD-Texturen, eigener Schriftzug,
Ghidra-Exporte; siehe docs/einrichten.md, „Privater Ordner“). Die Historien
beider Repos sind getrennt; nichts aus `privat/` kann so in das öffentliche
Repo gelangen.

Aufruf (aus dem Repo-Ordner):
  python -I scripts/privat_sichern.py --remote https://github.com/<konto>/<privates-repo>.git   (einmalig)
  python -I scripts/privat_sichern.py [-m "Nachricht"] [--ohne-push]

Große, neu erzeugbare Daten (game/, reference/, re/ghidra_project/) kommen
nicht hinein: Sie entstehen mit scripts/einrichten.py aus der
Homefront-Installation und dem Xbox-Image neu.
"""
import argparse
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GITDIR = os.path.join(REPO, '.git-privat')
PRIVAT = 'privat'


def git(*args, check=True, capture=False):
    cmd = ['git', '--git-dir=' + GITDIR, '--work-tree=' + REPO, *args]
    r = subprocess.run(cmd, cwd=REPO, check=check, text=True,
                       capture_output=capture)
    return r.stdout.strip() if capture else r.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--remote', help='URL des privaten Repos (setzt/ändert "origin" des Duplikats)')
    ap.add_argument('-m', '--nachricht', help='Commit-Nachricht')
    ap.add_argument('--ohne-push', action='store_true', help='nur lokal sichern')
    args = ap.parse_args()

    if not os.path.isdir(os.path.join(REPO, PRIVAT)):
        sys.exit(f'{PRIVAT}/ fehlt im Arbeitsbaum (siehe docs/einrichten.md)')
    if not os.path.isdir(GITDIR):
        subprocess.run(['git', 'init', '--bare', '-b', 'main', GITDIR], check=True)
        git('config', 'core.bare', 'false')
        print(f'Git-Ordner des Duplikats angelegt: {GITDIR}')
    if args.remote:
        if git('remote', check=False, capture=True):
            git('remote', 'set-url', 'origin', args.remote)
        else:
            git('remote', 'add', 'origin', args.remote)

    # öffentlicher Inhalt (gleiche .gitignore) + privat/ (dort ignoriert, daher -f)
    git('add', '-A')
    git('add', '-A', '-f', '--', PRIVAT)
    if not git('status', '--porcelain', capture=True):
        print('Duplikat ist schon auf dem Stand des Arbeitsbaums.')
    else:
        stand = subprocess.run(['git', 'rev-parse', '--short', 'HEAD'], cwd=REPO,
                               text=True, capture_output=True).stdout.strip()
        msg = args.nachricht or f'Voller Stand zu öffentlichem Commit {stand}'
        git('commit', '-q', '-m', msg)
        print(git('log', '--oneline', '-1', capture=True))
    if not args.ohne_push:
        if not git('remote', check=False, capture=True):
            sys.exit('kein Remote gesetzt: einmal mit --remote <URL> aufrufen')
        git('push', '-u', 'origin', 'main')


if __name__ == '__main__':
    main()
