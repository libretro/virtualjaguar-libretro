#include "gdbstub.h"

static int GDBHexVal(char c)
{
   if (c >= '0' && c <= '9')
      return c - '0';
   if (c >= 'a' && c <= 'f')
      return (c - 'a') + 10;
   if (c >= 'A' && c <= 'F')
      return (c - 'A') + 10;
   return -1;
}

int GDBChecksum(const char *payload, int len)
{
   int i;
   unsigned int sum = 0;

   for (i = 0; i < len; i++)
      sum += (unsigned char)payload[i];

   return (int)(sum & 0xFF);
}

int GDBDecodePacket(const char *raw, int rawLen, char *out, int outMax)
{
   int i;
   int hashAt = -1;
   int payLen;
   int hi, lo;

   if (rawLen < 4 || raw[0] != '$')
      return -1;

   for (i = 1; i < rawLen; i++)
   {
      if (raw[i] == '#')
      {
         hashAt = i;
         break;
      }
   }

   if (hashAt < 0 || (hashAt + 2) >= rawLen)
      return -1;

   hi = GDBHexVal(raw[hashAt + 1]);
   lo = GDBHexVal(raw[hashAt + 2]);
   if (hi < 0 || lo < 0)
      return -1;

   payLen = hashAt - 1;
   if (payLen > outMax)
      return -3;

   if (GDBChecksum(raw + 1, payLen) != ((hi << 4) | lo))
      return -2;

   for (i = 0; i < payLen; i++)
      out[i] = raw[1 + i];

   return payLen;
}

int GDBEncodePacket(const char *payload, int len, char *out, int outMax)
{
   static const char hexDigits[] = "0123456789abcdef";
   int i;
   int cs;

   if ((len + 4) > outMax)
      return -1;

   out[0] = '$';
   for (i = 0; i < len; i++)
      out[1 + i] = payload[i];

   cs = GDBChecksum(payload, len);
   out[1 + len] = '#';
   out[2 + len] = hexDigits[(cs >> 4) & 0xF];
   out[3 + len] = hexDigits[cs & 0xF];

   return len + 4;
}
