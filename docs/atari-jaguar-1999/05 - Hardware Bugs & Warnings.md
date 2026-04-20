Hardware Bugs & Warnings WHardware Bugs & Warnings The following sections describe known bugs in the operation of the Jaguar hardware. Side-effects of these bugs should not be relied on, as they may be fixed in future versions of the hardware. 

**==> picture [28 x 20] intentionally omitted <==**

**----- Start of picture text -----**<br>
Pagel<br>**----- End of picture text -----**<br>


1) The scoreboard mechanism does not work on Although this code doesn't make much sense, it the data of any indexed store instruction. This might appear at the end of a loop as shown below: means that any indexed store instruction that stores data from a long latency operation (such as a diloop: vide or external load) should place an ‘or instrucjr EO, loop tion prior to the store. For example: div r2,r4 div r0,xr3 SUTRERERTTTETTT TET TE TET TTT TERT Tai aaae store r3,(rl4+6) Any number of instructions could ; appear here. Unless one of them reads should be written as:; >; R4,unreliable.the result of the MOVEQ will be SELGRERERTTATRTT TTT ETE T TTT TTT aaa ae 

div r0,r3 (yy) orstore **r3,** r3(r14+6) moveq #4,r4 In this case, when the loop condition fails, the 2) In any instruction where the destination register DIV/MOVEQ instruction sequence will occur and is written to without being read, the destination register R4 will be corrupted. This can be register will not be protected by the scoreboarding prevented by causing the destination register to be mechanism of the GPU/DSP. This includes MTOI, read prior to the move as is shown in the following NORMI, RESMAC, all MOVE variations, and all example: LOAD variations. loop: 

If one of these destination write-only instructions ir EQ, loop writes to the same destination register as a prior div r2,r4 instruction and there have been no intervening reads from that register, it is possible for the or r4,r4 second instruction to complete before (or moveq #4, r4 simultaneously with) the first, causing the register PI hat th . to become corrupt. This bug only becomes a . she note that t anne illustrate one parproblem when doing ‘dummy’ instructions as whi i sequence ( 17M Q). Any instruction shown in the following example: w ic writes to a register followed later in the instruction stream by a ‘destination write-only’ div r2,xr4 ; Divide starts instruction with no intervening reads of that ; (takes 18 ticks) register is unreliable. Ww moveq #4,x4 ; Move completes ; before divide In practice,: this. creates two cases. If- a DIV or LOAD instruction is used to write to a register, a read of that register must be inserted prior to any 

26 April, 1995 

© 1994 Atari Corp. 

Confidential Information PO® Property of Atari Corporation 

Page 2 Hardware Bugs & Warnings ‘destination write-only' instruction that writes to 6) The DSP and the GPU must not be stopped by — am the same register. an external processor writing directly to the Hs . In addition, any instruction which writes its result D_CTRLshould turnor off G_CTRL the GPU,registers.and onlyOnlythe DSPthe GPU should into a register and is immediately followed by a turn off the DSP. ‘destination write-only' instruction which writes to the same register wil] also corrupt the register. If one processor wants to shut down another one, This effect is shown in the example below: the best way is to ask them to do it to themselves. For example, place a special code into a loop: semaphore and then cause an interrupt for the 5r ° EQ, loop processor you want to shut down. The interrupt add r10,r12 handler would see the semaphore and shut down moveq #1,rl2 ; ADD will trash this the processor itself. You should also note that a ‘dummy’ instruction | sequence, as shown above, is rare. In normal 7) The DSP must not do an external write unless it program code where the result of a register write is 3S preceded by an external read that will complete used, the bug does not occur. This is illustrated in __ for the write starts. This problem is intermittent the following example: and could be missed by testing. Be careful in any { DSP code that writes to external memory. q load (r2),r4 add r4,r6 Example #1: | | moveq #4,r4 ; Safe because R4 was load (rl) ,r2 A 4 j read above or r10,ril “a : store rll,(r3) ' 1 3) Neither the DSP or the GPU will reliably 4 et ots i: . Example #2: : execute ‘jr’ or jump’ instructions unless they are load (r1) ,r2 F in internal RAM. or r2,rll ; store ril,(r3) | : 4) The in hi iority. The P*OMPICHload (rl),xr2 a|i Otherwise,DAREN itordoing an_FLAGSexternal[shouldatwaysbe0..] load or store will or r2,r2 ‘ cause the DSP to hang, needing a reset to recover. or rl0,rll i store ril,(r3) | 5) The GPU and blitter may not be used in high Example #1 will not work correctly but example ' bus priority while the object processor is running. —_42 wil]. This is because the result of the load is re: The DMAEN bit of G_FLAGS should be 0, and quired for the or operation to be performed. To 1 ‘ the BUSHI bit of B_CMD should be 0. make example #1 work change it to example #3. a ' No bus master may operate at a higher priority | " than the object processor. If something else gets 8) The value in the High Data Register inthe GPU @ ’ the bus between the second and third phrases of an #8 changed after ANY external load, not just a. § object header, then the line buffer address can be loadp. This means that if an interrupt in running in QA q corrupted, causing horizontal black stripes and the GPU that loads from external memory the ei : possibly other artifacts in the display. underlying program may not use loadp. Py : 26 April, 1995 Confidential Information “70% Property of Atari Corporation © 1994 Atari Corp. 2a 

Page 3 

| 

| j 

Hardware Bugs & Warnings } WG9) There is a bug in the divider of the GPU and changed in the following two instructions because DSP. If you try to do two consecutive divides of pipe-lining effects. If you are going to use the without there being at least 1 clock cycle of idle flags set by a STORE instruction, or are changing time between them, then the result of the second one of the other bits such as the register bank, then divide will be wrong. ensure that there are two NOP instructions after the STORE to either of these registers. This will only occur when the two divides are separated by less than 16 clock cycles, and the . second divide as the quotient of the first divide as | one of its register operands, and there is no scoreboard dependency on the quotient of the first one i prior to the second. 

The work-around should be to either make sure that more than 16 clock cycles occur between divide instructions, or make sure that an instruction which is dependent on the quotient of the first divide occurs before the second divide. 

| Example #1: div r0,rl div r5,rl Ww moved #3,xr5 should be like this: div r0,rl : moved #3,x5 / or rl,rli div r5,r1 Example #2: div r0,rl moveg #3,x5 div r5,r1l should be like this: div x0,ril moved #3,r5 or rl,ri div r5,xr1 

10) DSP matrix multiplies only work in the lower 4K of DSP RAM. The DSP matrix register can only point to memory locations in the first 4K of DSP RAM. Only address lines 2-11 are programmable; the rest of the matrix address is hard-wired Wy to $F1Bxxx. 

, 

11) When you write a value to the G_FLAGS or D_FLAGS registers, it may not appear to have © 1994 Atari Corp. Confidential Information FPR Property ofAtari Corporation 

26 April, 1995 

. Page 4 Page 4 4 

. Page 4 Page 4 4 - Hardware Bugs & Warnings i BlitterBugs@ Warnings ——“<i“‘<‘<;<;<COM”! 

| 1 | | ; 4 = 4 4 * q ; 7 a g a a a | } 3 q . © 1994 Atari Corp. 1 

3 

3) If Al_CLIP x is not on a phrase boundary, then | clipping occurs on the right side even if the | Al_CLIP bit is not set. This applies to the 4 destination even if the DSTA2 bit of the B_LCMD register is set. |q To avoid this problem, set Al_CLIP to 0 if not | clipping, and when using DSTA2 make sure the | source is an even phrase width. i 4) Unaligned blits in 2 bit per pixel mode are not i reliable. Use 1 bit per pixel blits instead. ' 5) If Z-buffer operation is enabled and the : ADDDSEL or SRCSHADE bits are set, then the i data is sometimes corrupted. i To work around this, break the operation into two work around this, break the operation into two around this, break the operation into two this, break the operation into two break the operation into two the operation into two operation into two into two two i blits, one to do the SRCSHADE or ADDDSEL one to do the SRCSHADE or ADDDSEL to do the SRCSHADE or ADDDSEL do the SRCSHADE or ADDDSEL the SRCSHADE or ADDDSEL SRCSHADE or ADDDSEL or ADDDSEL ADDDSEL 4 into an offscreen buffer, an offscreen buffer, offscreen buffer, buffer, and then a second one to then a second one to a second one to second one to one to to i perform the Z-bufter operation onto the screen. Z-bufter operation onto the screen. operation onto the screen. onto the screen. the screen. screen. 

1) The Y add control bits in the Al and A2 address generators in the blitter are not differentiated between properly. The A2 Y add control bit is ignored. The Al Y add control bit affects both address generators. However, if the Y sign bits are set in either address, the corresponding add control bit has to be set for the number to be negative. | Either do not use this function, or use it on both : address generators. | 

2) SRCSHADE only works if the GOURZ bit is set. No actual Z-buffer data needs to be calculated or written, but GOURZ must be set. 

To work around this, break the operation into two work around this, break the operation into two around this, break the operation into two this, break the operation into two break the operation into two the operation into two operation into two into two two blits, one to do the SRCSHADE or ADDDSEL one to do the SRCSHADE or ADDDSEL to do the SRCSHADE or ADDDSEL do the SRCSHADE or ADDDSEL the SRCSHADE or ADDDSEL SRCSHADE or ADDDSEL or ADDDSEL ADDDSEL into an offscreen buffer, an offscreen buffer, offscreen buffer, buffer, and then a second one to then a second one to a second one to second one to one to to perform the Z-bufter operation onto the screen. Z-bufter operation onto the screen. operation onto the screen. onto the screen. the screen. screen. 

a 

26 April, 1995 

Confidential Information “FPR Property ofAtari Corporation 

## b W@bject Processor Bugs & Warnings 

1) It is possible for the last column of pixels of a RMW (Read-Modify-Write) object to be corrupted if it is followed by another bitmap object. This will happen on the right side unless the REFLECT | _ bit is set, in which case it will happen on the left side. 

oe 

To work around this problem, you can ensure that the last pixels of the source data are all transparent (i.e. pad the object data). Or you can make sure that the next object in the object list will not appear on the same scanlines as the RMW object. Or you can place an always-false branch object after the RMW object. 

2) Setting the VSCALE field of a scaled bitmap __ will fail. As documented, values as high as 7.1F | object to a value greater than 7.0 (%111.00000) y @ (9111.11111) may be used with the HSCALE field. | 3) Setting the HSCALE field of a 24-bit scaled | bitmap object to any value other than 1.0 will | cause the object to be distorted. 

## | 

## Y 

ooo © 1994 Atari Corp. Confidential Information “JPR Property ofAtari Corporation 26 April, 1995 1995 

26 April, 1995 1995 7 

t RR 7 | | | ; | 1 1 

Hardware Bugs & Warnings 

Page 6 

1 a 3 i S| a ‘ 3 | | a P| 1 g , | Bg _ | ¥ : ' : _ **j** &e P| ; § ; | © 1994 Atari Corp. J 

; 

| registers and internal RAM. and internal RAM. internal RAM. RAM. | The address ranges with this restriction are | $F02000 to $FO7FFF and $F1A000 to | $F1F000. These instructions may be safely | used on memory addresses outside these ranges. 4 Because the 68000 has a 16-bit data bus, 32-bit i writes to memory actually occur as two separate : 16-bit writes which happen in succession. With ji certain instructions such as those shown above, : the order in which the high word and low word : are written is reversed, which causes problems : when writing to these address ranges. While these are the only ones we know only ones we know ones we know we know know about ai i present, it is is possible there are other other | instruction/address mode combinations that mode combinations that combinations that that : have this problem. this problem. problem. The best way around best way around way around around it is to is to to ; use the GPU and/or DSP instead of the 68000 the GPU and/or DSP instead of the 68000 GPU and/or DSP instead of the 68000 and/or DSP instead of the 68000 DSP instead of the 68000 instead of the 68000 of the 68000 the 68000 68000 : when you want to write to Jaguar GPU/DSP you want to write to Jaguar GPU/DSP want to write to Jaguar GPU/DSP to write to Jaguar GPU/DSP write to Jaguar GPU/DSP to Jaguar GPU/DSP Jaguar GPU/DSP GPU/DSP 4 26 April, 1995 1995 Confidential Information Information 

## Miscellaneous Hardware Bugs & Warnings 

1) There is a bug in the Jaguar UART. If a start registers, and to use the blitter when you want bit is detected at a certain phase in the UART’s to copy information into GPU or DSP RAM. divide by 16 timer, it will be shifted in twice, resulting in a left shift of the data byte. If you are using a high-level language compiler, make sure that it does not generate cir.| The problem may be avoided by preceeding problem may be avoided by preceeding may be avoided by preceeding be avoided by preceeding avoided by preceeding by preceeding preceeding a instructions for code that accesses this address data packet with with a dummy dummy byte where where the MSB MSB space. 

The problem may be avoided by preceeding problem may be avoided by preceeding may be avoided by preceeding be avoided by preceeding avoided by preceeding by preceeding preceeding a data packet with with a dummy dummy byte where where the MSB MSB is set (e.g. $80). The receiver code should discard this dummy byte. Subsequent bytes should be exactly aligned (i.e. 2, 3, or 4 stop bits exactly, before the next start bit). This will result in causing the falling edge of the next start bit to miss the phase ofthe UART counter which caues the problem. 

If a gap is left after a byte which is more than 2 bit times long, or is not exactly aligned with the previous byte, then the dummy byte must be retransmitted (to align the UART counter again). 

2) The clr.1 <ea> and move.| <ea>,-(an) instructions of the 68000 do not work correctly when writing to Jaguar GPU & DSP hardware registers and internal RAM. and internal RAM. internal RAM. RAM. 

While these are the only ones we know only ones we know ones we know we know know about ai present, it is is possible there are other other instruction/address mode combinations that mode combinations that combinations that that have this problem. this problem. problem. The best way around best way around way around around it is to is to to use the GPU and/or DSP instead of the 68000 the GPU and/or DSP instead of the 68000 GPU and/or DSP instead of the 68000 and/or DSP instead of the 68000 DSP instead of the 68000 instead of the 68000 of the 68000 the 68000 68000 when you want to write to Jaguar GPU/DSP you want to write to Jaguar GPU/DSP want to write to Jaguar GPU/DSP to write to Jaguar GPU/DSP write to Jaguar GPU/DSP to Jaguar GPU/DSP Jaguar GPU/DSP GPU/DSP 26 April, 1995 1995 Confidential Information Information “70% Property of Atari Corporation 

