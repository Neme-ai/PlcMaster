# PlcQtDebugTool

A lightweight PLC debug tool for Siemens S7-200 SMART based on Snap7 + Qt Widgets.

## Features
- PLC connect (S7 over TCP)
- Up to 16 digital points monitoring (I/Q/M configurable)
- Single-point read/write (write allowed for Q/M only)
- Auto refresh with adjustable interval
- Light theme (Fusion + external QSS)

## Environment
- Windows
- Qt 5.12.10 (MSVC2017 x64)  *(other Qt5 versions may work)*
- Snap7 (included in `thirdparty/snap7`)

## Build
1. Install Qt 5.12.10 MSVC2017 64-bit (or compatible kit)
2. Open `PlcQtDebugTool.pro` in Qt Creator
3. Build (Debug/Release)
4. Run

The build step copies:
- `thirdparty/snap7/bin/x64/snap7.dll` -> output folder
- `style/light.qss` -> output folder

## Run
- Set PLC IP, click Connect
- Configure points (I/Q/M), enable Auto Refresh if needed
- Use Data Read/Write to operate one point per time

## License
MIT