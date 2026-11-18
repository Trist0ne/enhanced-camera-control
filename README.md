# Enhanced Camera Control

Howdy all,

The front camera on the Bronco Sport shuts itself off at 6 mph and pops open whenever you pick a G.O.A.T. mode, whether
you wanted it or not. This mod puts the camera button in charge. Press it and the camera comes on, press it again and
it goes off, and it keeps working up to about 19 mph instead of 6. In sand, or anywhere the hood hides what's right in
front of you, that extra speed makes a big difference.

## What it does

- The camera only opens and closes when you press the button. A drive mode ending won't close it; shifting into Park
  will. After Reverse it goes back to however you left it.
- SYNC's front camera speed limits (6 mph, or 15 in the off-road modes) are lifted. The camera module itself stops
  sending a picture at 20 mph and nothing on SYNC's side can change that, so the mod closes the camera just under 20
  and opens it again once you slow down, if you had it on.
- Ford's "for your safety" message only shows if you press the button above that speed.
- Picking an off-road mode no longer opens the camera, unless you turn G.O.A.T. Mode Auto Camera Launch back on.
- It reopens in whichever front view you used last, normal or 180 degree split.

The changes are made in SYNC's memory while it runs and nothing on SYNC's storage is changed for them, so a restart
without the mod is completely stock. The mod sends the camera module the same requests SYNC's own camera buttons send,
and it doesn't read or clear any module. The rear camera isn't touched.

## What you need

- A Bronco Sport with the factory front camera and SYNC 3 on build **3.4.23188** (the installer refuses any other).
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
