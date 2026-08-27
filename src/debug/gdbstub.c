#include "gdbstub.h"
#include <string.h>

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

int GDBExpandRLE(const char *in, int inLen, char *out, int outMax)
{
   int i;
   int n = 0;

   for (i = 0; i < inLen; i++)
   {
      if (in[i] == '*')
      {
         int repeat;
         char prev;

         if (n == 0 || (i + 1) >= inLen)
            return -1;

         repeat = (int)(unsigned char)in[i + 1] - 29;
         if (repeat < 0)
            return -1;

         prev = out[n - 1];
         if ((n + repeat) > outMax)
            return -1;

         while (repeat-- > 0)
            out[n++] = prev;

         i++;
         continue;
      }

      if (n >= outMax)
         return -1;

      out[n++] = in[i];
   }

   return n;
}

int GDBParseHexU32(const char *s, int len, unsigned int *out)
{
   int i;
   unsigned int v = 0;

   if (len <= 0 || len > 8)
      return -1;

   for (i = 0; i < len; i++)
   {
      int d = GDBHexVal(s[i]);

      if (d < 0)
         return -1;

      v = (v << 4) | (unsigned int)d;
   }

   *out = v;
   return len;
}

void GDBSessionInit(struct GDBSession *s, const struct GDBTargetOps *ops,
                    void *user)
{
   s->ops       = ops;
   s->user      = user;
   s->noAckMode = 0;
}

static int GDBCopyReply(const char *text, char *reply, int replyMax)
{
   int len = (int)strlen(text);

   if (len > replyMax)
      return 0;

   memcpy(reply, text, (size_t)len);
   return len;
}

int GDBHandlePacket(struct GDBSession *s, const char *pay, int payLen,
                    char *reply, int replyMax)
{
   if (payLen <= 0)
      return 0;

   if (pay[0] == '?')
      return GDBCopyReply("S05", reply, replyMax);

   /* Matches both the bare "qSupported" and the "qSupported:xxx" form. */
   if (payLen >= 10 && memcmp(pay, "qSupported", 10) == 0)
      return GDBCopyReply("PacketSize=1000;QStartNoAckMode+", reply, replyMax);

   if (payLen == 15 && memcmp(pay, "QStartNoAckMode", 15) == 0)
   {
      s->noAckMode = 1;
      return GDBCopyReply("OK", reply, replyMax);
   }

   if (pay[0] == 'm')
   {
      unsigned int addr = 0, len = 0;
      int comma = -1;
      int i, n;

      if (!s->ops || !s->ops->readMemory)
         return 0;

      for (i = 1; i < payLen; i++)
      {
         if (pay[i] == ',')
         {
            comma = i;
            break;
         }
      }

      if (comma < 0)
         return GDBCopyReply("E01", reply, replyMax);

      if (GDBParseHexU32(pay + 1, comma - 1, &addr) < 0)
         return GDBCopyReply("E01", reply, replyMax);

      if (GDBParseHexU32(pay + comma + 1, payLen - comma - 1, &len) < 0)
         return GDBCopyReply("E01", reply, replyMax);

      n = s->ops->readMemory(s->user, addr, (int)len, reply, replyMax);
      if (n < 0)
         return GDBCopyReply("E01", reply, replyMax);

      return n;
   }

   /* RSP: an empty reply means "I do not implement this packet". */
   return 0;
}
