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
// JLH  02/23/2013  Finally removed commented out stuff :-P
//

#include "settings.h"
#include "jagcd_boot.h"
#include "log.h"

struct VJSettings vjs;
struct BootConfig bootConfig;

void ResolveBootConfig(struct BootConfig *cfg,
                       bool isCDGame, bool cdBiosFileLoaded,
                       uint32_t cdBootMode, bool userWantsBIOS)
{
   cfg->isCDGame        = isCDGame;
   cfg->cdBiosAvailable = cdBiosFileLoaded;

   if (!isCDGame)
   {
      cfg->showBootROM = userWantsBIOS;
      cfg->strategy    = &cd_boot_strategy_cart;
      LOG_INF("[BOOT] Cart game — showBootROM=%d\n", cfg->showBootROM);
      return;
   }

   switch (cdBootMode)
   {
   case CDBOOT_HLE:
      cfg->showBootROM = false;
      cfg->strategy    = &cd_boot_strategy_hle;
      LOG_INF("[BOOT] CD game, mode=HLE\n");
      break;

   case CDBOOT_BIOS:
      if (cdBiosFileLoaded)
      {
         cfg->showBootROM = true;
         cfg->strategy    = &cd_boot_strategy_bios;
         if (!userWantsBIOS)
            LOG_INF("[BOOT] CD game, mode=BIOS — boot ROM forced on "
                    "(required by real CD BIOS path)\n");
         LOG_INF("[BOOT] CD game, mode=BIOS (external BIOS loaded)\n");
      }
      else
      {
         cfg->showBootROM = false;
         cfg->strategy    = &cd_boot_strategy_hle;
         LOG_WRN("[BOOT] CD game, mode=BIOS but no BIOS file found — "
                 "falling back to HLE\n");
      }
      break;

   case CDBOOT_AUTO:
   default:
      if (cdBiosFileLoaded)
      {
         cfg->showBootROM = true;
         cfg->strategy    = &cd_boot_strategy_bios;
         if (!userWantsBIOS)
            LOG_INF("[BOOT] CD game, mode=AUTO — boot ROM forced on "
                    "(required by real CD BIOS path)\n");
         LOG_INF("[BOOT] CD game, mode=AUTO — using real BIOS\n");
      }
      else
      {
         cfg->showBootROM = false;
         cfg->strategy    = &cd_boot_strategy_hle;
         LOG_INF("[BOOT] CD game, mode=AUTO — no BIOS, using HLE\n");
      }
      break;
   }
}
