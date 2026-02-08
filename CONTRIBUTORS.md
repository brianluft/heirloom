# Getting started
- Install [Git for Windows](https://gitforwindows.org/).
- Install [Visual Studio 2022 Community](https://aka.ms/vs/17/release/vs_community.exe).
    - Workloads
        - "Desktop development with C++"
    - Individual components
        - "C++ Clang Compiler for Windows"
- Set up vcpkg with `scripts/vcpkg.sh integrate install`
- Build with `scripts/build.sh` or one of the Visual Studio solutions.

# Editing dialogs
- Set the font to Segoe UI / Regular / 9pt.
- (Optional) Set the margins to 7 DLU, it comes up in tiny text when you resize the guidelines.
- Set grid spacing to 7 DLU and resize the dialog in grid mode to be an exact multiple of the grid spacing. You will have to go back and forth between "None" and "Grid" guide modes. Size controls in "None" mode, then lay out the dialog in "Grid" mode. Don't resize controls in grid mode.
- Control sizes
    - Button: 50x14 DLU
    - Textbox: 12 DLU tall
    - Checkbox: 10 DLU tall (Size to Content does it correctly)
    - Label: 8 DLU tall (Size to Content does it correctly)
    - Dialog margin: 7
- Put labels above their associated textbox (not to the left). Using the grid editor, the label will be either too close or too far from the control. Put it in the "too close" position and hit the up arrow 3 times to move it up by 3 DLU.
- To stack multiple lines of labels, use the grid editor to place them on top of each other. This will be too tight. Use the arrow key to space them out by an additional 2 arrow key steps each.
- In the .rc file, set the left/top coordinates of the dialog (the first two numbers after DIALOGEX) to 20, 20.

## File Manager
We have `res_dummy.rc` which can be opened in Visual Studio, while `res.rc` doesn't open properly.
Copy your dialog from `res.rc` into `res_dummy.rc`, edit it there, and copy it back.
After saving in Visual Studio, it will trash `resource.h`. Restore it using git.

# Making a release
- Set version in `res.rc` and `progman.rc`, look for `VERSION`. Commit as "Version X.X.X".
- Install Windows SDK in order to get `signtool`. The only necessary features are "Windows SDK Signing Tools for Desktop Apps" and "MSI Tools". https://developer.microsoft.com/en-us/windows/downloads/windows-sdk/
- Search for signtool in `C:\Program Files (x86)`. Set `$signtool` to its path.
- Find the HSM entry in 1Password. Set `$sha1` to the SHA1 hash. Keep the entry up so you can copy the password out.
- Download the artifact zip from GitHub Actions.
- Set $sha1 to the hash of the code signing certificate, then: `& $signtool sign /v /tr http://timestamp.sectigo.com /fd SHA256 /td SHA256 /sha1 $sha1 winfile.exe` (for each build). Paste the password when prompted.
- Verify digital signatures in the file properties.
- Rename Arm64 and x64 folders to `heirloom-arch-X.X.X` and zip them (from the inside, just the files).
- Create release on GitHub and drag the two zips in.

# Future Ideas
| Classic app                                | Why it still matters in 2025                                                                                                                                                                                                                                            | Who’s asking for it?                                                                                                                               |
| ------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Classic Calculator** (Win95/7 calc.exe)  | The UWP calculator drags in half of .NET and takes \~2-3 s to cold-start on a fast NVMe drive. A GDI-only calc opens in < 50 ms, lives happily on the taskbar, and still supports Hex/Dec/Bin, date arithmetic, and programmer mode.                                    | Winaero’s perennial guide “*Get the old Calculator back in Windows 10*” has millions of hits and fresh comments every year([Winaero][2])           |
| **Paintbrush / MSPaint classic**           | New Paint’s AI-infusion, ribbon chrome, and dark-theme glitches make it a slog on low-end hardware or in VM snapshots. A 16-bit-spirit remake gives designers a “scratch pad” that launches instantly and never phones home.                                            | Microsoft Community thread: “*Installing Classic Paint onto Windows 11*”([Microsoft Answers][3])                                                   |
| **WordPad** (RTF lite editor)              | Microsoft is ripping WordPad out of Windows 11 24H2. Yet users still need a free editor that opens RTF, embeds images, and prints without the heft (or cost) of Word. A lean Win32 clone fits perfectly between Notepad and full office suites.                         | “*WordPad – Windows 11 – Please add it back*” (MS Community) + press coverage of backlash([Microsoft Answers][4], [The Sun][5])                    |
| **ClipBook Viewer** (multi-clipboard)      | Windows 10/11’s clipboard history (Win + V) is unreliable, opaque, and limited to 25 items. A modern ClipBook could store unlimited clips, tag them, and optionally share pages via LAN—without cloud sync or telemetry.                                                | “*Where is “Clipboard Viewer”? I want more than a single item…*” (MS Community) explains the pain([Microsoft Answers][6])                          |
| **Cardfile** (index-card database)         | Flat-file, tag-able note apps are trending again (Obsidian, Notion). A Win32 Cardfile revival offers an ultra-lightweight, portable way to keep recipes, lab notes, or passwords in encrypted .CRD files—no Electron, no subscription.                                  | Long-running plea: “*I have been dragging along cardfile.exe since Windows 3; will it work on Windows 10?*” (MS Community)([Microsoft Answers][7]) |
| **Windows Photo Viewer (Win7)**            | The Photos UWP app routinely takes 4-5 s to open a JPEG and consumes > 400 MB RAM. A GDI-based viewer displays images instantly, respects keyboard navigation order, and never nags about “Memories” or cloud syncing.                                                  | “*Windows Photo Viewer not available in Windows 11 – how can I restore it?*” (MS Answers)([Microsoft Answers][8])                                  |

[1]: https://www.reddit.com/r/PLC/comments/kb65w8/best_terminal_program_i_miss_hyperterminal/?utm_source=chatgpt.com "Best Terminal Program? (I miss Hyperterminal) : r/PLC - Reddit"
[2]: https://winaero.com/get-calculator-from-windows-8-and-windows-7-in-windows-10/?utm_source=chatgpt.com "Old Windows 7 Calculator for Windows 10 - Winaero"
[3]: https://answers.microsoft.com/en-us/windows/forum/all/installing-classic-paint-onto-windows-11/99e7feba-9d6b-4a86-a9c2-8f996884384c?utm_source=chatgpt.com "Installing Classic Paint onto Windows 11 - Microsoft Community"
[4]: https://answers.microsoft.com/en-us/windows/forum/all/wordpad-windows-11-24h2-please-add-it-back/2a5335b3-88f7-4006-9882-164cfff91eec?utm_source=chatgpt.com "WordPad - Windows 11 - 24H2 - Please add it back"
[5]: https://www.thesun.co.uk/tech/27003825/microsoft-windows-11-wordpad-app-closing-down-removal/?utm_source=chatgpt.com "'What's next?' people yell as Microsoft reveals beloved app will disappear soon - but fans argue 'it should stay'"
[6]: https://answers.microsoft.com/en-us/windows/forum/all/where-is-clipboard-viewer/1c0c2e6c-8bc1-44f0-a26b-373534e973f6?utm_source=chatgpt.com "Where is \"Clipboard Viewer\"? - Microsoft Community"
[7]: https://answers.microsoft.com/en-us/windows/forum/all/windows-10-cardfile/2677cdae-f7c0-4baa-856e-d76cbad46295?utm_source=chatgpt.com "Windows 10 Cardfile - Microsoft Community"
[8]: https://answers.microsoft.com/en-us/windows/forum/all/windows-photo-viewer-are-not-available-in-windows/0181645b-0fbd-4095-ab79-c34117a04364?utm_source=chatgpt.com "Windows photo viewer are not available in Windows 11"
