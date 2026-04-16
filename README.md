# Teams Faker

<img align="left" width="64" height="64" src="small.ico" alt="Teams Faker Icon">



Teams Faker is a modern background utility built in C++ that keeps Microsoft Teams active and triggers specific keyboard shortcuts (like opening Activity via `Ctrl+1` and `Ctrl+2`) periodically, **without ever stealing your system focus** or disrupting your workflow.

## Features

- **Background Execution:** Fully operates in the background, allowing you to use other applications completely uninterrupted.
- **Deep Target Injection:** Modern Chromium WebViews (which Teams uses) usually ignore background key inputs. Teams Faker uses powerful Win32 API hacks (Spoofing `WM_ACTIVATE` and temporarily mapping internal `AttachThreadInput` keyboard states) to trick the render engine into natively registering `.Ctrl.` shortcuts while minimized.
- **Smart Target Tracking:** Employs loose window classification and process tracing to ensure the correct Teams interface is detected even when minimized or pushed to the System Tray.
- **Interactive UI & Task Sequence:** A clean native Win32 GUI provides options for variable delays (up to 100 seconds) and tracks real-time progress. When activated, it seamlessly executes: `Send Ctrl + 1` ⭢ `Wait 2 Seconds` ⭢ `Send Ctrl + 2`.
- **System Tray:** Hide the tool out of the way in your system tray and control actions (Start/Stop) right from the context menu without bringing up the GUI.

## Technical Details

Newer Electron and Edge WebView architectures natively drop generic `PostMessage` synthetic inputs because of multi-threading rendering restrictions focusing on physical hardware input states (like via `keybd_event` or `SendInput`), which ruins the "no stealing focus" requirement.

This software bypasses those architectural focus restrictions purely programmatically inside the Windows kernel memory.

## Building / Compilation

This is a native Win32 application.

1. Open the project inside Visual Studio 2022 (e.g., using `teams-faker.slnx` or `teams-faker.vcxproj`).
2. Build the project using `Release` / `x64` configurations.
3. The executable will dynamically link against `comctl32.lib` to enforce modern visual Common Controls for a sleek aesthetic. 

## Usage

1. Launch `teams-faker.exe`. 
2. Choose your repeating interval from the Dropdown Box.
3. Click **Start Action**.
4. The tool will begin automatically sending the sequence precisely to `ms-teams.exe`. Minimizing the tool will hide it to the system tray.
