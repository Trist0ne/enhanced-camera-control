# Enhanced Camera Control

Howdy all,

This mod changes when the Bronco Sport's front camera turns on and off. Stock, the camera turns itself off above
6 mph and turns itself on whenever you pick a G.O.A.T. mode. With the mod, the camera works up to about 19 mph and
only turns on when you press the camera button.

## What it does

| | Stock | With the mod |
|---|---|---|
| Driving with the front camera on | Turns off above 6 mph (15 mph in the off-road modes) | Stays on up to about 19 mph, then turns off |
| Pressing the camera button while driving | "For your safety" message above 6 mph | Works up to about 19 mph; the message only shows above that |
| Picking a G.O.A.T. mode | The front camera turns on | Nothing happens (there's a switch to keep the stock behaviour) |
| Leaving a G.O.A.T. mode | The camera turns off | The camera stays the way it was |
| Opening the camera | Always the normal view | The view you used last, normal or 180 degree split |

Shifting into Park still turns the camera off, and the rear camera works exactly as stock.

Why 19 mph and not any speed? Above 20 mph the camera itself stops sending a picture, and nothing on SYNC's side can
change that. So the mod turns the camera off just before 20, instead of leaving you looking at a black screen. It
doesn't come back by itself when you slow down; press the button when you want it again.

Everything is done in SYNC's memory while it runs and nothing on SYNC's storage is changed for it, so a restart without
the mod is completely stock.

## What you need

- A Bronco Sport with the factory front camera and SYNC 3.4. Compatible builds: **22110, 22200, 22251, 23088, 23188
  and 26057**. Your build is under Settings > General > About SYNC.
- The F-Mods jailbreak with **FMods Tools 2.8 or newer**.
- A FAT32 USB stick.

## Install

1. Download the zip from the [Releases](https://github.com/Trist0ne/enhanced-camera-control/releases) page, or build
   it yourself (see Building below). Copy `DONTINDX.MSA` and the `SyncMyMod` folder to the top of an empty FAT32
   stick. Use one stick per mod, since every F-Mods mod uses a folder called `SyncMyMod`.
2. With the truck PARKED and the engine running, plug the stick into SYNC and wait for the screen to say it's
   installed.
3. Pull the stick out. SYNC restarts.

To remove the mod, do the same with the uninstall zip and SYNC's original files go back in place. If you build it
yourself, `./build.sh --uninstall` makes the uninstall stick.

## Settings

Settings > Vehicle > Driver Assistance > Front Camera has four switches:

| Switch | On | Off |
|---|---|---|
| Enhanced Camera Control | The mod is on. | The camera works exactly as stock, and the other three switches are greyed out. |
| Extended Speed Range | The front camera works up to about 19 mph. | Stock speed limits: 6 mph, or 15 in the off-road modes. |
| G.O.A.T. Mode Auto Camera Launch | Picking an off-road mode opens the camera, as stock. | Only the button opens it. |
| Remember Last View | Opens in the front view you used last. | Always opens in the normal view. |

![The switches on the Front Camera settings page (a render, not a photo)](docs/settings.png)

## Notes

- Right after SYNC starts, the mod needs 30 to 50 seconds before it can stop an off-road mode from opening the
  camera. Pick a mode in that window and the camera may flash on once.
- If SYNC restarts while you're driving, it can lose track of the rear camera and leave it stuck on, and the module
  then ignores the front camera button. Once you're past 6 mph the mod closes the rear view the same way SYNC normally
  would. This is also why you should install with the truck parked.
- Reverse, and Ford's delay before the rear camera closes after it, work as stock.

## Compatibility

Enhanced Camera Control has run on my truck on SYNC 3.4.23188. Builds 22110, 22200, 22251, 23088 and 26057 were
emulated: Ford's own files for each build were compared with 23188's, and the speed and popup changes were run against
each build's real programs. They haven't been on a truck yet, so if you're on one of them, let me know how it goes.

Other 3.4 builds will probably work too. The installer only edits Ford files that match the stock ones it expects, and
every change made in memory checks Ford's exact code before it writes, so on a build that differs those parts simply
stop instead of changing anything. On a build that hasn't been compared yet, the "for your safety" message is left as
stock.

## If something goes wrong

Turn "Enhanced Camera Control" off on the settings page and the camera goes back to stock straight away. To take the
mod off completely, use the uninstall stick.

## See also

[Front Tire Guides](https://github.com/Trist0ne/front-tire-guides) adds tire guides to the same camera. The two mods
work independently and share the Front Camera settings page.

## How it works

- `program/latch.c` is the rule: a small state machine that tracks what the driver wants, what the camera module is
  showing, and when a request is needed to bring the two in line. It has no SYNC code in it.
- `program/camctl.c` connects the rule to SYNC. It gets the camera module's reports and the button from SYNC's
  vehicle-data service, gets the gear from SYNC's notification service, and sends the requests.
- `program/rvcpatch.c` lifts the speed limits in the memory of SYNC's running camera program. It checks that the bytes
  it's about to change are exactly the ones this SYNC build has, and stops for good if they aren't. Given
  `installer/popup.places`, the same program raises the speed at which SYNC shows Ford's safety message.
- `program/gate.c` runs inside `camctl`. While you don't want the camera and G.O.A.T. Mode Auto Camera Launch is off,
  it makes SYNC's camera program refuse front views, so an off-road mode's opening never reaches the screen. It
  changes one byte in memory, checked the same way, and puts it back the moment you press the button. `camctl -r`
  puts it back for good.

## What it changes on SYNC

The uninstaller removes all of this and puts Ford's files back exactly as they were.

| What | Where |
|---|---|
| The programs | `/fs/images/fmods_apps_data/EnhancedCameraControl` |
| Its settings and working files | `/fs/rwdata/camcontrol` |
| One line in SYNC's startup script, after the line that launches SYNC's services | `/fs/mp/scripts/startup_gf.sh` |
| Its switches on the camera settings page, and a Front Camera entry in Driver Assistance | `RVCSettings.qml`, `DriverAssistSettingsMenu.qml` |

The Front Camera entry is shared with Front Tire Guides. Either mod can be installed or removed on its own, and the
entry is removed with whichever one goes last.

## Building

`./build.sh [out dir]` builds the stick folder. The programs are cross-compiled for QNX 6.5 (armle-v7) in a Docker
image with a QNX 6.5 gcc, set by `QNX_IMAGE` and `QNX_CC`. `./package.sh` builds the install and uninstall zips for a
release. No Ford software is included.

## Support

The mod is free. If you'd like to say thanks, you can buy me a coffee at [ko-fi.com/trist0n](https://ko-fi.com/trist0n).

## Disclaimer

This is an independent hobby project. It isn't made, endorsed or supported by Ford Motor Company or F-Mods, and it
contains none of their software. It changes how your truck's infotainment system behaves, so you install and use it at
your own risk, and it may affect your warranty. The camera is an aid; always look outside before you move.

## License

MIT: see [LICENSE](LICENSE).
