#!/bin/bash

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

source $controlfolder/control.txt
[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"
get_controls

GAMEDIR=/$directory/ports/Goldeneye007
CONFDIR="$GAMEDIR/conf"

mkdir -p "$CONFDIR"
cd "$GAMEDIR"
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"
export LD_LIBRARY_PATH="$GAMEDIR/libs:$LD_LIBRARY_PATH"

export MGB64_PORTMASTER=1
export MGB64_APP_SAVEDIR="$CONFDIR"

$GPTOKEYB "ge007" &
pm_platform_helper "$GAMEDIR/ge007"

# Route saves into conf/ explicitly. This script cd's to $GAMEDIR, so without an
# explicit --savedir the eeprom/ini would land beside the binary instead of the
# persisted conf dir (AUDIT-0033). The bare engine also honors MGB64_APP_SAVEDIR
# as a fallback (main_pc.c), but passing --savedir is unambiguous and wins.
./ge007 --rom "$GAMEDIR/baserom.u.z64" --savedir "$CONFDIR"

pm_finish
