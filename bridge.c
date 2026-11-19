#include <windows.h>
#include <mmsystem.h>
#include "endpoint_config.h"
#include "text_direction.h"

__declspec(dllexport)
void __cdecl configure_client(struct server_settings *settings){

  char config_path[MAX_PATH];

  /* Windows wakes the game every 15.6 ms by default, which made FPS
     jumpy. Ask for 1 ms so frames come out evenly. */
  timeBeginPeriod(1);

  apply_text_direction();

  if(!get_bridge_config_path(config_path, sizeof(config_path))){
    return;
  }

  apply_endpoint_config(settings,config_path);
}
