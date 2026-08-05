# ShortMetarForEuroScope

ShortMetar is a compact METAR plugin developed for EuroScope. It supports VATSIM and MGM RASAT sources, displays LT* aerodromes, filters by active traffic, highlights wind and QNH changes, updates automatically, and saves user settings.

## Commands

`.sm all` — **Recommended command.** Shows METARs for all LT* airports.

`.sm used` — **Recommended command.** Shows only airports with active departure or arrival traffic.

`.sm filter LTFM` — Shows a single requested airport.

`.sm filter LTFM,LTAW,LTAI` — Shows multiple requested airports.

`.sm online` — **Recommended command.** Shows active traffic in the sector your connected CID is responsible for.

`.sm online sct ANK_W78` (short form: `.sm online sct W78`) — Shows active traffic in the specified sector. Some positions may have a compact identifier with no underscore/number at all (e.g. `ANKC`).

`.sm vatsim` — Switches the data source to VATSIM.

`.sm rasat` — **Recommended command.** Switches the data source to MGM RASAT (**default**).

`.sm eng` — **Recommended command.** Switches the language to English (**default**).

`.sm tr` — Switches the language to Turkish.

`.sm refresh` — Updates METAR data immediately.

`.sm refresh airport` — Updates the traffic list immediately.

`.sm ack` — Acknowledges and clears all yellow alerts (same as the "C" button).

`.sm debug` — Toggles whether a chat message is shown on every successful RASAT/VATSIM update (default: off — the update is already visible via the yellow highlight, kept quiet to avoid spam).

`.sm save` — Saves panel position, font size, data source, and language settings to `SMconfig.json`.

`.sm reload` — Reloads settings from `SMconfig.json`.

`.sm show` or `.sm chatbox` — Reopens the hidden chatbox.

`.sm help` — Displays the command list.

## Panel Buttons

| Button | Function |
|---|---|
| **C** | Acknowledges all yellow alerts (same as `.sm ack`) |
| **-** / **+** | Decreases / increases font size |
| **^** / **v** | Collapses / expands the panel |
| Title bar | Drag to move the panel's position on screen |

## How It Works

* All LT* airport METAR data is loaded at startup.
* Data is fetched via VATSIM or MGM RASAT (default source: **RASAT**).
* Departure and arrival traffic on the VATSIM network is tracked continuously.
* QNH and wind changes are detected automatically over time; if only QNH changes, only the QNH is highlighted yellow, and if both QNH and wind change, the entire line is highlighted yellow.
* Newly detected traffic is highlighted briefly in blue.
* Filtering lets you display only the airport(s) or sector-related traffic you want.
* The `.sm online` / `.sm online sct` filter automatically determines which position is responsible for which airports using the `[POSITIONS]` and `[AIRSPACE]` data in the EuroScope sector file (`.ese`).
* The interface language can be switched between English and Turkish (default: **English**).
* Panel position, font size, data source, and language settings are stored persistently in `SMconfig.json`.

---
## How to Install
* First, download the plugin.
* After opening EuroScope, go to Settings > Plug-ins.
* Click Load and select the downloaded .dll file.
* After the plugin loads, scroll down and click the button marked in red in the image below to allow it to draw on the Standard ES Radar Screen.
* Once you close the settings window, the plugin will be ready to use.

The required steps are shown in order in the images below.

<img width="276" height="450" alt="1" src="https://github.com/user-attachments/assets/562bac05-0ab6-4a5f-89a6-8e3de70a4ae3" />
<img width="727" height="576" alt="2" src="https://github.com/user-attachments/assets/5c9b2422-ab93-4d1a-8b75-18f5944be08e" />
<img width="950" height="782" alt="3" src="https://github.com/user-attachments/assets/16f36ded-0974-4d3d-bc60-06b02f18cad7" />
<img width="937" height="765" alt="4" src="https://github.com/user-attachments/assets/992c8da0-17e7-4c27-ad72-4647ba3f2647" />
<img width="947" height="780" alt="5" src="https://github.com/user-attachments/assets/4d1c61ff-6638-4f6f-a367-39f5b5f0264a" />

made by alp-1863530

Discord: perlanmayer

---
## Changelog

### v1.1.5
* Fixed RASAT sometimes showing an outdated observation instead of the latest one.
* Fixed RASAT occasionally failing to fetch data on some networks.
* Clicking a single METAR row now acknowledges just that airport, instead of only being able to acknowledge everything at once.
* The "C" button now also clears the blue "new traffic" highlight immediately, not just yellow alerts.
* Added `.sm debug` to toggle the "RASAT/VATSIM updated" chat messages (default: off); the setting is remembered between sessions.
