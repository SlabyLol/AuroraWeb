# AuroraWeb

Simple **HTTP text browser** for [AuroraOS](https://github.com/TeamAuroraOS/AuroraOS) (Nintendo 3DS custom OS).

Written in C with the official AuroraOS C SDK.

> **Important limitation:** AuroraOS currently has **no TLS**.  
> Only plain `http://` works. `https://` URLs are automatically rewritten to HTTP (and will usually fail on modern sites).

## Features

- Address bar with on-screen keyboard
- Load HTTP pages and show them as readable plain text
- Title extraction
- Basic history (Back with B)
- Scroll with D-Pad / Circle Pad
- Network spinner while loading
- English UI

## Controls

| Button | Action              |
|--------|---------------------|
| **A**  | Reload current page |
| **B**  | Back in history     |
| **X**  | Edit address bar    |
| **Y**  | Go to example.com   |
| D-Pad / Circle Pad | Scroll |
| **START** | Quit             |

## Build (GitHub Actions)

Every push to `main` builds the app automatically.

1. Go to the **Actions** tab
2. Download the artifact `AuroraWeb-bin`
3. Copy `AuroraWeb.bin` to `SD:/Aurora/Apps/C/`
4. Launch it from the Aurora Home Menu (it will show **C SDK** under the name)

## Build locally

You need [devkitARM](https://devkitpro.org/wiki/Getting_Started) and Python 3.8+.

```bash
git clone https://github.com/TeamAuroraOS/AuroraOS.git
git clone https://github.com/SlabyLol/AuroraWeb.git
cd AuroraWeb
python ../AuroraOS/sdk/aurcc.py build src/main.c -o AuroraWeb.bin --icon icon.icon
```

## Notes

- The browser is intentionally simple: it strips HTML tags and shows text only.
- Large pages are truncated to stay inside the SDK HTTP limits.
- Network calls block the ARM11 core; a spinner is shown while waiting.

Made for AuroraOS Beta 6 / 7+ (C SDK 1.3).
