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
wordt opnieuw aangemaakt in een eigen nieuw venster, met dezelfde naam, kleur en
tabbladen. Chrome bewaart
nieuwe groepen automatisch, dus na zo'n heropening staat die groep twee keer in de
lijst van Chrome; de extensie-API kan opgeslagen groepen niet zelf heropenen of
verwijderen. Draait Chrome niet, dan wordt het eerst gestart; blijft de brug
daarna uit (extensie niet geladen), dan gaan de url's in een gewoon nieuw venster.

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
qdbus6 be.synvio.KRunnerProjects /Chrome OpenGroup <guid>
tail ~/.cache/krunner-projects-host.log           # log van de brug
```

Ontwikkelen zonder te installeren: `cmake -B build && cmake --build build`, dan
`QT_PLUGIN_PATH=$PWD/build/bin krunner --replace`.
