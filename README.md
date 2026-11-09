# Asda 2 Client Config

This project solves a connection problem in our Asda 2 WCell setup. The Asda 2 client had its server address and port hardcoded in the executable, so pointing it at our WCell server meant patching the game binary.

The C DLL moves that endpoint into `client_config.ini`. During startup, the client’s assembly bridge calls the DLL’s `configure_client` function. The DLL reads the INI file beside the game executable and applies a valid IPv4 address and port to the client’s server settings. The game then connects using its existing network code.

The server this is for is [asda2-server-tahadi](https://github.com/wassim-triki/asda2-server-tahadi), our Asda 2 server built on [WCell](https://github.com/WCell/WCell), the server framework it uses.

Copy `client_config.ini.example` to `client_config.ini` beside the game executable and set the server endpoint:

```ini
[server]
host=127.0.0.1
port=5000
```

The DLL and assembly bridge must also be installed in the client for the override to work.
