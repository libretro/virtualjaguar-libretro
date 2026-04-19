#include <string.h>

#include "cheat.h"

static int hex_digit(char c)
{
   if (c >= '0' && c <= '9') return c - '0';
   if (c >= 'a' && c <= 'f') return c - 'a' + 10;
   if (c >= 'A' && c <= 'F') return c - 'A' + 10;
   return -1;
}

bool cheat_parse_one(const char *code,
                     uint32_t *addr_out,
                     uint32_t *val_out,
                     uint8_t *size_out)
{
   char buf[32];
   size_t n = 0;
   size_t addr_len, val_len, i;
   uint32_t addr = 0, val = 0;

   if (!code || !addr_out || !val_out || !size_out)
      return false;

   for (; *code && n < sizeof(buf) - 1; code++)
   {
      if (hex_digit((char)*code) >= 0)
         buf[n++] = *code;
      else if (*code != ' ' && *code != '\t' && *code != ':' &&
               *code != '-' && *code != '.')
         return false;
   }
   buf[n] = '\0';

   /* Layout:  address_hex + value_hex (concatenated after separator strip).
    * Accepted pairings cover byte/word/long values under 24- or 32-bit
    * nominal addresses; the address is always masked to 24 bits below. */
   switch (n)
   {
      case  8: addr_len = 6; val_len = 2; break;  /* 6 + byte  */
      case 10: addr_len = 6; val_len = 4; break;  /* 6 + word  */
      case 12: addr_len = 8; val_len = 4; break;  /* 8 + word (PAR) */
      case 14: addr_len = 6; val_len = 8; break;  /* 6 + long  */
      case 16: addr_len = 8; val_len = 8; break;  /* 8 + long  */
      default: return false;
   }

   for (i = 0; i < addr_len; i++)
      addr = (addr << 4) | (uint32_t)hex_digit(buf[i]);
   for (i = 0; i < val_len; i++)
      val  = (val  << 4) | (uint32_t)hex_digit(buf[addr_len + i]);

   *addr_out = addr & 0x00FFFFFFu;
   *val_out  = val;
   *size_out = (uint8_t)(val_len / 2);
   return true;
}

void cheat_list_remove_index(cheat_list_t *list, unsigned index)
{
   unsigned i, j = 0;
   if (!list)
      return;
   for (i = 0; i < list->count; i++)
   {
      if (list->entries[i].tag == (uint8_t)(index & 0xFF))
         continue;
      if (j != i)
         list->entries[j] = list->entries[i];
      j++;
   }
   list->count = j;
}

void cheat_list_reset(cheat_list_t *list)
{
   if (!list)
      return;
   memset(list, 0, sizeof(*list));
}

void cheat_list_set(cheat_list_t *list,
                    unsigned index,
                    bool enabled,
                    const char *code)
{
   const char *p;
   const char *start;

   if (!list || !code)
      return;

   cheat_list_remove_index(list, index);
   if (!enabled)
      return;

   p = code;
   start = p;
   for (;;)
   {
      if (*p == '+' || *p == '\n' || *p == '\r' || *p == '\0')
      {
         size_t len = (size_t)(p - start);
         if (len > 0 && list->count < CHEAT_MAX_ENTRIES)
         {
            char tmp[64];
            cheat_entry_t c;
            if (len >= sizeof(tmp))
               len = sizeof(tmp) - 1;
            memcpy(tmp, start, len);
            tmp[len] = '\0';
            if (cheat_parse_one(tmp, &c.address, &c.value, &c.size))
            {
               c.tag     = (uint8_t)(index & 0xFF);
               c.enabled = true;
               list->entries[list->count++] = c;
            }
         }
         if (*p == '\0')
            break;
         start = p + 1;
      }
      p++;
   }
}

void cheat_list_apply(const cheat_list_t *list,
                      cheat_write_fn write,
                      void *user)
{
   unsigned i;
   if (!list || !write)
      return;
   for (i = 0; i < list->count; i++)
   {
      if (!list->entries[i].enabled)
         continue;
      write(list->entries[i].address,
            list->entries[i].value,
            list->entries[i].size,
            user);
   }
}
