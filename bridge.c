#include <windows.h>
#include <mmsystem.h>
#include "endpoint_config.h"
#include "lang_overlay.h"
#include "text_direction.h"

__declspec(dllexport)
void __cdecl configure_client(struct server_settings *settings){

  char config_path[MAX_PATH];
  char language[16];

  /* Windows wakes the game every 15.6 ms by default, which made FPS
     jumpy. Ask for 1 ms so frames come out evenly. */
  timeBeginPeriod(1);

  if(!get_bridge_config_path(config_path, sizeof(config_path))){
    apply_text_direction();
    return;
  }

  /* English is the base client; other languages live in lang/<language>/. */
  GetPrivateProfileStringA("client", "language", "en",
                           language, sizeof(language), config_path);
  lang_overlay_install(language);

  apply_text_direction();
  apply_endpoint_config(settings,config_path);
}
