# Enhanced Camera Control

Makes the front camera of a 2021+ Bronco Sport with SYNC 3 behave the way its button suggests: it is on or off
because you pressed the button, at up to about 19 mph instead of 6, and an off-road mode no longer opens it for you
unless you want that.

> Status: pre-release, driven on one truck. Try it somewhere safe first.

## What it does

- **The button decides.** Nothing opens the front camera by itself, and a drive mode ending does not take it away.
  Shifting into Park closes it. After Reverse it returns to how you left it. A rear view that SYNC has lost track of
  (it restarted on the move, say), in which the module ignores the button, is ended the way SYNC itself ends one.
- **Extended speed range: up to about 19 mph.** SYNC's own front-camera speed limits (6 mph; 15 mph in off-road
  modes) are lifted. The camera module itself sends no picture above 20 mph (32 km/h), and nothing in SYNC can change
  that, so the camera closes just under it (31 km/h) and comes back by itself once the truck is slower (30 km/h), if
  it was on. A press above the limit opens nothing; Ford's "for your safety" message, which stock shows for any press
  above 6 mph, is shown only there. All of this is done in SYNC's memory only: nothing on SYNC's storage is changed
  for it, and a SYNC restart without this mod is stock. The rear camera is untouched.
- **Remembers the view.** The camera reopens in the front view you used last, normal or 180 degree.

It sends the camera module the same "show this view" / "off" requests SYNC's own screens send. It reads no module and
clears nothing.

## What you need

- A Bronco Sport with the factory front camera and SYNC 3 on build **3.4.23188** (the installer refuses any other).
- The F-Mods jailbreak with **FMods Tools 2.8 or newer**.
- A FAT32 USB stick.

## Install

1. Get the stick files: the zip from this repository's [Releases](https://github.com/Trist0ne/enhanced-camera-control/releases)
   page, or build them yourself (below). Copy `DONTINDX.MSA` and the `SyncMyMod` folder to the top of an empty
   FAT32 stick. (One mod per stick: every F-Mods mod uses the folder name `SyncMyMod`.)
2. With the truck parked and the engine running, plug the stick into SYNC and wait for "installed" on the screen.
3. Pull the stick. SYNC restarts.

To remove it, do the same with the uninstall folder (`./build.sh --uninstall`, or set `UNINSTALL=1` in
`SyncMyMod/install.conf`). SYNC's files are put back exactly as they were.

## Settings

Settings > Vehicle > Driver Assistance > Front Camera:

| Switch | On | Off |
|---|---|---|
| Enhanced Camera Control | The mod is active. | The camera is stock in every respect; the three switches below are greyed and do nothing. |
| Extended Speed Range | The front camera works up to about 19 mph. | Stock speed limits (6 mph; 15 in off-road modes). |
| G.O.A.T. Mode Auto Camera Launch | Selecting an off-road mode opens the camera by itself, as stock. | Only the button opens it. |
| Remember Last View | Reopens in the view used last. | Always opens in the normal view. |

![The switches on the Front Camera settings page (a render, not a photo)](docs/settings.png)

## Good to know

- **The camera closes at about 19 mph and comes back by itself.** Above 20 mph the camera module sends no picture,
  whatever SYNC asks; left open, SYNC would show a black screen and then "camera unavailable". So the camera is
  closed just under that speed and reopened when you are slower again, if you had it on.
- **Ford's "for your safety" message** still appears for a press above that speed, where there is nothing to show.
- **The first minute after SYNC starts.** The part that keeps an off-road mode's own opening off the screen needs 30
  to 50 seconds to find its place in SYNC's memory. A mode selected in that time can still flash the camera once.
- **The rear camera is left alone**: Reverse, and Ford's delay after it, work as stock.
- **One mod per stick.** Every F-Mods mod uses the folder name `SyncMyMod`.

## If something goes wrong

- **Switch it off:** "Enhanced Camera Control" off on the settings page makes the camera stock at once.
- **Remove it:** the uninstall stick puts Ford's files back exactly as they were.

## See also

[Front Tire Guides](https://github.com/Trist0ne/front-tire-guides): tire guides on the same camera. The two mods are
independent and share one "Front Camera" settings page.

## What it changes on SYNC

Everything below is removed by the uninstaller, and Ford's files are restored byte for byte.

| What | Where |
|---|---|
| The programs | `/fs/images/fmods_apps_data/EnhancedCameraControl` |
| Its settings and working files | `/fs/rwdata/camcontrol` |
| One line in SYNC's startup script, after the line that launches SYNC's services | `/fs/mp/scripts/startup_gf.sh` |
| Its switches on the camera settings page; a "Front Camera" entry in Driver Assistance | `RVCSettings.qml`, `DriverAssistSettingsMenu.qml` |

The "Front Camera" entry is shared with Front Tire Guides: either mod can be installed or removed without the other,
and the entry goes with the last of them.

## How it works

- **`program/latch.c`** is the rule itself, as a small state machine with no SYNC code in it: what the driver wants
  (from the button), what the camera module is showing, and when a request is needed to make the two agree.
- **`program/camctl.c`** connects it to SYNC: the camera module's reports and the button from SYNC's vehicle-data
  service, the gear from SYNC's notification service, and the requests it sends.
- **`program/rvcpatch.c`** lifts the speed limits in the running camera program's memory. It checks that the bytes it
  is about to change are exactly this build's, and stops for good if they are not. The same program, given
  `installer/popup.places`, moves the speed above which SYNC's HMI program shows Ford's safety message for a press.
- **`program/gate.c`** (inside `camctl`) makes that same camera program refuse a front view while the driver does not
  want the camera and "G.O.A.T. Mode Auto Camera Launch" is off, so a mode's own opening is never drawn. One byte in
  memory, checked the same way, put back the moment the button is pressed; `camctl -r` puts it back for good.

## Building

`./package.sh` packs the install and uninstall sticks as zip files for a release. `./build.sh [out dir]` builds the
stick folder (QNX 6.5 armle-v7, cross-compiled in a Docker image: `QNX_IMAGE`, `QNX_CC`).

No Ford software is included here.

## Support

This mod is free. If it is useful to you and you would like to say thanks, you can leave a tip at
[ko-fi.com/trist0n](https://ko-fi.com/trist0n). Entirely optional.

## Disclaimer

An independent hobby project. It is not made, endorsed or supported by Ford Motor Company or by F-Mods, and it
contains none of their software. It changes how your truck's infotainment system behaves: you install and use it at
your own risk, and it may affect your warranty. The camera is an aid; look outside before you move.

## License

MIT: see [LICENSE](LICENSE).
