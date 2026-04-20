Pagel 

Jaguar Workshop Series 

## PN 

WjaguarWorkshopSeries The Jaguar Workshop Series is designed to introduce new Jaguar developers to several basic concepts useful in creating unique multimedia applications with the Jaguar developer console. The first installment of this series is designed to introduce you to the specific steps necessary to properly initialize the Jaguar console for a very small application with very modest hardware demands. Later workshop topics will expand upon this basic application to take advantage of most of the inherent features in the Jaguar hardware and provide useful source code that you may use as 4 starting point for your own applications. The following table indicates those topics which are currently planned to be covered in this series. It is likely that we will add more in the future. The table also notes which topics have source code and which have documentation. Please keep up-to-date via our bulletin board for new topics as they become available. 

|| <br>w|#<br>SourceCode<br>Documentation<br>Topic<br>Naaeeeee<br>[Minimum Object ListUpdate<br>|_|<br>7 |Moving Bitmapwih tne ObjectProcessor—_—<br>[2 | |"<br>Cipping a Bitmap wit he Object Processor__—<br>[3_| _+_—,<br>Seatinga map wih the ObjetProcessor__<br>[=|__|<br>[sinePrimary Processor<br> S|;<br>interrunt ObjectProcessing<br>-$ | ____,——Heyatoe Reading Seroling over aLarge Objest<br>[|<br>[Copyinga tmpwit theBiter —___—<br>[8|__|<br>seating a itmap wih fheite<br>[|__|<br>Frasional tine DrawingwitfeBiter__<br>ef<br>sewing a itman wih theBiter—____—<br>|<br>oatng a tapwithteBiter<br>2 | ___,esosing a atmap wit the Biter —_—<br>3|__|PerformingLoa Operationswit theBiter___<br>3|__|<br>Fransparent Drawingwih theBiter—____<br>sf<br>character Ting with theBiter<br>[16|__|<br>brawing Monochrome Qveriayswii theBiter__<br>[|__| irinieruptProcessing<br>3|__|<br>[sto object Processing<br>ef |<br>osingJagpeg<br>38f-ing2a<br>eeee|
|---|---|



Ry 

©1994 Atari Corp. 

Confidential InformationFER Property ofAtari Corporation 

8 November, 1994 

ik 

**==> picture [548 x 99] intentionally omitted <==**

**----- Start of picture text -----**<br>
\ ™ WORKSHOP<br>a<br>i“ IA ¢ ~ SERIES<br>:<br>Copyright ©1994 Atari Corp. SS .<br>**----- End of picture text -----**<br>


Minimum Object List Update 

This application, MOU.COF, focuses on the most basic (and necessary) components of a Jaguar program, namely, the creation and maintenance of an object list that is used by the Object Processor (OP) to render screen images. 

| To follow along with this example you will need the following files included in the \JAGUAR\WORKSHOP\MOU directory: 

- # mou_init.s @ mou_list.s @ mou.inc @ makefile # jaguar.bin 

In addition I will assume that you have properly installed your developer’s toolkit and have the header files supplied by Atari in your include file directory. 

we 2 This example application will display a 16-bit CRY bitmap image (contained in JAGUAR.BIN) and do required maintenance during the vertical blanking period. The application will proceed through the following steps: 

1. Do basic hardware initialization and define a stack 

2. Copy the bitmap image to an absolute location in RAM. 

3. Initialize the video hardware. 

4. Create an object list. 

5. Define a vertical-blank interrupt handler. 

6. Turn on video and begin list processing. 

**==> picture [16 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
y<br>**----- End of picture text -----**<br>


7. Release control to the debugging stub. 

SC ©1994 Atari Corp. 

_ Confidential Information FERProperty ofAtari Corporation 8 November, 1994 

Page 2 

Minimum Object List Update 

{i 

‘ . — | a _ . - | @ — | @ | 2 ] 7 1 a 4 a (aimee + 4 i ] a ‘ q 4 j | 4 3 1 ] q q ] 3 Bi 

| | | q 

With the exception of step four, this code can be found in MOU_INIT.S. Step four is coded in MOU_LIST:S. 

MOU _INIT.S begins by including the global header file, JAGUAR.INC, and a program-specific header file named MOU.INC. These header files provide all of the constants used in the source code. The first instruction executed is as follows: 

## move.1 #$00070007,G_END 

This instruction ensures that the Graphics Processing Unit (GPU) is configured to use Motorola MSBLSB (big-endian) for its I/O registers. This line of code is required for all Jaguar programs. A similar line is required for D_END if the DSP is needed (which this sample doesn’t). 

move.w #$FFFF,VI move.l1 #stopob,d0 swap do move.1 d0,OLP 

The first line disables video interrupts and is required to prevent interrupts from occurring in the middle of your setup routines. The next lines temporarily set the current object list to be a single stop object. The next line of code you will find common to most Jaguar sample programs is: 

## move.l #INITSTACK,a7 

Most Jaguar programs will want to setup a stack. In this case, the equate INITSTACK is used. INITSTACK is defined in JAGUAR.INC to be $1FFFFC (the top longword of DRAM). 

Next, a generic subroutine, InitVideo, is called to initialize the video registers. InitVideo is capable of configuring video for any non-interlaced pixel resolution. The code for this subroutine follows: 

InitVideo: 

**==> picture [511 x 200] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||
|---|---|---|---|---|---|---|---|---|
|movem.1|d0-d6,-(sp)|
|move.w|CONFIG,d0|
|andi.w|#VIDTYPE,d0|;|0|=|PAL,|1|=|NTSC|
|beq|palvals|
|a|move.w|#NTSC_HMID,d2|;|Values|defined|in|JAGUAR.INC|
|move.w|#NTSC_WIDTH,d0|
|move.w|#NTSC_VMID,d6|
|move.w|#NTSC_HEIGHT,d4|
|bra|calc_vals|
|palvals:|
|move.w|#PAL_HMID,d2|;|Values|defined|in|JAGUAR.INC|
|©1994|Atari Corp.|Confidential Information|TER|Property ofAtari Corporation|8 November, 1994|3|

**----- End of picture text -----**<br>


Page 3 

10 

|§&|§&|MinimumObjectListUpdate|MinimumObjectListUpdate||||
|---|---|---|---|---|---|---|
|y|Ly||move.w|#PAL_WIDTH,d0|||
||:<br>|<br>@<br>=<br>'|calc_vals:|move.w<br>move.w<br>move.w<br>move.w|#PAL_VMID,d6<br>#PAL_HEIGHT,d4<br>da0,width<br>4d4,height|; <br>+|Width of screen in clocks<br> Height of screen in half-lines|
||||move.w<br>asr|d0,dl<br>#1,dal|;|Width/2|
||fj||||||
||'<br>|||sub.w<br>add.w|dl,d2<br>#4,d2|; <br>;|Mid - Width/2<br> (Mid - Width/2)+4|
|||||sub.w<br>ori.w|#1,dl<br>#$400,d1|; <br>;|Width/2 - 1<br> (Width/2 - 1)|$400|
||||move.w|dl,a_hde|||
||t||move.w|d1,HDE|||
||||move.w|d2,a_hdb|||
||||move.w|42,HDB1|||
||a||move.w|d2,HDB2|||
|- 7<br>y<br>ij|||move.w<br>sub.w<br>move.w|4d6,d5<br>44,d5<br>45,a_vdb|||
|||||add.w|4,d6|||
||||move.w|d6,a_vde|||
||:||||||
||||move.w<br>move.w|a_vdb,VDB<br>#$FFFF,VDE||; REQUIRED!!!|
||||move.1<br>move.1l|#0,BORD1<br>#0,BG||; Black Border<br>; Black Background|
||||movem.1<br>(sp)+,d0-d6||||
|.|||rts||||



* 

This routine first determines whether the console is a NTSC or PAL machine and loads four registers with pre-defined values for the right console type. The variables width and height are then loaded with two of those constants describing the width of the screen in pixel clocks and the height of the screen in pixels. . 

To obtain the actual horizontal resolution of the screen in pixels, we must first choose a pixel divisor. The following table lists the available pixel divisors and the approximate resulting overscanned and nonoverscanned resolutions: 

**==> picture [6 x 25] intentionally omitted <==**

**----- Start of picture text -----**<br>
s<br>**----- End of picture text -----**<br>


©1994 Atari Corp. 

Confidential Information TR Property ofAtari Corporation 

8 November, 1994 

Page 4 

Minimum Object List Update 

: : ; : | : | ; 

: 

: ¥ z | | : 1 : : j q q F 

| 

| 

| 

**==> picture [267 x 28] intentionally omitted <==**

**----- Start of picture text -----**<br>
Pixel Divisor Non-Overscanned Overscanned<br>Pt 0841830<br>**----- End of picture text -----**<br>


, 

Most of the workshop examples (including this one) will use a pixel divisor of four. This mode yields the closest approximation to square pixels and gives us plenty of pixels to work with. Whenever we need to know the width of our screen in pixels, the following formula may be used: 

pixel width = —______widt - pixel divisor 

Computing the vertical height of the screen is even easier. The height variable, set by our video initialization subroutine, is in already in pixels. The last lines of the video initialization sets the video border and background colors. The border color is the color used on those parts of the screen outside of the displayable region. When overscanning, this color does not matter. You should note that the BORD1 and BORD2 registers specify a color in 24-bit RGB. By setting both registers (using a longword write) to zero in our sample code we make the border black. 

If the BGEN bit (#7) is set in the Video Mode register (we’ll do this later), the line-buffer is initialized to the color specified in the BG register at the beginning of every scanline. This only has an effect in RGB16 or CRY16 mode and the contents of BG will be a CRY or 16-bit RGB color pixel depending upon the mode you’re in. This example will use 16-bit CRY mode but since we’re setting it to black, zero will work in either mode. 

Jaguar video display is accomplished using an object list. The object list is consulted by the Object Processor at the start of every horizontal scanline to determine what needs to be drawn. As the screen is drawn and each scanline is successively rendered, certain parts of the object list are destroyed. For this reason, the object list must be updated during each vertical blank. Generally, you should save copies of the phrases which will get destroyed when you first create the list, then you can simply restore those fields from the saved copies. 

The object list in this example is the minimum necessary to generate a display. It is arranged as follows: 

©1994 Atari Corp. 

Confidential Information FR Property ofAtari Corporation 

8 November, 1994 

Page 5 

} | 

| | 

**==> picture [537 x 386] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||
|---|---|---|---|---|---|
|B|Minimum|Object List Update|
|Phrase|Object Type|Description|
|i|1|Branch|This object causes a branch to the Stop object|if the|VC register|
|:|pointswhich pastis currently the visible being screen. prepared The for VC display. registerIts contains the value|is specified line|in|
||4|half-lines.|
|2|This object causes a branch to the stop object|if the|VC register|
|points before the beginning|of the visible screen.|
|i]|
|Bitmap|This object contains the data for the Jaguar logo we want to display|
|=|3&4|||on screen. Bitmap objects|take two phrases (16 bytes) and must be|
|&|||double-phrase aligned.|
|rs|Stop|This object ends object list processing for the current scan-line.|
|@|The first two branch objects simply skip the rest of the list and|jump straight to the stop object if the|
|®|vertical region being updated is outside of the area we want to be visible. This is a required component|
|of|every object list you set up. Because of a bug in the Jaguar chipset, the OP must run every scanline|
|}|(this is done by setting a_vde to $FFFF in the video initialization).|Please trust us on this, bad things will|
|||happen in the system|if you ignore this step.|
|Bs|The bitmap object is responsible for the display of the Jaguar logo. The stop object simply terminates list|
|||processing for the current scan-line.|
|Bae|Me sample code places the object list into a buffer referenced by the label main_obj_list. The buffer is|
|1|a|where the list is first created and where it will be updated during every vertical-blank.|
|The subroutine InitLister builds the initial copy of the object list in the buffer main_obj_list. The|
|subroutine begins|as|follows:|

**----- End of picture text -----**<br>


**==> picture [531 x 313] intentionally omitted <==**

**----- Start of picture text -----**<br>
movem.1 dil-d5/a0,-(SP)<br>lea InitLister,a0<br>move.1 a0,d2<br>add.l  #(LISTSIZE-1)*8,d2<br>Register A0.1 will be used as a roving list pointer which will be advanced as each phrase of the list is<br>written. D2.1 is initialized with this code to contain a pointer to the stop object. This pointer will be<br>needed for constructing each object in the list.<br>Throughout the entire routine, D1.1 and DO.! will be used to temporarily hold the high and low long of<br>the phrase being constructed. The first object to be written is a branch object. To review, a branch object<br>is arranged as follows:<br>Branch Object<br>63 55 47 39 31 23 15 7 0<br>w i eae aaa naan Cae eeee<br>©1994 Atari Corp. Confidential Information FRProperty ofAtari Corporation 8 November, 1994<br>**----- End of picture text -----**<br>


**==> picture [2 x 30] intentionally omitted <==**

**----- Start of picture text -----**<br>
‘<br>**----- End of picture text -----**<br>


8 November, 1994 

Page 6 

Minimum Object List Update 

| J ; F j ; : ] j 

: 

| 

“ 

. q : 4 j : ‘ 4 4 4 | q ; ; 1 { : 4 4 

j | | 

| 

: 

We will start by initializing D1 and DO to contain the object TYPE, CC (condition code), and LINK fields as follows: 

elr.1 dl move.1 #BRANCHOBJ|O_BRLT,d0 jsxr format_link 

The branch object only branches if a specified condition is met. This condition is encoded in the CC field of the object. The following table lists the five possible condition codes: 

**==> picture [343 x 83] intentionally omitted <==**

**----- Start of picture text -----**<br>
Equate CC Description<br>O_BREQ | QO | Branch if YPOS == VC or YPOS == $7FF.<br>O_BRGT Branch if YPOS > VC.<br>O BRLT |2_| Branch [if]  YPOS < VC.<br>O_BROP | 3 | Branch if the Object Processor Flag (OBF) is set.<br>O<br>BRHALF | 4 | Branch if on second half of display line (HC & 1 == 1).<br>**----- End of picture text -----**<br>


The last line calls a subroutine which takes the address we previously stored in D2.] and transforms it as necessary to place it in the LINK field of the phrase. The LINK field indicates the address of the next object to process if the branch condition is met. If the branch condition is not met the next object in the list is processed. The format_link subroutine is as follows: 

format_link: 

movem.1 d2-d3,-(sp) 

andi.1l #S3FFFF8,d2 ; Ensure alignment move.1l 4d2,d3 : Make a copy swap a2 : Equivalent to << 21 clr.w 4d2 1lsl.1 #5,d2 lsr.1 #8,d3 ; copy >> 11 lsr.1 #3,d3 or.1 a3,di 

movem.1 d2-d3,-(sp) . rts 

The only remaining field of the branch object that has not been filled in is the YPOS field. We want the branch object to branch if the VC register is past the end of the visible screen. To do this, the YPOS field is initialized with the same value the VDE register was initialized with. This value was stored ina variable called a_vde by the InitVideo routine. The following code retrieves this value, shifts it into po place and stores it. Next, the phrase is stored into the buffer. 

move.w a_vde,d3 ; YPOS = a_vde lsl.w #3,a3 : Shift to bits 13-3 or.w a3,d0 ; Store it Confidential Information TER Property ofAtari Corporation 

8 November, 1994 4 

©1994 Atari Corp. 

| Minimum Object List Update - move.l dl,(a0)+ ; Store the phrase move.l d0,(a0)+ ; in the list buffer / The next phrase is written in a similar manner. First, the CC and YPOS fields are stripped from the last | phrase. This branch object will branch if VC hasn’t reached the top of visible screen yet so YPOS will be set to a_vdb and CC will be set to YPOS > VC. The code follows: 

Page7 

i j 5 

: 

| : 

andi.l1 #$FF000007,d0 ; Mask away YPOS and CC ori.l #0_BRGT,d0 3; YPOS > vc move.w a_vdb,d3 3 YPOS = a_vdb lsl.w #3,d3 : Make it bits 13-3 or.w d3,d0 move.l di,(a0)+ ; Store second branch object move.l1 d0,(a0)+ 

| The next object that needs to be written to the list buffer is the bitmap object. Bitmap object require two phrases of space and must be double-phrase aligned. Since our entire list is double-phrase aligned with | the ‘.dphrase’ statement and the bitmap object will be preceded with two phrases of branch objects we ‘jm can be sure that the bitmap object will be properly aligned. The two phrases of a bitmap object are r arranged as follows: 

**==> picture [519 x 192] intentionally omitted <==**

**----- Start of picture text -----**<br>
Bitmap Object<br>63 55 47 39 31 23 15 7 0<br>| DATA Pointer (Bits 23-3) | _UNK [Pointer][ (Bits]  21-3) [|] HEIGHT ___YPOSTYPE<br>63 55 47 39 31 23 15 7 0)<br>Unused = FIRSTPIX, INDEX, WIDTH = OWIDTH, XPOS<br>RELEASE- ~~ REFLECT PITCH--- ~~ DEPTH<br>TRANSPARENT —- -— RMW<br>To begin processing the bitmap object, the temporary phrase storage registers must be cleared and the<br>: address of the stop object must be stored in the LINK field as follows:<br>**----- End of picture text -----**<br>


clr.l dl clr.1 do jsx format_link 

ul The LINK field of a bitmap object contains the address of the next object to be processed. Because the "address of the stop object remains in D2, a subroutine call to format_link is all that is necessary. You - should note that the TYPE field does not need to be filled in because the bitmap object TYPE code is 0. ae ©1994 Atari Corp. Confidential Information TER Property ofAtari Corporation 8 November, 1994 

**==> picture [2 x 23] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


Page 8 

Minimum Object List Update 

j | : j Pk a Po i | 4 , : j 1 1 | 4 : 1 : E | 4 q | : _ : | | | q ; j 4 1 | : 

| 

} 

| 

: 

The next field to be filled in is HEIGHT. This field simply specifies the height of the bitmap in pixels. The sample code that follows takes the equate BMP_HEIGHT (defined in MOU.INC), shifts it into place, and stores it in our temporary phrase: move.l #BMP_HEIGHT,d5 lsl.1 #8, d5 lsl.1 #6,d5 or.1 d5,d0 

The YPOS field of a bitmap object contains the vertical position where the bitmap will be displayed in half-lines. To center the bitmap in our example we use the following formula: 

YPOS = eee -| x2+a_vdb Because YPOS must be specified in half-lines, the pixel result must be multiplied by two to convert it. a_vdb, which is the topmost displayable scanline set by InitVideo, is already in half-lines. To simplify the code which sets YPOS below, both the division and multiplication may be removed because they cancel each other out in the equation. The constant BMP_HEIGHT is set in MOU.INC and isequalto the height of the bitmap in pixels. The result of the equation is AND’ed with $FFFE to ensure that the resulting value is even (which is required). - 

move.w height,d3 sub.w #BMP_HEIGHT,d3 add.w a_vdb,d3 andi.w #$FFFE,d3 lsl.w #3,da3 or.w a3,d0 , 

| lsl.w #3,da3 | or.w a3,d0 , The last field in the first phrase that needs to be completed is the DATA field. This field will contain a pointer to our sample bitmap. For this example, the bitmap image is left in ROM (the Alpine board) and its address is assigned to the label jagbits by the linker. Under most circumstances you should copy bitmaps to RAM with the Blitter prior to displaying it. ROM access speed can be up to ten times slower than RAM (in the case of fetching object data, it is)! If you try to display more than a couple of bitmaps from ROM, the Object Processor will run out of time and your display will be distorted. The only reason we don’tusea RAM copy in the first few examples is to avoid having to explore the Blitter as well as the Object Processor. 

We also expect most bitmaps to be compressed in ROM. If you have enough ROM space to leave your bitmaps uncompressed then you should instead compress your bitmaps and enhance your game by adding a level, more music, etc.. 

You should note that the DATA field only encodes bits 23-3 of the bitmap address. Bits 2-0 aren’t needed because the bitmap must be phrase-aligned. The following code forces the bitmap address tobe phrase-aligned, shifts it into place, and stores it (note: if the bitmap isn’t really phrase-aligned, it will just look funny on screen): 

ee ©1994 Atari Corp. Confidential Information FER Property ofAtari Corporation 8 November, 1994 

Page 9 

& w 

## Minimum Object List Update 

move.l #jagbits,d3 . andi.l #$FFFFFO,d3 lsl.1 #8,d3 or.1 d3,d0 

In the diagram of a bitmap object presented earlier, two fields had a gray background. These fields are modified by the Object Processor as it renders scanlines. For this reason, these portions of the object list must be updated during each vertical blank. This example does the least work possible by simply storing a copy of the phrase that gets destroyed so that it may be restored during the vertical blank. In order to do this, the following code stores the first phrase of the bitmap object with a copy in the variables bmp_highl and bmp_lowl: 

move.1 di,(a0)+ move.1 d1,bmp_highl move.1 d0,(a0)+ move.1 d0,bmp_lowl 

The second phrase of a bitmap object contains more fields, however several may be set by simply OR’ing together equated values. The following code sets three fields. The TRANS bit is set causing the object processor to skip drawing pixels with the color $0000 effectively making these pixels transparent. The DEPTH field is set to O_DEPTH1G6 indicating a 16-bit-per-pixel bitmap. The PITCH field is set & : to O_NOGAP which means that there is no gap between successive phrases of the bitmap data. w move.1 #0_TRANS,d1 move.1 #0DEPTH16|O_NOGAP,d0 

The next section of code creates the XPOS field. Again, we will center the bitmap horizontally in a similar manner to how we centered it vertically. There are some key differences, however. The value in width is the number of pixel clocks in a scanline. This must first be divided by the pixel divisor to determine the true horizontal screen resolution. You should also note that XPOS = 0 begins display at HDB so there is no reason to add the horizontal display offset as we did with YPOS. The constant BMP_WIDTH comes from MOU.INC and is equal to the bitmap width in pixels. Examine the following code: 

move.w width,d3 ; Width in clocks lsr.w #2,da3 ; /4 Pixel Divisor sub.w #BMP_WIDTH,d3 ; - BMP WIDTH isr.w #1,0a3 : /2 to center it or.w d3,d0 ; Store it 

## The last fields that must be set are IWIDTH and DWIDTH. IWIDTH contains the actual image width in phrases. DWIDTH contains the width (also in phrases) of the image to display. For now, these fields should be set to the same value. A later example will examine hardware clipping using these fields. & w The following code sets the IWIDTH and DWIDTH fields to the constant BMP_PHRASES (defined " “ in MOU.INC) and stores the second phrase of the bitmap object: 

**==> picture [2 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
]<br>**----- End of picture text -----**<br>


a©1994 Atari Corp. Confidential Information FER Property ofAtari Corporation 8 November, 1994 

Page 10 

Minimum Object List Update 

a | & | @ E z =. | @ ¥ | = | i a 1 

' = }| 

| 4 | 4 ; " 

‘ 

| q : i q 1 { 

| 

| | | 

1 

7 

| 

move.l1 #BMP_PHRASES,d4 move.l d4,d3 Isl.1 #8,d4 ; DWIDTH 1sl.1 #8,d4 lsl.1 #2,d4 or.1 a4,da0 lsl.1 #8,d4 ; IWIDTH Bits 31-28 lsl.1 #2,d4 or.1 d4,do lsr.1 #4,d3 ; IWIDTH Bits 37-32 or.1 d3,dl move.1 dl,(a0)+ ; Store phrase move.1 d0,(a0)+ . 

The last object that is required in the object list is the stop object. The stop object is written as follows: 

clr.1l di move.1 #(STOPOBJ|O STOPINTS) , dO move.l di,(a0)+ move.l1 d0,(a0)+ 

Besides the object TYPE field, the equate O_STOPINTS allows CPU stop object interrupts to be processed (if we enable them later). 

To complete the InitLister subroutine, the address of the list buffer is reloaded, word-swapped (the pointer to the object list must be word-swapped) and returned in DO as shown by the following code: : move.1 #main_obj_list,d0 swap do movem.1 (sp)+,d1-d5/a0 rts 

The final subroutine called by the initialization segment is InitVBint. This routine installs the vertical blank handler, enables video interrupts, and lowers the 68000’s interrupt priority level (IPL) to actually allow CPU interrupts to occur. 

All Jaguar interrupts appear to the CPU as Level 0 Autovector interrupts. Whenever a Level 0 Autovector interrupt occurs, the vector at address LEVEL ($100) is jumped through. When more than one type of interrupt is enabled, the INT1 register must be consulted to determine what type of interrupt 

] 

| 

©1994 Atari Corp. 

Confidential Information PERProperty ofAtari Corporation 

8 November, 1994 F 

|Minimum Object List Update 

Page 11 

Pactually caused the handler to be called. In this example that step isn’t necessary because the only kind | of interrupts we’re concerned with are video interrupts. 

| The Jaguar Vertical Interrupt register (VI @ $FO004E) controls which half-scanline the vertical blank | interrupt occurs (this must be an odd value). The following code installs the 68k Autovector handler and | configures the VI register properly. 

move.1 #UpdateList,LEVELO move.w a_vde,d0 ori.w #1,d0 move.w d0,VI 

; 

The next section of code enables CPU video interrupts by setting the correct bit in INT1: 

move.w INT1,d0 ori.w #C_VIDENA,dO move.w d0,INT1 

Finally, the last section of the subroutine lowers the 68k IPL to level 0 to allow interrupts to occur. 

move.w sr,d0d andi.w #S$F8FF,d0 ; move.w d0,sr 

## | Enabling Video Processing, 

|[Only][ two][ more][ statements][are][ required][to][ enable][ the][ video][ display.][ The][ routine][InitLister][ returned][ a][ pre-] _ swapped pointer to the object list buffer in DO. This value must now be stored in the Object List Pointer (OLP @ $F00020). The final command reconfigures the video controller by correctly setting the Video Mode register (VMODE @ $F00028). Sample code follows: 

move.l1 d0,OLP move.l #CRY16|CSYNC|BGEN|PWIDTH4 | VIDEN, VMODE 

The CRY16 equate enables 16-bit CRY mode. The CSYNC equate enables output to composite sync | (which is required for television output). The BGEN equate causes the line buffer to be cleared to the background color prior to starting each scanline. The PWIDTH4 equate enables a pixel divisor of four. Finally, the VIDEN equate enables video. Please note that Jaguar video should never be tured off by not setting the VIDEN flag. 

The last instruction in our initialization is ‘illegal’. This is a brute-force way to return control to the debugger. Most applications will enter their main logic loop at this point. Please note, however, that even though the debugger regains control, interrupts will continue to occur and be serviced by our handler. 

| | | ' : 

| ©1994 Atari Corp. 

Confidential Information “PU™ Property of Atari Corporation 

8 November, 1994 

Page 12 

Minimum Object List Update 

: 

blank handler for this sample handler for this sample for this sample this sample sample is very simple. very simple. simple. It must must first restore any modified any modified modified fields in in the q it must signal must signal signal that it has handled the has handled the handled the the interrupt by using the sequence by using the sequence using the sequence the sequence sequence illustrated below: 4 i . move.l a0,-(sp) 4 move.1 #main_obj_list+BITMAPOFF,a0 move.1 bmp_highi, (a0) move.l1 bmp_lowl,4(a0) q move.w #$101,INT1 ] move.w #$0,INT2 : move.l (sp)+,a0 | rte BITMAP_OFF comes from MOU.INC and comes from MOU.INC and from MOU.INC and MOU.INC and and is the offset offset in bytes from bytes from from the beginning of the beginning of the of the the : phrase of the bitmap. the bitmap. bitmap. Because this is an an interrupt routine it must end with must end with end with with the 68k RTE 68k RTE RTE ; 4 for the sample code the sample code sample code code is provided, provided, different developers may choose developers may choose may choose choose different : environments for assembly and for assembly and assembly and and linkage. This section will only This section will only section will only will only only illustrate the command the command command line 4 MADMAC and ALN and why they were chosen. and ALN and why they were chosen. ALN and why they were chosen. and why they were chosen. why they were chosen. they were chosen. were chosen. chosen. 4 file is assembled assembled with MADMAC with the command the command command line options options ‘-fb’ and and ‘-g’. The The ; ' causes MADMAC MADMAC to output BSD format object files output BSD format object files BSD format object files format object files object files files (the type strongly recommended recommended for 14 The ‘-g’ switch causes source-level source-level information to be added be added added to the object file. J table shows the flags used with the Atari Linker ALN and their purpose: shows the flags used with the Atari Linker ALN and their purpose: the flags used with the Atari Linker ALN and their purpose: flags used with the Atari Linker ALN and their purpose: used with the Atari Linker ALN and their purpose: the Atari Linker ALN and their purpose: Atari Linker ALN and their purpose: Linker ALN and their purpose: ALN and their purpose: and their purpose: their purpose: purpose: 4 Switch Meaning V-V Enable medium-verbosity. The -v switch may be used from 4 zero to three times for increasing levels of verbosity. J l-e~~_| Output a COFF format executable. 4 lg~~—~—S—*~«<‘C«t~*«*:*CSCS Place sourrccee-leveell information in the output file. 4 rtSSS Include local as well as global symbols in the output[ file.] 4 Align each object module to a double-phrase boundary. 4 -a 802000 x 4000 Create an absolute file with the TEXT segment starting at : $802000, the DATA segment being contiguous with the TEXT segment, and the BSS segment starting at $4000. 4 -i jaguar.bin jagbits include a raw binary file named JAGUAR.BIN. The start 4 address of the file will be assigned to the label ‘jagbits’. The . end address of the label will be assigned the label ‘jagbitsx’. 19 Name the output file MOU.COF. 4 

: 

| 

| 

: | 

The vertical blank handler for this sample handler for this sample for this sample this sample sample is very simple. very simple. simple. It must must first restore any modified any modified modified fields in in the object list. Next, it must signal must signal signal that it has handled the has handled the handled the the interrupt by using the sequence by using the sequence using the sequence the sequence sequence illustrated below: 

## UpdateList: 

The constant BITMAP_OFF comes from MOU.INC and comes from MOU.INC and from MOU.INC and MOU.INC and and is the offset offset in bytes from bytes from from the beginning of the beginning of the of the the list to the first phrase of the bitmap. the bitmap. bitmap. Because this is an an interrupt routine it must end with must end with end with with the 68k RTE 68k RTE RTE instruction. 

Though a MAKEFILE for the sample code the sample code sample code code is provided, provided, different developers may choose developers may choose may choose choose different : development environments for assembly and for assembly and assembly and and linkage. This section will only This section will only section will only will only only illustrate the command the command command line 4 switches used with MADMAC and ALN and why they were chosen. and ALN and why they were chosen. ALN and why they were chosen. and why they were chosen. why they were chosen. they were chosen. were chosen. chosen. 4 

Each assembly file is assembled assembled with MADMAC with the command the command command line options options ‘-fb’ and and ‘-g’. The The switch ‘-fb’ causes MADMAC MADMAC to output BSD format object files output BSD format object files BSD format object files format object files object files files (the type strongly recommended recommended for Jaguar development). The ‘-g’ switch causes source-level source-level information to be added be added added to the object file. 

The following table shows the flags used with the Atari Linker ALN and their purpose: shows the flags used with the Atari Linker ALN and their purpose: the flags used with the Atari Linker ALN and their purpose: flags used with the Atari Linker ALN and their purpose: used with the Atari Linker ALN and their purpose: the Atari Linker ALN and their purpose: Atari Linker ALN and their purpose: Linker ALN and their purpose: ALN and their purpose: and their purpose: their purpose: purpose: 

Confidential Information FRProperty ofAtari Corporation 

9 November, 19943 : 

©1994 Atari Corp. 

Page 13 

i Minimum Object List Update 

| fmMocr Ooo[the][ sample][ program][ may][ be][ easily][ transferred][ to][ the] }[Once][ MOU.COF][ has][ been][ successfully][ output,] | ROMULATOR by typing ‘rdbjag mou’ or <wdb mou’ at a DOS or TOS command line prompt | depending upon which debugger you prefer. You should ensure that the ROMULATOR’s write-inhibit | switch is not enabled or the file will not be correctly transferred. By the placing the name of the file on | the command line it will be automatically loaded as an absolute file. To load the file after the debugger has started, type ‘aread mou.cof’. | To start the sample program and display the Jaguar logo, simply type ‘g 802000’ and hit return. The | sample program may also be started by resetting the Alpine while holding down the ‘B’ button on Joypad 1. 

## SB Be B 

©1994 Atari Corp. 

Confidential Information AU™ Property of Atari Corporation 

8 November, 1994 

| a AG 

z 

| 

| -_ 7 

: i 

: 

| 

| : | 

Mig InitMovevars: e : | 

| 

**==> picture [405 x 97] intentionally omitted <==**

**----- Start of picture text -----**<br>
™ WORKSHOP<br>‘i SERIES<br>Copyright ©1994 Atari Corp. SS<br>**----- End of picture text -----**<br>


nnn Eyam Moving a Bitmap with the Object Processor 

| Medion After reading through the first installment in this series you should now be able to construct a basic ® object list and maintain it during the vertical blank. This document will expand upon the first example, | adding motion to the bitmap that is displayed. Each Workshop Series tutorial will not spend much time @ reviewing old material. Each installment will usually only talk about the differences between the current @ §=§=©example and the last. To follow along with this tutorial you will want the source code files to the MOVE.COF executable | | — which may be found in the VJAGUAR\WORKSHOP\MOVE directory: 

# mov_init.s # mov_list.s @ mov_move.s # move.inc @ jaguar.bin @ makefile 

. 

Sa | As with our last example, this sample code will display a 16-bit CRY Jaguar logo. This time, however, the code will update the position of the object during each vertical blank so it moves around, reversing direction each time it hits the edge of the display area. | Brograminitialization= The source file MOV_INIT.S is identical to the last example’s initialization code with the exception of the following line (highlighted in bold): 

jsx InitVideo jsr InitMoveVars jsr InitLister jsr InitVBint 

The external subroutine InitMoveVars is located in MOV_MOVES. It initializes a few BSS variables that we will use to track the object’s movement as follows: 

move.1 d0,-(sp) move.w #X_MOTION,x_motion move.w #¥Y_MOTION,y_ motion 

] 

] 

©1994 Atari Corp. 

Confidential Information “AU® Property of Atari Corporation 

8 November, 1994 

Page 2 

Moving a Bitmap with the Object Processor 

{ 4 

a 

: ee rf o4 1 4 : 4 : mt. : a j 4 ‘ ; : E ; ; 4 2: ; . 1 : ; q ' | 4 4 = ‘ q 4 | —_ 4 | | 

| 

} 

**==> picture [2 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
;<br>**----- End of picture text -----**<br>


clr.w frame_count clr.w x_min move.w width,d0 lsr.w #2,d0 sub.w #BMP_WIDTH,d0O move.w d0,x_max move.w a_vdb,d0 andi.w #SFFFE,d0 move.w d0,y_min move.w a_vde,d0 sub.w #BMP_LINES,d0 andi.w #SFFFE,d0 sub.w #2,a0 move.w d0,y_max move.l (sp)+,d0 rts 

The variables x_motion and y_motion are initialized with constants stored in MOVE.INC. By altering these constants you can change the speed and initial direction of the bitmap’s motion (negative values move up and and to the left, positive values move down and down and and to the right). 

| move up and and to the left, positive values move down and down and and to the right). | The variable frame_count is initialized to zero. This variable will be incremented each timea vertical : blank occurs and is zeroed each time we actually move the object. This allows the sample code to set a : frequency (some divisor of the frame rate) at which the bitmap will be updated. : The rest of the initialization sets up four variables that will contain the logical extents of the viewscreen. Each time the object is moved its position is compared to the values in these variables and its direction is reversed if necessary. You will also notice that the width and height of the bitmap are subtracted from the width and height of the bounding rectangle. This is to account for the fact that the movement constraints must be relative to the upper-left hand corner of the bitmap. 

In this example we can use the same object list that was used in MOU.COF. The only difference is that a copy of the bitmap’s initial XPOS and YPOS are stored in the variables x_pos and y_pos. | TheVerticalBlankHandier ###=# = # # #§# = ) As with MOU.COF, the UpdateList routine is called during each vertical blank. It updates the fields of 7 the object list that were modified by the object processor. Because this example requires very little work to be done to move a bitmap around, all of this processing is done during the vertical blank. This also | allows us to return control to the debugger so we can manipulate the movement variables in realtime. | The Programmable Programmable Interrupt Timer would normally be used to regulate the speed of processing game Timer would normally be used to regulate the speed of processing game would normally be used to regulate the speed of processing game normally be used to regulate the speed of processing game be used to regulate the speed of processing game used to regulate the speed of processing game to regulate the speed of processing game regulate the speed of processing game the speed of processing game speed of processing game of processing game processing game game 

The Programmable Programmable Interrupt Timer would normally be used to regulate the speed of processing game Timer would normally be used to regulate the speed of processing game would normally be used to regulate the speed of processing game normally be used to regulate the speed of processing game be used to regulate the speed of processing game used to regulate the speed of processing game to regulate the speed of processing game regulate the speed of processing game the speed of processing game speed of processing game of processing game processing game game logic (or in this case, the speed of the moving bitmap) however, for this example, the frequency ofthe vertical blank itself will be used as the timer. 

Confidential Information FRProperty ofAtari Corporation 

©1994 Atari Corp. 

8 November, 1994 

Page 3 

q j | | : | |1 {: 1 ; : 

: : 

Moving a Bitmap with the Object Processor 

uu ; After saving registers, the very first thing UpdateList does is to call the routine MoveBitmap which can | be found in MOV_MOVE.S. MoveBitmap starts out by incrementing the variable frame_count. By comparing the frame_count variable with the pre-defined constant UPDATE_FREQ (defined in | MOVE.INC) the sample code determines whether the subroutine will actually modify the object position variables or wait for more frames to occur first. The code to this logic follows: 

|uu<br>; <br>| <br>||uu<br> After saving registers, the very first thing UpdateList does is to call the routine MoveBitmap which cansaving registers, the very first thing UpdateList does is to call the routine MoveBitmap which canregisters, the very first thing UpdateList does is to call the routine MoveBitmap which canthe very first thing UpdateList does is to call the routine MoveBitmap which canvery first thing UpdateList does is to call the routine MoveBitmap which canfirst thing UpdateList does is to call the routine MoveBitmap which canthing UpdateList does is to call the routine MoveBitmap which canUpdateList does is to call the routine MoveBitmap which candoes is to call the routine MoveBitmap which canis to call the routine MoveBitmap which canto call the routine MoveBitmap which cancall the routine MoveBitmap which canthe routine MoveBitmap which canMoveBitmap which canwhich cancan<br> be found in MOV_MOVE.S. MoveBitmapfound in MOV_MOVE.S. MoveBitmapin MOV_MOVE.S. MoveBitmapMOV_MOVE.S. MoveBitmapMoveBitmap starts out by incrementing the variable frame_count. Byout by incrementing the variable frame_count. Byby incrementing the variable frame_count. Byincrementing the variable frame_count. Bythe variable frame_count. Byframe_count. ByBy<br>comparing thetheframe_count variable with the pre-defined constant UPDATE_FREQ (defined invariable with the pre-defined constant UPDATE_FREQ (defined inwith the pre-defined constant UPDATE_FREQ (defined inthe pre-defined constant UPDATE_FREQ (defined inpre-defined constant UPDATE_FREQ (defined inUPDATE_FREQ (defined in(defined inin<br> MOVE.INC) the sample code determines whether the subroutine will actually modify the object positionthe sample code determines whether the subroutine will actually modify the object positionsample code determines whether the subroutine will actually modify the object positiondetermines whether the subroutine will actually modify the object positionwhether the subroutine will actually modify the object positionthe subroutine will actually modify the object positionsubroutine will actually modify the object positionwill actually modify the object positionactually modify the object positionmodify the object positionthe object positionobject positionposition<br>variables or wait for more frames to occur first. The code to this logic follows:or wait for more frames to occur first. The code to this logic follows:wait for more frames to occur first. The code to this logic follows:for more frames to occur first. The code to this logic follows:more frames to occur first. The code to this logic follows:frames to occur first. The code to this logic follows:to occur first. The code to this logic follows:occur first. The code to this logic follows:first. The code to this logic follows:The code to this logic follows:code to this logic follows:to this logic follows:this logic follows:logic follows:|
|---|---|
|||MoveBitmap:<br>movem.l1 d0-d1,-(sP)|
||move.w<br>frame_count,d0|
|‘<br>a|add.w<br>#1,da0<br>cmp.w<br>#UPDATE_FREQ,d0|
||beq<br>do_move|
||move.w<br>d0,frame_count|
|]|bra<br>move_done|
|||do_move:<br>clr.w<br>frame_count|
|f Whenthesubroutineactually gets thechancetoupdatethe object’spositionitmustfirstchecktoensure<br>thattheobjectremainswithintheboundssetbythex_min,x_max,y_min, andy_maxvariables. Ifthe<br>f objectreachesthelimitoftheseboundaries,theappropriatemotionvariableisnegatedtoreverseits<br>direction.Finally,themotionvariableforeachdirectionisaddedtotheobject’spositionvariableandthe<br>functionreturns.Theremainingcodeforthisfunction follows:||
|:<br>q<br>;|move.w<br>x_pos,d0<br>; verify X range<br>cmp.w<br>x_min,do<br>ble<br>change_x<br>; if at left edge<br>cmp.w<br>x_max,d0<br>5 or at right edge|
||bit<br>add_xmot|
|1|change_x:<br>neg.w<br>x motion<br>; reverse X direction|
|f<br>,|add_xmot:<br>add.w<br>x_motion<br>;addmotionamount|



|:<br>q<br>;||move.w<br>cmp.w<br>ble<br>cmp.w|x_pos,d0<br>x_min,do<br>change_x<br>x_max,d0|; <br>; <br>5|verify X range<br> if at left edge<br> or at right edge||
|---|---|---|---|---|---|---|
|||bit|add_xmot||||
|1|change_x:|neg.w|x motion|; reverse X direction|||
|f<br>,|add_xmot:|add.w|x_motion|; add motion amount|||
|||move.w<br>cmp.w<br>ble|y_pos,dl<br>y_min,dl<br>change_y||; verify Y range<br>; if at top edge||
|1|||||||
|||cmp.w|y_max,dl||; or at bottom edge||
|||bit|add_ymot|||.|
||change_y:|neg.w|y_motion||1 reverse ¥ direction||
||add_ymot:|add.w|y_motion,dl||; add motion amount||
|-||move.w|d0,x_pos||; store new values||
|=||move.w|dl,y_pos||||
|;<br>:|move_done:|movem.1(sp)+,d0-dl|||||



**==> picture [1 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


Confidential Information JPR property ofAtari Corporation 

©1994 AtariCorp. 

8November, 1994 

| 

Page 4 

Moving a Bitmap with the Object Processor 

| 

‘ | 

away with an AND with an AND an AND AND instruction and replaced with the contents contents of the the variable i | code illustrates the updating updating of the the first phrase: phrase: L move.1 #main_obj_list+BITMAP_OFF,a0 4 move.l bmp_highl, (a0) + restore first longword a move.l bmp_lowl,do ; grab long with YPOS 4 andi.l #$FFFFC007,d0 ; strip old value | move.wisl.w y_pos,dl#3,al ; and replace new : |R Oor.w di,do . move.l d0,4(a0) : now store it ' 

a | vi, 4 = | g | | a s f a : | | 

| 

- : : | | 

| 

| : 

Dee rrlQA.—<(—s—s—sSO—té—FéOCté—é—OCéCtr*=C‘“=>P During each vertical blank, the interrupt handler UpdateList restores the stored copy of the first bitmap ‘ phrase which was modified by the object processor. As an additional step, however, the YPOS portion of that phrase is stripped away with an AND with an AND an AND AND instruction and replaced with the contents contents of the the variable i y_pos. The following code illustrates the updating updating of the the first phrase: phrase: L 

; 

Next, the XPOS field in the second phrase of the bitmap must be updated. This time, however, the phrase to be modified comes directly from the object list buffer. This is possible since the Object Processor never modified this phrase. The following code updates the XPOS field in the second phrase of the bitmap and exits the interrupt handler: 

move.l 12(a0),d0 ; Low long of phrase 2 andi.l #$FFFFF000,d0 ; Extract XPOS move.w x_pos,dl + Fill in current XPOS or.w da1,do move.1 d0,12(a0) ; Store it back move.w #$101,INT1 move.w #0,INT2 

movem.l (sp)+,d0-d1/a0 rte 

Use your favorite variation of MAKE to create MOVE.COF (the flags should be the same as MOU.COF) and load it into the debugger by typing ‘wdb move’ or ‘rdbjag move’. Type “g’ and hit return to see the results of this sample program. 

As an experiment, you can try modifying the values for XK_MOTION, Y_MOTION, and UPDATE_FREQ in MOVE.INC. You will get different horizontal and vertical speeds depending on the values you select. 

|= 

©1994 Atari Corp. 

Confidential Information FRProperty ofAtari Corporation 

8 November, 1994 

| 7| G 

i 

| This example builds upon the original example in this series, MOU.COF, to demonstrate the built-in ® capability of the Object Processor to horizontally clip bitmap objects. Before examining this example, . please familiarize your self with Workshop Series #1: Minimum Object List Update. | — The following source code files to CLIP.COF may be found in the \JAGUAR\WORKSHOP\CLIP sub§ = directory: ‘ @ clp_init-s Fi @ clp_list.s : @ clp_clip.s # clip.inc ' @ jaguar.bin ££ @ makefile 

j 

**==> picture [333 x 101] intentionally omitted <==**

**----- Start of picture text -----**<br>
| ™<br>G<br>,<br>74Copyright ©1994 Atari Corp. SS<br>**----- End of picture text -----**<br>


**==> picture [88 x 66] intentionally omitted <==**

**----- Start of picture text -----**<br>
WORKSHOP<br>SERIES<br>**----- End of picture text -----**<br>


| 

## Clipping a Bitmap Object with the Object Processor 

Underconstuction The tutorial document for this example has not yet been created. Please refer to the source code comments in each of the files for specific information about this example. 

1 | 

©1994 Atari Corp. 

Confidential Information “JER property ofAtari Corporation 

9 November, 1994 

, 

| 

| | 

| 

: 

: 

P 

**==> picture [266 x 96] intentionally omitted <==**

**----- Start of picture text -----**<br>
™<br>IAG<br>Copyright ©1994 Atari Corp. Sa<br>**----- End of picture text -----**<br>


**==> picture [91 x 76] intentionally omitted <==**

**----- Start of picture text -----**<br>
WORKSHOP<br>SERIES<br>**----- End of picture text -----**<br>


ie Scaling a Bitmap Object with the Object Processor 

This example builds upon the original example in this series, MOU.COF, to demonstrate the built-in capability of the Object Processor to scale bitmap objects. Before examining this example, please | familiarize your self with Workshop Series #1: Minimum Object List Update. | — The following source code files to SCALE.COF may be found in the JAGUAR\WORKSHOP\SCALE | sub-directory: 

@ scl_init-s @ scl_list.s @ scl_scal.s @ scale.inc @ jaguar.bin # makefile 

: 

| 

Underconstruction The tutorial document for this example has not yet been created. Please refer to the source code comments in each of the files for specific information about this example. 

©1994 Atari Corp. 

Confidential Information FER Property ofAtari Corporation 

9 November, 1994 

: ij / i : : 

| 

i 

| 

**==> picture [219 x 96] intentionally omitted <==**

**----- Start of picture text -----**<br>
G ™<br>y<br>NS<br>Copyright ©1994 Atari Corp. ~~<br>**----- End of picture text -----**<br>


**==> picture [89 x 64] intentionally omitted <==**

**----- Start of picture text -----**<br>
SERIES<br>WORKSHOP<br>**----- End of picture text -----**<br>


GPU Interrupt Object Processing 

| This example builds upon the original example in this series, MOU.COF, to demonstrate GPU interrupt | objects. Before examining this example, please familiarize yourself with Workshop Series #1: Minimum | Object List Update. The following source code files to GPUINT.COF may be found in the 

\JAGUAR\WORKSHOP\GPUINT directory: 

} 

a 

# gpu_init.s @ gpu_list.s @ gpu_hndl-.s @ gpuint.inc # jaguar.bin # makefile 

The tutorial document for this example has not yet been created. Please refer to the source code comments in each of the files for specific information about this example. 

©1994 Atari Corp. 

Confidential Information FERProperty ofAtari Corporation 

8 November, 1994 

’ 

| 

| 

| , | | | | i 

a a 

| 1™ if|AG[4] ~ | Copyright ©1994 Atari Corp. > 

**==> picture [90 x 78] intentionally omitted <==**

**----- Start of picture text -----**<br>
WORKSHOP<br>SERIES<br>**----- End of picture text -----**<br>


| 

’ Rotating a Bitmap with the Blitter ln.2 | } This example demonstrates bitmap rotation using the Blitter. Initialization and object list creation/maintenance is handled in the same manner as the first Workshop Series example, MOU.COF. @| | ListBeforeUpdate. examining this example, please familiarize yourself with Workshop Series #1: Minimum Object B= The following source code files to JAGROT.COF may be found in the ' \JAGUAR\WORKSHOP\JAGROT directory: # jx init-s @ jr_list.s @ jr_grot.s a @ jr.inc a @ jaguar.bin @ makefile 

| Undeconstucton The tutorial document for this example has not yet been created. Please refer to the source code Hs comments in each of the files for specific information about this example. 

ConfidentialInformation“PO® Property ofAtari Corporation 

8 November, 1994 

©1994 Atari Corp. 

