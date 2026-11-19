# Asda 2 Client Bridge

`bridge.dll` changes how the Asda 2 client behaves from inside its own process, without editing the game files. It is built for [asda2-server-tahadi](https://github.com/wassim-triki/asda2-server-tahadi), our Asda 2 server built on [WCell](https://github.com/WCell/WCell).

The client's assembly stub loads `bridge.dll` at the start of the game and calls its `configure_client` function before any game data is read. The DLL then:

- **Server endpoint:** applies the server address and port from `bridge.ini` (the client had them hardcoded).
- **Frame pacing:** requests a 1 ms system timer so frames come out evenly.
- **Language folders:** serves files from `lang/<language>/` in place of the client's own files when they exist there, so one client can switch languages.
- **Text direction:** draws text left to right when the language's `juidata/font/FontInfo.ini` sets `TextDirection=LTR` (the client is right to left by default).

## Settings

Copy `bridge.ini.example` to `bridge.ini` beside the game executable:

```ini
[server]
host=127.0.0.1
port=5000

[client]
; en (the base files) or a folder under lang\, e.g. ar
language=en
```

## Build

32-bit Windows DLL, built with [Zig](https://ziglang.org/):

```
zig cc -target x86-windows-gnu -shared -O2 -o bridge.dll bridge.c endpoint_config.c text_direction.c lang_overlay.c -lwinmm -lws2_32
```

Copy `bridge.dll` into the client's `system` folder. The client must also have the assembly stub that loads it.
