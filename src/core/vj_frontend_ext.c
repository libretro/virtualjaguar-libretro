/*
 * vj_frontend_ext.c -- private core -> frontend event channel (ABI 1).
 * Contract and rules: vj_frontend_ext.h, docs/app-frontend-extension.md.
 */

#include "vj_frontend_ext.h"

#include <string.h>

/* File scope, reset in VJExtDeinit(): iOS never dlcloses the core. */
static retro_environment_t vj_ext_env  = 0;
static int                 vj_ext_on   = 0;
static unsigned            vj_ext_sent = 0;
static unsigned            vj_ext_caps = 0;

void VJExtInit(retro_environment_t env_cb, const char *core_version)
{
   struct vj_env_hello hello;

   vj_ext_env  = 0;
   vj_ext_on   = 0;
   vj_ext_sent = 0;
   vj_ext_caps = 0;
   if (!env_cb)
      return;

   hello.abi          = VJ_FRONTEND_EXT_ABI;
   hello.core_version = core_version ? core_version : "";
   hello.caps_out     = 0;
   if (env_cb(VJ_ENV_HELLO, &hello) && (hello.caps_out & VJ_CAP_EVENTS))
   {
      vj_ext_env  = env_cb;
      vj_ext_on   = 1;
      vj_ext_caps = hello.caps_out;
   }
}

void VJExtDeinit(void)
{
   vj_ext_env  = 0;
   vj_ext_on   = 0;
   vj_ext_sent = 0;
   vj_ext_caps = 0;
}

int VJExtActive(void)
{
   return vj_ext_on;
}

int VJExtHasCap(unsigned cap)
{
   return vj_ext_on && (vj_ext_caps & cap) == cap;
}

int VJExtBiosPath(unsigned role, char *buf, unsigned size)
{
   struct vj_env_bios_path q;
   size_t n;

   if (!buf || size == 0)
      return 0;
   buf[0] = '\0';
   if (!VJExtHasCap(VJ_CAP_BIOS))
      return 0;
   q.role = role;
   q.path = 0;
   if (!vj_ext_env(VJ_ENV_GET_BIOS_PATH, &q) || !q.path || !q.path[0])
      return 0;
   n = strlen(q.path);
   if (n >= size)
      return 0;
   memcpy(buf, q.path, n + 1);
   return 1;
}

unsigned VJExtBootTarget(void)
{
   unsigned t = VJ_BOOT_CD_BIOS;

   if (!VJExtHasCap(VJ_CAP_BIOS))
      return VJ_BOOT_CD_BIOS;
   if (!vj_ext_env(VJ_ENV_GET_BOOT_TARGET, &t))
      return VJ_BOOT_CD_BIOS;
   return (t == VJ_BOOT_CART_BOOT_ROM) ? VJ_BOOT_CART_BOOT_ROM : VJ_BOOT_CD_BIOS;
}

void VJExtEmit(unsigned type, unsigned a, unsigned b, unsigned c, unsigned d,
               const char *s)
{
   struct vj_event ev;

   if (!vj_ext_on)
      return;
   ev.type = type;
   ev.a    = a;
   ev.b    = b;
   ev.c    = c;
   ev.d    = d;
   ev.s    = s;
   vj_ext_sent++;
   vj_ext_env(VJ_ENV_EVENT, &ev);
}

unsigned VJExtSentCount(void)
{
   return vj_ext_sent;
}
