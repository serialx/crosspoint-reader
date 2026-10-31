# UI states still needing fixtures

This inventory expands the gaps in `coverage.json` at P9-O2d's base,
`0d7de0d5`. “Captured” means only the named cases in `screens.json`.
The proposals below are not implemented except for Details and Frontlight.
They preserve the real activity rendering and input code; they do not qualify
network protocols, flash writes, or hardware.

## Entry and capability gates

| Activity/state family | Why the existing suite cannot reach it | Smallest proposed fixture and source contract |
| --- | --- | --- |
| Details: plain/HTML, cover, More, primary/cancel | Production callers supply content only after an online catalog response. | Implemented: construct public `DetailsSpec` and enter the real activity; `util/DetailsActivity.h:12`, `browser/OpdsBookBrowserActivity.cpp:864`. Covers text, placeholder/PNG cover and navigation, not service acquisition. |
| Frontlight: mono/warm, on/off, brightness | Emulated X3/X4 have no detected light; the global entry and slider layout check capabilities. | Implemented: local forced-include facade for the `Frontlight` HAL macro, then enter the real activity. `ActivityManager.cpp:143`, `util/FrontlightPanelActivity.cpp:284`. No SDK/production HAL changes. Touch quick tiles, warmth gestures and slide-switch variants still need their own input/capability fixture. |
| Home-button settings and alternate menu choices | X3/X4 lack a Home key. | Direct public activity entry for settings pixels; a local input capability fixture for navigation/gesture behavior. `lib/hal/HalGPIO.cpp:151`, `src/SettingsList.cpp:166`. Do not claim actual Home-key behavior from a direct entry. |
| USB Drive: waiting, connected, eject/disconnect, suspended, I/O error | X3/X4 compile without USB MSC; normal routing excludes the activity. | A separate local profile with the existing USB compile gate and an inert storage/host-state facade. `network/NetworkModeSelectionActivity.cpp:17`, `network/UsbDriveActivity.cpp:13`. Never let a fixture expose real SD blocks or USB devices. |
| BLE pairing, populated paired list, mapping/learning, connected/disconnected/error states | The emulator has no BLE peer; current cases cover only menu, scanning/no peers and empty paired list. | Script the existing radio port/status/event boundary, retaining the real BLE state machine and `BleSettingsActivity` rows. `lib/BlePageTurner/src/RadioPort.h:50`, `settings/BleSettingsActivity.cpp:99,256`. Existing host fakes are examples, not emulator proof. |

Paths under `settings/`, `reader/`, `network/`, `browser/`, or `util/` in these
tables are relative to `src/activities/`.

## Network and service responses

Wi-Fi connection gates must be paired with downstream service fixtures. Returning
“connected” alone does not produce a catalog, a valid update, or a sync response.
The pinned emulator already offers `run --wifi` with NAT, so connection fixtures
should try that existing facility first with isolated local canned services.
Explicitly record the network mode, AP data and response scripts in the capture
contract; the current suite intentionally disables APs.
Prefer the existing transport boundaries below over adding setters for private
activity states. Not every concrete class is injectable today: an env-only source
replacement or narrowly reviewed local binding is still needed.

| Activity/state family | Missing prerequisite | Proposed seam and limitations |
| --- | --- | --- |
| Wi-Fi AP rows, saved credentials, auto-connect, connected, save/forget prompts | No APs configured in current captures; scan/status/IP and saved-network data drive these branches. | First use the pinned emulator’s existing `--wifi` NAT option with synthetic saved-network files (the present harness omits it and disables APs); verify deterministic scan/status/address results before adding an env-only Arduino Wi-Fi facade; `network/WifiSelectionActivity.cpp:199,203,504,551`. Preserve real selection and persistence logic. Capture named completion states rather than waiting a guessed duration. |
| OPDS catalog lists, search, paging, auth, empty/error responses, details and download/progress/results | `CatalogActivity::checkAndConnectWifi()` gates entry; then feeds and downloads require responses. | Use `freeink::opds::OpdsTransport` via the concrete binding in `src/network/OpdsHttpTransport.h:10`, with canned feed/auth responses and deterministic `HttpDownloader` file/progress results. `CatalogActivity.cpp:52`, `browser/OpdsBookBrowserActivity.cpp:313,518`. Keep parsing and activity transitions real. |
| File transfer AP/server starting/running, address/QR, transfer progress and errors | Current transfer group captures mode selection and Join Network's Wi-Fi gate; AP and server/client results are not recorded. | Deterministic `WiFi.softAP`/address results and an inert server/client event source; `network/CrossPointWebServerActivity.cpp:238,319,343`. Direct server entry alone would still start sockets and cannot qualify real transfers. |
| Calibre starting/running, discovery/pairing/transfer and errors | Successful connection and server/client events are absent. | Same Wi-Fi gate fixture plus scripted server status at `network/CalibreConnectActivity.cpp:65,75,98,120`; retain rendering and lifecycle. Socket/protocol correctness needs separate tests. |
| Font manifest/group/family lists, installed/update rows, delete dialogs, download progress/completion/errors | Current case stops at Wi-Fi; catalog and valid font bytes are absent. | Canned `HttpDownloader::downloadToFile` response files and progress callbacks, with public valid font data and checksums; `settings/FontDownloadActivity.cpp:160,184,526,579`. Preserve manifest parsing and `FontInstaller` checks. A rendered success is not installation proof. |
| Plugin hub install/download/completion/error | Wi-Fi and a hub response/archive are absent. | Synthetic archive/status/progress through `HttpDownloader`, preserving real install orchestration; `settings/PluginHubInstallActivity.h:28`, `.cpp:39`. Installed plugin help is already covered separately. |
| OTA check, version confirmation, no-update, progress, failed/finished | Release metadata, image bytes and partition outcomes are absent. | Canned `OtaUpdater::checkForUpdate`/download service results with an inert flash sink, `src/network/OtaUpdater.h:36`. Preserve real confirmation/progress rendering. No fixture should select a real boot partition or flash a device. |
| KOReader login/register, success, user-exists and other errors | Wi-Fi and auth responses are absent. | Script the sync transport operations used by `KOReaderSyncClient`, retaining response/error mapping and activity rendering; `lib/KOReaderSync/KOReaderSyncClient.cpp:166,267`, `settings/KOReaderAuthActivity.cpp:17,40`. The original golden revision has an older client implementation, so fixtures must bind that revision's equivalent boundary too. |
| KOReader fetch/upload, remote-position confirmation, no-remote-progress, complete/failure/no-credentials | Current sync case captures only the connection gate with synthetic credentials. | Canned sync transport responses plus public book/position fixtures; `reader/KOReaderSyncActivity.h:36`, `lib/KOReaderSync/KOReaderSyncClient.cpp:282,288`. Keep position conversion and actual user decisions real; include missing credentials as a separate SD fixture. |
| Clock sync: syncing, success, no-Wi-Fi/failure | X3 reaches Wi-Fi selection, not NTP results; X4 lacks the normal RTC entry. | Fixed `halClock.syncFromNTP` result and fixed clock value plus connection fixture; `settings/ClockSyncActivity.cpp:59,67,83`. A direct X4 entry could test pixels only, not RTC support. |

## Timing, fault and combinatorial limits

| State family | Gap | Smallest proposed fixture |
| --- | --- | --- |
| Dictionary indexing/lookup busy; not-found, OOM, read/decompression/index errors | Busy paints are traversed but are not settled goldens; error popups expire. | Public absent/corrupt dictionary files for natural failures, a narrow allocation/I/O fault seam only where necessary, and capture at the existing blocking paint before continuation. `reader/DictionaryWordSelectActivity.cpp:48,72,88,119,134`. An arbitrary quiet wait can miss the popup. |
| SD firmware update success/progress/flash failures | Only invalid firmware is captured. | Public valid image plus inert `firmware_flash::flashFromSdPath` sink/progress callbacks (`src/network/FirmwareFlasher.h:50`); never real flash writes. |
| Other OOM, failed writes, corrupt cache, intermediate loading and popup-selection paths | Current assets are healthy and the suite records representative settled states. | Name a concrete operation and expected error first, then inject a bounded failure at its allocator/storage/service boundary. Do not add a global random-failure mode or call private render methods directly. |
| Crash and SD failure recovery | Existing fixtures force a reset-cause return or eject emulated media; they only prove the resulting screen route. | Separate recovery workloads with explicit reset/media history. Keep current captures labeled as route evidence. |
| Touch/remapping, languages/fonts, content sizes, other setting combinations | Finite English/button-based synthetic matrix; nonreader screens use portrait, XTC forces portrait, Sleep ignores inversion. | Add named public input/content/config cases and the matching supported board/input model. These are scope limits, not all network or hardware blockers, and no finite list proves their full Cartesian product. |

For each future fixture, record fresh harness and firmware provenance, two
independent captures, exact pixels and ordered refresh/startup records. Keep the
old recordings. A fixture must not silently make an unsupported board look
hardware-qualified.
