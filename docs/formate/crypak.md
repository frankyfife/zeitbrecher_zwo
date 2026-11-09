# CryPak in Homefront: The Revolution

Gefunden in `Homefront2_Release.exe` (Build 781467, `HF2_Tech_PC_(781467)_04_04_all`,
2017-02-13, laut `buildinfo.txt` in `chunkmap.pak`), Adressen als VA bei
ImageBase `0x140000000`. Umgesetzt in `tools/hfpak/hfpak.cpp`. Es ist das
Verschlüsselungsschema von CRYENGINE ("stream cipher with key table"), mit
zwei Homefront-Besonderheiten: einem 12-Byte-Trailer und Brotli.

## Aufbau einer Pak

Gewöhnliches ZIP-Layout. Klar lesbar ist nur der *End of Central Directory
Record* (`PK 05 06`). Lokale Header und Daten sind verschlüsselt oder
unbrauchbar; das Spiel liest sie nicht (die Datenposition folgt aus dem
Zentralverzeichnis: Offset des lokalen Headers + 30 + Namenslänge +
Extra-Länge).

Der Kommentar des EOCD (bei allen Paks 2332 Byte) enthält nacheinander:

| Struktur | Größe | Inhalt |
| --- | --- | --- |
| CryCustomExtendedHeader | 8 | `nHeaderSize = 8`, `nEncryption = 3`, `nSigning = 1` |
| CrySignedCDRHeader | 0x84 | `nHeaderSize = 0x84`, RSA-1024-Signatur des CDR |
| CryCustomEncryptionHeader | 0x884 | `nHeaderSize = 0x884`, `CDR_IV[128]`, `keys_table[16][128]` |
| Homefront-Trailer | 12 | `'Q' 'L' 05 06`, `uint32` Offset-Verschiebung (bisher immer 0), `uint32` 0 |

Gelesen in `0x140D4DD90` (Kopfprüfung, Fehlertexte "Bad extended header",
"Bad encryption technique in header", "Bad signature header", "Bad encryption
header", "Unexpected header" für den QL-Trailer). Verschlüsselungsarten 3 und 4
werden akzeptiert, alle Paks des Spiels nutzen 3. Die Verschiebung aus dem
Trailer wird von der Größe des Archivs abgezogen und dem Datenbeginn
zugeschlagen (`+0x5C`/`+0x60` des Cache-Fabrik-Objekts).

Im Zentralverzeichnis selbst stehen die Signaturen als `PK 01 02`; die EXE
kennt außerdem `QL 01 02`. Jeder Eintrag hat einen 8-Byte-Kommentar
`'Q' 'L' 09 0B` + 4 Byte (Zweck offen).

## Schlüssel

`CDR_IV` und die 16 Schlüssel sind RSA-Blöcke (1024 Bit). Jeder Block wird
mit dem öffentlichen Schlüssel der EXE potenziert (`c^e mod n`, e = 65537)
und dann OAEP-dekodiert (SHA-256, MGF1-SHA-256, leeres Label). Ergebnis: je
16 Byte. Der öffentliche Schlüssel liegt als PKCS#1-`RSAPublicKey` in der EXE
(Muster `30 81 89 02 81 81 00`, Dateioffset 0x24D0060 in diesem Build);
hfpak sucht ihn selbst.

## Chiffre

Twofish, 128-Bit-Schlüssel, CTR-Modus von libtomcrypt mit
`CTR_COUNTER_LITTLE_ENDIAN`: Schlüsselstrom `E(IV)`, `E(IV+1)`, … wobei der
16-Byte-IV als eine Little-Endian-Zahl hochgezählt wird.
`0x140D2F4C0(buf, size, key, iv)` ist `DecryptBufferWithStreamCipher`
(`find_cipher("twofish")`, `ctr_start`, `ctr_decrypt`, `ctr_done`).

- **Zentralverzeichnis:** Schlüssel 0, IV = `CDR_IV` (Aufruf in `0x140D31CF0`).
- **Dateidaten:** verschlüsselt, wenn Bit 14 (`0x4000`) des Methodenfelds
  gesetzt ist. Schlüssel `keys[(~(crc32 >> 2)) & 0xF]`, IV aus vier
  Little-Endian-`uint32` (Aufruf in `0x140D53B32`):

  ```c
  iv[0] = usize ^ (csize << 12);
  iv[1] = (csize == 0);
  iv[2] = crc32 ^ (csize << 12);
  iv[3] = (usize == 0) ^ csize;
  ```

## Kompression

`method & 0x1F` (`ZipDir::Cache::DecompressFile`, `0x140D31920`):

| Wert | Bedeutung |
| --- | --- |
| 0 | gespeichert |
| 8 | Deflate ohne zlib-Kopf (zlib 1.2.1 in der EXE) |
| 11 | Brotli, alte Stream-API (`BrotliCreateState` `0x1417D58B0`, `BrotliDecompressStream` `0x1417D9D50`) |

Die übrigen Bits des Methodenfelds (`0x0080`…`0x3F00`) sind noch nicht
gedeutet; das Lesen braucht sie nicht. Die CRC32 im Verzeichnis gilt für die
entpackten Daten.

## Wo TS2 liegt

| Pak | TS2-Inhalt |
| --- | --- |
| `gamehf2/lsao_cached.pak` | ausschließlich `ts2/` (9665 Dateien, 1,7 GB entpackt) |
| `gamehf2/core_patch.pak` | `ts2/data/shaders/ts.pp`, `tso.pp`, `tsx.pp` (überlagern die Originale) |

Der Name `lsao_cached` ist Tarnung: die Datei hat nichts mit LSAO
(Umgebungsverdeckung) zu tun. In `ts2/` liegen die Daten in den Formaten der
Konsolenfassungen: TS2-Paks (`P4CK`) unter `ts2/data/`, Texturen als `.rgb`,
Klänge als `.wav`, Musik und Sprache als `.ogg`, Modelle als `.pob`/`.xbr`,
Animationen als `.raw`/`.war`/`.sta`/`.dsm`.

Andere Paks erwähnen "ts2" nur zufällig in Dateinamen (`…prompts2…`,
`…strikepoints2…`).
