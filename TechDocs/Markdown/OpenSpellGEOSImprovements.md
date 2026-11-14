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

Eine neue C89-Bibliothek (`openmorph.inc`, ca. 680 Zeilen) wurde in die
Spell-Bibliothek integriert. Sie akzeptiert ein unbekanntes Wort, wenn eine
begrenzte Regelsequenz eine im Wörterbuch gespeicherte Basisform
rekonstruiert. Alle Regeln prüfen Kandidaten gegen das Wörterbuch — es wird
niemals ein Wort ohne gespeicherte Basis akzeptiert.

| Regel | Typ | Beispiel |
|---|---|---|
| Aux/Modal-Verbtabelle | Suppletiv | ist → sein, kann → können, wäre → sein |
| Endungsanalyse | Flexion | behandelt → behandel + n = behandeln |
| ge-Partizip | Flexion | gezeichnet → zeichn + en = zeichnen |
| Trennbare Vorsilbe | Flexion | angezeigt → an + zeig + en = anzeigen |
| De-Umlaut | Flexion | Wände → wand (ä→a) |
| zu-Infinitiv | Flexion | aufzulösen → auflösen |
| -bar | Ableitung | einstellbar → einstell + en = einstellen |
| -ung | Ableitung | Prüfung → prüf + en = prüfen |
| -keit | Ableitung | Möglichkeit → mög + ig = möglich |
| -heit | Ableitung | Freiheit → frei |
| un- | Präfix | unlesbar → lesbar → lesen |
| Komposita (7 Fugenelemente) | Wortbildung | Arbeitsplatz → Arbeit + s + Platz |
| Komposita (direkt) | Wortbildung | Schriftgröße → Schrift + Größe |
| Bindestrich-Komposita | Wortbildung | GeoWrite-Tutorial → GeoWrite + Tutorial |

Die Regeln sind nach Sprachcode gegated: die deutschen Regeln laufen nur,
wenn der OLX1-Dateiheader `DE` enthält. Komposita-Splitting und `un-` sind
sprachneutral. Der Sprachcode wird aus dem OLX1-Dateiheader gelesen, nicht
aus der GEOS.INI.

### 2. Auxiliar- und Modalverbtabelle

Eine kompakte Tabelle mit 117 irregehulären Formen von `sein`, `haben`,
`werden`, den sechs Modalverben (`können`, `dürfen`, `mögen`, `müssen`,
`sollen`, `wollen`) und `wissen` wird vor den allgemeinen Morphologie-Regeln
geprüft. Die Tabelle umfasst Indikativ, Konjunktiv und Partizip und kostet
ca. 1.025 Bytes im Codesegment (nicht im Heap).

### 3. Komposita-Fugenelemente

Der Komposita-Splitter verwendet 7 Fugenelemente mit Kostenbewertung:

| Fugenelement | Kosten | Beispiel |
|---|---:|---|
| (keines) | 0 | Kraft + Fahrzeug |
| s | 1 | Arbeit + s + Platz |
| es | 2 | Jahr + es + Zeit |
| n | 2 | Straße + n + Bahn |
| en | 3 | Sonne + n + Schein |
| er | 3 | Kind + er + Wagen |
| e | 4 | Hund + e + Leine |

Der Linker wird nur vom linken Bestandteil entfernt; der rechte Bestandteil
wird exakt geprüft. Es werden höchstens 4 Kandidaten gesammelt und der
niedrigste Kostenwert ausgewählt.

### 4. Wörterbuch-Neuauswahl mit Morphologie-Ranking

Die Prioritätsdatei wurde neu aufgebaut. Anstatt der Längensortierung werden
nun Morphologie-Familien als Ranking-Signal verwendet. Eine Form wird als
Kern (Core) eingestuft, wenn ihre Morphologie-Familie mindestens vier
attestierte Verwandte hat (bei Nomini mindestens zwei). Die Kerne ersetzen
die bisherige Längensortierung als Fallback. Zusätzlich wurden 971
häufige deutsche Wörter aus der heise-Korpusanalyse zur Prioritätsdatei
hinzugefügt.

| Parameter | Vorher | Nachher |
|---|---|---|
| Budget | 1.000.000 Bytes | 1.500.000 Bytes |
| Datensätze | 130.396 | 162.467 |
| Wörterbuchgröße | 999.766 Bytes | 1.499.617 Bytes |
| Korpus-Trefferquote (Typen) | 6,0 % | 49,6 % |
| Korpus-Trefferquote (Token) | — | 76,4 % |

### 5. Behandlung von Sonderzeichen und Zeilenumbrüchen

| Problem | Lösung |
|---|---|
| Weicher Zeilenumbruch (0x1F, C_OPTHYPHEN) | Wird beim Wortaufbau übersprungen |
| Geschütztes Leerzeichen (0xCA, C_NONBRKSPACE) | Wird übersprungen |
| Bedingter Bindestrich (0x1E, C_NONBRKHYPHEN) | Wird übersprungen |
| Zeilenumbruch (0x0D, 0x0A) | Wird übersprungen — Wort wird verbunden |
| GEOS-Striche (0xD0–0xD5) | Werden übersprungen |
| ASCII-Bindestrich (0x2D) | Wird als Trennzeichen für Komposita behalten |

### 6. Zahlen und Symbole

Die alte `numeric`-Prüfung (nur Ziffern 0–9) wurde durch `hasAlpha` ersetzt:
Jeder Text ohne Buchstaben wird als *pre-processed* (kein Fehler) markiert.
Dadurch werden Zahlen wie `0,999` und Symbole wie `>`, `->` korrekt
behandelt. Zusätzlich wurden ASCII-Symbole (`<`, `>`, `=`, `+`, `#`, `*`,
`&`, `%`, `@`, `|`, `~`, `` ` ``) zur Randzeichensetzung hinzugefügt.

### 7. GDI-Datei und GEOS-Konventionen

Die GDI-Datei wurde an die GEOS-InfoBase-Konventionen angepasst:

- ISO-8859-1-Codierung (ß als 0xDF)
- DOS-Zeilenumbrüche (CR/LF)
- Sprachcode 6 (Deutsch), Dialektcode 128 (scharfes ß)

### 8. Sprachcode aus OLX1-Header

Der Sprachcode für die Morphologie-Regeln wird aus dem OLX1-Dateiheader
(Offset 36, ASCII-Sprach-ID wie "DE_DE") gelesen, nicht aus der GEOS.INI.
Dadurch funktionieren die deutschen Regeln zuverlässig, auch wenn die
GEOS.INI keinen `language`-Eintrag enthält.

## Messergebnisse

### Korpus-Test (heise-Magazin, 500 Artikel, 74.380 eindeutige Wörter, 707.708 Token)

| Wörterbuchgröße | Datensätze | Typen-Trefferquote | Token-Trefferquote | Exact/Folded | Morphologie |
|---:|---:|---:|---:|---:|---:|
| 500 KB | 61.535 | 35,8 % | 69,2 % | 7.874 | 18.748 |
| 750 KB | 90.074 | 44,6 % | 74,9 % | 11.175 | 22.032 |
| 1.000 KB | 117.045 | 47,2 % | 75,7 % | 12.548 | 22.540 |
| **1.500 KB** | **162.467** | **49,6 %** | **76,5 %** | **13.119** | **23.781** |
| 2.000 KB | 213.059 | 50,3 % | 76,6 % | 13.142 | 24.244 |
| 3.000 KB | 325.491 | 56,4 % | 80,2 % | 14.486 | 27.488 |

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
| Aux/Modal-Verbtabelle (Codesegment) | ca. 1.025 Bytes |
| Wörterbuchdatei auf Disk | 1.499.617 Bytes |

Die Wörterbuchdatei wird nicht in den Arbeitsspeicher geladen — der Reader
cacht ein 1.024-Byte-Leaf on demand. Der Heap-Verbrauch bleibt konstant,
unabhängig von der Wörterbuchgröße. Die Aux/Modal-Tabelle liegt als
`static const` im Codesegment und belastet den Heap nicht.

## Dokumentierte Lücken

- Starke Verben ohne adjektivische Partizipverwendung (z. B. *geschoben*
  von *schieben*) — der Ablaut-Rückschluss (o→ie) ist mehrdeutig. Die
  häufigsten Fälle werden jedoch als Kerne im Wörterbuch gespeichert.
- Nominalisierungen mit Umlaut-Plural ohne gespeicherte Singularform
  (z. B. *Wände* — *Wand* fehlt im Free German Dictionary)
- Tier-2-Ableitungen (-isch, -ig, -los, -lich) sind nicht automatisiert
  (ihre Derivate werden gespeichert, wenn sie im Wörterbuch belegt sind)
- Das Fugenelement `-ens` ist nicht implementiert (sehr selten, hohe
  False-Split-Gefahr; für v1 ausgelassen)

## Technische Details

Alle Änderungen sind in C89 (kein Heap, keine libc, keine
maschinenabhängigen Instruktionen) implementiert und laufen auf dem
286-Ziel. Die Morphologie-Regeln verwenden das bestehende `olFind`-API
des OLX1-Readers. Keine Änderung am OLX1-Dateiformat — volle Kompatibilität
mit dem Upstream-Projekt bleibt erhalten.

Compile-Time-Checks sicherstellen die GEOS-Speicherlimits:
- `OpenSpellContext <= 8192` Bytes (Heap-Budget)
- `OLContext <= 3328` Bytes (Reader-Cache)
- `olMorphAuxForms <= 2048` Bytes (Aux/Modal-Tabelle)

## Software-Engineering

- Alle Änderungen wurden in kleinen, sicher rollbaren Schritten
  entwickelt und getestet
- Host-Validierung mit C89-Compiler vor jedem GEOS-Build
- Korpus-Tests gegen 500 heise-Magazinartikel
- Git-Tags für jede Arbeitsversion (dd_mm_yy-Konvention)
- Fork unter https://github.com/hastho/OpenSpellGEOS
