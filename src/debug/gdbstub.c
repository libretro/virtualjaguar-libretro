#include "gdbstub.h"
#include <string.h>
#include <stdio.h>

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

void GDBSessionInit(struct GDBSession *s, const struct GDBTargetOps *ops68k,
                    void *user68k)
{
   int i;

   for (i = 0; i < GDB_NUM_TARGETS; i++)
   {
      s->ops[i]  = NULL;
      s->user[i] = NULL;
   }

   s->ops[GDB_TGT_68K]  = ops68k;
   s->user[GDB_TGT_68K] = user68k;
   s->noAckMode = 0;
   s->threadG   = 1;
   s->threadC   = 1;
}

void GDBSessionSetTargetOps(struct GDBSession *s, int target,
                            const struct GDBTargetOps *ops, void *user)
{
   if (target < 0 || target >= GDB_NUM_TARGETS)
      return;

   s->ops[target]  = ops;
   s->user[target] = user;
}

/* Resolves an RSP thread number (1-based; 0 or -1 mean "any", treated as
 * thread 1 -- this stub never runs more than one processor's worth of
 * "current thread" logic at a time) to a target index. */
static int GDBThreadToTarget(int threadId)
{
   if (threadId <= 0)
      return GDB_TGT_68K;
   if (threadId > GDB_NUM_TARGETS)
      return GDB_TGT_68K;
   return threadId - 1;
}

static int GDBCopyReply(const char *text, char *reply, int replyMax)
{
   int len = (int)strlen(text);

   if (len > replyMax)
      return 0;

   memcpy(reply, text, (size_t)len);
   return len;
}

/* qRcmd's command text arrives hex-encoded (one byte per two hex
 * digits); decode it into a NUL-terminated C string. Returns the decoded
 * length (excluding the NUL), or -1 on malformed hex / overflow. */
static int GDBHexDecodeToText(const char *hex, int hexLen, char *out, int outMax)
{
   int i, n = 0;

   if (hexLen & 1)
      return -1;

   for (i = 0; i < hexLen; i += 2)
   {
      unsigned int byte;

      if (n >= outMax)
         return -1;
      if (GDBParseHexU32(hex + i, 2, &byte) < 0)
         return -1;

      out[n++] = (char)byte;
   }

   if (n < outMax)
      out[n] = '\0';

   return n;
}

/* The inverse: plain text -> hex, for qRcmd's reply (GDB decodes and
 * prints it). Returns hex chars written, or -1 if out is too small. */
static int GDBHexEncodeText(const char *text, int textLen, char *out, int outMax)
{
   static const char hexDigits[] = "0123456789abcdef";
   int i;

   if ((textLen * 2) > outMax)
      return -1;

   for (i = 0; i < textLen; i++)
   {
      unsigned char b = (unsigned char)text[i];

      out[i * 2]     = hexDigits[(b >> 4) & 0xF];
      out[i * 2 + 1] = hexDigits[b & 0xF];
   }

   return textLen * 2;
}

/* Scans a "vCont;action[:tid];action[:tid]..." payload for whether any
 * action is a step ('s') -- if so the caller treats the whole batch as a
 * step, otherwise a continue. Good enough for this stub's halt-everyone-
 * together model: only ever one processor is actually halted, so only
 * its own action (however GDB addressed it) matters, and GDBHalt() acts
 * on behalf of whichever processor is blocked, not on a thread ID parsed
 * here. */
static int GDBVContHasStep(const char *pay, int payLen)
{
   int i;

   for (i = 0; i < payLen; i++)
   {
      if (pay[i] == ';' && i + 1 < payLen && pay[i + 1] == 's')
         return 1;
   }

   return 0;
}

int GDBHandlePacket(struct GDBSession *s, const char *pay, int payLen,
                    char *reply, int replyMax,
                    int *resumeRequested, int *resumeIsStep)
{
   int targetG;
   static const char qxferPrefix[] = "qXfer:features:read:";
   const int qxferPrefixLen = (int)sizeof(qxferPrefix) - 1;

   *resumeRequested = 0;
   *resumeIsStep     = 0;

   if (payLen <= 0)
      return 0;

   targetG = GDBThreadToTarget(s->threadG);

   if (pay[0] == '?')
      return GDBCopyReply("S05", reply, replyMax);

   /* Matches both the bare "qSupported" and the "qSupported:xxx" form. */
   if (payLen >= 10 && memcmp(pay, "qSupported", 10) == 0)
      return GDBCopyReply(
         "PacketSize=1000;QStartNoAckMode+;swbreak+;hwbreak+"
         ";qXfer:features:read+;vContSupported+",
         reply, replyMax);

   if (payLen == 15 && memcmp(pay, "QStartNoAckMode", 15) == 0)
   {
      s->noAckMode = 1;
      return GDBCopyReply("OK", reply, replyMax);
   }

   /* qC -- report the current general thread. */
   if (payLen == 2 && pay[0] == 'q' && pay[1] == 'C')
   {
      char buf[16];
      sprintf(buf, "QC%x", (s->threadG > 0) ? s->threadG : 1);
      return GDBCopyReply(buf, reply, replyMax);
   }

   /* qfThreadInfo/qsThreadInfo -- the roster is fixed and small enough to
    * report in one shot: 1=68K, 2=GPU, 3=DSP, always, whether or not GPU/
    * DSP ops are registered (an unregistered target just answers "g"/"m"
    * etc. as unsupported for its own thread, same as any other missing
    * ops function). */
   if (payLen == 12 && memcmp(pay, "qfThreadInfo", 12) == 0)
      return GDBCopyReply("m1,2,3", reply, replyMax);
   if (payLen == 12 && memcmp(pay, "qsThreadInfo", 12) == 0)
      return GDBCopyReply("l", reply, replyMax);

   /* Hg<tid> / Hc<tid> -- select the thread subsequent g/G/m/M/Z/z (Hg) or
    * c/s/vCont (Hc) apply to. Thread IDs are hex, optionally "-1" for
    * "all threads". */
   if (payLen >= 2 && pay[0] == 'H' && (pay[1] == 'g' || pay[1] == 'c'))
   {
      unsigned int tid = 0;
      int neg = 0;
      int off = 2;

      if (off < payLen && pay[off] == '-')
      {
         neg = 1;
         off++;
      }

      if (payLen - off > 0)
      {
         if (GDBParseHexU32(pay + off, payLen - off, &tid) < 0)
            return GDBCopyReply("E01", reply, replyMax);
      }

      if (pay[1] == 'g')
         s->threadG = neg ? -1 : (int)tid;
      else
         s->threadC = neg ? -1 : (int)tid;

      return GDBCopyReply("OK", reply, replyMax);
   }

   /* T<tid> -- thread-alive query (a bare hex thread id, or "-1"; not to
    * be confused with any 'q'/'Q'-prefixed tracepoint packet, which this
    * stub does not implement at all -- see "Out of scope"). All three of
    * our threads always exist for the lifetime of the session. */
   if (payLen >= 2 && pay[0] == 'T' &&
       (pay[1] == '-' || (pay[1] >= '0' && pay[1] <= '9') ||
        (pay[1] >= 'a' && pay[1] <= 'f') || (pay[1] >= 'A' && pay[1] <= 'F')))
      return GDBCopyReply("OK", reply, replyMax);

   if (pay[0] == 'm')
   {
      unsigned int addr = 0, len = 0;
      int comma = -1;
      int i, n;

      if (!s->ops[targetG] || !s->ops[targetG]->readMemory)
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

      n = s->ops[targetG]->readMemory(s->user[targetG], addr, (int)len, reply, replyMax);
      if (n < 0)
         return GDBCopyReply("E01", reply, replyMax);

      return n;
   }

   if (pay[0] == 'M')
   {
      unsigned int addr = 0, len = 0;
      int comma = -1, colon = -1;
      int i, rc;

      if (!s->ops[targetG] || !s->ops[targetG]->writeMemory)
         return 0;

      for (i = 1; i < payLen; i++)
      {
         if (pay[i] == ',' && comma < 0)
            comma = i;
         else if (pay[i] == ':' && comma >= 0)
         {
            colon = i;
            break;
         }
      }

      if (comma < 0 || colon < 0)
         return GDBCopyReply("E01", reply, replyMax);

      if (GDBParseHexU32(pay + 1, comma - 1, &addr) < 0)
         return GDBCopyReply("E01", reply, replyMax);

      if (GDBParseHexU32(pay + comma + 1, colon - comma - 1, &len) < 0)
         return GDBCopyReply("E01", reply, replyMax);

      if ((unsigned int)(payLen - colon - 1) != len * 2)
         return GDBCopyReply("E01", reply, replyMax);

      rc = s->ops[targetG]->writeMemory(s->user[targetG], addr, (int)len,
                                         pay + colon + 1, (int)len * 2);
      if (rc < 0)
         return GDBCopyReply("E01", reply, replyMax);

      return GDBCopyReply("OK", reply, replyMax);
   }

   if (payLen == 1 && pay[0] == 'g')
   {
      int n;

      if (!s->ops[targetG] || !s->ops[targetG]->readRegisters)
         return 0;

      n = s->ops[targetG]->readRegisters(s->user[targetG], reply, replyMax);
      if (n < 0)
         return GDBCopyReply("E01", reply, replyMax);

      return n;
   }

   if (payLen >= 1 && pay[0] == 'G')
   {
      int rc;

      if (!s->ops[targetG] || !s->ops[targetG]->writeRegisters)
         return 0;

      rc = s->ops[targetG]->writeRegisters(s->user[targetG], pay + 1, payLen - 1);
      if (rc < 0)
         return GDBCopyReply("E01", reply, replyMax);

      return GDBCopyReply("OK", reply, replyMax);
   }

   if (pay[0] == 'Z' || pay[0] == 'z')
   {
      int insert = (pay[0] == 'Z');
      unsigned int type = 0, addr = 0, kind = 0;
      int c1 = -1, c2 = -1;
      int i, rc;
      const struct GDBTargetOps *ops = s->ops[targetG];

      if (payLen < 3 || pay[1] < '0' || pay[1] > '4')
         return 0;

      type = (unsigned int)(pay[1] - '0');

      /* Format is fixed: "Z<type-digit>,<addr-hex>,<kind-hex>[;...]" --
       * the first comma is always right after the single type digit. */
      if (pay[2] != ',')
         return GDBCopyReply("E01", reply, replyMax);
      c1 = 2;

      for (i = c1 + 1; i < payLen; i++)
      {
         if (pay[i] == ',')
         {
            c2 = i;
            break;
         }
      }

      if (c2 < 0)
         return GDBCopyReply("E01", reply, replyMax);

      if (GDBParseHexU32(pay + c1 + 1, c2 - c1 - 1, &addr) < 0)
         return GDBCopyReply("E01", reply, replyMax);

      /* The kind field may be followed by ";cond_list"/";cmds" (agent
       * expressions / conditional breakpoints) we don't implement --
       * stop at the first ';' rather than failing the whole packet. */
      {
         int kindEnd = payLen;

         for (i = c2 + 1; i < payLen; i++)
         {
            if (pay[i] == ';')
            {
               kindEnd = i;
               break;
            }
         }

         if (GDBParseHexU32(pay + c2 + 1, kindEnd - c2 - 1, &kind) < 0)
            return GDBCopyReply("E01", reply, replyMax);
      }

      if (!ops || (insert ? !ops->insertBreak : !ops->removeBreak))
         return 0;

      rc = insert ? ops->insertBreak(s->user[targetG], (int)type, addr, kind)
                  : ops->removeBreak(s->user[targetG], (int)type, addr, kind);

      if (rc == -1)
         return 0;
      if (rc == -2)
         return GDBCopyReply("E01", reply, replyMax);

      return GDBCopyReply("OK", reply, replyMax);
   }

   /* 'c'/'s' with an optional resume-address argument (unsupported --
    * we never relocate PC on resume) are both handled the same way: no
    * immediate reply. The eventual stop reply is sent later, out of
    * band, by GDBHalt() when the processor actually halts again. */
   if (pay[0] == 'c' || pay[0] == 's')
   {
      *resumeRequested = 1;
      *resumeIsStep     = (pay[0] == 's');
      return 0;
   }

   if (payLen >= 5 && memcmp(pay, "vCont", 5) == 0)
   {
      if (payLen == 6 && pay[5] == '?')
         return GDBCopyReply("vCont;c;s", reply, replyMax);

      if (payLen >= 6 && pay[5] == ';')
      {
         *resumeRequested = 1;
         *resumeIsStep     = GDBVContHasStep(pay, payLen);
         return 0;
      }

      return 0;
   }

   if (payLen > 6 && memcmp(pay, "qRcmd,", 6) == 0)
   {
      char cmd[256];
      char text[512];
      int cmdLen, textLen, hexLen;

      cmdLen = GDBHexDecodeToText(pay + 6, payLen - 6, cmd, (int)sizeof(cmd) - 1);
      if (cmdLen < 0)
         return GDBCopyReply("E01", reply, replyMax);

      if (!s->ops[GDB_TGT_68K] || !s->ops[GDB_TGT_68K]->monitorCmd)
         return 0;

      textLen = s->ops[GDB_TGT_68K]->monitorCmd(s->user[GDB_TGT_68K], cmd,
                                                text, (int)sizeof(text));
      if (textLen <= 0)
         return GDBCopyReply("OK", reply, replyMax);

      hexLen = GDBHexEncodeText(text, textLen, reply, replyMax);
      if (hexLen < 0)
         return 0;

      return hexLen;
   }

   if (payLen > qxferPrefixLen && memcmp(pay, qxferPrefix, (size_t)qxferPrefixLen) == 0)
   {
      const struct GDBTargetOps *ops = s->ops[targetG];
      const char *rest = pay + qxferPrefixLen;
      int restLen = payLen - qxferPrefixLen;
      int colonAt = -1, commaAt = -1;
      unsigned int off = 0, len = 0;
      const char *xml;
      int xmlLen = 0;
      int chunk;
      int i;

      /* rest is "<annex>:<offset>,<length>"; we serve one document per
       * target so the annex is accepted but not inspected. */
      for (i = 0; i < restLen; i++)
      {
         if (rest[i] == ':')
         {
            colonAt = i;
            break;
         }
      }
      if (colonAt < 0)
         return GDBCopyReply("E00", reply, replyMax);

      for (i = colonAt + 1; i < restLen; i++)
      {
         if (rest[i] == ',')
         {
            commaAt = i;
            break;
         }
      }
      if (commaAt < 0)
         return GDBCopyReply("E00", reply, replyMax);

      if (GDBParseHexU32(rest + colonAt + 1, commaAt - colonAt - 1, &off) < 0)
         return GDBCopyReply("E00", reply, replyMax);
      if (GDBParseHexU32(rest + commaAt + 1, restLen - commaAt - 1, &len) < 0)
         return GDBCopyReply("E00", reply, replyMax);

      if (!ops || !ops->targetXML)
         return 0;

      xml = ops->targetXML(s->user[targetG], &xmlLen);
      if (!xml || xmlLen <= 0)
         return 0;

      if ((int)off >= xmlLen)
         return GDBCopyReply("l", reply, replyMax);

      chunk = xmlLen - (int)off;
      if (chunk > (int)len)
         chunk = (int)len;
      if (chunk > replyMax - 1)
         chunk = replyMax - 1;
      if (chunk < 0)
         chunk = 0;

      reply[0] = ((int)off + chunk >= xmlLen) ? 'l' : 'm';
      memcpy(reply + 1, xml + off, (size_t)chunk);

      return chunk + 1;
   }

   /* RSP: an empty reply means "I do not implement this packet". */
   return 0;
}
