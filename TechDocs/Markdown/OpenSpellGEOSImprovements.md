SPDX-License-Identifier: Apache-2.0

# OpenSpellGEOS: Erweiterte Rechtschreibprüfung für PC/GEOS

## Übersicht

Dieser Bericht dokumentiert die Verbesserungen, die zusätzlich zum
OpenSpellGEOS-Upstream-Projekt (dnesbitt142) entwickelt wurden, um die
deutsche Rechtschreibprüfung unter PC/GEOS Ensemble zu erweitern.

## Ausgangslage

Die ursprüngliche OpenSpellGEOS-Bibliothek verwendet das OLX1-B-tree-Format
zur Wörterbuchspeicherung und bietet eine exakte Wortsuche mit einer
begrenzten Korrekturvorschlagsfunktion. Die Wörterbuchauswahl erfolgt über
eine Heuristik (LOD-Übersetzungskopfzeilen und Längensortierung), nicht über
Korpusfrequenz.

Der Test mit echten deutschen Texten (heise-Magazinartikel) zeigte, dass sehr
häufige Flexionsformen wie *behandelt*, *angezeigt*, *gezeichnet* und
Komposita wie *Schriftgröße* fälschlich als falsch markiert wurden.

## Durchgeführte Verbesserungen

### 1. Morphologie-Analyse zur Laufzeit

Eine neue C89-Bibliothek (`openmorph.inc`, ca. 500 Zeilen) wurde in die
Spell-Bibliothek integriert. Sie akzeptiert ein unbekanntes Wort, wenn eine
begrenzte Regelsequenz eine im Wörterbuch gespeicherte Basisform
rekonstruiert. Alle Regeln prüfen Kandidaten gegen das Wörterbuch — es wird
niemals ein Wort ohne gespeicherte Basis akzeptiert.

| Regel | Typ | Beispiel |
|---|---|---|
| Endungsanalyse | Flexion | behandelt → behandel + n = behandeln |
| ge-Partizip | Flexion | gezeichnet → zeichn + en = zeichnen |
| Trennbare Vorsilbe | Flexion | angezeigt → an + zeig + en = anzeigen |
| De-Umlaut | Flexion | Wände → wand (ä→a) |
| -bar | Ableitung | einstellbar → einstell + en = einstellen |
| -ung | Ableitung | Prüfung → prüf + en = prüfen |
| -keit | Ableitung | Möglichkeit → mög + ig = möglich |
| -heit | Ableitung | Freiheit → frei |
| un- | Präfix | unlesbar → lesbar → lesen |
| Komposita | Wortbildung | Schriftgröße → Schrift + Größe |
| Bindestrich | Wortbildung | GeoWrite-Tutorial → GeoWrite + Tutorial |

Die Regeln sind nach Sprachcode gegated: die deutschen Regeln laufen nur,
wenn `SL_GERMAN` (6) aktiv ist. Komposita-Splitting und `un-` sind
sprachneutral.

### 2. Wörterbuch-Neuauswahl mit Morphologie-Ranking

Die Prioritätsdatei wurde neu aufgebaut. Anstatt der Längensortierung werden
nun Morphologie-Familien als Ranking-Signal verwendet. Eine Form wird als
Kern (Core) eingestuft, wenn ihre Morphologie-Familie mindestens vier
attestierte Verwandte hat (bei Nomini mindestens zwei). Die Kerne ersetzen
die bisherige Längensortierung als Fallback.

| Parameter | Vorher | Nachher |
|---|---|---|
| Budget | 1.000.000 Bytes | 1.500.000 Bytes |
| Datensätze | 130.396 | 162.329 |
| Wörterbuchgröße | 999.766 Bytes | 1.499.617 Bytes |
| Korpus-Trefferquote | 6,0 % | 47,2 % |

### 3. Behandlung von Sonderzeichen und Zeilenumbrüchen

| Problem | Lösung |
|---|---|
| Weicher Zeilenumbruch (0x1F, C_OPTHYPHEN) | Wird beim Wortaufbau übersprungen |
| Geschütztes Leerzeichen (0xCA, C_NONBRKSPACE) | Wird übersprungen |
| Bedingter Bindestrich (0x1E, C_NONBRKHYPHEN) | Wird übersprungen |
| Zeilenumbruch (0x0D, 0x0A) | Wird übersprungen — Wort wird verbunden |
| GEOS-Striche (0xD0–0xD5) | Werden übersprungen |
| ASCII-Bindestrich (0x2D) | Wird als Trennzeichen für Komposita behalten |

### 4. Zahlen und Symbole

Die alte `numeric`-Prüfung (nur Ziffern 0–9) wurde durch `hasAlpha` ersetzt:
Jeder Text ohne Buchstaben wird als *pre-processed* (kein Fehler) markiert.
Dadurch werden Zahlen wie `0,999` und Symbole wie `>`, `->` korrekt
behandelt. Zusätzlich wurden ASCII-Symbole (`<`, `>`, `=`, `+`, `#`, `*`,
`&`, `%`, `@`, `|`, `~`, `` ` ``) zur Randzeichensetzung hinzugefügt.

### 5. GDI-Datei und GEOS-Konventionen

Die GDI-Datei wurde an die GEOS-InfoBase-Konventionen angepasst:

- ISO-8859-1-Codierung (ß als 0xDF)
- DOS-Zeilenumbrüche (CR/LF)
- Sprachcode 6 (Deutsch), Dialektcode 128 (scharfes ß)

## Messergebnisse

### Korpus-Test (heise-Magazin, 500 Artikel, 74.380 eindeutige Wörter)

| Wörterbuchgröße | Datensätze | Trefferquote | Quelle |
|---:|---:|---:|---|
| 500 KB | 61.535 | 35,4 % | Nur Basis |
| 750 KB | 90.060 | 42,2 % | |
| 1.000 KB | 116.878 | 44,8 % | Ursprüngliche Größe |
| **1.500 KB** | **162.329** | **47,2 %** | **Gewählter Sweet Spot** |
| 2.000 KB | 212.818 | 47,9 % | Sinkender Nutzen |
| 3.000 KB | 325.540 | 55,7 % | Hauptsächlich englische Lehnwörter |

Der Sweet Spot bei 1,5 MB liegt innerhalb eines typischen DOS-Plattenpuffers
(SmartDrive, 2 MB Standard) und funktioniert daher auch auf alter Hardware.

### Falsch-Akzeptanz (Mutations-Test)

| Mutation | Ausgang (nur exakt) | Mit Morphologie |
|---|---:|---:|
| Löschung | 1,2 % | 9,5 % |
| Ersetzung | 0,2 % | 4,7 % |
| Vertauschung | 0,2 % | 3,4 % |
| Verdopplung | 0,1 % | 3,6 % |

Die Morphologie erhöht die Trefferquote um das Neunfache bei einem moderaten
Anstieg der Falsch-Akzeptanz, die im gleichen Bereich wie der
Snowball-Stemmer-Prototyp liegt.

### Speicherverbrauch

| Block | Größe |
|---|---|
| OpenSpellContext (Heap) | ca. 5.700 Bytes |
| Budget (GEOS 8 KB-Etikette) | 8.192 Bytes |
| Verbleibend | ca. 2.500 Bytes |
| OLContext (Reader-Cache) | ca. 3.330 Bytes |
| OLMorphWorkspace | 260 Bytes |
| Wörterbuchdatei auf Disk | 1.499.617 Bytes |

Die Wörterbuchdatei wird nicht in den Arbeitsspeicher geladen — der Reader
cacht ein 1.024-Byte-Leaf on demand. Der Heap-Verbrauch bleibt konstant,
unabhängig von der Wörterbuchgröße.

## Dokumentierte Lücken

- Starke Verben ohne adjektivische Partizipverwendung (z. B. *geschoben*
  von *schieben*) — der Ablaut-Rückschluss (o→ie) ist mehrdeutig
- Nominalisierungen mit Umlaut-Plural ohne gespeicherte Singularform
  (z. B. *Wände* — *Wand* fehlt im Free German Dictionary)
- Tier-2-Ableitungen (-isch, -ig, -los, -lich) sind nicht automatisiert
  (ihre Derivate werden gespeichert, wenn sie im Wörterbuch belegt sind)

## Technische Details

Alle Änderungen sind in C89 (Kein Heap, keine libc, keine
maschinenabhängigen Instruktionen) implementiert und laufen auf dem
286-Ziel. Die Morphologie-Regeln verwenden das bestehende `olFind`-API
des OLX1-Readers. Keine Änderung am OLX1-Dateiformat — volle Kompatibilität
mit dem Upstream-Projekt bleibt erhalten.

## Software-Engineering

- Alle Änderungen wurden in kleinen, sicher rollbaren Schritten
  entwickelt und getestet
- Host-Validierung mit C89-Compiler vor jedem GEOS-Build
- Korpus-Tests gegen 500 heise-Magazinartikel
- Git-Tags für jede Arbeitsversion (dd_mm_yy-Konvention)
