//
// SETTINGS.CPP: Virtual Jaguar configuration loading/saving support
//
// by James Hammons
// (C) 2010 Underground Software
//
// JLH = James Hammons <jlhamm@acm.org>
//
// Who  When        What
// ---  ----------  -------------------------------------------------------------
// JLH  01/16/2010  Created this log
//

#include "settings.h"

//#include <stdlib.h>
//#include <string>
//#include "SDL.h"
//#include "sdlemu_config.h"
//#include "log.h"

using namespace std;

// Global variables

VJSettings vjs;

#if 0
// Private function prototypes

void CheckForTrailingSlash(char * path);

//
// Load Virtual Jaguar's settings
//
void LoadVJSettings(void)
{
	if (sdlemu_init_config("./vj.cfg") == 0			// CWD
		&& sdlemu_init_config("~/vj.cfg") == 0		// Home
		&& sdlemu_init_config("~/.vj/vj.cfg") == 0	// Home under .vj directory
		&& sdlemu_init_config("vj.cfg") == 0)		// Somewhere in the path
		WriteLog("Settings: Couldn't find VJ configuration file. Using defaults...\n");

	vjs.useJoystick = sdlemu_getval_bool("useJoystick", false);
	vjs.joyport = sdlemu_getval_int("joyport", 0);
	vjs.hardwareTypeNTSC = sdlemu_getval_bool("hardwareTypeNTSC", true);
	vjs.frameSkip = sdlemu_getval_int("frameSkip", 0);
	vjs.useJaguarBIOS = sdlemu_getval_bool("useJaguarBIOS", false);
	vjs.DSPEnabled = sdlemu_getval_bool("DSPEnabled", false);
	vjs.usePipelinedDSP = sdlemu_getval_bool("usePipelinedDSP", false);
	vjs.fullscreen = sdlemu_getval_bool("fullscreen", false);
	vjs.useOpenGL = sdlemu_getval_bool("useOpenGL", true);
	vjs.glFilter = sdlemu_getval_int("glFilterType", 0);
	vjs.renderType = sdlemu_getval_int("renderType", 0);

	// Keybindings in order of U, D, L, R, C, B, A, Op, Pa, 0-9, #, *
	vjs.p1KeyBindings[0] = sdlemu_getval_int("p1k_up", SDLK_UP);
	vjs.p1KeyBindings[1] = sdlemu_getval_int("p1k_down", SDLK_DOWN);
	vjs.p1KeyBindings[2] = sdlemu_getval_int("p1k_left", SDLK_LEFT);
	vjs.p1KeyBindings[3] = sdlemu_getval_int("p1k_right", SDLK_RIGHT);
	vjs.p1KeyBindings[4] = sdlemu_getval_int("p1k_c", SDLK_z);
	vjs.p1KeyBindings[5] = sdlemu_getval_int("p1k_b", SDLK_x);
	vjs.p1KeyBindings[6] = sdlemu_getval_int("p1k_a", SDLK_c);
	vjs.p1KeyBindings[7] = sdlemu_getval_int("p1k_option", SDLK_QUOTE);
	vjs.p1KeyBindings[8] = sdlemu_getval_int("p1k_pause", SDLK_RETURN);
	vjs.p1KeyBindings[9] = sdlemu_getval_int("p1k_0", SDLK_KP0);
	vjs.p1KeyBindings[10] = sdlemu_getval_int("p1k_1", SDLK_KP1);
	vjs.p1KeyBindings[11] = sdlemu_getval_int("p1k_2", SDLK_KP2);
	vjs.p1KeyBindings[12] = sdlemu_getval_int("p1k_3", SDLK_KP3);
	vjs.p1KeyBindings[13] = sdlemu_getval_int("p1k_4", SDLK_KP4);
	vjs.p1KeyBindings[14] = sdlemu_getval_int("p1k_5", SDLK_KP5);
	vjs.p1KeyBindings[15] = sdlemu_getval_int("p1k_6", SDLK_KP6);
	vjs.p1KeyBindings[16] = sdlemu_getval_int("p1k_7", SDLK_KP7);
	vjs.p1KeyBindings[17] = sdlemu_getval_int("p1k_8", SDLK_KP8);
	vjs.p1KeyBindings[18] = sdlemu_getval_int("p1k_9", SDLK_KP9);
	vjs.p1KeyBindings[19] = sdlemu_getval_int("p1k_pound", SDLK_KP_DIVIDE);
	vjs.p1KeyBindings[20] = sdlemu_getval_int("p1k_star", SDLK_KP_MULTIPLY);

	vjs.p2KeyBindings[0] = sdlemu_getval_int("p2k_up", SDLK_UP);
	vjs.p2KeyBindings[1] = sdlemu_getval_int("p2k_down", SDLK_DOWN);
	vjs.p2KeyBindings[2] = sdlemu_getval_int("p2k_left", SDLK_LEFT);
	vjs.p2KeyBindings[3] = sdlemu_getval_int("p2k_right", SDLK_RIGHT);
	vjs.p2KeyBindings[4] = sdlemu_getval_int("p2k_c", SDLK_z);
	vjs.p2KeyBindings[5] = sdlemu_getval_int("p2k_b", SDLK_x);
	vjs.p2KeyBindings[6] = sdlemu_getval_int("p2k_a", SDLK_c);
	vjs.p2KeyBindings[7] = sdlemu_getval_int("p2k_option", SDLK_QUOTE);
	vjs.p2KeyBindings[8] = sdlemu_getval_int("p2k_pause", SDLK_RETURN);
	vjs.p2KeyBindings[9] = sdlemu_getval_int("p2k_0", SDLK_KP0);
	vjs.p2KeyBindings[10] = sdlemu_getval_int("p2k_1", SDLK_KP1);
	vjs.p2KeyBindings[11] = sdlemu_getval_int("p2k_2", SDLK_KP2);
	vjs.p2KeyBindings[12] = sdlemu_getval_int("p2k_3", SDLK_KP3);
	vjs.p2KeyBindings[13] = sdlemu_getval_int("p2k_4", SDLK_KP4);
	vjs.p2KeyBindings[14] = sdlemu_getval_int("p2k_5", SDLK_KP5);
	vjs.p2KeyBindings[15] = sdlemu_getval_int("p2k_6", SDLK_KP6);
	vjs.p2KeyBindings[16] = sdlemu_getval_int("p2k_7", SDLK_KP7);
	vjs.p2KeyBindings[17] = sdlemu_getval_int("p2k_8", SDLK_KP8);
	vjs.p2KeyBindings[18] = sdlemu_getval_int("p2k_9", SDLK_KP9);
	vjs.p2KeyBindings[19] = sdlemu_getval_int("p2k_pound", SDLK_KP_DIVIDE);
	vjs.p2KeyBindings[20] = sdlemu_getval_int("p2k_star", SDLK_KP_MULTIPLY);

	strcpy(vjs.jagBootPath, sdlemu_getval_string("JagBootROM", "./BIOS/jagboot.rom"));
	strcpy(vjs.CDBootPath, sdlemu_getval_string("CDBootROM", "./BIOS/jagcd.rom"));
	strcpy(vjs.EEPROMPath, sdlemu_getval_string("EEPROMs", "./EEPROMs"));
	strcpy(vjs.ROMPath, sdlemu_getval_string("ROMs", "./ROMs"));
	CheckForTrailingSlash(vjs.EEPROMPath);
	CheckForTrailingSlash(vjs.ROMPath);

	vjs.hardwareTypeAlpine = false;	// No external setting for this yet...
}

//
// Save Virtual Jaguar's settings
//
void SaveVJSettings(void)
{
}

//
// Check path for a trailing slash, and append if not present
//
void CheckForTrailingSlash(char * path)
{
	if (strlen(path) > 0)
		if (path[strlen(path) - 1] != '/')
			strcat(path, "/");	// NOTE: Possible buffer overflow
}
#endif
