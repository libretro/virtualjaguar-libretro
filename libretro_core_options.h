#ifndef LIBRETRO_CORE_OPTIONS_H__
#define LIBRETRO_CORE_OPTIONS_H__

#include <stdlib.h>
#include <string.h>

#include <libretro.h>
#include <retro_inline.h>

#ifndef HAVE_NO_LANGEXTRA
#include "libretro_core_options_intl.h"
#endif

#define INPUT_OPTIONS        \
{                            \
   { "up",     "Up" },       \
   { "down",   "Down" },     \
   { "left",   "Left" },     \
   { "right",  "Right" },    \
   { "btn_a",  "A" },        \
   { "btn_b",  "B" },        \
   { "btn_c",  "C" },        \
   { "pause",  "Pause" },    \
   { "option", "Option" },   \
   { "num_0",  "Numpad 0" }, \
   { "num_1",  "Numpad 1" }, \
   { "num_2",  "Numpad 2" }, \
   { "num_3",  "Numpad 3" }, \
   { "num_4",  "Numpad 4" }, \
   { "num_5",  "Numpad 5" }, \
   { "num_6",  "Numpad 6" }, \
   { "num_7",  "Numpad 7" }, \
   { "num_8",  "Numpad 8" }, \
   { "num_9",  "Numpad 9" }, \
   { "star",   "Numpad *" }, \
   { "hash",   "Numpad #" }, \
   { "---",    NULL },       \
   { NULL, NULL },           \
}

/*
 ********************************
 * VERSION: 2.0
 ********************************
 *
 * - 2.0: Add support for core options v2 interface
 * - 1.3: Move translations to libretro_core_options_intl.h
 *        - libretro_core_options_intl.h includes BOM and utf-8
 *          fix for MSVC 2010-2013
 *        - Added HAVE_NO_LANGEXTRA flag to disable translations
 *          on platforms/compilers without BOM support
 * - 1.2: Use core options v1 interface when
 *        RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION is >= 1
 *        (previously required RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION == 1)
 * - 1.1: Support generation of core options v0 retro_core_option_value
 *        arrays containing options with a single value
 * - 1.0: First commit
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 ********************************
 * Core Option Definitions
 ********************************
*/

/* RETRO_LANGUAGE_ENGLISH */

/* Default language:
 * - All other languages must include the same keys and values
 * - Will be used as a fallback in the event that frontend language
 *   is not available
 * - Will be used as a fallback for any missing entries in
 *   frontend language definition */

struct retro_core_option_v2_category option_cats_us[] = {
   {
      "input_p1",
      "Input Port 1",
      "Change input mappings for port 1."
   },
   {
      "input_p2",
      "Input Port 2",
      "Change input mappings for port 2."
   },
   { NULL, NULL, NULL },
};

struct retro_core_option_v2_definition option_defs_us[] = {
   {
      "virtualjaguar_usefastblitter",
      "Fast Blitter",
      NULL,
      "Use the faster blitter, it is older and less compatible, it will break some games.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_doom_res_hack",
      "Doom Resolution Hack",
      NULL,
      "Hack to fix the halved resolution in Doom.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_bios",
      "BIOS",
      NULL,
      "Use the Jaguar BIOS, required for some games.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_pal",
      "PAL (Restart)",
      NULL,
      "Emulate a PAL Jaguar instead of NTSC.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_alt_inputs",
      "Enable Core Options Remapping",
      NULL,
      "Enabling this option will let you rebind controllers from the core options, removing the 'Controls' menu limitation that makes Numpad 7, 8, 9, * and # impossible to remap.\nNOTE: the 'Controls' menu can still conflict with the core options remapping, if you're using a remap file it is recommended to delete/reset it.",
      NULL,
      NULL,
      {
         { "disabled", NULL },
         { "enabled",  NULL },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_p1_numpad_to_kb",
      "Port 1 > Numpad Buttons to Keyboard Keys",
      "Numpad Buttons to Keyboard Keys",
      "Map Jaguar numpad 0-9, * and # to keyboard keys. 'Number Row Keys' will use 1234567890-= keys, 'Keypad Keys' will use 0123456789/* keypad keys.",
      NULL,
      "input_p1",
      {
         { "disabled", NULL },
         { "numbers",  "Number Row Keys" },
         { "keypad",   "Keypad Keys" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_p1_retropad_up",
      "Port 1 > RetroPad Up",
      "RetroPad Up",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "up"
   },
   {
      "virtualjaguar_p1_retropad_down",
      "Port 1 > RetroPad Down",
      "RetroPad Down",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "down"
   },
   {
      "virtualjaguar_p1_retropad_left",
      "Port 1 > RetroPad Left",
      "RetroPad Left",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "left"
   },
   {
      "virtualjaguar_p1_retropad_right",
      "Port 1 > RetroPad Right",
      "RetroPad Right",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "right"
   },
   {
      "virtualjaguar_p1_retropad_a",
      "Port 1 > RetroPad A",
      "RetroPad A",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "btn_a"
   },
   {
      "virtualjaguar_p1_retropad_b",
      "Port 1 > RetroPad B",
      "RetroPad B",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "btn_b"
   },
   {
      "virtualjaguar_p1_retropad_x",
      "Port 1 > RetroPad X",
      "RetroPad X",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_0"
   },
   {
      "virtualjaguar_p1_retropad_y",
      "Port 1 > RetroPad Y",
      "RetroPad Y",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "btn_c"
   },
   {
      "virtualjaguar_p1_retropad_select",
      "Port 1 > RetroPad Select",
      "RetroPad Select",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "pause"
   },
   {
      "virtualjaguar_p1_retropad_start",
      "Port 1 > RetroPad Start",
      "RetroPad Start",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "option"
   },
   {
      "virtualjaguar_p1_retropad_l1",
      "Port 1 > RetroPad L1",
      "RetroPad L1",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_1"
   },
   {
      "virtualjaguar_p1_retropad_r1",
      "Port 1 > RetroPad R1",
      "RetroPad R1",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_2"
   },
   {
      "virtualjaguar_p1_retropad_l2",
      "Port 1 > RetroPad L2",
      "RetroPad L2",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_3"
   },
   {
      "virtualjaguar_p1_retropad_r2",
      "Port 1 > RetroPad R2",
      "RetroPad R2",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_4"
   },
   {
      "virtualjaguar_p1_retropad_l3",
      "Port 1 > RetroPad L3",
      "RetroPad L3",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_5"
   },
   {
      "virtualjaguar_p1_retropad_r3",
      "Port 1 > RetroPad R3",
      "RetroPad R3",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "num_6"
   },
   {
      "virtualjaguar_p1_retropad_analog_lu",
      "Port 1 > RetroPad Left Analog Up",
      "RetroPad Left Analog Up",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_ld",
      "Port 1 > RetroPad Left Analog Down",
      "RetroPad Left Analog Down",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_ll",
      "Port 1 > RetroPad Left Analog Left",
      "RetroPad Left Analog Left",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_lr",
      "Port 1 > RetroPad Left Analog Right",
      "RetroPad Left Analog Right",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_ru",
      "Port 1 > RetroPad Right Analog Up",
      "RetroPad Right Analog Up",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_rd",
      "Port 1 > RetroPad Right Analog Down",
      "RetroPad Right Analog Down",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_rl",
      "Port 1 > RetroPad Right Analog Left",
      "RetroPad Right Analog Left",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p1_retropad_analog_rr",
      "Port 1 > RetroPad Right Analog Right",
      "RetroPad Right Analog Right",
      NULL,
      NULL,
      "input_p1",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_numpad_to_kb",
      "Port 2 > Numpad Buttons to Keyboard Keys",
      "Numpad Buttons to Keyboard Keys",
      "Map Jaguar numpad 0-9, * and # to keyboard keys. 'Number Row Keys' will use 1234567890-= keys, 'Keypad Keys' will use 0123456789/* keypad keys.",
      NULL,
      "input_p2",
      {
         { "disabled", NULL },
         { "numbers",  "Number Row Keys" },
         { "keypad",   "Keypad Keys" },
         { NULL, NULL },
      },
      "disabled"
   },
   {
      "virtualjaguar_p2_retropad_up",
      "Port 2 > RetroPad Up",
      "RetroPad Up",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "up"
   },
   {
      "virtualjaguar_p2_retropad_down",
      "Port 2 > RetroPad Down",
      "RetroPad Down",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "down"
   },
   {
      "virtualjaguar_p2_retropad_left",
      "Port 2 > RetroPad Left",
      "RetroPad Left",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "left"
   },
   {
      "virtualjaguar_p2_retropad_right",
      "Port 2 > RetroPad Right",
      "RetroPad Right",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "right"
   },
   {
      "virtualjaguar_p2_retropad_a",
      "Port 2 > RetroPad A",
      "RetroPad A",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "btn_a"
   },
   {
      "virtualjaguar_p2_retropad_b",
      "Port 2 > RetroPad B",
      "RetroPad B",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "btn_b"
   },
   {
      "virtualjaguar_p2_retropad_x",
      "Port 2 > RetroPad X",
      "RetroPad X",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_0"
   },
   {
      "virtualjaguar_p2_retropad_y",
      "Port 2 > RetroPad Y",
      "RetroPad Y",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "btn_c"
   },
   {
      "virtualjaguar_p2_retropad_select",
      "Port 2 > RetroPad Select",
      "RetroPad Select",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "pause"
   },
   {
      "virtualjaguar_p2_retropad_start",
      "Port 2 > RetroPad Start",
      "RetroPad Start",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "option"
   },
   {
      "virtualjaguar_p2_retropad_l1",
      "Port 2 > RetroPad L1",
      "RetroPad L1",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_1"
   },
   {
      "virtualjaguar_p2_retropad_r1",
      "Port 2 > RetroPad R1",
      "RetroPad R1",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_2"
   },
   {
      "virtualjaguar_p2_retropad_l2",
      "Port 2 > RetroPad L2",
      "RetroPad L2",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_3"
   },
   {
      "virtualjaguar_p2_retropad_r2",
      "Port 2 > RetroPad R2",
      "RetroPad R2",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_4"
   },
   {
      "virtualjaguar_p2_retropad_l3",
      "Port 2 > RetroPad L3",
      "RetroPad L3",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_5"
   },
   {
      "virtualjaguar_p2_retropad_r3",
      "Port 2 > RetroPad R3",
      "RetroPad R3",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "num_6"
   },
   {
      "virtualjaguar_p2_retropad_analog_lu",
      "Port 2 > RetroPad Left Analog Up",
      "RetroPad Left Analog Up",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_ld",
      "Port 2 > RetroPad Left Analog Down",
      "RetroPad Left Analog Down",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_ll",
      "Port 2 > RetroPad Left Analog Left",
      "RetroPad Left Analog Left",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_lr",
      "Port 2 > RetroPad Left Analog Right",
      "RetroPad Left Analog Right",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_ru",
      "Port 2 > RetroPad Right Analog Up",
      "RetroPad Right Analog Up",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_rd",
      "Port 2 > RetroPad Right Analog Down",
      "RetroPad Right Analog Down",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_rl",
      "Port 2 > RetroPad Right Analog Left",
      "RetroPad Right Analog Left",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   {
      "virtualjaguar_p2_retropad_analog_rr",
      "Port 2 > RetroPad Right Analog Right",
      "RetroPad Right Analog Right",
      NULL,
      NULL,
      "input_p2",
      INPUT_OPTIONS,
      "---"
   },
   { NULL, NULL, NULL, NULL, NULL, NULL, {{0}}, NULL },
};

struct retro_core_options_v2 options_us = {
   option_cats_us,
   option_defs_us
};

/*
 ********************************
 * Language Mapping
 ********************************
*/

#ifndef HAVE_NO_LANGEXTRA
struct retro_core_options_v2 *options_intl[RETRO_LANGUAGE_LAST] = {
   &options_us, /* RETRO_LANGUAGE_ENGLISH */
   NULL,        /* RETRO_LANGUAGE_JAPANESE */
   NULL,        /* RETRO_LANGUAGE_FRENCH */
   NULL,        /* RETRO_LANGUAGE_SPANISH */
   NULL,        /* RETRO_LANGUAGE_GERMAN */
   NULL,        /* RETRO_LANGUAGE_ITALIAN */
   NULL,        /* RETRO_LANGUAGE_DUTCH */
   NULL,        /* RETRO_LANGUAGE_PORTUGUESE_BRAZIL */
   NULL,        /* RETRO_LANGUAGE_PORTUGUESE_PORTUGAL */
   NULL,        /* RETRO_LANGUAGE_RUSSIAN */
   NULL,        /* RETRO_LANGUAGE_KOREAN */
   NULL,        /* RETRO_LANGUAGE_CHINESE_TRADITIONAL */
   NULL,        /* RETRO_LANGUAGE_CHINESE_SIMPLIFIED */
   NULL,        /* RETRO_LANGUAGE_ESPERANTO */
   NULL,        /* RETRO_LANGUAGE_POLISH */
   NULL,        /* RETRO_LANGUAGE_VIETNAMESE */
   NULL,        /* RETRO_LANGUAGE_ARABIC */
   NULL,        /* RETRO_LANGUAGE_GREEK */
   NULL,        /* RETRO_LANGUAGE_TURKISH */
   NULL,        /* RETRO_LANGUAGE_SLOVAK */
   NULL,        /* RETRO_LANGUAGE_PERSIAN */
   NULL,        /* RETRO_LANGUAGE_HEBREW */
   NULL,        /* RETRO_LANGUAGE_ASTURIAN */
   NULL,        /* RETRO_LANGUAGE_FINNISH */
};
#endif

/*
 ********************************
 * Functions
 ********************************
*/

/* Handles configuration/setting of core options.
 * Should be called as early as possible - ideally inside
 * retro_set_environment(), and no later than retro_load_game()
 * > We place the function body in the header to avoid the
 *   necessity of adding more .c files (i.e. want this to
 *   be as painless as possible for core devs)
 */

static INLINE void libretro_set_core_options(retro_environment_t environ_cb,
      bool *categories_supported)
{
   unsigned version  = 0;
#ifndef HAVE_NO_LANGEXTRA
   unsigned language = 0;
#endif

   if (!environ_cb || !categories_supported)
      return;

   *categories_supported = false;

   if (!environ_cb(RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION, &version))
      version = 0;

   if (version >= 2)
   {
#ifndef HAVE_NO_LANGEXTRA
      struct retro_core_options_v2_intl core_options_intl;

      core_options_intl.us    = &options_us;
      core_options_intl.local = NULL;

      if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
          (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH))
         core_options_intl.local = options_intl[language];

      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL,
            &core_options_intl);
#else
      *categories_supported = environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2,
            &options_us);
#endif
   }
   else
   {
      size_t i, j;
      size_t option_index              = 0;
      size_t num_options               = 0;
      struct retro_core_option_definition
            *option_v1_defs_us         = NULL;
#ifndef HAVE_NO_LANGEXTRA
      size_t num_options_intl          = 0;
      struct retro_core_option_v2_definition
            *option_defs_intl          = NULL;
      struct retro_core_option_definition
            *option_v1_defs_intl       = NULL;
      struct retro_core_options_intl
            core_options_v1_intl;
#endif
      struct retro_variable *variables = NULL;
      char **values_buf                = NULL;

      /* Determine total number of options */
      while (true)
      {
         if (option_defs_us[num_options].key)
            num_options++;
         else
            break;
      }

      if (version >= 1)
      {
         /* Allocate US array */
         option_v1_defs_us = (struct retro_core_option_definition *)
               calloc(num_options + 1, sizeof(struct retro_core_option_definition));

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            struct retro_core_option_v2_definition *option_def_us = &option_defs_us[i];
            struct retro_core_option_value *option_values         = option_def_us->values;
            struct retro_core_option_definition *option_v1_def_us = &option_v1_defs_us[i];
            struct retro_core_option_value *option_v1_values      = option_v1_def_us->values;

            option_v1_def_us->key           = option_def_us->key;
            option_v1_def_us->desc          = option_def_us->desc;
            option_v1_def_us->info          = option_def_us->info;
            option_v1_def_us->default_value = option_def_us->default_value;

            /* Values must be copied individually... */
            while (option_values->value)
            {
               option_v1_values->value = option_values->value;
               option_v1_values->label = option_values->label;

               option_values++;
               option_v1_values++;
            }
         }

#ifndef HAVE_NO_LANGEXTRA
         if (environ_cb(RETRO_ENVIRONMENT_GET_LANGUAGE, &language) &&
             (language < RETRO_LANGUAGE_LAST) && (language != RETRO_LANGUAGE_ENGLISH) &&
             options_intl[language])
            option_defs_intl = options_intl[language]->definitions;

         if (option_defs_intl)
         {
            /* Determine number of intl options */
            while (true)
            {
               if (option_defs_intl[num_options_intl].key)
                  num_options_intl++;
               else
                  break;
            }

            /* Allocate intl array */
            option_v1_defs_intl = (struct retro_core_option_definition *)
                  calloc(num_options_intl + 1, sizeof(struct retro_core_option_definition));

            /* Copy parameters from option_defs_intl array */
            for (i = 0; i < num_options_intl; i++)
            {
               struct retro_core_option_v2_definition *option_def_intl = &option_defs_intl[i];
               struct retro_core_option_value *option_values           = option_def_intl->values;
               struct retro_core_option_definition *option_v1_def_intl = &option_v1_defs_intl[i];
               struct retro_core_option_value *option_v1_values        = option_v1_def_intl->values;

               option_v1_def_intl->key           = option_def_intl->key;
               option_v1_def_intl->desc          = option_def_intl->desc;
               option_v1_def_intl->info          = option_def_intl->info;
               option_v1_def_intl->default_value = option_def_intl->default_value;

               /* Values must be copied individually... */
               while (option_values->value)
               {
                  option_v1_values->value = option_values->value;
                  option_v1_values->label = option_values->label;

                  option_values++;
                  option_v1_values++;
               }
            }
         }

         core_options_v1_intl.us    = option_v1_defs_us;
         core_options_v1_intl.local = option_v1_defs_intl;

         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL, &core_options_v1_intl);
#else
         environ_cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS, option_v1_defs_us);
#endif
      }
      else
      {
         /* Allocate arrays */
         variables  = (struct retro_variable *)calloc(num_options + 1,
               sizeof(struct retro_variable));
         values_buf = (char **)calloc(num_options, sizeof(char *));

         if (!variables || !values_buf)
            goto error;

         /* Copy parameters from option_defs_us array */
         for (i = 0; i < num_options; i++)
         {
            const char *key                        = option_defs_us[i].key;
            const char *desc                       = option_defs_us[i].desc;
            const char *default_value              = option_defs_us[i].default_value;
            struct retro_core_option_value *values = option_defs_us[i].values;
            size_t buf_len                         = 3;
            size_t default_index                   = 0;

            values_buf[i] = NULL;

            if (desc)
            {
               size_t num_values = 0;

               /* Determine number of values */
               while (true)
               {
                  if (values[num_values].value)
                  {
                     /* Check if this is the default value */
                     if (default_value)
                        if (strcmp(values[num_values].value, default_value) == 0)
                           default_index = num_values;

                     buf_len += strlen(values[num_values].value);
                     num_values++;
                  }
                  else
                     break;
               }

               /* Build values string */
               if (num_values > 0)
               {
                  buf_len += num_values - 1;
                  buf_len += strlen(desc);

                  values_buf[i] = (char *)calloc(buf_len, sizeof(char));
                  if (!values_buf[i])
                     goto error;

                  strcpy(values_buf[i], desc);
                  strcat(values_buf[i], "; ");

                  /* Default value goes first */
                  strcat(values_buf[i], values[default_index].value);

                  /* Add remaining values */
                  for (j = 0; j < num_values; j++)
                  {
                     if (j != default_index)
                     {
                        strcat(values_buf[i], "|");
                        strcat(values_buf[i], values[j].value);
                     }
                  }
               }
            }

            variables[option_index].key   = key;
            variables[option_index].value = values_buf[i];
            option_index++;
         }

         /* Set variables */
         environ_cb(RETRO_ENVIRONMENT_SET_VARIABLES, variables);
      }

error:
      /* Clean up */

      if (option_v1_defs_us)
      {
         free(option_v1_defs_us);
         option_v1_defs_us = NULL;
      }

#ifndef HAVE_NO_LANGEXTRA
      if (option_v1_defs_intl)
      {
         free(option_v1_defs_intl);
         option_v1_defs_intl = NULL;
      }
#endif

      if (values_buf)
      {
         for (i = 0; i < num_options; i++)
         {
            if (values_buf[i])
            {
               free(values_buf[i]);
               values_buf[i] = NULL;
            }
         }

         free(values_buf);
         values_buf = NULL;
      }

      if (variables)
      {
         free(variables);
         variables = NULL;
      }
   }
}

#ifdef __cplusplus
}
#endif

#endif
