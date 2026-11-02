# krunner-projects

KRunner-plugin voor KDE Plasma 6 dat een **project** opent: een VS Code-map (of
`.code-workspace`) samen met een **opgeslagen Chrome-tabgroep**. Een Chrome-tabgroep
kan ook los geopend of gefocust worden.

```
p vdb        → VS Code + Chrome-groep van het project "VDB Transit"
cg hels      → enkel de Chrome-tabgroep "Hels"
```

De triggerwoorden zijn instelbaar, of je zet ze uit en typt gewoon de naam.

## Hoe het werkt

```
KRunner ──► krunner_projects.so ──(D-Bus be.synvio.KRunnerProjects)──► krunner_projects_host.py ──(native messaging)──► Chrome-extensie
                 │                                                             │
                 └── python3 chromegroups.py --json  ◄─────────────────────────┘
                     (leest "Sync Data/LevelDB" van het Chrome-profiel)
```

- **`host/chromegroups.py`** leest de opgeslagen tabgroepen rechtstreeks uit het
  Chrome-profiel (LevelDB + snappy + protobuf, puur Python, geen dependencies).
  Chrome biedt daar geen API voor.
- **`extension/`** is een minimale Chrome-extensie (MV3) die via native messaging
  met de host praat en open groepen kan opsommen, focussen of een groep aanmaken.
- **`host/krunner_projects_host.py`** wordt door Chrome gestart zodra de
  extensie laadt, en registreert een D-Bus-service waar de runner tegen praat.
- **`runner/`** bevat het C++-plugin en zijn configuratiemodule (de
  "configureer"-knop in Systeeminstellingen › Zoeken › KRunner).

Een opgeslagen groep die **al open** staat wordt gefocust (venster naar voor,
groep uitgeklapt, actief tabblad geactiveerd). Een groep die **gesloten** is
wordt hersteld via Chromes lijst van onlangs gesloten vensters en tabbladen
(`chrome.sessions`): Chrome koppelt de herstelde tabbladen zelf weer aan de
bestaande opgeslagen groep en opent die in een eigen venster, zonder kopie.
Die lijst bevat de laatste 25 sluitingen en overleeft een herstart van Chrome.

Zit de groep daar niet meer in, dan maakt het plugin **niets** aan (Chrome zou
een nieuw aangemaakte groep meteen opnieuw opslaan en dan staat hij dubbel in de
lijst); je krijgt een melding en opent de groep één keer vanuit de tabgroepbalk
van Chrome, waarna KRunner hem weer kan openen. De extensie-API kan opgeslagen
groepen namelijk niet zelf opsommen, openen of verwijderen. Een kopie aanmaken
kan nog wel bewust, via D-Bus `RecreateGroup`.

Draait Chrome niet, dan wordt het eerst gestart; blijft de brug daarna uit
(extensie niet geladen), dan gaan de url's in een gewoon nieuw venster.

Een dubbele opgeslagen groep verwijder je door de open kopie in Chrome te
degroeperen (rechtsklik op de groep › *Groep opheffen*): Chrome gooit dan die
opgeslagen groep weg.

## Installatie

```bash
./install.sh
```

Dat bouwt, installeert onder `/usr` (sudo), schrijft het native-messaging-manifest
in `~/.config/google-chrome/NativeMessagingHosts/` en herstart KRunner. Daarna
eenmalig de extensie laden: `chrome://extensions` › Developer mode › *Load
unpacked* › `/usr/share/krunner-projects/extension`. De extensie-ID ligt vast
door de `key` in `manifest.json` (zie `keys/extension_id.txt`).

Benodigdheden (Ubuntu 26.04): `extra-cmake-modules libkf6runner-dev
libkf6kcmutils-dev libkf6i18n-dev libkf6config-dev qt6-base-dev python3-dbus
python3-gi`.

## Configuratie

Systeeminstellingen › Zoeken › KRunner › *Projects & Chrome groups* › configureer:

- triggerwoorden (`p`, `cg`) aan of uit, en welke;
- VS Code- en Chrome-commando, Chrome-profielmap (auto-detect);
- projecten: naam, VS Code-map of workspace, Chrome-groep (keuzelijst uit de
  opgeslagen groepen), optioneel een icoon.

Alles staat in `~/.config/krunnerrc` onder `[Runners][krunner_projects]`; projecten
in subgroepen `[Runners][krunner_projects][Projects][<naam>]`.

## Handig om te weten

```bash
python3 host/chromegroups.py                      # lijst van opgeslagen groepen
qdbus6 be.synvio.KRunnerProjects /Chrome ListGroups   # idem, met open/gesloten-status (Chrome moet draaien)
qdbus6 be.synvio.KRunnerProjects /Chrome OpenGroup <guid>     # focus / herstel
qdbus6 be.synvio.KRunnerProjects /Chrome RecreateGroup <guid> # bewust een kopie maken
python3 tests/test_host.py                        # protocoltest van de brug zonder Chrome
tail ~/.cache/krunner-projects-host.log           # log van de brug
```

Ontwikkelen zonder te installeren: `cmake -B build && cmake --build build`, dan
`QT_PLUGIN_PATH=$PWD/build/bin krunner --replace`.
