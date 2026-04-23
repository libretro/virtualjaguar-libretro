#include <string.h>

#include "cheat.h"

static int hex_digit(char c)
{
   if (c >= '0' && c <= '9') return c - '0';
   if (c >= 'a' && c <= 'f') return c - 'a' + 10;
   if (c >= 'A' && c <= 'F') return c - 'A' + 10;
   return -1;
}

/* Longest accepted form is 8 hex (addr) + 8 hex (value) after separator strip. */
#define CHEAT_PARSE_MAX_HEX 16

static bool hex_only_strip(const char *in, char *hex_out, size_t hex_max, size_t *n_hex)
{
   size_t n = 0;

   for (; *in; in++)
   {
      if (hex_digit((char)*in) >= 0)
      {
         if (n >= hex_max - 1)
            return false;
         hex_out[n++] = (char)*in;
      }
      else if (*in != ' ' && *in != '\t' && *in != ':' &&
               *in != '-' && *in != '.')
         return false;
   }
   hex_out[n] = '\0';
   *n_hex = n;
   return true;
}

static bool has_ascii_ws(const char *s)
{
   for (; *s; s++)
   {
      if (*s == ' ' || *s == '\t')
         return true;
   }
   return false;
}

/* Split on the last ':', '-', or '.' so "0000:3D00:FF" → address 00003D00 + value FF. */
static bool split_last_cd_sep(const char *code,
                              char *left, size_t lmax,
                              char *right, size_t rmax)
{
   const char *last = NULL;
   const char *p;
   size_t ln;
   size_t rn;

   for (p = code; *p; p++)
   {
      if (*p == ':' || *p == '-' || *p == '.')
         last = p;
   }
   if (!last || last == code || !last[1])
      return false;

   ln = (size_t)(last - code);
   if (ln + 1 >= lmax)
      return false;
   memcpy(left, code, ln);
   left[ln] = '\0';

   for (p = last + 1, rn = 0; *p; p++)
   {
      if (rn + 1 >= rmax)
         return false;
      right[rn++] = *p;
   }
   right[rn] = '\0';
   return ln > 0 && rn > 0;
}

static bool split_first_ws(const char *code,
                           char *left, size_t lmax,
                           char *right, size_t rmax)
{
   size_t ln = 0;
   size_t rn = 0;
   const char *p = code;

   while (*p && *p != ' ' && *p != '\t')
   {
      if (ln + 1 >= lmax)
         return false;
      left[ln++] = *p++;
   }
   left[ln] = '\0';
   if (*p == '\0')
      return false;
   while (*p == ' ' || *p == '\t')
      p++;
   if (*p == '\0')
      return false;
   while (*p)
   {
      if (rn + 1 >= rmax)
         return false;
      right[rn++] = *p++;
   }
   right[rn] = '\0';
   return ln > 0 && rn > 0;
}

static bool pair_from_segment_lengths(size_t la, size_t lv,
                                      size_t *addr_len, size_t *val_len)
{
   if (la == 6 && lv == 2) { *addr_len = 6; *val_len = 2; return true; }
   if (la == 6 && lv == 4) { *addr_len = 6; *val_len = 4; return true; }
   if (la == 8 && lv == 4) { *addr_len = 8; *val_len = 4; return true; }
   if (la == 8 && lv == 2) { *addr_len = 8; *val_len = 2; return true; }
   if (la == 6 && lv == 8) { *addr_len = 6; *val_len = 8; return true; }
   if (la == 8 && lv == 8) { *addr_len = 8; *val_len = 8; return true; }
   return false;
}

static bool parse_digits_pair(const char *addr_digits, size_t addr_len,
                              const char *val_digits, size_t val_len,
                              uint32_t *addr_out,
                              uint32_t *val_out,
                              uint8_t *size_out)
{
   size_t i;
   uint32_t addr = 0, val = 0;

   for (i = 0; i < addr_len; i++)
      addr = (addr << 4) | (uint32_t)hex_digit(addr_digits[i]);
   for (i = 0; i < val_len; i++)
      val = (val << 4) | (uint32_t)hex_digit(val_digits[i]);

   *addr_out = addr & 0x00FFFFFFu;
   *val_out  = val;
   *size_out = (uint8_t)(val_len / 2);
   return true;
}

bool cheat_parse_one(const char *code,
                     uint32_t *addr_out,
                     uint32_t *val_out,
                     uint8_t *size_out)
{
   char buf[CHEAT_PARSE_MAX_HEX + 1];
   size_t n = 0;
   size_t addr_len, val_len;
   uint32_t addr = 0, val = 0;
   size_t i;

   if (!code || !addr_out || !val_out || !size_out)
      return false;

   while (*code == ' ' || *code == '\t')
      code++;

   if (!hex_only_strip(code, buf, sizeof(buf), &n))
      return false;

   /* Two-field form (10 hex digits total): disambiguate 6+4 vs 8+2.
    * Use first whitespace run, else last ':', '-', or '.' between fields
    * ("00003D00 FF", "00003D00:FF", "0000:3D00:FF") so 8+2 is not read as 6+4. */
   if (n == 10 && (has_ascii_ws(code) || strpbrk(code, ":-.") != NULL))
   {
      char left[96];
      char right[96];
      char addr_digits[CHEAT_PARSE_MAX_HEX + 1];
      char val_digits[CHEAT_PARSE_MAX_HEX + 1];
      size_t la;
      size_t lv;
      bool split_ok;

      if (has_ascii_ws(code))
         split_ok = split_first_ws(code, left, sizeof(left), right, sizeof(right));
      else
         split_ok = split_last_cd_sep(code, left, sizeof(left), right, sizeof(right));

      if (!split_ok)
         return false;
      if (!hex_only_strip(left, addr_digits, sizeof(addr_digits), &la))
         return false;
      if (!hex_only_strip(right, val_digits, sizeof(val_digits), &lv))
         return false;
      if (!pair_from_segment_lengths(la, lv, &addr_len, &val_len))
         return false;
      return parse_digits_pair(addr_digits, addr_len,
                               val_digits, val_len,
                               addr_out, val_out, size_out);
   }

   /* Layout:  address_hex + value_hex (concatenated after separator strip).
    * For 10 digits with no field boundary, use 6+4 (PAR short-address + word). */
   switch (n)
   {
      case  8: addr_len = 6; val_len = 2; break;  /* 6 + byte  */
      case 10: addr_len = 6; val_len = 4; break;  /* 6 + word (contiguous) */
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
      if (list->entries[i].tag == index)
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

   if (!list)
      return;

   cheat_list_remove_index(list, index);
   if (!enabled)
      return;

   if (!code)
      return;

   p = code;
   start = p;
   for (;;)
   {
      if (*p == '+' || *p == '\n' || *p == '\r' || *p == '\0')
      {
         size_t len = (size_t)(p - start);
         /* Reject oversized segments outright (do not truncate and parse). */
         if (len > 0 && len <= CHEAT_SEGMENT_INPUT_MAX &&
             list->count < CHEAT_MAX_ENTRIES)
         {
            char tmp[CHEAT_SEGMENT_TMP_SIZE];
            cheat_entry_t c;

            memcpy(tmp, start, len);
            tmp[len] = '\0';
            if (cheat_parse_one(tmp, &c.address, &c.value, &c.size))
            {
               c.tag     = index;
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
