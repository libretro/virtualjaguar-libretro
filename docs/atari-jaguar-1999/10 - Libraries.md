Page I 

1 if | | :j { i ' | 1 ' { 

i 

| 

**==> picture [38 x 12] intentionally omitted <==**

**----- Start of picture text -----**<br>
Libraries<br>**----- End of picture text -----**<br>


This section describes the various libraries that are included with the Jaguar development kit. @ = Because Atari is constantly updating and improving the Jaguar libraries and sample code, it's possible @ sthat there may be differences between the documentation and the most current release of a library. Always check the library distribution archive for one or more text files with additional or replcement documentation. 

- @ =the following libraries aze included: : . Jaguar Startup Code a 3D Graphics ° BPEG Image Compression & Decompression 

- : ° Cinepak Decompression & Playback (See separate Cinepak For Jaguar section) | 

- } Es Networking (see Jaguar Voice Modem section) . Music & Sound . Jaguar Music Driver 

- | «~~ BEPROM Access Library : ° NV-RAM Cartridge Access Library | See also the Sample Programs section. 

© 1995 AtariCorp. 

Confidential Information “JPR Property ofAtariCorporation 

26 April, 1995 

: Page 2 

Libraries 

7 

| ; @ . 4 — ' : 

| | 

] ‘ j 7 4 

| Our startup performs the following steps: | 1. Sets GPU and DSP Endian registers correctly. | 2. Disables video refresh. 

| : } . _ 1 4 ; ; 4 | ‘ 4 \ : s 5 { e | 4 q ’ cP 

5 

| 

| 

| 

| | ' 

JaguarStartupCode—_ a Starting up a Jaguar (initializing video, the object list, etc...) is the most important thing a program must do correctly. This startup code (STARTUP.S) performs all of the program initialization correctly and | must always be used. Note that modifying, reordering, or omitting any part of this startup, except ' those portions explicitly marked as being changeable, will likely cause your software to fail our hardware testing procedures. 

SS ,rCS—r—"C*teN—i(i‘é‘O;@*wswOC:wsCsCN«sCiséSCUCiéC(;iéH Link STARTUP:S first to make it the first code to be executed. Do not perform any initialization of any kind prior to running this startup code. When this code finishes it will jump to the label _start to enter your code. 

3. Sets the 68k stack pointer to the end of DRAM. 4. Initializes video registers. 

5. Creates an object list as follows: BRANCH Object (Branches to stop object if past display area) BRANCH Object (Branches to stop object if prior to display area) BITMAP Object (Jaguar License Acknowledgement - see below) STOP Object 

6. Installs an interrupt handler, configures VI, enables 68k video interrupts, lowers 68k IPL to allow interrupts. 

7. Uses GPU routine gSetOLP to stuff OLP with pointer to object list. 

8. Turns on RGB video ($6C7 in VMODE). 

" 9. Jumps to _start (your supplied code). As soon as your code gains control you should perform whatever other initialization tasks your code j may need to allow the graphic to be on screen for a reasonable amount of time. 1 26 April, 1995 Confidential Information FER Property ofAtari Corporation © 1995 Atari Corp. j oo eeeeeeeFSFSsSaeseFeFeeFeeFeFeeeeeee Si 

Page 3 

| i k i ; | | q j q { i q ‘ { ' 

| Libraries 

q 

: 

t 

When you need to transfer control to your object list (for your title screen OF whatever else) you should poll the variable ‘ticks' for a change. At this point (vertical blank) you should switch interrupt handlers ™ (by placing anew value at LEVELO $100) and change the OLP. Remember, the OLP should only be | changed by the GPU (you can use our DRAM routine if the GPU isn't already running). : | ee en @ = The macro license_logo definition at the top of STARTUP.S should be changed as necessary to indicate @ ~~ either the “I icensed by” or “Licensed to” graphic respectively. The “1 jcensed to” graphic should only B be used by our subcontractors doing a port of an existing game created by a company other than Atari. | The “Licensed by” graphic should be used in all other cases. | “AOE LLL LPM LAAT ' This collection of files should always be used as the baseline startup reference. For example, at the time | of this writing, many of our other sample programs have not yet been updated to reflect some of the | new things this startup does more correctly. They will be updated soon. However, whenever an update a needs to be made, this startup code will always be updated first. 

j 

© 1995 Atari Corp. 

Confidential Information “AOR Property of Atari Corporation 

26 April, 1995 

Page 4 

Libraries 

j 

| | ! | 

i 

& to q and/or ZZ j : CG into a a j ' this utility, . 7 File ToolKit: ToolKit: _ are created, created, 1 ’ binary data data a data in this in this this | @ pages 49-79. 49-79. i ; ge by the the 3 3DS2JAG | @ PF 4 | | 4 | :. 4 : : ' : { ] © 1995 Atari Corp. | 

| = : | | | | 

J Constriction OTA JAGFile ==— | Once the .3DS model has been completely parsed and assembled, the JAG model created by the the | conversion utility must be assembled and output. The following is a sample of output from 3DS2JAG : for a cube created in 3D Studio: 

i : ' j 

| : ' 

## @QpiGraphies 

## == 

Please note that there is nothing preventing developers from using a different 3D modeling program to create their 3D objects. However, you will have to provide your own object conversion utilities and/or 3D transformation and rendering functions. 

## SDS2JAG Object/Texture Conversion Utility = 

The utility 3DS2JAG converts an object file created with AutoCAD 3-D Studio v2.0 or v3.0 into a a format that can be used with the Jaguar 3D graphics routines. For detailed information on this utility, see the Tools chapter. 

For a full description of the 3D-Studio object data format refer to the manual "3D Studio File ToolKit: ToolKit: reference, publication 100672-A, December 18, 1992". As newer versions of 3D Studio are created, created, 3DS2JAG will have to be modified to reflect any new commands. The structure of the .3DS binary data data file can be found in Chapter 2, page 7, and the Data Structure Reference, page 35-47. The data in this in this this file is grouped into chunks, defined by a Command, Size, and Data block. See Chapter 3, pages 49-79. 49-79. 

**==> picture [190 x 252] intentionally omitted <==**

**----- Start of picture text -----**<br>
+* File: cube .JAG<br>o* Created From: cube. 3ds<br>-data<br>-phrase<br>SEGOFFSET EQU $4<br>.include "blit.ine”<br>-globl data<br>-phrase<br>data:<br>**----- End of picture text -----**<br>


26 April, 1995 

Confidential Information “AO Property of Atari Corporation 

Page 5 

**==> picture [557 x 725] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|
|||Libraries|
|y||xs|dc.wdc.w|812|;*;*|numbernumber|ofof|VerticesFaces|
|dc.1|-vertlist|;*|pointer|to|vertices|
|de.1l|.texlist|;*|pointer|to|texture|maps|
|de.l|-tboxlist|;*|pointer|to|texture|boxes|
|7eee|ES|SSS TSS|SST TSS|
|;*|FACE|DATA|-|negative|values|signify|reversing|the|segment|vertext|pair|
|.|ee|cen|peewee|eee see|eee|SRS|SE|RSS|RR|SRS|SR|
|i|
|.facelist:de.l|SFFFFOOOO|;*|Gouraud|shaded.|No|texture.|
|de.w|3|;*|Face|0:|Segments|in|Face|
|de.w|$008f|:*|color|GREEN|MATTE|(GOURAUD)|i|
|de.w|4|*|8|
|dc.w|6|*|8|
|de.w|7|*|8|
|de.l|SFFFFO000|;*|Gouraud|shaded.|No|texture.|
|de.w|3|;*|Face|1:|Segments|in|Face|
|de.w|$008f|;*|color|GREEN|MATTE|(GOURAUD)|
|de.w|4|*|8|j|
|de.w|5|*|8|
|de.w|6|*|8|
|dce.l|SFFFFO000|;*|Gouraud|shaded.|No|texture.|
|de.w|3|;*|Face|2:|Segments|in|Face|
|!|
|de.w|0|*|8|
|W|dce.w|$00f9|;*|color|ORANGE|MATTE|(GOURAUD)|||
|dc.w|5|*|8|
|de.w|47|8|||
|dc.l|SFFFFO000|;*|Gouraud|shaded.|No|texture.|
|dce.w|3|;*|Face|3:|Segments|in|Face|
|dce.w|$00f9|3*|color|ORANGE|MATTE|(GOURAUD|}|
|de.w|0|*|8|
|de.w|1|*|8|
|de.w|5|*|8|
|de.l|SFFFFO000|;*|Gouraud|shaded.|No|texture.|
|de.wdc.w|3$0089|;*.*|Facecolor|GRAY4:|SegmentsMATTE|(GOURAUD)in|Face|||
|do.w|1|*|8|
|'|dc.w|6|*|8|
|de.w|5|*|8|
|dc.l|$FFFFOO00O0|;*«|Gouraud|shaded.|No|texture.|
|de.w|3|;*|Face|5:|Segments|in|Face|
|de.w|$0089|;*|color|GRAY|MATTE|(GOURAUD)|
|de.w|1|*|8|
|de.w|2|*|8|
|de.w|6|*|8|
|de.l|S$FFFFO000|;*|Gouraud|shaded.|No|texture.|
|de.w|3|;*|Face|6:|Segments|in|Face|
|de.w|$00f1|;*|color|RED|MATTE|(GOURAUD|)|
|de.w|3|*|8|
|RS|de.w|4|*|8|
|de.w|7|*|8|
|de.l|SFFFFO000|:*|Gouraud|shaded.|No|texture.|
|de.w|3|;*|Face|7:|Segments|in|Face|
|de.w|S$00f1|;*|color|RED|MATTE|(GOURAUD)|
|© 1995|Atari Corp.|Confidential Information|JER|Property ofAtari Corporation|26|April, 1995|

**----- End of picture text -----**<br>


26 April, 1995 

| : 7 | Zz| | ; . vi i ji i | ‘ ‘ f I B a i ‘ | 

Libraries =. 4 4 | | = |= = % | a | : | g | a | F i 7 = ¢ a Z | oa ; 4 & ? 4 3 : = q | a | a _| f gg f 4 P| F 4 a 4 fF 4 | = | = | @ -— . ™ a q eS 7 . } e ©1995 Atari Corp. | 

Page 6 

de.w 3 * 8 dce.w 0 * 8 de.w 4* 8 dc.1 SFFFFO000 ;* Gouraud shaded. No texture. dc.w 3 3* Face 8: Segments in Face de.wde.w 2S0O0ff* 8 ;* color YELLOW MATTE (GOURAUD) dc.w 7 * 8 dc.w 6 * 8 dc.l SFFFF0000 ;* Gouraud shaded. No texture. de.w 3 ;* Face 9: Segments in Face de.w Soofft ;* color YELLOW MATTE (GOURAUD) de.w 2 * 8 dce.wde.w 37 ** 88 dc.i SFFFF0000 :* Gouraud shaded. No texture. dc.w 3 ;* Face 10: Segments in Face de.w $0001 ;* color BLUE MATTE (GOURAUD) de.w 0* & de.w 2 * 8 dce.w 1 * 8 dc.1 SFFFFOO000 ;* Gouraud shaded. No texture. de.w 3 ;* Face 11: Segments in Face de.w $0001 ;* color BLUE MATTE (GOURAUD) dc.w 0 * 8 dce.w 3 * 8 de.w 2+* 8 3 ete ee SE SS SSS SSS SSS SSS SSS SS SSS SS SSS SSS SSS SSS SSS SSS SS 7* VERTEX DATA j Seem e ese RSS SS SSS SS SS TESS SSS SESS SSS SS SSS S SS SSS SSS SS SS SSS ESTE -vertlist: : 3* vertex: 0 \ dc.1 SFFCFO031 s* xX ly (16.0,16.0) (-49,49) dc.1 $FFCFDBOD ;* Z |Nx (16.0,0.16) (-49) dc.1 $24F3DBOD ;* Ny|Nz (0.16,0.16) s* vertex: 1 de.l $00310031 :* X |¥ (16.0,16.0) (49,49) dc.l $FFCF24F3 ;* 2 [Nx (16.0,0.16) (-49) dc.1 $24F3DBOD ;* NyiNz (0.16,0.16) 4% vertex: 2 dc.1 $0031FFCE 7*X {¥ (16.0,16.0) (49,-50) de.l S$FFCF24F3 ;* Z [Nx (16.0,0.16) (-49) de.1 S$DBODDBOD ;* Ny|[Nz (0.16,0.16) ;* vertex: 3 de.l S$FFCFFFCE :* X fy (16.0,16.0) (-49,-50) dce.1 S$FFCFDBOD ;* Z |Nx (16.0,0.16) (-49) dc.1 SDBODDBOD 7* Ny|Nz (0.16,0.16) ;* vertex: 4 dc.l S$FFCFO031 7* xX [Y (16.0,16.0) (-49,49) de.1 $0032DB0D ;* Z [Nx (16.0,0.16) (50) 26 April, 1995 Confidential Information “PER Property of Atari Corporation 

Libraries 

| | 

: | i | 1 | | 1 | i | | 4 q { 

**==> picture [2 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


Libraries . Page 7 dc.1 $24F324F3 ;* Ny|Nz (0.16,0.16) 7* vertex: 5 de.1 $00310031 s* x [Y (16.0,16.0) (49,49) dc.1 $003224F3 ;* zZ [Nx (16.0,0.16) (50) | dc.1l $24F324F3 ;* Ny|Nz (0.16,0.16) ;* vertex: 6 dc.1 $0031FFCE 7* X |Y (16.0,16.0) (49,-50) de.l $003224F3 ;* Z Nx (16.0,0.16) (50) de.1 SDBOD24F3 s* Ny!Nz2 (0.16,0.16) { 7* vertex: 7 | dc.1 S$FFCFFFCE | ;* xX |Y (16.0,16.0) (-49,-50) . | de.l1 $0032DB0D ;* Z2 {Nx (16.0,0.16) (50) | | dc.1 $DBOD24F3 s* Ny|Nz (0.16,0.16) | ;* Model Size = ( 232 = Oxe8 ) bytes -texlist: \ 

«tboxlist: 

», See the sources for the 3D Demo program for further detail. | Fransformation & Display Routines At this time, the only documentation for the 3D transformation & display routines is contained within the comments of the actual source code itself. Please examine the 3D demo program source code for more information. 

The 3D demo program demonstrates the use of the 3D object transformation & rendering routines. It shows a detailed, texture-mapped spaceship and lets you move it around using the joypad. See the more detailed description in the Sample Programs section. 

© 1995 Atari Corp. 

Confidential Information “FER Property ofAtari Corporation 

26 April, 1995 

' 

| 

Page 8 

Libraries 

i | 4 | ; | 4 ; 5 a & 4 = 

| JPEG is a "lossy" compression scheme, meaning that the after being compressed and then ; | decompressed, the picture will not be exactly identical to the original. You can fine tune the | | compression quality as needed to strike the most acceptible balance between image quality and ' compression ratio. 4 Note: BPEG is primarily designed for RGB-mode graphics, and the compression utility takes RGB; : mode graphics files as input. However, the BPEG decompression library is capable of converting the 5 : images to CRY-mode on the fly when they are decompressed (at the cost of longer decompression a : times). & Note: The BPEG package replaces the JAGPEG package previously included with the Jaguar i Developer’s kit. The BPEG utility is easier to use, and the decompression library is faster and includes 4 : complete source code so that you can make any modifications required by your specific application. = Using the Compression UUlity) #§ #=§=§##= i The first thing you have to do is have a compressed image. Atari provides a tool in the Jaguar i developer's kit that allows you to compress Targa-format? picture files into BPEG format. See the | a Tools chapter for information about this utility. 8 | LetsCompressSomeimages== = = ................,ssCsd@ ( Using the compression tools is quite simple. Included in the BPEG package is a sample program that | 3 i displays two compressed pictures on the Jaguar screen. Normally, compressing the images istakencare @@% i of automatically by the MAKEFILE used by the sample program, but let’s do it manually so that you | 3 4 are familiar with the process. . : 1) Move to the \JAGUAR\BPEG to the \JAGUAR\BPEG the \JAGUAR\BPEG \JAGUAR\BPEG directory. The sample sample pictures FISH.TGA and PATRICK.TGA FISH.TGA and PATRICK.TGA and PATRICK.TGA PATRICK.TGA | 

| 8 | 3 @@% | 3 . | @ -_. 

i ‘ ! i i 4 

E : , j a 4 2 | Gi 

## Jaguar BPEG image Compression &Decompression__ 

BPEG is a version of JPEG! for the Jaguar. The BPEG utility and library are provided to allow you to compress bitmapped RGB graphics to a small fraction of their original size, so that they use minimal space in your Jaguar programs. 

1) Move to the \JAGUAR\BPEG to the \JAGUAR\BPEG the \JAGUAR\BPEG \JAGUAR\BPEG directory. The sample sample pictures FISH.TGA and PATRICK.TGA FISH.TGA and PATRICK.TGA and PATRICK.TGA PATRICK.TGA provided are located in this directory. 

> 1 JPEG stands for Joint Photographic Experts Group. A JPEG picture is one that has been compressed using& the JPEG lossy file compression scheme. 2 Targa is a popular image file format for 16-bit and 24-bit RGB true color graphics. If your graphics programs do not support the Targa file format, then you should investigate one of the various file format conversion utilities. HiJack Pro for Windows is available at computer stores everywhere, and the shareware program Paint Shop Pro (for MS-Windows) is available online. 26 April, 1995 Confidential Information 7@® Property ofAtari Corporation © 1995 1995 Atari Corp. 

© 1995 1995 Atari Corp. 

Page 9 

{ | | i ‘ i j ' | i i 1| | | | |[1] ' q | | i 4} ' ' | | j ; | 

- am Libraries2) Type in the command: 3 cbpeg -quality 25 fish.tga fish.bpg | We are compressing the file FISH.TGA to get the file FISH.BPG, using 2 quality setting of 25. ' The compression process will normally take just a few seconds, but of course this will vary depending on the size of the image, the quality percentage selected, and the speed of your 

- | computer. 3) Now you should have a file named FISH.BPG which is 9112 bytes, that's less than 5% the size of the original FISH.TGA file! 

- 4) Now type in the command: cbpeg -quality 75 patrick.tga patrick.bpg 

- | Now we are compressing the file PATRICK.TGA to PATRICK.BPG using a quality setting of 75. This should result in a file that is 6864 bytes long (less than 4% of the original file size). 

- | Note that this picture compressed to a smaller size than FISH.TGA even though we are using a higher quality setting. 

Later we will examine the sample program that displays these pictures on the Jaguar. ~ mn oa ee The BPEG:S file contains the source for the BPEG decompression routines. This file contains several flags which customize the operation of BPEG. While these flags are meant to be used at assembly time, you may wish to modify the code so that they may be set at runtime. The source is provided so that this sort of program-specific modification can be made. q The flags CRY15, CRY16, RGB1S5, RGB16, RGB32 defined at the top of BPEG:S control the output mode of the decompressor. One, and only one, of these flags must be set to TRUE (non-zero) and the others set to FALSE (zero). 

The BPEG functions are accessed via two 68000-based routines which call the GPU-based decompression code with the proper parameters. The decoding steps are: 1) Call BPEGInit (no input or output parameters). 

- | 2) _~—s Call BPEGDecode 

- 

AO.1 is the BPEG stream pointer A1.1 is the output buffer address DO.1 is the output buffer line width (in bytes) 

Confidential Information JER Property ofAtari Corporation 

} 3 © 1995 Atari Corp. 

26 April, 1995 

, Libraries = - k i ; 7 P| 

Page 10 

| DO = 0 (no problem)/ 1 (bad format) = 0 (no problem)/ 1 (bad format) 0 (no problem)/ 1 (bad format) (no problem)/ 1 (bad format) 1 (bad format) (bad format) format) | 3) Test BPEGSuatus BPEGSuatus (long). Possible values are: -1 (decoding) , | O (finished) (finished) 2 (decoding (decoding aborted, Huffman error) | If you want to decode another image, just go to step 2. BPEGInit copies copies the GPU GPU code in the GPU RAM, GPU RAM, RAM, without using the the blitter. You can change change this if the blitter is not not used at this moment. moment. : BPEGDecode sets some sets some some variables in the GPU, the GPU, GPU, and run it. The GPU GPU uses (corrupts) ALL REGISTERS (corrupts) ALL REGISTERS ALL REGISTERS REGISTERS E FROM BOTH BANKS, BOTH BANKS, BANKS, and almost almost all GPU memory GPU memory memory (the exact amount of memory exact amount of memory amount of memory of memory memory used depends depends onthe fl chosen output mode). mode). If you you require that some GPU some GPU GPU registers be be left alone (like for interrupt processing), for interrupt processing), processing), then you will you will will a edit the BPEG.S BPEG.S source file so that it leaves leaves a few few registers free. However, recognize that this will will | result in slower decode slower decode decode times. [ Note: If you're decoding an image in CRY15/CRY16 modes, you must have the 32Kb RGB->CRY P conversion table, and declare the GLOBAL symbol CRYTable, at the start of the table. This table is : included in the file RGB2CRY.S. a Tip: Don't forget that cartridge forget that cartridge that cartridge cartridge access is slower than RAM slower than RAM than RAM RAM access. It's a good idea to copy some of a good idea to copy some of good idea to copy some of idea to copy some of to copy some of copy some of some of of the 

| : 1 @@| ; a yo. | & L | | 7 . | rf | @ Eo -— | @ ‘ E: | 3 | 2 ._ . rf og ] 2 ] a ' 

| [ 4 \ 

; q 1 1 

**==> picture [265 x 119] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>Output:<br>DO = 0 (no problem)/ 1 (bad format) = 0 (no problem)/ 1 (bad format) 0 (no problem)/ 1 (bad format) (no problem)/ 1 (bad format) 1 (bad format) (bad format) format)<br>3) Test BPEGSuatus BPEGSuatus (long). Possible values are:<br>-1 (decoding) ,<br>O (finished) (finished)<br>2 (decoding (decoding aborted, Huffman error)<br>**----- End of picture text -----**<br>


BPEGInit copies copies the GPU GPU code in the GPU RAM, GPU RAM, RAM, without using the the blitter. You can change change this if the blitter is not not used at this moment. moment. 

BPEGDecode sets some sets some some variables in the GPU, the GPU, GPU, and run it. The GPU GPU uses (corrupts) ALL REGISTERS (corrupts) ALL REGISTERS ALL REGISTERS REGISTERS FROM BOTH BANKS, BOTH BANKS, BANKS, and almost almost all GPU memory GPU memory memory (the exact amount of memory exact amount of memory amount of memory of memory memory used depends depends onthe chosen output mode). mode). 

If you you require that some GPU some GPU GPU registers be be left alone (like for interrupt processing), for interrupt processing), processing), then you will you will will have to edit the BPEG.S BPEG.S source file so that it leaves leaves a few few registers free. However, recognize that this will will result in slower decode slower decode decode times. 

Tip: Don't forget that cartridge forget that cartridge that cartridge cartridge access is slower than RAM slower than RAM than RAM RAM access. It's a good idea to copy some of a good idea to copy some of good idea to copy some of idea to copy some of to copy some of copy some of some of of the BPEG tables into RAM before running the decoder, for ultimate speed. 

. 

TESTBPEG is a sample program that demonstrates how to take the files created with the BPEG tool and use them. This sample program is similar to many of the other sample programs for the most part, except that it sets up the video a bit differently with a 16-bit RGB mode instead of 16-bit CRY, anda creates a 16-bit RGB bitmap object instead of an 8-bit palette-based object. This is, of course, to accomodate the JPEG pictures which the program displays. 

Do not use this sample program as a demonstration of anything other than how to use the BPEG library. 

The interesting parts of this are in the TEST-S file, which sets up and calls the BPEG routines to decompress the pictures. It switches back and forth between two different pictures which were compressed with different quality settings. One of the pictures is 75% quality, the other is set to only 25% but still manages to look reasonably decent. 26 April, 1995 Confidential Information AER Property ofAtari Corporation © 1995 1995 Atari Corp. 

© 1995 1995 Atari Corp. 

Page I] 

j ' i | | | § { | { {| | ' i q : : { 4 j i ' j | | 

4 

‘ | 

4 Libraries | com Below are some annotated excerpts from the TEST-S file of the TESTBPEG sample program. First we must declare the external references to the pictures and decompression code that will be 

added in at link time. 

: extern BPEGInit ; Copy over GPU code into GPU RAM -extern.extern BPEGDecodeBPEGStatus ;; Executesemaphoredecodefor "finishedroutines decoding” status extern fish_jpg ; picture #1 extern pat_jpg ; picture #2 Here's the code to actually call the BPEG routine to decompress and display one image, wait for it to finish decoding, and then go onto the next image. Note that this simple example does not check for errors returned by the BPEGDecode function. 

bsr BPEGInit ; copy over GPU code -show_fish: . dy lea fish_jpg,a0 ; Address of compressed picture data y nC lea bitmap_addr,al ; Get destination address move .1 4 ( (WIDTH*DEPTH) /8) ,d0 ; Width of destination bitmap, in bytes bsr BPEGDecode ; Decode image : .wait_fish:tst.l BPEGStatus ; Wait for decompression to finish bmi.s .wait_fish ; before continuins.-lea pat_jpg,a0 ; Address of compressed picture data lea bitmap addr,al 3 Get destination address move .1 ¥( (WIDTH*DEPTH) /8) ,d0 ; Width of destination bitmap, in bytes bsr BPEGDecode ; Decode image .wait_patrick:tst.1 BPEGStatus ; Wait for decompression to finish bmi.s .wait_patrick ; before continuing.-bra .show_fish ; Loop forever through both pictures Note that the pictures are switched back and forth as quickly as the decompression code can spit them out. Also take a look at the MAKEFILE, which shows how you can specify a command input file for the ALN linker to get around the 128-byte MSDOS commandline length limitation. The "-c testbpeg.Ink" option specifies that the linker should read input from the file TESTJPG.LNK, which in turn contains additional commands for the linker. 

i 1995 © 1995 Atari Corp. Confidential Information JER Property ofAtari Corporation 26 April, 1995 

: 

a. vj j 4 

: , 

q = : ' | s | : a = F 4 r 4 

P | : | | j 

## From the MAKEFILE for TESTBPEG: 

testjpg.abs: $(OBJ) dehuff-.dat aln $(ALNFLAGS) ${OBJ) -c testjpg-lnk 

The contents of the TESTJPG.LNK file shows how the .JAG picture files are included in the program, as well as the DEJAG routine's -BIN file and .DAT files. 

## Contents of the TESTJPG.LNK file: 

-i fish.bpg fish jpg -i patrick.bpg pat_jpg 

The "-i" option tells ALN to include the file specified by the next parameter, and to create a label at that address as specified by the next parameter after that. Therefore, the first line of this file tells ALN to include the file FISH.BPG (the BPEG-compressed version of FISH.TGA) and to create a label "fish_jpg" at the address where the data from this file ends up in the resulting file. Then our test program refers to " fish_jpg " when it decompresses the picture (as shown in the sample code above). 

q 

26 April, 1995 

Confidential Information ‘PER Property ofAtari Corporation 

© 1995 Atari Corp. 

4 4 ; 

Page 13 

| A i j j : j | |: Er: 

| 

Libraries 

4 

/ 

: 

| 

Cinepak Video Decompession & Playback . . : The Cinepak Video Decompression & Playback libraries,: related sample programs, and utilities are : . : @ discussed in a separate chapter. Please see the chapter Cinepak For Jaguar for more information. 

There are two basic types of networking that can be used with the Atari Jaguar. The first type is a local area network (LAN) with multiple Jaguar consoles in the same room or building connected via the asynchronous serial port. This is similar to a computer LAN setup. The second type of network is two | Jaguar consoles connected to each other over the telephone lines via the Jaguar modem. i At this time, the specifications for LAN-style networking is still in development within Atari. The ' specification for The Jaguar Voice Modem is given in its own section.. 

**==> picture [1 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


© 1995AtariCorp. 

Confidential Information “JPR Property ofAtariCorporation 

26April, 1995 

| Page 14 Libraries | Sound in Jaguar is produced by the requires a synthesizer program running in the Digital Signal I Processor (DSP) in Jerry. This document describes the lowest level interface to one such program, i FULSYN, aka “the Jaguar Synth”. The Jaguar Synth is voice table driven. The main loop checks a voice table to see which voices are ; turned on, and then it calls the appropriate module for each active voice. There are twelve synthesis ie modules: 7 e 6 Sampler modules. e 3 FM Modules. : e 1 Wave Table module. e 2 Envelope-based Waveform modules q All of the modules can be placed at a stereo pan location. Sampler Modulesgggg i The Sampler modules allow either 8-bit or Sampler modules allow either 8-bit or modules allow either 8-bit or allow either 8-bit or either 8-bit or or 16-bit signed sample signed sample sample data, as well as a special compressed well as a special compressed as a special compressed a special compressed special compressed compressed ‘ format where where 16-bit data has been compressed data has been compressed has been compressed been compressed compressed 2:13. This compression compression is slightly lossy. All Samplers use use use i. data that is not not in Jerry's Jerry's internal RAM. RAM. All samplers also also support pitch shifting. The Samplers The Samplers Samplers have the | ability to loop within the sample so that long sustains may be achieved without using too much memory. loop within the sample so that long sustains may be achieved without using too much memory. within the sample so that long sustains may be achieved without using too much memory. the sample so that long sustains may be achieved without using too much memory. sample so that long sustains may be achieved without using too much memory. so that long sustains may be achieved without using too much memory. that long sustains may be achieved without using too much memory. long sustains may be achieved without using too much memory. sustains may be achieved without using too much memory. may be achieved without using too much memory. be achieved without using too much memory. achieved without using too much memory. using too much memory. too much memory. much memory. memory. much memory. memory. memory. 4 The parameters for the Sampler modules are: +o 4 e Pitch e Loop flag/Volume E e Pointer to sample data e End of loop : e Size of loop e Pan value | e Envelope Information (optional) 1 LL,rrrrrt~—t«s”—ia‘“‘“‘ONCOONCCOCONOCOC#COCC;’'CC;:CUCitéiéC®#® j The FM modules are simple to understand but produce a wide variety of sounds. In simple terms, an FM FM ’ synthesizer takes a 128 sample waveform where each sample consists of a 16 bit signed integer sign i extended to a 32 bit long. The synth then modulates the frequency according to another waveform (built ‘ like the first). The simple FM parameters are: | e Pitch e Volume 4 e Pointer to Sample Waveform e Pointer to Modulating Waveform q e Frequency of modulation e Depth of modulation 4 © Pan Value 4 | 3 This compression is done by the SNDCOMP utility. : 26 April, 1995 Confidential Information ‘JPR Property ofAtari Corporation © 1995 1995 Atari Corp. Corp. 

a Libraries | Signal @ program, I 4 a voices are synthesis = a |g modules = a compressed ] Samplers use use use have the (im too much memory. much memory. memory. much memory. memory. memory. @ E a | = . a : = terms, an FM FM : integer sign rf waveform (built | 4 P| i 4 4 , 4 fi. M F | © 1995 1995 Atari Corp. Corp. i , 

## Sampler Modulesgggg 

The Sampler modules allow either 8-bit or Sampler modules allow either 8-bit or modules allow either 8-bit or allow either 8-bit or either 8-bit or or 16-bit signed sample signed sample sample data, as well as a special compressed well as a special compressed as a special compressed a special compressed special compressed compressed ] format where where 16-bit data has been compressed data has been compressed has been compressed been compressed compressed 2:13. This compression compression is slightly lossy. All Samplers use use use data that is not not in Jerry's Jerry's internal RAM. RAM. All samplers also also support pitch shifting. The Samplers The Samplers Samplers have the (im ability to loop within the sample so that long sustains may be achieved without using too much memory. loop within the sample so that long sustains may be achieved without using too much memory. within the sample so that long sustains may be achieved without using too much memory. the sample so that long sustains may be achieved without using too much memory. sample so that long sustains may be achieved without using too much memory. so that long sustains may be achieved without using too much memory. that long sustains may be achieved without using too much memory. long sustains may be achieved without using too much memory. sustains may be achieved without using too much memory. may be achieved without using too much memory. be achieved without using too much memory. achieved without using too much memory. using too much memory. too much memory. much memory. memory. much memory. memory. memory. @ The parameters for the Sampler modules are: E a 

Libraries 

Page 15 

**==> picture [214 x 23] intentionally omitted <==**

**----- Start of picture text -----**<br>
@ © The complex FM module adds:<br>**----- End of picture text -----**<br>


| 

e Pointer to Modulator of Modulation e Frequency of modulation of frequency e Depth of modulation of frequency e Frequency of modulation of depth e Depth of modulation of depth 

All envelope handling is done outside of the DSP by adjusting the volume of each voice. 

## Wavetable Module 

The wavetable synth uses a conceptually complex synthesis technique that offers a very wide degree of flexibility of sound with a modest computational overhead. The wavetable synth plays a set of instructions. An instruction defines a waveform, a time, a volume change, a fade time and a next instruction. The waveforms consist of 128 samples. Each sample is a 16 bit signed integer sign extended to a long. The waveforms are 512 bytes long and must start on a 512 byte boundary. The instructions may loop to form a sustain. Much of the flexibility of the wavetable synth is derived from the fact that as the synth switches from one instruction to the next, the output waveform is the linear interpolation between the waveforms in the two instructions. 

The parameters for the wave table synth are: 

## @ \@_ 

**==> picture [445 x 64] intentionally omitted <==**

**----- Start of picture text -----**<br>
_ e@ PitchVolume ee Release FlagPointer to First Instruction<br>\ Pointer to Release Instruction @ Sample Length QN size)<br>® Pan Value<br>**----- End of picture text -----**<br>


The Instructions contain: 

e Pointer to Sample e Number of Ticks to Play the Sample e Number of Ticks to Fade to Next Sample e Amplitude Fade e Pointer to Next Instruction 

The wavetable amplitude fade control acts like a built-in envelope. 

The Waveform module allows any 128 sample waveform (as defined for the wavetable synth) to be played to the DACs at any musical pitch. The volume of this is then modulated by what may be thought of as a very slow sample as an envelope. This envelope has the ability to loop so that long sustains may be achieved without using too much memory. The parameters for the waveform module are: 

ad Pointer to Waveform e Pointer to Envelope e Pitch 6 Loop flag & @ e Volume e Envelope rate e End of loop e Size of loop e Pan value 

oo © 1995 Atari Corp. Confidential Information “FOR Property of Atari Corporation 26 April, 1995 

1 

second version of the waveform module exists. version of the waveform module exists. of the waveform module exists. the waveform module exists. waveform module exists. module exists. exists. It uses a slope-destination, time envelope. The uses a slope-destination, time envelope. The a slope-destination, time envelope. The slope-destination, time envelope. The time envelope. The envelope. The The 4 j " amplitude information is about about the current point and the time current point and the time point and the time and the time the time time is the amount of time the amount of time amount of time of time time it takes to get from takes to get from to get from get from from ‘ previous point's amplitude point's amplitude amplitude to this this point's amplitude. The amplitude. The The sustain point for this envelope point for this envelope for this envelope this envelope envelope is the second the second second & the last point. The parameters point. The parameters The parameters parameters for this version version of the waveform the waveform waveform module are: p | Pointer to Waveform to Waveform Waveform e Pointer to Envelope Envelope 1 | Pitch e Loop flag flag - loops at the sustain point loops at the sustain point at the sustain point the sustain point sustain point point = Volume e Release slope = Pan value value Lf There are also two versions two versions versions of the sampler module which the sampler module which sampler module which module which which use this slope-destination slope-destination envelope. One is a ’ bit sampler and the other one sampler and the other one and the other one the other one other one one is a compressed compressed 16 bit sampler. sampler. | @ The last FM module, last FM module, FM module, module, called the FM/Env synth, combines the Simple FM wave generation with the the FM/Env synth, combines the Simple FM wave generation with the FM/Env synth, combines the Simple FM wave generation with the synth, combines the Simple FM wave generation with the combines the Simple FM wave generation with the the Simple FM wave generation with the Simple FM wave generation with the FM wave generation with the wave generation with the generation with the with the the : Waveform synth envelope generation. envelope generation. generation. @ To use the the synth follow these steps: 1) Load the synth code into the synth code into synth code into code into into the DSP. DSP. yo 2) Initialize some locations in DSP RAM. DSP RAM. P| 3) Initialize the DAC and DAC and and start the DSP. DSP. I | 4) Set up a "Voice Table". up a "Voice Table". a "Voice Table". "Voice Table". Table". f 4 5) Start the voice. the voice. | @ 6) Turn off voices off voices voices as required required , 4 7) Repeat from from (4). rf 4 Voice Tables Tables are stored in DSP RAM. stored in DSP RAM. in DSP RAM. DSP RAM. 1 | The DSP code, and all its internal variables, are in the bottom of DSP RAM. This allows DSP code, and all its internal variables, are in the bottom of DSP RAM. This allows code, and all its internal variables, are in the bottom of DSP RAM. This allows and all its internal variables, are in the bottom of DSP RAM. This allows all its internal variables, are in the bottom of DSP RAM. This allows internal variables, are in the bottom of DSP RAM. This allows variables, are in the bottom of DSP RAM. This allows are in the bottom of DSP RAM. This allows in the bottom of DSP RAM. This allows j | | TABLESTART (the start of the Voice Tables) of the Voice Tables) the Voice Tables) Voice Tables) Tables) to be quite low in DSP RAM (TABLESTART is a 4 define, use use it as the position may change). as the position may change). may change). change). The size of the table of the table the table table at TABLESTART TABLESTART is not defined in the synth itself, itself, it is determined by the programmer at run time (see table below). The remainder of DSP _ RAM should be used to store the following, should be used to store the following, be used to store the following, used to store the following, to store the following, store the following, the following, following, (a) Custom samples for both wavetable and FM synthesis, | & (b) Voice Tables, these must be contiguous with TABLESTART, Voice Tables, these must be contiguous with TABLESTART, Tables, these must be contiguous with TABLESTART, these must be contiguous with TABLESTART, must be contiguous with TABLESTART, be contiguous with TABLESTART, contiguous with TABLESTART, with TABLESTART, TABLESTART, (c) Wave Table instructions and (d) | @ Waveform envelopes. envelopes. Other uses for DSP RAM may arise as new synthesis modules are written. Each rg Voice Table starts with a long (32 bit) value that indicates Table starts with a long (32 bit) value that indicates starts with a long (32 bit) value that indicates with a long (32 bit) value that indicates a long (32 bit) value that indicates long (32 bit) value that indicates (32 bit) value that indicates bit) value that indicates value that indicates that indicates indicates if the voice is active or not. The legal values _. are: _ “ Value Voice Type Type Value Voice Type Type [0 |[Endofactivevoicesssss | 24| Wavetorm/Envelope Wavetorm/Envelope | 26 April, 1995 1995 Confidential Information Information “FER Property ofAtari Corporation ofAtari CorporationAtari Corporation Corporation © 1995 Atari Corp. 2 

q To use the the synth follow these steps: f 1) Load the synth code into the synth code into synth code into code into into the DSP. DSP. | 2) Initialize some locations in DSP RAM. DSP RAM. i 3) Initialize the DAC and DAC and and start the DSP. DSP. | 4) Set up a "Voice Table". up a "Voice Table". a "Voice Table". "Voice Table". Table". ‘ 5) Start the voice. the voice. 4 6) Turn off voices off voices voices as required required 4 7) Repeat from from (4). Voice Tables Tables are stored in DSP RAM. stored in DSP RAM. in DSP RAM. DSP RAM. i The DSP code, and all its internal variables, are in the bottom of DSP RAM. This allows DSP code, and all its internal variables, are in the bottom of DSP RAM. This allows code, and all its internal variables, are in the bottom of DSP RAM. This allows and all its internal variables, are in the bottom of DSP RAM. This allows all its internal variables, are in the bottom of DSP RAM. This allows internal variables, are in the bottom of DSP RAM. This allows variables, are in the bottom of DSP RAM. This allows are in the bottom of DSP RAM. This allows in the bottom of DSP RAM. This allows q TABLESTART (the start of the Voice Tables) of the Voice Tables) the Voice Tables) Voice Tables) Tables) 4 define, use use it as the position may change). as the position may change). may change). change). The size of the table of the table the table table at TABLESTART TABLESTART 4 synth itself, itself, | RAM should be used to store the following, should be used to store the following, be used to store the following, used to store the following, to store the following, store the following, the following, following, 4 (b) Voice Tables, these must be contiguous with TABLESTART, Voice Tables, these must be contiguous with TABLESTART, Tables, these must be contiguous with TABLESTART, these must be contiguous with TABLESTART, must be contiguous with TABLESTART, be contiguous with TABLESTART, contiguous with TABLESTART, with TABLESTART, TABLESTART, | Waveform envelopes. envelopes. 4 Voice Table starts with a long (32 bit) value that indicates Table starts with a long (32 bit) value that indicates starts with a long (32 bit) value that indicates with a long (32 bit) value that indicates a long (32 bit) value that indicates long (32 bit) value that indicates (32 bit) value that indicates bit) value that indicates value that indicates that indicates indicates 4 are: 4 Value Voice Type Type Value Voice Type Type 1 [0 |[Endofactivevoicesssss | 24| Wavetorm/Envelope Wavetorm/Envelope j 26 April, 1995 1995 Confidential Information Information “FER Property ofAtari Corporation ofAtari CorporationAtari Corporation Corporation 

( Page 16 Libraries Waveiaule With Envelope Monte= | A second version of the waveform module exists. version of the waveform module exists. of the waveform module exists. the waveform module exists. waveform module exists. module exists. exists. It uses a slope-destination, time envelope. The uses a slope-destination, time envelope. The a slope-destination, time envelope. The slope-destination, time envelope. The time envelope. The envelope. The The 4 { amplitude information is about about the current point and the time current point and the time point and the time and the time the time time is the amount of time the amount of time amount of time of time time it takes to get from takes to get from to get from get from from ‘ the previous point's amplitude point's amplitude amplitude to this this point's amplitude. The amplitude. The The sustain point for this envelope point for this envelope for this envelope this envelope envelope is the second the second second & to the last point. The parameters point. The parameters The parameters parameters for this version version of the waveform the waveform waveform module are: p | e Pointer to Waveform to Waveform Waveform e Pointer to Envelope Envelope 1 | : e Pitch e Loop flag flag - loops at the sustain point loops at the sustain point at the sustain point the sustain point sustain point point = ® Volume e Release slope = | e Pan value value Lf There are also two versions two versions versions of the sampler module which the sampler module which sampler module which module which which use this slope-destination slope-destination envelope. One is a ’ ‘ 16 bit sampler and the other one sampler and the other one and the other one the other one other one one is a compressed compressed 16 bit sampler. sampler. | i The last FM module, last FM module, FM module, module, called the FM/Env synth, combines the Simple FM wave generation with the the FM/Env synth, combines the Simple FM wave generation with the FM/Env synth, combines the Simple FM wave generation with the synth, combines the Simple FM wave generation with the combines the Simple FM wave generation with the the Simple FM wave generation with the Simple FM wave generation with the FM wave generation with the wave generation with the generation with the with the the : | Waveform synth envelope generation. envelope generation. generation. @ 

i } fi y i i { | i | f { i i i || | q | ( j q ‘ | | | ; j1 ( i 4 ‘ 4 . ; : 

Page 17 

**==> picture [500 x 88] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||
|---|---|---|---|---|
|Libraries|
|i|Value|Voice Type|Value|Voice Type|
|.|16-bit Sampler|||40 _| 16-bit Sample/Slope Destination Envelope|
|44|| Compressed|16-bit Sample/Slope|
|Destination|Envelope|
|2N wavetable wavetable|48|Sound Effects Sampler Module Effects Sampler Module Sampler Module|
|(uses|16-bit compressed samples) compressed samples) samples)|

**----- End of picture text -----**<br>


, S 2N wavetable wavetable 48 Sound Effects Sampler Module Effects Sampler Module Sampler Module (uses 16-bit compressed samples) compressed samples) samples) | | ‘The values in the rest of the Voice table are given in the following pages. In the tables that follow, the § = symbol * means this value may be changed while the note is active. Values not specified do not need to B be set. The end of the Table list is indicated by a O where the next table would start. When doing polyphonic synthesis (more than one note at a time), the volume of each voice must be reduced to avoid overflow. For example a single loud voice would have a volume of about $6000. Adding 3 of these would overflow 16 bits. To avoid this you must scale down the volume of each voice | such that the total fits into 16 bits. In the preceding example a reduction of about 3 would work. | ‘The values to use for pitch are given in the accompanying spreadsheet. Find the note that you want the ' value for. The values for the FM synths and the wavetable synth are in the column marked (64K) for the other modules the value to use is in the column (256). | — = The synth has a certain amount of time available to synthesize each sample, during that time it can do ga, Only SO much. The total time available is 168 time units (these are not clock ticks). The following is a Be list of the approximate number of time units used by each synth module: Simple FM ~15 time units ; Complex FM ~24 time units ; FM/Env ~23 time units | Samplers ~19 time units Wave Table ~18 time units ' Waveform synth ~19 time units Waveform with slope-destination envelope ~17 time units Sampler with slope-destination envelope ~23 time units | Skip a voice ~3 time units 

These numbers may change as the synth modules are modified and optimized. The timings above assume that all table and sample data are in internal DSP memory (except for sample used by the Sampler module). The numbers given for the Sampler modules assume that the main bus is not busy doing other things. The total number of time units used can be computed from these numbers and kept below 167. The number available can be read from a location in DSP RAM called TIMELEFT. Note: The 168 time units will reduce if oversampling is added to the synth. y The above timings assume that the synth is running at the default rate of ~20kHz. This can be changed by modifying the value stored in SCLK. If this is done then all of the pitch information will need to be # © ~=— modified. 

] | 

© 1995 Atari Corp. 

Confidential Information JER Property ofAtari Corporation 

26 April, 1995 

| Page 18 | Module Definitions = | / Offset q (longs) Description | ) Voice type type (8) i. 1 Pointer to Carrier Wave. to Carrier Wave. Carrier Wave. Wave. Must be on a long | 2 Pointer to Modulating Wave. to Modulating Wave. Modulating Wave. Wave. | 3 Reset to zero. to zero. zero. : 4 Pitch. Given as the size the size size of a a step | 5 Reset to to zero. 4 6 Volume of this voice, of this voice, this voice, voice, 15 bits. 7 Reset to to zero. : 8 Frequency of Modulation. Modulation. 9 Depth of modulation. modulation. This is a a 7.8 number. 19 Pan Value. Value. 0 is full right, | : Offset 

’ Ps r | . 3 | § - q : P| : i Z 4 : | | @ = - 4 q . 

**==> picture [37 x 26] intentionally omitted <==**

**----- Start of picture text -----**<br>
Libraries<br>**----- End of picture text -----**<br>


**==> picture [486 x 190] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|Simple|FM|
|Offset|
|(longs)|Description|
|)|Voice type type|(8)|
|1|Pointer to Carrier Wave. to Carrier Wave. Carrier Wave. Wave.|Must be on a long|(82|bit)|boundary|(should|be DSP memory|for speed).|#|
|2|Pointer to Modulating Wave. to Modulating Wave. Modulating Wave. Wave.|Must be on|a|long|(32|bit)|boundary|(should be DSP memory for|
|3|Reset to zero. to zero. zero.|
|4|Pitch.|Given as the size the size size|of a a step|in samples as a 15.16 number.|%|
|5|Reset to to|zero.|
|6|Volume of this voice, of this voice, this voice, voice,|15|bits.|&|
|7|Reset to to|zero.|
|8|Frequency|of Modulation. Modulation.|Given|as the size|of a step|in samples|as a 15.16|number.|*|
|9|Depth|of modulation. modulation.|This|is a a 7.8 number.|=|
|19|Pan Value. Value.|0|is|full|right,|$3FFF|is|balanced,|$7FFF|is|full|left.|%|

**----- End of picture text -----**<br>


## Offset (longs) Description 

## Complex FM 

[__2 _| Pointer to Modulating Wave. Must be on a long (82 bit) boundary in internal DSP memory. ® | 

**==> picture [39 x 24] intentionally omitted <==**

**----- Start of picture text -----**<br>
| . :<br>**----- End of picture text -----**<br>


q 

26 April, 1995 

Confidential Information FER Property ofAtari Corporation 

© 1995 Atari Corp. 

Page 19 

| i j i ij i j | | {i {|[|] | { | q 1 { / | 1 | |["] , 4 : 

» |@ 

**==> picture [535 x 203] intentionally omitted <==**

**----- Start of picture text -----**<br>
Libraries<br>:<br>7 rewi §6Offset Sampler<br>| [0(longs)__| VoiceDescription type (12 = 16 bit, 28 = 8 bit; 32 = compressed 16 bit)<br>[2 High bit is the loop flag. The low 15 bits are the volume. ©<br>[3 _ _ | P ointeritch. Given to Sample. as the Must be on size of a step a  inword samples (sample as size) a 23.8 boundary number. outside_*  of internal DSP memory.<br>End of loop in samples as a 23.8 number. For a non-looping sample this is the sample number at<br>end of the sample. When the current pointer passes this point the Voice type is set to -4. Fora<br>looped sample this is end point of the loop. This is given in samples as an integer with no fractional<br>part. %<br>| [5<br>||6_.19 _|_ | Pan[Loop lengthReset Value. to zero. 0 inis full samples. right, This $SFFF is a is 23.8 balanced, number. $7FFF©  is full left. *<br>**----- End of picture text -----**<br>


: 

Samples can be looped. (Note that this is a separate issue from looping in a music score.) Sample looping works like this. Assume a sample in memory. There are four points of interest. 

## y 

. @e TheThe beginningstart of the ofsample. the loop. @ The end of the loop. e The end of the sampie. |o To play a looped sample: e Turn on the loop flag. e Set the End Loop to the end of the loop. (In samples) e Set the loop length (in samples) so that (Loop End - Loop length) = (beginning of the loop). 

This will play the sample until it reaches the loop point, at which point it will loop backwards by loop length samples. Looping will occur continuously until you stop it. To stop looping, set the End loop value to the end of the sample (in samples) and clear the loop flag. At the end of a sample the voice type is set to -4 by the synth. This allows the voice to be skipped. The voice may be reused at this point. 

| |@ | © 1995 Atari Corp. Confidential Information “PO® Property of Atari Corporation 26 April, 1995 ' | i 

| ; | ia | 1 

Page 20 

Libraries 

| a a a —_ ; = = 

| |: i. 

Pg 

; J . j Ss 

] 

; | | & | - : 2 =_ 

1 | 

Lo ; : 

| 4 4 F | 

_ 

**==> picture [513 x 212] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||
|---|---|---|---|---|---|
|N|
|2|Wave|Table|
|Offset|
|(longs)|Description|
|23.8 number.|&|
|performance|reasons|it should|be|in DSP|RAM.|
||__||[feromanceressonstshousteinbermai]|
|Size|of wavetable|sample.|This|23.8|numberis2__.|
|}|performance reasons|it should be in DSP RAM. At the end of the release sequence this|is set to -1.|

**----- End of picture text -----**<br>


After the release sequence completes, the pointer at offset 10 is set to -1 to indicate that the voice may be reused. 

**==> picture [487 x 220] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|
|Waveform|
|Offset|
|(longs)|Description|
|1|Pointer to Waveform.|Must be on 512|byte boundary.|For performance|it should|be|in|internal DSP|
|a|Pointeperfo|r|mance to Simpleit|should Envelopebe|in (seeinternal separateOSP|memory. definition).|Must be on a long (32|bit) boundary. For|
|End|of loop|in samples|as a 15.16|number.|For a non-looping sample this|is the sample number at|
|end|of the sample. When the|current|pointer|passes|this|point the Voice type|is|set to|-4.|For a|
|part.|&|

**----- End of picture text -----**<br>


Note: See the discussion on looping for the Sampler module. 

4 

26 April, 1995 

Confidential Information “POR Property of Atari Corporation 

©1995 Atari Corp. 

Wi. j 

g Libraries ae E | Offset 

Page 21 

i | t : | | : : i 1 q | 1 | ii 

ft 

Fa 

| 

## FM Envelope 

Offset (longs) Description 1 Pointer to Carrier Wave. Must be on a long (32 bit) boundary (should be DSP memory for a FoRperformance). © 

Reset to zero. 

Pointer to Simple Envelope (see separate definition). Must be on a long (32 bit) boundary (should be DSP memory for best performance). = 

- 79 Pan Value. 0 is full ight, SSFFF is balanced, $7FFF is fullleft# 

$m Note: See the information on looping for the Sampler module. 

Offset (longs) Description 

## Waveform with Slope-Destination Envelope 

memory 

na Pointerboundary. to Slope-DestinationFor best performance envelope in should (see separate be in internal definition). DSP memory.Must be on a long (32-bit) 

4 ‘ . 

i 

© 1995 Atari Corp. 

Confidential Information “F@® Property of Atari Corporation 

26 April, 1995 

Page 22 

Libraries 

o, 

. c ; 

||Sampler With Envelope|
|---|---|
|Offset||
|(longs)<br>FO|Description<br> |Voletype(40=16bi,44= compressed16b)|
|[6 <br>le||Resettozero.<br>Endof **S**ample This<br>aga numbenOOSOS—SCOCCCCCSCSC~S*Y|
||(shouldbeDSPmemoryforbestperformance).*|



Note: See the information on looping for the Sampler module. 

|Sound Effects Sampler|.|
|---|---|
|Offset<br>(longs)<br>Description<br>| 0 |Voicetype(48= compressed 16bi)||
|[ehcherptawonotadnces<br>exact,otherpitchesmightaddnoise*<br>[6 |Resettozero.<br>[8|EndofSample.Thisisa2a8number||



This is a one-shot, non-looping, non-interpolated sampler module. The sample will only sound exact when played at its original pitch. The advantage of this module is that it is very fast, using only 12 to 13 time units. It is ideal for one-shot samples like sound effects or percussion instruments. 

**==> picture [32 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
ce\ 4<br>**----- End of picture text -----**<br>


| | 

26 April, 1995 

Confidential Information “70® Property ofAtari Corporation 

© 1995 Atari Corp. 

|{ | |i i} 1 : i ' | i | 

; 

i 

| 

| ; ‘ t :: . : . 

Offset 

## Wave Table Instructions 

(tongs) Description Pointer to sample to be played. Must be on a 512 byte boundary. For performance should be it | should be in internal DSP memory. [27 __| —TonsedSSCSTime. Length of time, in ticks to play this sample. Fade value. This value sets the amplitude change per tick of fade. A becomes A*n, where n is a scaled 15 bit number. n = $4000 is no change, n = $2000 is divide volume by two, etc. 4 N-1 } Fade length. The length of the fade given as N where the fade lasts o! ) ticks. 2 <=N <= 14. Pointer to next instruction. May be anywhere in memory on a long (32 bit) boundary. For performance reasons it should be in DSP RAM. This should be set to -1 to indicate the end of the : voice. 

Offset (longs) Description 

## Simple Envelope 

**==> picture [538 x 326] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||
|---|---|---|---|---|---|---|---|---|---|
|aesses|
|| ees|
|Ce|eer|ee|e|n|
|||
|7|Slope-Destination|Envelope|
|Offset|
|;|(longs)|Description|
|||0|__||Must be set to 0x00010000|
|Must|be|set|to|0x00000001|
|:|||2|_||Slope value,|in|15.15 format|
|||[3|__||Destination|value,|in|15.15 format|
|'|Slope value,|in|15.15|format|
|||||5|||Destination|value,|in|15.15 format|
|||6|__||Slope value,|in 15.15 format|
|Destination|value,|in|15.15|format|
|[8|
|-|9|__|||Must|be|set|to|0x000|02|0000|

**----- End of picture text -----**<br>


© 1995 Atari Corp. 

Confidential Information “FO® Property of Atari Corporation 

26 April, 1995 

t 

: Page 24 24 Libraries Jaguar Music Driver Driver sc The Jaguar Music Jaguar Music Music driver is an extension is an extension an extension extension to the sound system the sound system sound system system described in the section The in the section The the section The The Jaguar Synth. Synth. : It is assumed is assumed assumed that the reader the reader reader is familiar with familiar with with that section. section. In either case, either case, the code is the same, FULSYN. code is the same, FULSYN. is the same, FULSYN. the same, FULSYN. same, FULSYN. FULSYN. 1 The only difference only difference difference is that one of Jerry's timers that one of Jerry's timers one of Jerry's timers of Jerry's timers Jerry's timers timers is used to run a used to run a to run a run a a real time time interpreter of preparsed MIDI preparsed MIDI MIDI | data. This is then used to automatically This is then used to automatically is then used to automatically then used to automatically used to automatically to automatically automatically turn the first n voices on and off. n voices on and off. voices on and off. on and off. and off. off. This requires the voicetable requires the voicetable the voicetable voicetable to 1 be at least n n entries in in length. The number of voices used The number of voices used number of voices used of voices used voices used used is set set in the the file PARSE.CNF. PARSE.CNF. For simplicity, simplicity, i this document will document will will assume that n = n = = 8. The sample The sample sample rate of the underlying synth of the underlying synth the underlying synth underlying synth is assumed assumed to be the be the the ] default ~20kHz. ~20kHz. If this this is changed then a new copy of NOTES.CNF must be generated. changed then a new copy of NOTES.CNF must be generated. then a new copy of NOTES.CNF must be generated. new copy of NOTES.CNF must be generated. copy of NOTES.CNF must be generated. of NOTES.CNF must be generated. NOTES.CNF must be generated. must be generated. generated. 1 The system system is used as follows: ' 1) A MIDI MIDI file is created in created in in file 0 format with no more than 8 note polyphony. format with no more than 8 note polyphony. with no more than 8 note polyphony. no more than 8 note polyphony. more than 8 note polyphony. than 8 note polyphony. 8 note polyphony. note polyphony. polyphony. This file is converted to a simplified format by simplified format by format by by the program program PARSE, just just type ‘parse filename.mid' on the commandline‘. commandline‘. It creates creates a MADMAC MADMAC assembly source code code file containing data : statements representing the MIDI MIDI score information. information. The default output filename output filename filename is TEST.OUT. TEST.OUT. When PARSE runs, PARSE runs, runs, it also produces a description of the also produces a description of the produces a description of the a description of the description of the of the the file to standard output (this can to standard output (this can standard output (this can (this can can optionally be disabled). be disabled). disabled). This should usually be redirected usually be redirected be redirected redirected to a file. If one one exists in the current current directory, PARSE also reads a file named PARSE.CNF. named PARSE.CNF. PARSE.CNF. This file is used to create patch maps. The default mapping is for all channels channels to map map to the the patch at their channel channel number (see (see the provided PARSE.CNF PARSE.CNF file for the format). for the format). the format). j Looping in the MIDI the MIDI MIDI file is supported supported using the following following controller events: Controller 12 marks marks | loop targets, the value on controller value on controller on controller controller 12 is the target number; the target number; target number; number; Controller 13 selects a loop target a loop target loop target target and should be be followed immediately by a Controller Controller 14 event that gives gives the number number of times to loop. A negative A negative negative loop count causes count causes causes it to loop forever. A comment comment is inserted into inserted into into the output output file that can be made be made made into a label so that loop counts can be counts can be can be reset to loop more than more than than 127 times. For more information see the format of the the music events at the end of this of this this document. pS 2) A set of patches and envelopes are created using the format described in The Jaguar Synth for of patches and envelopes are created using the format described in The Jaguar Synth for patches and envelopes are created using the format described in The Jaguar Synth for and envelopes are created using the format described in The Jaguar Synth for envelopes are created using the format described in The Jaguar Synth for are created using the format described in The Jaguar Synth for created using the format described in The Jaguar Synth for the format described in The Jaguar Synth for format described in The Jaguar Synth for described in The Jaguar Synth for in The Jaguar Synth for The Jaguar Synth for Jaguar Synth for Synth for for 

Page 24 24 Libraries Jaguar Music Driver Driver sc CR The Jaguar Music Jaguar Music Music driver is an extension is an extension an extension extension to the sound system the sound system sound system system described in the section The in the section The the section The The Jaguar Synth. Synth. ' It is assumed is assumed assumed that the reader the reader reader is familiar with familiar with with that section. section. In either case, either case, the code is the same, FULSYN. code is the same, FULSYN. is the same, FULSYN. the same, FULSYN. same, FULSYN. FULSYN. The only difference only difference difference is that one of Jerry's timers that one of Jerry's timers one of Jerry's timers of Jerry's timers Jerry's timers timers is used to run a used to run a to run a run a a real time time interpreter of preparsed MIDI preparsed MIDI MIDI a data. This is then used to automatically This is then used to automatically is then used to automatically then used to automatically used to automatically to automatically automatically turn the first n voices on and off. n voices on and off. voices on and off. on and off. and off. off. This requires the voicetable requires the voicetable the voicetable voicetable to a be at least n n entries in in length. The number of voices used The number of voices used number of voices used of voices used voices used used is set set in the the file PARSE.CNF. PARSE.CNF. For simplicity, simplicity, | this document will document will will assume that n = n = = 8. The sample The sample sample rate of the underlying synth of the underlying synth the underlying synth underlying synth is assumed assumed to be the be the the 3 default ~20kHz. ~20kHz. If this this is changed then a new copy of NOTES.CNF must be generated. changed then a new copy of NOTES.CNF must be generated. then a new copy of NOTES.CNF must be generated. new copy of NOTES.CNF must be generated. copy of NOTES.CNF must be generated. of NOTES.CNF must be generated. NOTES.CNF must be generated. must be generated. generated. ] The system system is used as follows: : 1) A MIDI MIDI file is created in created in in file 0 format with no more than 8 note polyphony. format with no more than 8 note polyphony. with no more than 8 note polyphony. no more than 8 note polyphony. more than 8 note polyphony. than 8 note polyphony. 8 note polyphony. note polyphony. polyphony. This file is -_ converted to a simplified format by simplified format by format by by the program program PARSE, just just type ‘parse filename.mid' g on the commandline‘. commandline‘. It creates creates a MADMAC MADMAC assembly source code code file containing data | statements representing the MIDI MIDI score information. information. The default output filename output filename filename is TEST.OUT. TEST.OUT. : When PARSE runs, PARSE runs, runs, it also produces a description of the also produces a description of the produces a description of the a description of the description of the of the the file to standard output (this can to standard output (this can standard output (this can (this can can j optionally be disabled). be disabled). disabled). This should usually be redirected usually be redirected be redirected redirected to a file. If one one exists in the current current ra directory, PARSE also reads a file named PARSE.CNF. named PARSE.CNF. PARSE.CNF. This file is used to create patch maps. The default mapping is for all channels channels to map map to the the patch at their channel channel number (see (see the 4 provided PARSE.CNF PARSE.CNF file for the format). for the format). the format). 4) « Looping in the MIDI the MIDI MIDI file is supported supported using the following following controller events: Controller 12 marks marks | z= loop targets, the value on controller value on controller on controller controller 12 is the target number; the target number; target number; number; Controller 13 selects a loop target a loop target loop target target j and should be be followed immediately by a Controller Controller 14 event that gives gives the number number of times to g loop. A negative A negative negative loop count causes count causes causes it to loop forever. A comment comment is inserted into inserted into into the output output file .- that can be made be made made into a label so that loop counts can be counts can be can be reset to loop more than more than than 127 times. For = more information see the format of the the music events at the end of this of this this document. a 2) A set of patches and envelopes are created using the format described in The Jaguar Synth for of patches and envelopes are created using the format described in The Jaguar Synth for patches and envelopes are created using the format described in The Jaguar Synth for and envelopes are created using the format described in The Jaguar Synth for envelopes are created using the format described in The Jaguar Synth for are created using the format described in The Jaguar Synth for created using the format described in The Jaguar Synth for the format described in The Jaguar Synth for format described in The Jaguar Synth for described in The Jaguar Synth for in The Jaguar Synth for The Jaguar Synth for Jaguar Synth for Synth for for : | voicetable entries, with a few differences. . a) In all of the FM modulation frequency controls, the rate may be made proportional to the f 7 . pitch of the note or left absolute. This is controlled by the high order bit of the frequency. The 4 relative frequency is a 23:8 integer:fraction number. For example the value $80000100 results in ? the modulation frequency being the same as the pitch. , 4 b) A new parameter, the envelope/sample end point, is specified in the patch at the following ; locations: = 4 You can also manipulate your program's MAKEFILE so that the MIDI file is essentially the 'source' file and whenever | L it is updated, the PARSE and MADMAC programs will be called automatically by the MAKE utility. See the 3 MAKEFILE for the sample program provided with the Jaguar Synth & Music Driver. 4 26 April, 1995 Confidential Information FER Property ofAtari Corporation ©1995 AtariCorp, 2h 

j [ j 

j : 

Page 25 ( \ Module Offset : i Samplers 8 ‘ Waveform 10 : FM/Env 15 c) For all samplers, For all samplers, all samplers, samplers, the pitch may be adjusted by a factor placed in the pitch parameter of the may be adjusted by a factor placed in the pitch parameter of the be adjusted by a factor placed in the pitch parameter of the adjusted by a factor placed in the pitch parameter of the by a factor placed in the pitch parameter of the a factor placed in the pitch parameter of the placed in the pitch parameter of the in the pitch parameter of the the pitch parameter of the pitch parameter of the parameter of the of the the patch. The value $1000 means no change, $800 drops the pitch by a factor of 2 (one octave) and The value $1000 means no change, $800 drops the pitch by a factor of 2 (one octave) and value $1000 means no change, $800 drops the pitch by a factor of 2 (one octave) and $1000 means no change, $800 drops the pitch by a factor of 2 (one octave) and means no change, $800 drops the pitch by a factor of 2 (one octave) and no change, $800 drops the pitch by a factor of 2 (one octave) and change, $800 drops the pitch by a factor of 2 (one octave) and $800 drops the pitch by a factor of 2 (one octave) and drops the pitch by a factor of 2 (one octave) and the pitch by a factor of 2 (one octave) and pitch by a factor of 2 (one octave) and by a factor of 2 (one octave) and a factor of 2 (one octave) and factor of 2 (one octave) and of 2 (one octave) and 2 (one octave) and (one octave) and octave) and and | a value of $2000 raises the pitch by a factor of 2. value of $2000 raises the pitch by a factor of 2. $2000 raises the pitch by a factor of 2. raises the pitch by a factor of 2. the pitch by a factor of 2. by a factor of 2. a factor of 2. factor of 2. of 2. 2. ; d) For all patches, all patches, patches, the volume may be adjusted by a factor placed in the volume parameter of volume may be adjusted by a factor placed in the volume parameter of may be adjusted by a factor placed in the volume parameter of be adjusted by a factor placed in the volume parameter of adjusted by a factor placed in the volume parameter of by a factor placed in the volume parameter of a factor placed in the volume parameter of factor placed in the volume parameter of placed in the volume parameter of in the volume parameter of the volume parameter of volume parameter of parameter of of \ the patch. The value $100 means no change, $80 drops the volume by a factor of 2, and a value value $100 means no change, $80 drops the volume by a factor of 2, and a value $100 means no change, $80 drops the volume by a factor of 2, and a value means no change, $80 drops the volume by a factor of 2, and a value no change, $80 drops the volume by a factor of 2, and a value change, $80 drops the volume by a factor of 2, and a value $80 drops the volume by a factor of 2, and a value drops the volume by a factor of 2, and a value the volume by a factor of 2, and a value volume by a factor of 2, and a value by a factor of 2, and a value a factor of 2, and a value factor of 2, and a value of 2, and a value 2, and a value and a value a value value of $200 raises the volume by $200 raises the volume by raises the volume by the volume by volume by by a factor of 2. of 2. 2. \ The files are built into a program (see below) i E| The program program is run and out comes the music. run and out comes the music. and out comes the music. out comes the music. comes the music. the music. music. program PARSE converts the MIDI file into MADMAC assembler source code using dc.] PARSE converts the MIDI file into MADMAC assembler source code using dc.] converts the MIDI file into MADMAC assembler source code using dc.] the MIDI file into MADMAC assembler source code using dc.] MIDI file into MADMAC assembler source code using dc.] file into MADMAC assembler source code using dc.] into MADMAC assembler source code using dc.] MADMAC assembler source code using dc.] assembler source code using dc.] source code using dc.] code using dc.] using dc.] dc.] i It is assembled and converted to a SCR files. At this time PARSE and the interpreter is assembled and converted to a SCR files. At this time PARSE and the interpreter assembled and converted to a SCR files. At this time PARSE and the interpreter converted to a SCR files. At this time PARSE and the interpreter to a SCR files. At this time PARSE and the interpreter a SCR files. At this time PARSE and the interpreter SCR files. At this time PARSE and the interpreter files. At this time PARSE and the interpreter At this time PARSE and the interpreter this time PARSE and the interpreter time PARSE and the interpreter PARSE and the interpreter and the interpreter the interpreter interpreter | the MIDI functions for note on/off, MIDI volume, pitch bend, pan, tempo change, and MIDI functions for note on/off, MIDI volume, pitch bend, pan, tempo change, and functions for note on/off, MIDI volume, pitch bend, pan, tempo change, and for note on/off, MIDI volume, pitch bend, pan, tempo change, and note on/off, MIDI volume, pitch bend, pan, tempo change, and on/off, MIDI volume, pitch bend, pan, tempo change, and MIDI volume, pitch bend, pan, tempo change, and volume, pitch bend, pan, tempo change, and pitch bend, pan, tempo change, and bend, pan, tempo change, and pan, tempo change, and tempo change, and change, and and i i The system assumes envelopes are also provided using dc.| directives. These are assembled system assumes envelopes are also provided using dc.| directives. These are assembled assumes envelopes are also provided using dc.| directives. These are assembled envelopes are also provided using dc.| directives. These are assembled are also provided using dc.| directives. These are assembled provided using dc.| directives. These are assembled using dc.| directives. These are assembled dc.| directives. These are assembled directives. These are assembled These are assembled are assembled assembled | into the DSP the DSP DSP at runtime runtime Jaguar sound system may be thought of as having two separate components, a synthesizer and a sound system may be thought of as having two separate components, a synthesizer and a system may be thought of as having two separate components, a synthesizer and a may be thought of as having two separate components, a synthesizer and a be thought of as having two separate components, a synthesizer and a thought of as having two separate components, a synthesizer and a as having two separate components, a synthesizer and a having two separate components, a synthesizer and a two separate components, a synthesizer and a separate components, a synthesizer and a components, a synthesizer and a a synthesizer and a synthesizer and a and a a i i interpreter. These two sections are quite independent, These two sections are quite independent, two sections are quite independent, sections are quite independent, are quite independent, quite independent, independent, although the second requires the first to the second requires the first to second requires the first to requires the first to first to to i generate sound. use the system, follow these steps. For clarity follow along ig the sample code (DRIVER:S), Load the system, follow these steps. For clarity follow along ig the sample code (DRIVER:S), Load system, follow these steps. For clarity follow along ig the sample code (DRIVER:S), Load follow these steps. For clarity follow along ig the sample code (DRIVER:S), Load these steps. For clarity follow along ig the sample code (DRIVER:S), Load steps. For clarity follow along ig the sample code (DRIVER:S), Load For clarity follow along ig the sample code (DRIVER:S), Load clarity follow along ig the sample code (DRIVER:S), Load follow along ig the sample code (DRIVER:S), Load along ig the sample code (DRIVER:S), Load ig the sample code (DRIVER:S), Load the sample code (DRIVER:S), Load sample code (DRIVER:S), Load code (DRIVER:S), Load (DRIVER:S), Load Load | DSP code into DSP RAM, set up 2 voice table, turn on the IS port, start the DSP and turn off mute. code into DSP RAM, set up 2 voice table, turn on the IS port, start the DSP and turn off mute. into DSP RAM, set up 2 voice table, turn on the IS port, start the DSP and turn off mute. DSP RAM, set up 2 voice table, turn on the IS port, start the DSP and turn off mute. RAM, set up 2 voice table, turn on the IS port, start the DSP and turn off mute. set up 2 voice table, turn on the IS port, start the DSP and turn off mute. up 2 voice table, turn on the IS port, start the DSP and turn off mute. 2 voice table, turn on the IS port, start the DSP and turn off mute. voice table, turn on the IS port, start the DSP and turn off mute. table, turn on the IS port, start the DSP and turn off mute. turn on the IS port, start the DSP and turn off mute. on the IS port, start the DSP and turn off mute. the IS port, start the DSP and turn off mute. IS port, start the DSP and turn off mute. port, start the DSP and turn off mute. start the DSP and turn off mute. the DSP and turn off mute. DSP and turn off mute. and turn off mute. turn off mute. mute. |: system is now ready for use as a synth. This functionality is primarily intended for interactive is now ready for use as a synth. This functionality is primarily intended for interactive now ready for use as a synth. This functionality is primarily intended for interactive ready for use as a synth. This functionality is primarily intended for interactive for use as a synth. This functionality is primarily intended for interactive use as a synth. This functionality is primarily intended for interactive as a synth. This functionality is primarily intended for interactive a synth. This functionality is primarily intended for interactive This functionality is primarily intended for interactive functionality is primarily intended for interactive is primarily intended for interactive primarily intended for interactive intended for interactive for interactive interactive t en | turn on the music interpreter set SCORE_ADD to the location of the tokenized music (this must be a on the music interpreter set SCORE_ADD to the location of the tokenized music (this must be a the music interpreter set SCORE_ADD to the location of the tokenized music (this must be a music interpreter set SCORE_ADD to the location of the tokenized music (this must be a interpreter set SCORE_ADD to the location of the tokenized music (this must be a set SCORE_ADD to the location of the tokenized music (this must be a SCORE_ADD to the location of the tokenized music (this must be a to the location of the tokenized music (this must be a the location of the tokenized music (this must be a location of the tokenized music (this must be a of the tokenized music (this must be a the tokenized music (this must be a tokenized music (this must be a music (this must be a (this must be a must be a be a a i aligned address), set TIMER_ADD to 0, start the timer and out comes music. address), set TIMER_ADD to 0, start the timer and out comes music. set TIMER_ADD to 0, start the timer and out comes music. TIMER_ADD to 0, start the timer and out comes music. to 0, start the timer and out comes music. 0, start the timer and out comes music. start the timer and out comes music. the timer and out comes music. timer and out comes music. and out comes music. out comes music. comes music. music. The remaining code remaining code code shows how to add in custom effects. how to add in custom effects. to add in custom effects. add in custom effects. in custom effects. custom effects. effects. To play music and sound effects simultaneously make sure that play music and sound effects simultaneously make sure that music and sound effects simultaneously make sure that and sound effects simultaneously make sure that effects simultaneously make sure that simultaneously make sure that make sure that sure that that 4 you restrict sound effects to the voice table entries that the music interpreter does not use. restrict sound effects to the voice table entries that the music interpreter does not use. sound effects to the voice table entries that the music interpreter does not use. effects to the voice table entries that the music interpreter does not use. to the voice table entries that the music interpreter does not use. the voice table entries that the music interpreter does not use. voice table entries that the music interpreter does not use. table entries that the music interpreter does not use. entries that the music interpreter does not use. that the music interpreter does not use. the music interpreter does not use. music interpreter does not use. interpreter does not use. does not use. not use. use. During each sample period the synth goes thru the voice tables (starting at TABLESTART) and checks each sample period the synth goes thru the voice tables (starting at TABLESTART) and checks sample period the synth goes thru the voice tables (starting at TABLESTART) and checks period the synth goes thru the voice tables (starting at TABLESTART) and checks the synth goes thru the voice tables (starting at TABLESTART) and checks synth goes thru the voice tables (starting at TABLESTART) and checks goes thru the voice tables (starting at TABLESTART) and checks thru the voice tables (starting at TABLESTART) and checks the voice tables (starting at TABLESTART) and checks voice tables (starting at TABLESTART) and checks tables (starting at TABLESTART) and checks (starting at TABLESTART) and checks at TABLESTART) and checks TABLESTART) and checks and checks checks : the first longword of each one to find out which synth module to use next. first longword of each one to find out which synth module to use next. longword of each one to find out which synth module to use next. of each one to find out which synth module to use next. each one to find out which synth module to use next. one to find out which synth module to use next. to find out which synth module to use next. find out which synth module to use next. out which synth module to use next. which synth module to use next. synth module to use next. module to use next. to use next. use next. next. 5 This is actually controlled by your MAKEFILE. You can use the standard .O extension normally used by object 7 modules, or you can use a different extension to identify that this object module contains music score data. In the latter case, the SCR filename extension (for Musical Score) is recommended. i © 1995 Atari Corp. Confidential Information “JPR Property ofAtari Corporation 26 April, 1995 

Libraries i 

‘ 

| c) For all samplers, For all samplers, all samplers, samplers, the pitch may be adjusted by a factor placed in the pitch parameter of the may be adjusted by a factor placed in the pitch parameter of the be adjusted by a factor placed in the pitch parameter of the adjusted by a factor placed in the pitch parameter of the by a factor placed in the pitch parameter of the a factor placed in the pitch parameter of the placed in the pitch parameter of the in the pitch parameter of the the pitch parameter of the pitch parameter of the parameter of the of the the patch. The value $1000 means no change, $800 drops the pitch by a factor of 2 (one octave) and The value $1000 means no change, $800 drops the pitch by a factor of 2 (one octave) and value $1000 means no change, $800 drops the pitch by a factor of 2 (one octave) and $1000 means no change, $800 drops the pitch by a factor of 2 (one octave) and means no change, $800 drops the pitch by a factor of 2 (one octave) and no change, $800 drops the pitch by a factor of 2 (one octave) and change, $800 drops the pitch by a factor of 2 (one octave) and $800 drops the pitch by a factor of 2 (one octave) and drops the pitch by a factor of 2 (one octave) and the pitch by a factor of 2 (one octave) and pitch by a factor of 2 (one octave) and by a factor of 2 (one octave) and a factor of 2 (one octave) and factor of 2 (one octave) and of 2 (one octave) and 2 (one octave) and (one octave) and octave) and and a value of $2000 raises the pitch by a factor of 2. value of $2000 raises the pitch by a factor of 2. $2000 raises the pitch by a factor of 2. raises the pitch by a factor of 2. the pitch by a factor of 2. by a factor of 2. a factor of 2. factor of 2. of 2. 2. d) For all patches, all patches, patches, the volume may be adjusted by a factor placed in the volume parameter of volume may be adjusted by a factor placed in the volume parameter of may be adjusted by a factor placed in the volume parameter of be adjusted by a factor placed in the volume parameter of adjusted by a factor placed in the volume parameter of by a factor placed in the volume parameter of a factor placed in the volume parameter of factor placed in the volume parameter of placed in the volume parameter of in the volume parameter of the volume parameter of volume parameter of parameter of of the patch. The value $100 means no change, $80 drops the volume by a factor of 2, and a value value $100 means no change, $80 drops the volume by a factor of 2, and a value $100 means no change, $80 drops the volume by a factor of 2, and a value means no change, $80 drops the volume by a factor of 2, and a value no change, $80 drops the volume by a factor of 2, and a value change, $80 drops the volume by a factor of 2, and a value $80 drops the volume by a factor of 2, and a value drops the volume by a factor of 2, and a value the volume by a factor of 2, and a value volume by a factor of 2, and a value by a factor of 2, and a value a factor of 2, and a value factor of 2, and a value of 2, and a value 2, and a value and a value a value value of $200 raises the volume by $200 raises the volume by raises the volume by the volume by volume by by a factor of 2. of 2. 2. 

## a) 

A) The program program is run and out comes the music. run and out comes the music. and out comes the music. out comes the music. comes the music. the music. music. q The program PARSE converts the MIDI file into MADMAC assembler source code using dc.] PARSE converts the MIDI file into MADMAC assembler source code using dc.] converts the MIDI file into MADMAC assembler source code using dc.] the MIDI file into MADMAC assembler source code using dc.] MIDI file into MADMAC assembler source code using dc.] file into MADMAC assembler source code using dc.] into MADMAC assembler source code using dc.] MADMAC assembler source code using dc.] assembler source code using dc.] source code using dc.] code using dc.] using dc.] dc.] directives. It is assembled and converted to a SCR files. At this time PARSE and the interpreter is assembled and converted to a SCR files. At this time PARSE and the interpreter assembled and converted to a SCR files. At this time PARSE and the interpreter converted to a SCR files. At this time PARSE and the interpreter to a SCR files. At this time PARSE and the interpreter a SCR files. At this time PARSE and the interpreter SCR files. At this time PARSE and the interpreter files. At this time PARSE and the interpreter At this time PARSE and the interpreter this time PARSE and the interpreter time PARSE and the interpreter PARSE and the interpreter and the interpreter the interpreter interpreter understand the MIDI functions for note on/off, MIDI volume, pitch bend, pan, tempo change, and MIDI functions for note on/off, MIDI volume, pitch bend, pan, tempo change, and functions for note on/off, MIDI volume, pitch bend, pan, tempo change, and for note on/off, MIDI volume, pitch bend, pan, tempo change, and note on/off, MIDI volume, pitch bend, pan, tempo change, and on/off, MIDI volume, pitch bend, pan, tempo change, and MIDI volume, pitch bend, pan, tempo change, and volume, pitch bend, pan, tempo change, and pitch bend, pan, tempo change, and bend, pan, tempo change, and pan, tempo change, and tempo change, and change, and and looping. The system assumes envelopes are also provided using dc.| directives. These are assembled system assumes envelopes are also provided using dc.| directives. These are assembled assumes envelopes are also provided using dc.| directives. These are assembled envelopes are also provided using dc.| directives. These are assembled are also provided using dc.| directives. These are assembled provided using dc.| directives. These are assembled using dc.| directives. These are assembled dc.| directives. These are assembled directives. These are assembled These are assembled are assembled assembled idj@ and loaded into the DSP the DSP DSP at runtime runtime The Jaguar sound system may be thought of as having two separate components, a synthesizer and a sound system may be thought of as having two separate components, a synthesizer and a system may be thought of as having two separate components, a synthesizer and a may be thought of as having two separate components, a synthesizer and a be thought of as having two separate components, a synthesizer and a thought of as having two separate components, a synthesizer and a as having two separate components, a synthesizer and a having two separate components, a synthesizer and a two separate components, a synthesizer and a separate components, a synthesizer and a components, a synthesizer and a a synthesizer and a synthesizer and a and a a music interpreter. These two sections are quite independent, These two sections are quite independent, two sections are quite independent, sections are quite independent, are quite independent, quite independent, independent, although the second requires the first to the second requires the first to second requires the first to requires the first to first to to | actually generate sound. To use the system, follow these steps. For clarity follow along ig the sample code (DRIVER:S), Load the system, follow these steps. For clarity follow along ig the sample code (DRIVER:S), Load system, follow these steps. For clarity follow along ig the sample code (DRIVER:S), Load follow these steps. For clarity follow along ig the sample code (DRIVER:S), Load these steps. For clarity follow along ig the sample code (DRIVER:S), Load steps. For clarity follow along ig the sample code (DRIVER:S), Load For clarity follow along ig the sample code (DRIVER:S), Load clarity follow along ig the sample code (DRIVER:S), Load follow along ig the sample code (DRIVER:S), Load along ig the sample code (DRIVER:S), Load ig the sample code (DRIVER:S), Load the sample code (DRIVER:S), Load sample code (DRIVER:S), Load code (DRIVER:S), Load (DRIVER:S), Load Load 1 | the DSP code into DSP RAM, set up 2 voice table, turn on the IS port, start the DSP and turn off mute. code into DSP RAM, set up 2 voice table, turn on the IS port, start the DSP and turn off mute. into DSP RAM, set up 2 voice table, turn on the IS port, start the DSP and turn off mute. DSP RAM, set up 2 voice table, turn on the IS port, start the DSP and turn off mute. RAM, set up 2 voice table, turn on the IS port, start the DSP and turn off mute. set up 2 voice table, turn on the IS port, start the DSP and turn off mute. up 2 voice table, turn on the IS port, start the DSP and turn off mute. 2 voice table, turn on the IS port, start the DSP and turn off mute. voice table, turn on the IS port, start the DSP and turn off mute. table, turn on the IS port, start the DSP and turn off mute. turn on the IS port, start the DSP and turn off mute. on the IS port, start the DSP and turn off mute. the IS port, start the DSP and turn off mute. IS port, start the DSP and turn off mute. port, start the DSP and turn off mute. start the DSP and turn off mute. the DSP and turn off mute. DSP and turn off mute. and turn off mute. turn off mute. mute. | The system is now ready for use as a synth. This functionality is primarily intended for interactive is now ready for use as a synth. This functionality is primarily intended for interactive now ready for use as a synth. This functionality is primarily intended for interactive ready for use as a synth. This functionality is primarily intended for interactive for use as a synth. This functionality is primarily intended for interactive use as a synth. This functionality is primarily intended for interactive as a synth. This functionality is primarily intended for interactive a synth. This functionality is primarily intended for interactive This functionality is primarily intended for interactive functionality is primarily intended for interactive is primarily intended for interactive primarily intended for interactive intended for interactive for interactive interactive sounds. | en To turn on the music interpreter set SCORE_ADD to the location of the tokenized music (this must be a on the music interpreter set SCORE_ADD to the location of the tokenized music (this must be a the music interpreter set SCORE_ADD to the location of the tokenized music (this must be a music interpreter set SCORE_ADD to the location of the tokenized music (this must be a interpreter set SCORE_ADD to the location of the tokenized music (this must be a set SCORE_ADD to the location of the tokenized music (this must be a SCORE_ADD to the location of the tokenized music (this must be a to the location of the tokenized music (this must be a the location of the tokenized music (this must be a location of the tokenized music (this must be a of the tokenized music (this must be a the tokenized music (this must be a tokenized music (this must be a music (this must be a (this must be a must be a be a a long aligned address), set TIMER_ADD to 0, start the timer and out comes music. address), set TIMER_ADD to 0, start the timer and out comes music. set TIMER_ADD to 0, start the timer and out comes music. TIMER_ADD to 0, start the timer and out comes music. to 0, start the timer and out comes music. 0, start the timer and out comes music. start the timer and out comes music. the timer and out comes music. timer and out comes music. and out comes music. out comes music. comes music. music. The remaining code remaining code code shows how to add in custom effects. how to add in custom effects. to add in custom effects. add in custom effects. in custom effects. custom effects. effects. To play music and sound effects simultaneously make sure that play music and sound effects simultaneously make sure that music and sound effects simultaneously make sure that and sound effects simultaneously make sure that effects simultaneously make sure that simultaneously make sure that make sure that sure that that you restrict sound effects to the voice table entries that the music interpreter does not use. restrict sound effects to the voice table entries that the music interpreter does not use. sound effects to the voice table entries that the music interpreter does not use. effects to the voice table entries that the music interpreter does not use. to the voice table entries that the music interpreter does not use. the voice table entries that the music interpreter does not use. voice table entries that the music interpreter does not use. table entries that the music interpreter does not use. entries that the music interpreter does not use. that the music interpreter does not use. the music interpreter does not use. music interpreter does not use. interpreter does not use. does not use. not use. use. ' During each sample period the synth goes thru the voice tables (starting at TABLESTART) and checks each sample period the synth goes thru the voice tables (starting at TABLESTART) and checks sample period the synth goes thru the voice tables (starting at TABLESTART) and checks period the synth goes thru the voice tables (starting at TABLESTART) and checks the synth goes thru the voice tables (starting at TABLESTART) and checks synth goes thru the voice tables (starting at TABLESTART) and checks goes thru the voice tables (starting at TABLESTART) and checks thru the voice tables (starting at TABLESTART) and checks the voice tables (starting at TABLESTART) and checks voice tables (starting at TABLESTART) and checks tables (starting at TABLESTART) and checks (starting at TABLESTART) and checks at TABLESTART) and checks TABLESTART) and checks and checks checks 4 the first longword of each one to find out which synth module to use next. first longword of each one to find out which synth module to use next. longword of each one to find out which synth module to use next. of each one to find out which synth module to use next. each one to find out which synth module to use next. one to find out which synth module to use next. to find out which synth module to use next. find out which synth module to use next. out which synth module to use next. which synth module to use next. synth module to use next. module to use next. to use next. use next. next. 

**==> picture [2 x 34] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


BP a a Ss { Fd E i 1 | 4 

| More details may be found in the example files. | Stoppingthe Music[interpreter] To stop your music before the end of the score is reached, you do the following steps: 

| | 3 7 : | : 

a j 1 

q first long word of each word of each of each each voice structure.) This tells the synth to do nothing for those voices. voices. j You may want your sound effects to continue even if your music stops. If you are playing music only 1 with the first five or six voices, and are using the last two or three voices for sound effects, then in step q 1 you would change the volume parameters in the individual voice tables that are being used for music, : and leave the volume of the sound effects voices alone (and don’t turn off those voices in step 3). If f you want to change the volume of everything, including sound effects, then you can either change all of : the individual voices or you can change the UEBERVOLUME variable, which will affect all voices. q The MIDIVOLUME variable will only affect new notes generated by the music driver; changing it will q not change the volume of a note that has started but not yet finished. 

1 4 | ; 1 yy} a | @ q 

| 

/ 

| Page 26 Libraries is created by the program PARSE. A list is kept by the parser of all voices that are in use anda warning ym 4 | The Music driver interprets a structure in memory to manipulate entries in the voice table. This structure . | is given if the desired polyphony fails to accommodate the needs of the MIDI file being parsed. The { | voice assigned to a note on event is determined by taking the jast used voice, adding one until an | available voice is found. At any given time the voice table can be quite complex. A representative voice 7 table follows (showing only the voice type in detail): a 12 x xX xX 20K BP aq 28 x x x + 2X a q ~4 x XX .+X a : -4 x x x o+X Ss ; -4 x x x 2.x q 16 x xX xX re4 { -4 x x xX 2-X Fd 4 24 x xX x 2X E | 0 i 

This type of table would be expected while playing an eight voice music file with two channels reserved for sound effects. 

- 1) Ramp down the volume to fade out the music and/or sound effects. This step is optional, but it will probably sound better this way than if you just cut off the music abruptly. 

- 2) Set the SCORE_ADD pointer to point at the end of your music score. This should contain a long word value of $7FFFFFFF. 

- 3) Step 2 will cause the music driver to stop feeding the synthesizer's voice tables with new information, but it won’t stop the synthesizer from processing the information already there. To do this, we must set the voice type value to -4 for each voice you want to turn off. (That’s the first long word of each word of each of each each voice structure.) This tells the synth to do nothing for those voices. voices. 

26 April, 1995 

Confidential Information TR Property ofAtari Corporation 

© 1995 Atari Corp. 

Page 27 

; | | | | i | |1 | 5 | | | || | 4 i { | ; : { i . : : | { 

Libraries When you want to restart your music, you would simply reset the voice types, volume, and SCORE_ADD variable to the appropriate values. 

|ee Each event consists of two long words. The first long is the time (in milliseconds) from the start of the | song the the event is scheduled for (this limits the length of any individual tune, without loops, to about 6 weeks). The next long is the actual event encoded as follows. 

Coded events look like this: | BEEV| VVxx| xxxx|xxxx | xxxx | xxxx | Xxxx | xxxx EEE = Event type . ixx NOTE ON | 1xxV|VVPP | PPPF | FFFF | FFFF | FFFF | FARA | AAAA : vivvPP|PPP= Voice= Patchnumbernumber F|FFFF|FFFF|FFFF|F = Frequency AAA|AAAA = Amplitude | 000 NOTE OFF[|][ Xxxx] 000V | VVxx | xxxx | xxxx | xxxx | xxxx[|][xxxx] | v|vv = Voice number | 011p|011pppJUMP| DDDWITH| DDDCOUNT| Dppp | ppp | cece | CCCC j eccc|cccc is number of loops played j D| DDDD| DDDD | DDDD | DDDD | DDDD is the number of phrases to jump 2 010 CONTROLLER CHANGE 010v | VWPP | PPPF | CCCC | CNNN | NNNN | NNNN | NNNN 

v|vv = Voice Number pp|PP = Patch Number F = Flag to change the base pitch eccc|c = Controller Code NNN |NNNN|NNNN|NNNN = Value © 1995 Atari Corp. Confidential Information JPR Property ofAtari Corporation 

**==> picture [60 x 28] intentionally omitted <==**

**----- Start of picture text -----**<br>
26 April, 1995<br>**----- End of picture text -----**<br>


-_ Libraries | 4. ir g | 

Og 

| 2 s 

SBSEGW, | merge them them j The MERGE MERGE a note values values | the frequency : Synth, you | MIDI files. If is & = 50% of its of its its | good. The = utility is | a = © 1995 Atari Corp. ‘ 

' : 

| , Page 28 Libraries . : : Controllers77 = Volumarar **e** : i 9 = Pitch Bend 10 = Stereo Pan | Patse-MIDIFileParser== = The MIDI parser is a command line program which translates a MIDI file into commands recognized by ' the Jaguar syntheziser. The output of the parser is a MADMAC assembler source file (ASCII) containing the sound data for the synthesizer in assembly language format. This file has to be assembled ' and linked in with your program, playing the music. The PARSE utility is documented in the Tools | chapter of the documentation. | eerrrrt——~—Ss—=CVCisSN®COWOWCOW®C(‘(’RCS(NYRRRRKN.Crrrrt——~—Ss—=CVCisSN®COWOWCOW®C(‘(’RCS(NYRRRRKN.C : The MERGE utility is designed to take multiple music data files created with PARSE and merge them them together into a single file that will contain everything interleaved together appropriately. The MERGE MERGE utility is documented in the Tools chapter of the documentation. | The XNOTES utility is designed to automatically create a NOTES.CNF file with the correct note values values | for a given sampling rate. The NOTES.CNF file is used by the PARSE utility to contro] the frequency | value that is used for each musical note. If you change the sample rate used by the Jaguar Synth, you 1 should run XNOTES to create a new NOTES.CMF file, then run PARSE again on your MIDI files. If j you skip these steps, the pitch of the notes will be incorrect. The use of the XNOTES utility is documented in the Tools chapter. 

: Controllers77 = Volumarar **e** : 9 = Pitch Bend 10 = Stereo Pan Patse-MIDIFileParser== = 

eerrrrt——~—Ss—=CVCisSN®COWOWCOW®C(‘(’RCS(NYRRRRKN.Crrrrt——~—Ss—=CVCisSN®COWOWCOW®C(‘(’RCS(NYRRRRKN.C 

The SNDCOMP utility is designed to take a 16-bit digitized sound file and compress it to 50% of its of its its original size. The compression it does is a "lossy" compression, but the quality is quite good. The compressed sound files it creates are then used with the Jaguar Synthesizer. The SNDCOMP utility is documented in the Tools chapter of the documentation. 

26 April, 1995 

Confidential Information PR Property ofAtari Corporation 

| Libraries - Page 29 Jaguar SoundTooiUserGuidejé= =#=..44..s Ci The Jaguar sound tool was written to provide a “user friendly" interface to the Jaguar synthesizer | module. The sound tool provides a way of editing up to 8 voices by using one of the seven synthesizer | modules. Each voice can be turned on individually or, together with other voices. Voices can be saved | to or loaded from the host machine allowing you to save work in progress. Additionally, you may save 4 : your work in ASCII form, ready to be linked into your source code. For the rest of this section, it will be assumed that you have read TheJaguar Synth section. | In general, each of the synth modules share the same user interface. Whenever possible, you'll find that | the joypad keys display the same functionality throughout the different synth editors. You can move | | from object to object within an editor by holding down the Fire B button and then pressing up, down | left, or right depending on the placement of the object that you would like to go to. An object is defined ’ : as a single slider, a group of buttons, or any other item that allows you to edit the voice that you're | working on. | As you move you move move to each object, each object, object, you'll see it being being selected by an green box drawn around by an green box drawn around an green box drawn around box drawn around around it. The two main two main main | object types types are numerical numerical sliders and buttons. and buttons. buttons. To change the value of a numerical change the value of a numerical the value of a numerical value of a numerical of a numerical a numerical numerical slider, use the | k joypad up and down keys to add up and down keys to add and down keys to add down keys to add keys to add to add add to or subtract from or subtract from subtract from from the total. Using the the left and right buttons, and right buttons, right buttons, buttons, you can can | move the the slider cursor cursor left or right. This will will allow you you to increment increment or decrement your decrement your your slider value by value by by | a larger or smaller amount. or smaller amount. amount. Notice that the value the value value will only increment or decrement by decrement by by 1 each time you you | press the up or down up or down or down down key. To scroll through these numbers more more quickly, hold down the option key down the option key the option key option key key | while pressing up or down. pressing up or down. up or down. or down. down. Alternatively, you may may type in the direct value and value and and the number will number will will appear i at the cursor location. the cursor location. cursor location. location. Button groups dre much simpler much simpler simpler to use. Simply select the joypad key which joypad key which key which which i represents the button which you wish to button which you wish to which you wish to you wish to wish to to select. i The following following is a brief discussion a brief discussion brief discussion discussion of each of the the synth editors along with a description of the the main : Menu screen. screen. : 

As you move you move move to each object, each object, object, you'll see it being being selected by an green box drawn around by an green box drawn around an green box drawn around box drawn around around it. The two main two main main object types types are numerical numerical sliders and buttons. and buttons. buttons. To change the value of a numerical change the value of a numerical the value of a numerical value of a numerical of a numerical a numerical numerical slider, use the k joypad up and down keys to add up and down keys to add and down keys to add down keys to add keys to add to add add to or subtract from or subtract from subtract from from the total. Using the the left and right buttons, and right buttons, right buttons, buttons, you can can move the the slider cursor cursor left or right. This will will allow you you to increment increment or decrement your decrement your your slider value by value by by a larger or smaller amount. or smaller amount. amount. Notice that the value the value value will only increment or decrement by decrement by by 1 each time you you press the up or down up or down or down down key. To scroll through these numbers more more quickly, hold down the option key down the option key the option key option key key while pressing up or down. pressing up or down. up or down. or down. down. Alternatively, you may may type in the direct value and value and and the number will number will will appear at the cursor location. the cursor location. cursor location. location. Button groups dre much simpler much simpler simpler to use. Simply select the joypad key which joypad key which key which which represents the button which you wish to button which you wish to which you wish to you wish to wish to to select. The following following is a brief discussion a brief discussion brief discussion discussion of each of the the synth editors along with a description of the the main } Menu screen. screen. 

Each of the 8 synth voices can be edited through this main menu screen. As discussed earlier, use the Fire B key along with joypad up and down to scroll through each voice. When a voice is chosen, hit i the up and down buttons to select a synth editor then hit 2 to edit the voice. Turn the voice on or off by hitting the 1 key. Hitting the Fire A key will turn on all of your enabled voices at once. Note that at startup, each of the voices except for the first one is disabled. Once you have edited a voice, you can can } . return to the main menu by either using the main menu button or, by hitting the pause key. 

you can can 5 move will cause : box with with | the 3 3 | 26 April, April, 1995 

The final row of buttons allows you to load or save out your current work. To save your work, move down until you've selected the last row of buttons. Hit the 2 key and the SNDTOOL program will cause a break command in the debugger on your host computer. You will be prompted by an alert box with with instructions on saving your file. In the same manner, an ASCII file can be saved out by hitting the 3 3 © 1995 Atari Corp. Confidential Information “FER Property ofAtari Corporation 26 April, April, 1995 

t Page 30 Libraries i key. Note that this is a 100% ASCII file which can be read into any text editor. Each of the voices is ( separated by a different label, voicel:, voice2:, etc. You will also find envelopes, user defined waveforms, and wavetable instructions saved out as well. All addresses within the voice table will be represented by a label. This label will either correspond to one of the labels embedded in the file, or, as | in the case of sample addresses, simply be referenced as an external lable at the top of the file. \ Use the Load Waves button the Load Waves button Load Waves button Waves button button to load in user defined waveforms. load in user defined waveforms. in user defined waveforms. user defined waveforms. defined waveforms. waveforms. You can load in up to 5 can load in up to 5 load in up to 5 in up to 5 up to 5 to 5 5 different user i defined waveforms. waveforms. They are stored at the addresses UWAVE1, UWAVE2, the addresses UWAVE1, UWAVE2, addresses UWAVE1, UWAVE2, UWAVE1, UWAVE2, UWAVE2, ... UWAVES. UWAVES. To read ina read ina ina i waveform for the first user user defined wave, wave, use the command: command: ; i read filename .UWAVE1] 1 The Cwave button performs harmonic synthesis using a table of 32 partials with user specified Cwave button performs harmonic synthesis using a table of 32 partials with user specified button performs harmonic synthesis using a table of 32 partials with user specified performs harmonic synthesis using a table of 32 partials with user specified harmonic synthesis using a table of 32 partials with user specified synthesis using a table of 32 partials with user specified using a table of 32 partials with user specified a table of 32 partials with user specified table of 32 partials with user specified of 32 partials with user specified 32 partials with user specified partials with user specified with user specified user specified specified : amplitude relationships. Briefly, any sound can be broken down sound can be broken down can be broken down be broken down broken down down intoaa series of sine waves called of sine waves called sine waves called waves called called q partials or harmonics. The Cwave or harmonics. The Cwave harmonics. The Cwave The Cwave Cwave utility allows the specification of the relative allows the specification of the relative the specification of the relative specification of the relative of the relative the relative relative amplitudes of thirty-two of thirty-two thirty-two 

Libraries 

j 7 . : up | ( | g } 4 : Z = ' 4 | i q { j ale { a -_ | 2 , , 4 ‘ 

| q | 7 q | | 

| j Y Use the numerical sliders to change frequency and depth of modulation. Use the text sliders to select your waveforms and pitch. Select these values by using the up and down joypad keys until the selected ’ : pitch or waveform appears in the slider. Use the Frequency mode button to select the way the frequency [ E : value is calculated. When in "Fixed" mode, the frequency value in the voice table will be whatever is | = shown in the slider. When in "ratio" mode, the frequency value will be whatever is in the slider 4 E multiplied by whatever pitch value you have. Note that the frequency multiplier will be in the 15.16 1 a format so for instance, 1.32768 in the slider will represent a multiplier value of 1.5. Exit the synth by _ using the Main Menu button or by hitting the pause key in any object. Play the sample by pressing the 9a Fire A button. Press it again to turn the voice off. 4 ; 

| : { 1 : 7 : 

Qi 

Use the Load Waves button the Load Waves button Load Waves button Waves button button to load in user defined waveforms. load in user defined waveforms. in user defined waveforms. user defined waveforms. defined waveforms. waveforms. You can load in up to 5 can load in up to 5 load in up to 5 in up to 5 up to 5 to 5 5 different user defined waveforms. waveforms. They are stored at the addresses UWAVE1, UWAVE2, the addresses UWAVE1, UWAVE2, addresses UWAVE1, UWAVE2, UWAVE1, UWAVE2, UWAVE2, ... UWAVES. UWAVES. To read ina read ina ina waveform for the first user user defined wave, wave, use the command: command: ; 

The Cwave button performs harmonic synthesis using a table of 32 partials with user specified Cwave button performs harmonic synthesis using a table of 32 partials with user specified button performs harmonic synthesis using a table of 32 partials with user specified performs harmonic synthesis using a table of 32 partials with user specified harmonic synthesis using a table of 32 partials with user specified synthesis using a table of 32 partials with user specified using a table of 32 partials with user specified a table of 32 partials with user specified table of 32 partials with user specified of 32 partials with user specified 32 partials with user specified partials with user specified with user specified user specified specified amplitude relationships. Briefly, any sound can be broken down sound can be broken down can be broken down be broken down broken down down intoaa series of sine waves called of sine waves called sine waves called waves called called partials or harmonics. The Cwave or harmonics. The Cwave harmonics. The Cwave The Cwave Cwave utility allows the specification of the relative allows the specification of the relative the specification of the relative specification of the relative of the relative the relative relative amplitudes of thirty-two of thirty-two thirty-two harmonics, which are mathematically combined into a resuitant waveform. 

After pressing the 5 number key the harmonics can be entered by typing: 

sl .awave 

At this point the first harmonic can be entered by typing a hexadecimal value and pressing [Return]. This automatically displays the field for the second harmonic. Pressing {Return] again brings up the field for the third harmonic, etc. After entering the last harmonic and pressing [Return] a dot (’.’) has to be entered followed by a [Return] . The debugger then returns to its command line. To continue, type: 

g .continue 

The Cwave utility stores the waveform it creates in user wave 1. After a wave has been created, it may be saved using the Waveform Load/Save button. 

1 

26 April, 1995 

Confidential Information “FO®. Property ofAtari Corporation 

© 1995 Atari Corp. 

Libraries Page 31 CompiexFMEditor = | Identical to Simple FM except for extra sliders to provide an extra indirection of modulation. The synth documentation will provide the needed details. | qepisampisedion 0 — | 46BitCompressed SampleEditor Froma user interface standpoint these two editors are virtually identical. There is currently a default 16 | bit sample built into the sound editor. To load additional samples, select the Load Sample button from | the first group of buttons. 

| -The sound tool will currently handle Audio IFF files and AVR files as well as raw sample files. Since | there is no header information stored with a raw sample file, you must set the variable .samplesize to let the sound tool know how big the newly loaded sample is. You can accomplish this by typing in the following: sl .samplesize (type in new number of samples here) You can now type in "g .continue" to return to the program. Currently the maximum sample file size “], thatinformation the sound from tool AIFF will acc fil **e** s.)pt is 200000 bytes. (NOTE: The tool currently does not extract pitch Use the numerical sliders to set loop length, loop end and pitch values. You can play the sample by pressing the Fire A button at any time. If the Loop On button has been selected, the sample will play continuously, looping through the parameters which you have set up. Once the Fire A button has been released, the synth will play the rest of the sample. 

WavelormEditor Use the numerical sliders to set rate, loop end, and loop length. Use the up and down buttons to cycle through the given pitches and waveforms. You can edit the envelope by first making it the current object. Use the joypad up and down buttons to increase or decrease values at the current point. Move to the next point in the envelope by holding down the Fire C button and using the joypad left and right buttons. Insert points by pressing the 1 number key on the keypad. In the same way, delete points by the 4 key. Pressing the 0 number key will restore the envelope to a standard default. You may choose any one of five envelopes (through the envelope slider) to sample or edit. Each time you scroll through an envelope you will be able to see it change visually on the screen. The voice can be played by using the Fire A button. As with the sample editor, the sound will loop until the Fire A button is released. “am A new envelope can be saved or loaded by selecting the load/save menu button. Load or save functions will affect the current envelope. (The one displayed in the slider) After breaking, you will be promted to input the correct commands to load an envelope. At this point you can also save out the current envelope to be used at another time. 

: | : | i i | | ' | iI | | ? : ; : . 

; , 

© 1995 Atari Corp. 

Confidential Information “FO® Property of Atari Corporation 

26 April, 1995 

' Page 32 Libraries | FMEnvelope = j This synth editor combines the features of the waveform and simple FM synths. See The Jaguar Synth } section for details. 

_ Ve § g q | & fg ‘ a | 5 r q = | OY | | 1 a ] 7 a | j | @ ' : | = | 2 YJ © | 3 | | a 

1 1 j { : | 

’ to the synth. the synth. | 46 bit CompressedSampler/Envelope | This synth editor combines the features of the waveform and 16 bit sampler synth. Note that the q envelope is of a different kind in this module. The new envelope for this module is a basic slopes destination, time envelope. The Amplitude information is about the current point and the Time is the amount of time it takes to get a from the previous point's amplitude to this point's amplitude. You can add points by pressing the 1 number key while inside the envelope window and delete points ; by pressing the 4 key. To move from point to point hold down the Fire C button and use the joypad. : The point can be edited vertically as well as horizontally. The two parameters that are available to the user are: | - Amplitude (0 - 32767) i - Time (0 - 2,000,000,000 ms) | The information (Amplitude and Time) about each point are updated as the points are moved. See The Jaguar Synth for details. 

The 2N Wavetable editor will allow you to edit a set of wavetable instructions. Use the sustain/release buttons to select which list of instructions you want to edit. The large object in the center of the screen will hold your list of instructions. Notice that the current instruction in this list will be highlighted in green. Use the up and down joypad keys to scroll the list. This current instruction will also be represented by the sliders at the bottom of the screen. You can use these sliders to create a new wavetable instruction. Use the panel of buttons on the right side of the screen to insert the new instruction (represented by the slider values) into the actual wavetable instruction list. You can also change the existing instruction or remove an instruction using this bank of buttons. The last instruction in your sustain list will automatically loop to the first instruction. If you would rather loop to another instruction, place the index of the instruction that you want to loop to into the Loop To slider. Notice that the Fade Length slider shows positive values. The too] will negate the value before passing it on to the synth. the synth. 

rs ' 26 April, 1995 Confidential Information ‘JER Property ofAtari Corporation ©1995 Atari Corp. 

Libraries : gh| ( | can use use | Ei | required to complete these document. , | ] { 4 - ] 

| | 

1 

| 

1 ’ 

| : j : 

4 j| . ” 3 q 

: 

a 7 

| 

| 

| ; 

j : j 

- Page 34 

- | ProcedureSummary The basic tasks for processing MIDI files consist of: ° converting (or parsing) your MIDI file into a form that the Jaguar can use use ° creating synthesizer and sample patches ° incorporating patch information into files used by the Jaguar synthesizer 

Figure 1 illustrates these tasks. The following is a summary of the steps required to complete these tasks. Each of these steps is described in detail in later sections of this document. 

1. Install the Jaguar Music System tools. 

   - a. Install] the tools and sample code from the distribution archives b. Create a new directory for your music project. Cc. Copy the Jaguar sound files to the new directory. 

## 2. Create your sound patches. 

- a. Design and save your synthesized and sample patches. b. Save ASCII versions of your patches. Cc. Convert your samples to raw format, compress them, and write down sample information. 

- 3. Prepare your MIDI file. 

; 

**==> picture [14 x 17] intentionally omitted <==**

**----- Start of picture text -----**<br>
3<br>**----- End of picture text -----**<br>


   - a. Clean up your MIDI sequences. b. Write down information about your MIDI sequences. c. Save your MIDI file in sections as separate type 0 MIDI files. 

4. Copy your MIDI Type0 files, patch ASCII files, and samples. 

5. Extract patch data, envelope, waveform and wavetable data to separate ASCII files. 

   - a. Extract patch data to separate ASCII files. 

   - b. Replace the label names in your patch data. 

   - c. Adjust other patch values in your patch data. d. Extract envelope data to separate ASCII files. €. Extract user waveform data to separate ASCII files. f. Extract wavetable data to separate ASCII files. 

6. Modify the file synth.s. a. Set the number of patches. b. Include patch data files. c. Write down patch numbers. d. Add sample labels and include sample files. 

26 April, 1995 Confidential Information FER Property ofAtari Corporation 

© 1995 Atari Corp. 

**==> picture [45 x 179] intentionally omitted <==**

**----- Start of picture text -----**<br>
| 1<br>|<br>j<br>.<br>|<br>yi q<br>a<br>; =<br>**----- End of picture text -----**<br>


} | i { ' : { | | ] i 

Page 35 

_. 

7 Libraries i €. Initialize the voice table to the correct number of voices. 4 i ” f. Add waveform labels and include user waveform files. Zz g. Add envelope labels and include envelope files. . h. Add wavetable labels and include wavetable files. 

] Ss : 

ft 4 a | 

i \ 

7. Add MIDI information to parse.cnf. 

8. Run the parse program to parse your MIDI tiles. 9. After testing your music one section at 2 time, run the merge tool to combine your sections. 10. For each MIDI file, change the MIDIFILE entry in the makefile. 

11. Run the make tool. 

- 

12. Load and run test.cof. 13. Refine your MIDI files, patches, and voice settings. 

14. Adjust volume and tempo in synth.cnf if necessary. 15. Repeat steps 5 through 14 until your music plays correctly. 

## , 

© 1995 Atari Corp. 

Confidential Information “JPR Property ofAtari Corporation 26 April, 1995 : 

j :{ 

z Hy),. 4, 

| | 4 

{ | | 

gg3 g 

: : 

| 4 ; 

a 

: 

, 

| j 

4 4 : ; , 

4 - 

| 

**==> picture [505 x 466] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 36 Libraries<br>. MIDI Sequencer Sound Tool<br>Create MIDI file Create patches and Create samples<br>save as ASCII<br>Extxtract informationinf 3 Convert to<br>WwW from ASCII patch raw compressformat and<br>Parse and merge sections Patches,<br>one at a time Waveforns,<br>Envelopes,<br>Wavetables |<br>Include<br>Refine music and patches<br>make<br>**----- End of picture text -----**<br>


Figure 1. Processing a MIDI File 

## Step-by-Step Procedure 

This section presents the steps for processing a MIDI file in detail. 

26 April, 1995 

Confidential Information “FUR Property ofAtari Corporation 

—_— 

© 1995 Atari Corp. fF | 

|| I i | | i | | 

7 Libraries Page 37 ,. Sapa neal Whe daquar MAIE’SystemTools ; a. Install the tools and sample code from the distribution archives. : | The Jaguar Music System tools and sample files are installed automatically when you install the disks g that come with a Jaguar Development System. If you have received updated archives containing the | 7 tools (or downloaded them from an online service), then you should extract the archives into a t temporary directory. The directory structure used in the archives is: 1 JAGUAR\BIN[-][Various][ tools][ such][ as the][ MIDI][ parser,][ sound][ sample][ file][ format][conversion][utilites,][etc.] j JAGUAR\MUSIC\FULSYN - The Jaguar Synthesizer, source code and linkable object code. JAGUAR\MUSIC\SNDTOOL - The Jaguar Synthesizer Sound Tool - Used for creating patches for the | Jaguar Synth. j JAGUAR\MUSIC\SNDTOOL.MID - The MIDI version of the Sound Tool. 1 JAGUAR\MUSIC\SOUNDSA variety of ready-made sound patches for use with the Jaguar Synth and i the Sound Tool. N : JAGUAR\MUSIC\MUSICDRV - The sample program for the Jaguar Synth. This is the sample program . described in this document. JAGUAR\MUSIC\SYNDEMO- This is an alternate sample program for the Jaguar Synth. This one includes a more complex MIDI score that uses multiple instruments and looping. Also, this one uses multiple FM patches and no samples. To extract the various archives using this directory structure, use the following command: 

pkunzip -d music. zip 

Where “music.zip” is the name of the archive you are extacting at the moment. The PKUNZIP tool is supplied on your original Jaguar Developer System disks. 

If you are installing an update, please always extract the archives to a temporary directory first, so you can backup the existing files before copying over the new ones. b. Create a new directory for your music project. Make a new directory on your hard disk. You will use this directory to hold your MIDI file, synthesizer | w patches, samples, and several Jaguar files and programs . 7 The Jaguar Music System Tools distribution includes two sample projects. One plays a simple scale of notes using the Jaguar Synth’s Sample module. This project is contained in the directory , { JAGUAR\MUSIC\MUSICDRV. The second sample plays a more complex song with multiple voices, ' © 1995 Atari Corp. Confidential Information JER Property ofAtari Corporation 26 April, 1995 

iy q Page 38 | | and uses FM patches instead of samples. This project is found in the JAGUAR\MUSIC\SYNDEMO directory. 

Libraries 

ay "al ' | : , : 2 

: | q ' | | | q 

j ° synth.cnf { This file contains settings for global and MIDI volume of the synthesizer file contains settings for global and MIDI volume of the synthesizer contains settings for global and MIDI volume of the synthesizer settings for global and MIDI volume of the synthesizer global and MIDI volume of the synthesizer and MIDI volume of the synthesizer MIDI volume of the synthesizer volume of the synthesizer of the synthesizer the synthesizer synthesizer and the system clock : used to adjust music tempo. This file also allows the Jaguar Synth to be to adjust music tempo. This file also allows the Jaguar Synth to be adjust music tempo. This file also allows the Jaguar Synth to be music tempo. This file also allows the Jaguar Synth to be tempo. This file also allows the Jaguar Synth to be This file also allows the Jaguar Synth to be file also allows the Jaguar Synth to be also allows the Jaguar Synth to be allows the Jaguar Synth to be the Jaguar Synth to be Jaguar Synth to be Synth to be to be be reconfigured for the Bs optimum performance and memory usage requirements for individual performance and memory usage requirements for individual and memory usage requirements for individual memory usage requirements for individual usage requirements for individual requirements for individual for individual individual | the Jaguar Synth source code be reassembled -- see below). Jaguar Synth source code be reassembled -- see below). Synth source code be reassembled -- see below). source code be reassembled -- see below). code be reassembled -- see below). be reassembled -- see below). reassembled -- see below). -- see below). see below). below). : You will not need to change the following files: ° driver.s : This file contains initialization information for the Jaguar synthesizer. | ° fulsyn.inc | This file contains parameter settings and instructions file contains parameter settings and instructions contains parameter settings and instructions parameter settings and instructions settings and instructions and instructions instructions for the Jaguar the Jaguar Jaguar synthesizer. 7 located in the JAGUAR\MUSIC\FULSYN the JAGUAR\MUSIC\FULSYN JAGUAR\MUSIC\FULSYN directory.) | * £802_50.das : —____ ne q 26 April, 1995 Confidential Information ‘JER Property ofAtari Corporation 

i, 

c. Copy the Jaguar sound files to the new directory. 

This document uses the MUSICDRV project as its example. You will need the following files to perform the procedure described in this document. During this procedure, you will need to modify some of these files. Be sure to save the original copies of these files so you can use them for other projects. 

You will need to change the following files using a text editor. 

° makefile This file is used by the MAKE tool to compile various files into an executable program file. 

° parse.cnf 

This file contains MIDI channel, MIDI note range, voice number, and transposition data for the MIDI parsing process. It is used by the PARSE utility. 

This file is used to assemble patch data, samples, envelopes, user waveforms, and wavetables that must reside in the Jaguar's memory. 

This file contains settings for global and MIDI volume of the synthesizer file contains settings for global and MIDI volume of the synthesizer contains settings for global and MIDI volume of the synthesizer settings for global and MIDI volume of the synthesizer global and MIDI volume of the synthesizer and MIDI volume of the synthesizer MIDI volume of the synthesizer volume of the synthesizer of the synthesizer the synthesizer synthesizer and the system clock used to adjust music tempo. This file also allows the Jaguar Synth to be to adjust music tempo. This file also allows the Jaguar Synth to be adjust music tempo. This file also allows the Jaguar Synth to be music tempo. This file also allows the Jaguar Synth to be tempo. This file also allows the Jaguar Synth to be This file also allows the Jaguar Synth to be file also allows the Jaguar Synth to be also allows the Jaguar Synth to be allows the Jaguar Synth to be the Jaguar Synth to be Jaguar Synth to be Synth to be to be be reconfigured for the optimum performance and memory usage requirements for individual performance and memory usage requirements for individual and memory usage requirements for individual memory usage requirements for individual usage requirements for individual requirements for individual for individual individual projects (this requires that the Jaguar Synth source code be reassembled -- see below). Jaguar Synth source code be reassembled -- see below). Synth source code be reassembled -- see below). source code be reassembled -- see below). code be reassembled -- see below). be reassembled -- see below). reassembled -- see below). -- see below). see below). below). 

° fulsyn.inc This file contains parameter settings and instructions file contains parameter settings and instructions contains parameter settings and instructions parameter settings and instructions settings and instructions and instructions instructions for the Jaguar the Jaguar Jaguar synthesizer. (This file is located in the JAGUAR\MUSIC\FULSYN the JAGUAR\MUSIC\FULSYN JAGUAR\MUSIC\FULSYN directory.) 

| ' | | | 

© 1995 Atari Corp. 

Page 39 

Libraries ° This file is the Jaguar DSP source code for the Jaguar synthesizer. You should not have to 7 change it, but you may recompile it to add or delete different synthesizer modules according to j the needs of individual projects (controlled by the SYNTH.CNF file). (This file is located in , the JAGUAR\MUSIC\FULSYN directory, but depending on the version, the filename may : change.) fF 6 © £802_50.03 This file is the linkable object module for the Jaguar synthesizer (This file is located in the : JAGUAR\MUSIC\FULSYN directory. Depending on the version, the filename may change.) CALLE EES : 1 a. Design and save your synthesized and sample patches. | Create the sound patches to be played by your MIDI file. You may want to perform this step before you : ; compose your music, or perhaps at the same time. This way, you will have a better idea of what sounds : q the Jaguar is capable of producing. i S. You can use the Sound Tool to create synthesized patches or use sampling software to create 16-bit i we samples. : If you use samples, we suggest you use 4 sampling rate of approximately 20 KHz to match the default : , 4 playback frequency of the Jaguar. You must use mono samples. If you have stereo samples, you can use i 4 the MONO utility to convert them to mono. | j We suggest you use the Sound Tool to set parameters of your samples, including pitch, loop parameters, ' : and envelopes. For more on voicing samples on the Jaguar, see the More on Voicing Samples section. H 7 Load the Sound Tool into the Jaguar using rdbjag by typing the following: : | rdbjag ' load sndtool.db : For more information about creating sound patches, see the Jaguar Sound Tool Users Guide and the 1 Jaguar Synth document. q The Sound Tool creates two kinds of patch files. One is an ASCII file designed to be assembled as q Madmac source code as part of your project. The other is a binary file used to load and save patches 4 that are being edited. Although it creates both types of files, the Sound Tool only knows how to load q the binary files. Therefore, after creating a patch, we suggest you always save it in a non-ASCII file so 1o you can reload it into the Sound Tool at a later time and make changes as needed. When saving these > files, we suggest you save the files with an extension of .ptc in a directory called sounds. 1 Important: Synthesizer patches use a lot less memory than samples. And, samples use outside . . 4 resources that are shared by graphics, causing slower game play and possible sample distortion. Because ; © 1995 Atari Corp. Confidential Information JPR Property of Atari Corporation 26 April, 1995: 

**i** - emPage 40 Asp Libraries i of these problems, you should avoid using samples as much as possible and instead use synthesized i sounds for your music. This is particularly important for games in which the available space for music is i very limited. If you must use samples, restrict them to important sounds that you cannot synthesize. i y y Pp I y \ b. Save ASCII versions of your patches. q For each patch you create, use the Sound Tool to save it as an ASCII file. If you created any patch data | information for samples, you should save this patch data as ASCII as well. i To save a patch in ASCII format, go to the main page of the Sound Tool and select the Save Patch i command. We suggest you name these files with an extension of . asc, and place these files in a | directory called ascii. 

7% 

j 

1 : 4 

b/ 7 

G ; 1 ' : : ] ' ' ‘ | : : 

1 

c. Convert your samples to raw format, compress them, and write down sample information. 

The Jaguar DSP plays raw samples only. Raw samples contain the sample sound information, but do not contain other information such as looping data. If you created your sample in another format, such as the Audio Interchange File (AIF) format, you need to convert your samples to raw format for them to play correctly on the Jaguar. To do this, use the stripaif tool on your samples, and create other sample parameters (looping and pitch) in the patch data using the Sound Tool. 

Next, compress your samples using the sndcmp tool. This tool compresses samples from 16 bit to 8 bit. Also, write down the file name and file sizes of each sample. You may need the file size information when adding patch data to synth.s. 

## a. Clean up your MIDI sequences. 

After composing your music, you may want to clean up or modify your MIDI sequences before processing them for the Jaguar. Use your sequencing software to inspect each of your MIDI tracks. When examining your tracks, look for the following and make changes as needed: 

1. Verify that the number of voices being played by all of your tracks at one time (the polyphony) does not exceed the polyphony you are allowed for your game music. 

The Jaguar's polyphony is determined by the amount of time the synthesizer has to create each sound. The amount of time the Jaguar takes to create a sound depends on which synth module is for the sound. The total time available for the Jaguar to create sounds is 168 time units. Therefore, when determining the polyphony for your music, you must add the time values for each module you use to make sure the total time is at or below 167. Also keep in mind that some @ of the Jaguar synth's time available may be used to synthesize sound effects instead of music. For more information about calculating polyphony, see the Jaguar Synth document. 

**==> picture [2 x 17] intentionally omitted <==**

**----- Start of picture text -----**<br>
§<br>**----- End of picture text -----**<br>


26 April, 1995 

Confidential Information “7% Property ofAtari Corporation 

© 1995 Atari Corp. 

Page 41 

: q Libraries ae 2. Check the quantization of your tracks to be sure that the timing of your notes (when notes start , and end) is what you want. You may choose to leave your music as you recorded it to give it a a | | more natural feel. Or, you may need to quantize some or all of your notes to correct for timing 1 problems. : | 3. Check that the note durations are what you want them to be. For example, if a note is used to : trigger a sample that does not use an envelope, you may want to shorten the note duration to q prevent undesired looping. You can also adjust the loop parameters of a sample and apply an : envelope to it using the Sound Tool. 4 : Be aware that any notes that trigger patches with long decays may affect your polyphony 4 _ galculations since decay of the patch sound may overlap new notes being triggered. Too avoid 3 this problem, be sure that your patch envelopes decay before the next note is triggered for that 4 patch. For example, suppose there are two sequential half notes, with the first note ending before ’ the second is triggered. Also suppose that the tempo of your music causes each note to last for ; one second. If the patch you use for these notes has an envelope that decays in one second or less, there is no problem. However, it the envelope decays in longer than a second, another voice will be needed to play the second note. If you are at the limit of your polyphony, the second note | may not play at all. 4 4. Verify that the note on velocities are set to the desired level. For example, you may want to 0 make the attack of a track consistent. On the other hand, you may want to leave them exactly as r you performed them. q | 5. Adjust the volume the instruments used for each track (MIDI controller 7) as needed. You will likely be using different sounds on the Jaguar than the ones you used to compose your music. Because of this, it is hard to predict the what the relative volumes wiil be for your Jaguar sounds. For example, you might set the volume of your kick drum to be just right when you play it back on your sequencer. But, when you play it on the Jaguar, the kick may not be loud enough. Because it is hard to know ahead of time what the relative volumes will be for your patches, you may want to set some Or all of your instruments volumes to a constant level (such as MIDI value 100). You can then mix the volumes on the Jaguar as needed from within the patch data file (synth.s) until they sound right. 6. If you want to have your MIDI file loop in the game, you need to set loop points in your MIDI file. For more information about how to set MIDI file loop points, see the Looping MIDI Files section of this document. b. Write down information about your MIDI sequences. Write down your MIDI file information for later use. | v0 1. Write down the MIDI channel numbers for each track in your MIDI sequences. You will need these numbers when you parse your MIDI file in step 11. 

start ‘ it a a ; : to to : . an : avoid = that i before i for : or a voice : note |: to : i i will : music. | : you play play " 4 your | (such : the MIDI . Files : will need need 26 April, 1995 ; 

© 1995 Atari Corp. 

Confidential Information FER. Property ofAtari Corporation 

He “Page 42 Libraries q 2. Write down the MIDI note ranges (as MIDI note numbers) for each track. This information is Hi required if you intend to play different sounds on the same MIDI channei. For example, if you you i recorded a track using a split keyboard, or drum machine, you need to write down which which notes 4 are for which sounds. You will use this information when you parse your MIDI file. : c. Save your MIDI file in sections as type 0 MIDI files. | The Jaguar music driver software plays type 0 MIDI files. This is a standard MIDI file format that 4 merges multiple-channel tracks into single tracks. Type 0 MIDI files still retain the MIDI channel 4 information of your tracks. 4 Therefore, to play your MIDI music, you must first convert it to one or more type 0 MIDI files. To test To test test | your music on the Jaguar, we suggest you save individual tracks (or groups of musically related tracks) tracks) q as separate type 0 MIDI files. This way, you can test and refine separate parts of your music, making it 7 easier to identify and fix problems you may find. | After testing and refining your tracks, you can use the merge tool to merge these files into one file for : use on the Jaguar. . : When saving your MIDI sequences, we suggest you name them with an extension of .mid. | ss St4.” Copy your MIDI Type 0 files; patchASCITfIes)andamples: | If they are not already there, copy your MIDI type 0 files, each of the ASCII patch files you created, and your samples, to your music project directory. a «xxrrti‘i‘ééSSCONOOCOOOCNONONONCOi#CUiésCNiéCaiCiaiCC#Sg?m ; a. Extract patch data to separate ASCII files. q Edit each ASCII patch file you created and locate the patch data. This data is a column of .dc.1 values ; used by the Jaguar synthesizer and music driver. The patch data is located after the label | _sounddata: ' Each ASCII patch file contains data for all pieces needed for your synthesis module. All envelopes, user waves etc. associated with your sound will be save in one file. j | Once you have located the patch data, copy it from your ASCII patch file to a separate file. { ' We suggest you name these files with an extension of .dat, and place them in a directory called ; : patches. = i 26 April, 1995 Confidential Information FOR Property ofAtari Corporation ©1995 Atari Corp. Corp. | 

information is a example, if you you " down which which notes file. format that channel files. To test To test test | related tracks) tracks) making it into one one file for you created, created, | of .dc.1 values .dc.1 values values : envelopes, user | j file. { called ; = ©1995 Atari Corp. Corp. | 

Page 43 

: Libraries Dy. Replace the label names in your patch data : Replace the temporary labe] names (_env0, _envl, and so on) in your patch data to match the label : names you will put in synth.s. For synthesized patches, you may need to replace envelope, user | waveform, and wavetable labels within your patch data. For sample patches, you will need to replace | sample and envelope labels. ' We suggest you prefix label names for envelopes with e_ , user waveforms with w_, wave tables with : | t_, and samples with s_. For consistency across platforms, we also recommend you use labels of eight | or fewer characters. c. Adjust other patch values in your patch data. | : There are other voice parameters you may want to modify in the voice data of your patches. These — = parameters include the volume and pan value, among others. The location of the volume parameter | varies with the type of patch you are editing. The pan parameter is always the four rightmost digits in the last parameter in a patch. You can adjust mm =[the][ pan][ value][ between][ 00000000][(pan][ full][ right)][ and][OOOO7FFF][ (pan][ full][left).][ Setting][ this][ parameter] |, 10 00003FFF centers the balance. } Refer to the Jaguar Synth document for descriptions of these and other parameters for the type of patch fF you are adjusting. | d. Extract envelope data to separate ASCH files. Edit each ASCII patch file you created that uses envelopes (such as EM envelope and sample patches). ' Within each file, locate the envelope data that your patch actually uses. Envelope data is located in the | fille after the patch and user waveform data. j Each ASCII patch file contains data for the envelope used in your sound (_env0 - _env7). ' Once you have located the envelope data for your patch, create a separate file and copy the data into the ; file. Do this for each patch that uses an envelope. We suggest you name each file as patch. env, where patch is an abbreviation of the patch name { associated with the envelope. Write down the file names for future reference. You will need to include : these file names in synth.-s. ig When saving an envelope data file, we suggest you place it in one of two directories, env OF 7 slopeenv. Place envelopes you extracted from sample envelope patches in slopeenv directory. 7 Place all other envelopes in the env directory. 

; . : : | : : : | i i : : : 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


j { 

© 1995Atari Corp. 

Confidential Information JPR Property ofAtari Corporation 

26 April, 1995 

: Page 44 1 OT | e. Extract user waveform data to separate ASCII files. 

Libraries : ~ c . in | = 4 into F = | | i the : @ data ‘ 1 data q 9 mz" the = | 3 4 ' ] | : j i | = for : a 7 | = © . J Corp. F . : 

| | ; q ' : ' 

} j 

Edit each ASCII patch file you created that uses a user waveform. Within each file, locate the user waveform data that your patch actually uses. User waveform data is located after the envelope data in the file. Once you have located the wavetable data for your patch, create a separate file and copy the data into the file. Do this for each patch that uses a user waveform. 

We suggest you name each file as patch.wav, where patch is an abbreviation of the patch name associated with the user waveform. Place these files in a directory called waveform. Write down the file names for future reference. 

f. Extract wavetable data to separate ASCII files. 

Edit each ASCII patch file you created that uses a wavetable. Within each file, locate the wavetable data that your patch actually uses. Wavetable data is located after the patch data in the file. 

Once you have located the user waveform data for your patch, create a separate file and copy the data into the file. Do this for each patch that uses a wavetable. 

We suggest you name each file as patch .tbl, where patch is an abbreviation of the patch name associated with the user waveform. Place these files in a directory called wavetabl. Write down the file name for future reference. 

_ a. Set the number of patches. ) Set the dc.w value under patches: : to be the number of patches you are using. For example: patches:: de.w 7 ; NUMBER OF PATCHES b. Include patch data files. Once you have created separate ASCII patch files include the file names in synth. s. The location for including these patch files is labeled in synth.s as patches: : 

It is important to realize the order in which you put your patches in synth.s defines the patch number used by the Jaguar. For example, the first patch in synth.s will be patch 0. 

| 

26 April, 1995 

Confidential Information FER Property ofAtari Corporation 

© 1995 Atari Corp. 

Page 45 

; 

[ 

] 

## Libraries 

1 ; Patch 0 | a .include ‘patches\\strlow.ptc' ; strlow patch ( \\ is needed because \ is a j 3 ; special character )}. uses ‘sstrlow' sample , 4 ; and 'estrlow' envelove 

@ = For acomplete example of this file, see the Example Files section. 

## | a Write down patch numbers. 

. 

@ = Write down the numbers for the patches you add. You will need to know these numbers when you @ modify parse.cnf to map your MIDI channel numbers to the actual patches you use. 

## d. Add sample labels and include sample files. 

M@ 

Add labels for your samples and include your sample files. The labels you choose must match those you | — specified in your ASCII sample patch files. For example: 

Me s_strlow: oa eincbin “"samples\\synstrgs.cmp" ; sample used in patch 0 

## e. Initialize the voice table to the correct number of voices. 

Add a zero to the voice table field that is the last voice to be used. For example, the following table places a zero at voice 7, indicating eight voice polyphony: 

. 

- ORG tablestart 

j TABSSTART: : ; DO NOT EDIT THIS LABEL de.l -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,6,0,0,0,0 ; voice 0 de.1 -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 + voice 1 dc.l ~4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice 2 de.l -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice 3 dc. ~4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 3 voice 4 de. -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 3 voice 5 de.l -4,0,0,6,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice 6 ] de.l 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 3 voice 7-LAST j dc.1 -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 : voice 8 i de.1 -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 3; voice 9 a dc. ~4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice 10 : de.1 -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice 11 dc.1 -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice 12 dc.1 -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice 13 dc.1 0 

**==> picture [1 x 12] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


} 

© 1995 Atari Corp. 

Confidential Information “JPR Property ofAtari Corporation 

26 April, 1995 

] 

Page 46 

Libraries 

j a 

| 4 | a gg 

| | 

Add labels for your envelopes and include your envelope files. The labels you choose must match those 1 | you specified in your ASCII patch files that use the envelopes. ra h. Add wavetable labels and include wavetable files. i Add labels for your wavetable and include your wavetable files. The labels you choose must match 1 7 those you specified in your ASCII patch files that use the wavetable. } | Step?. Add MIDI information to parsevent. es Edit the file parse.cnf to set the polyphony of your music, map your MIDI channels to the voice ve numbers you set in synth.s, define the note ranges for your voices, and transpose your tracks if { 4 3 necessary. The format for entering this information is: _ n = note polyphony _ j MIDI_channel - 1: note_range patch number transpose value value : 1 : MIDI_channel - 1 1 sets the MIDI channel number. You must subtract one from the MIDI channel number. You must subtract one from MIDI channel number. You must subtract one from channel number. You must subtract one from number. You must subtract one from You must subtract one from must subtract one from subtract one from one from from it since the Jaguar since the Jaguar the Jaguar Jaguar i a 2 voice numbers are zero-based. numbers are zero-based. are zero-based. zero-based. = note_range sets the range of notes played bya particular sound. This allows you to achieve the same a ; effect as a split keyboard or a drum machine in which one MIDI channel is used but different sounds are 4 triggered depending on the notes played. For example, for MIDI channel 1, MIDI note 36 may trigger a = kick drum sound, while MIDI note 38 will trigger a snare. _ patch_number is the number of the patch the number of the patch number of the patch of the patch the patch patch to use based on the sounds you defined in synth.s. use based on the sounds you defined in synth.s. based on the sounds you defined in synth.s. on the sounds you defined in synth.s. the sounds you defined in synth.s. sounds you defined in synth.s. you defined in synth.s. defined in synth.s. in synth.s. synth.s. | = j transpose_value is the amount in which to transpose the defined note range The transposition isinone 3 7. 4 note increments and can be either positive or negative A value of 12 will transpose up an octave, avalue { a4 of -12 will transpose down an octave, and a value of 0 will leave the notes untransposed For example: re | n= 8 ; 8 note polyphony | 4 O: 36-36 0 0 ; kick _ | 0: 42-42 1 0 ; clsdhat ] E 26 April, 1995 1995 Confidential Information Information “7O® Property ofAtari Corporation ofAtari CorporationAtari Corporation Corporation ©1995 AtariCorp. | eS4 

| 

| ' | i ‘ j 

| 

| 

## f. Add waveform labels and include user waveform files. 

Add labels for your user waveform and include your waveform files. The labels you choose must match those you specified in your ASCII patch files that use the waveform. 

## g. Add envelope labels and include envelope files. 

n = note polyphony _ _ MIDI_channel - 1: note_range patch number transpose value value MIDI_channel - 1 1 sets the MIDI channel number. You must subtract one from the MIDI channel number. You must subtract one from MIDI channel number. You must subtract one from channel number. You must subtract one from number. You must subtract one from You must subtract one from must subtract one from subtract one from one from from it since the Jaguar since the Jaguar the Jaguar Jaguar voice numbers are zero-based. numbers are zero-based. are zero-based. zero-based. 

patch_number is the number of the patch the number of the patch number of the patch of the patch the patch patch to use based on the sounds you defined in synth.s. use based on the sounds you defined in synth.s. based on the sounds you defined in synth.s. on the sounds you defined in synth.s. the sounds you defined in synth.s. sounds you defined in synth.s. you defined in synth.s. defined in synth.s. in synth.s. synth.s. 

n= 8 ; 8 note polyphony O: 36-36 0 0 ; kick 0: 42-42 1 0 ; clsdhat 26 April, 1995 1995 Confidential Information Information “7O® Property ofAtari Corporation ofAtari CorporationAtari Corporation Corporation 

| ] For a complete example of this file, see the Example Files section. f Sigp8 Run'the parse 'programite parse your MIDI Mies, ] | Normally you would edit the makefile file for your project to include the names of your MIDI files so ; q that the PARSE tool is called automatically when required. See the makefile for the sample programs 4 j for examples of this. However, you can also run the PARSE utility directly from the commandline if f necessary. Type the following command to parse your MIDI files: 

: 

**==> picture [110 x 38] intentionally omitted <==**

**----- Start of picture text -----**<br>
[46-46] [2] [0]<br>| 4 [WM).] Libraries<br>**----- End of picture text -----**<br>


**==> picture [52 x 15] intentionally omitted <==**

**----- Start of picture text -----**<br>
; openhat<br>**----- End of picture text -----**<br>


## Page47 

## parse -q yourMIDIfile 

The -q is an optional flag to suppress the output of the parse command. If you want to examine the parsing process as it occurs, do not use this flag. The parse output will be displayed to the screen. You can also redirect this output to a file so you can inspect it later. The parsing information may be useful for finding a problem if your MIDI file does not play correctly. ; q __Acommon error you may see is that note on or note off has failed. This occurs when the polyphony of y q t your MIDI file exceeds the polyphony you defined in parse.cnf. If this happens, increase the polyphony _ "value (if possible) or reduce the polyphony in your MIDI file. 

, 

## i See also the PARSE utility release notes (in the JAGUAR\DOCS directory). | Sige: AHS Y testing your musie one Section at atime, wun the merge toolto == combine yoursections, 

Merge your separate MIDI sections into one file. Use the merge tool to do this as follows: 

| 

, 

merge merged file input_filel.out input_file2.out ... 

| 

where merged[_file][is][the][resulting][ merged][ MIDI][file,][and][ input][files][are][the][parsed][output][files][of][ your] individual sections generated by the parse program. 

Normally, you would edit your project’s makefile so that the MERGE tool would be called by the MAKE utility when appropriate. 

Edit the makefile and change the file name of the MIDI file you are processing. For example: 

Page 48 

Libraries 

| = ij 4 

| ) 

MIDIFILE = cscale 

; 

] Zs 

| ! ! 

| ] ' 

j : i 4 

| 

; 

“_ i] 

: ’ - : . q 4 3 , : | | = 1 .- . 4 —_ 

| | 

For a complete example of this file, see the Example Files section. 

Note: Do not change anything else in the makefile unless you are familiar with how it works. Changing other text , spaces, or tabs in this file may cause it to not work correctly. 

Step 11, Mun the mske tooleed Run the make program as follows to create the file test .cof. This file is the executable version of | your music for the Jaguar. Type: ] 

make 

Run the debugger rdbjag and load the file test .cof. This command will play your music on the Jaguar as it will sound in the actual game. Type the following commands: 

rdbjag 

aread test.cof g 

7 Repeat the steps above as needed to refine your MIDI files, patches, and voice settings. It is often \ necessary to adjust the volume of your instruments and mix between them using the pan parameters. You may also need to adjust the pitch and loop parameters for your samples. 

If necessary, adjust the global or MIDI volume settings in synth.cnf. Also, adjust the tempo. If your music plays too slowly adjust the SCLKVALUE parameter down. If it plays too quickly, adjust the parameter up. For example: 

GLOBALVOLUME equ $7fff MIDIVOLUME equ S7fff SCLKVALUE equ 19 

**==> picture [43 x 22] intentionally omitted <==**

**----- Start of picture text -----**<br>
| -<br>**----- End of picture text -----**<br>


| 

26 April, 1995 

Confidential Information “FAR Property of Atari Corporation 

© 1995 Atari Corp. 

Page 49 

Libraries ' Step 15. Repeat Steps S through 14 until your music plays correctly. j Rerun parse, merge, and make to generate a new test .cof file. Then, run rdbjag, load | test.cof, and type ‘g‘ to play your music. Repeat this process until your music plays correctly. 

; voice type (a The first parameter in the voice data of a sample. The voice type must be $0000002C for 16a bit compressed samples. 

j 

f if The fifth parameter in the voice data of a sample. The end of loop point for the sample. The ~ value for this parameter is: ] ((file_size/2) <<8) - 1 where the file size is the size of the sample you noted in step 9. © 1995 Atari Corp. Confidential Information “JPR Property ofAtari Corporation 26 April, 1995 

## weeanotvocngsanpes 0 

We suggest you minimize your use of samples in your music because they use a lot of memory. | However, if you use samples, you can either use the Sound Tool to create sample patch data for you, or copy the patch data of any sample that already exists in synth.s and modify it as needed. In general, / we suggest you use the Sound Tool to set sample parameters, particularly if you need to adjust loop | parameters, such as beginning, ending, and length of the loop, or if you want to apply a volume envelope to your sample. | If you have not used the Sound Tool to create the voice data for your samples, and instead have copied : data for an existing sample, you must change the following .dc.1 parameters of the sample voice: 

° volume , The second parameter in the voice data of a sample. The volume can be any hexadecimal number that occupies the four rightmost digits. The maximum volume is OOOO7FFF. 

° sample label 

The third parameter in the voice data of a sample. The sample label is a label you define to identify the sample in the makefile. This parameter is also known as the start of the sample. 

° sample pitch 

The fourth parameter in the voice data of a sample. The sample pitch is typically $00001000, which indicates no change from the original sample pitch. A value of $00002000 doubles the pitch (raises it an octave) and a value of $00000800 halves the pitch (lowers it an octave). 

**==> picture [2 x 24] intentionally omitted <==**

**----- Start of picture text -----**<br>
)<br>**----- End of picture text -----**<br>


° end of loop point 

26 April, 1995 

Page 50 

Libraries 

: , bi é | ; j : ] 

| | | ; 

{ 

s 

: 

j . 

: 4 = f 4 q 4 | a | a _ | } : | = 2 3 | oa | a 

| : ' 

q 

| 

| | 

## ° loop length 

The sixth parameter in the voice data of a sample. The loop length for the sample. The value for this parameter is also: 

((file_size/2) <<8) - 1 

. end of sample 

The ninth parameter in the voice data of a sample. The end of sample point for the sample. The value for this parameter is also: 

((file size/2) <<8) - 1 

- . sample envelope label 

The tenth parameter in the voice data of a sample. The label of the sample envelope as defined in tables.das: 

- During game play, you may want one or more of your MIDI files to repeat until the player completes a task of moves to another level. To do so, you need to add loop parameters to your MIDI file before processing it. The following procedure describes how to add this information. 1. Identify the point in your MIDI file where you want to start looping. This is called the loop target. At that point in your MIDI file, insert a MIDI controller 12 event with a value of the target number (for example, a 0 for the first target, a 1 for a second target (if any). 

- 2. Locate the position in your MIDI file where you want to stop looping. At this point in the file, insert a MIDI controller 13 with a value of the loop target you defined in Step 1. 

3. Insert a MIDI controller 14 event with a value of the number of times to loop (up to 127 times). If you set the value to a negative number, the MIDI file will loop forever. Insert controller 14 right after the controller 13 event. 

4. You can loop for longer than the value you assigned for controller 14 by setting the loop count value in synth.s. For example, setting this value to 128 will cause the MIDI file to loop infinitely. 

**==> picture [40 x 18] intentionally omitted <==**

**----- Start of picture text -----**<br>
| a<br>**----- End of picture text -----**<br>


26 April, 1995 

Confidential Information 7% Property of Atari Corporation 

© 1995 Atari Corp. 

Page 51 

| Libraries 

: SYNTHPATH = /jaguar/music/fulsyn q gocceeceseseses ses Se ssa SSe ss ee seem sasansa } # Use ‘erase’ and ‘rename’ on MS-DOS / # Use ‘rm' and ‘'mv' on Atari w/ csh | ERASE = erase | RENAME = rename 

}. # MIDI FILE WITHOUT EXTENTION (!!) 3 eresesence se Se SSS SSeS SSS SSS SS SSS SS SST SERS MIDIFILE = cscale ’ # MIDI Parser flags 4 #eeceeeenaeseSs SSeS SSS SSS SSS SSS SSS SSS TS SRSS | PARSERFLAGS = -¢ j # Assembler & Linker flags MACFLAGS = -fb -i$(SYNTHPATH) ;$(MACPATH) : ALNFLAGS = -g -e -1 -a 802000 x 4000 q # Default Rules ' #neewee ass eseneewee ass ese ass ese se RSS ESSE SSS TSS SST SSS SSRI SSRE RSS ESSE SSS TSS SST SSS SSRI SSRE ESSE SSS TSS SST SSS SSRI SSRE SSS TSS SST SSS SSRI SSRE SST SSS SSRI SSRE SSS SSRI SSRE SSRE : . SUFFIXES: .scer .mid smid.scr: : parse $(PARSERFLAGS) -o S*.out $*.mid iG mac $(MACFLAGS) -o$*.scr $*.out S(ERASE) $*.out 7 F-3eee re sieeee re sie re sie sie Se SSS SSS SSS SSS SS TSS SSIS SS TSS SSIS TSS SSIS SSIS SS ‘ .SUFFIXES: -out .mid 

F The following code listings are examples of the four files (makefile, parse.cnf, synth.cnf, ; andsynth.s) you need to modify when preparing music for the Jaguar. 

**==> picture [1 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
;<br>**----- End of picture text -----**<br>


# Makefile MUSIC DRIVER Josonsecesesssseessssese ssa ssss ss asamaasass 

- # Default Rules #neewee ass eseneewee ass ese ass ese se RSS ESSE SSS TSS SST SSS SSRI SSRE RSS ESSE SSS TSS SST SSS SSRI SSRE ESSE SSS TSS SST SSS SSRI SSRE SSS TSS SST SSS SSRI SSRE SST SSS SSRI SSRE SSS SSRI SSRE SSRE 

F-3eee re sieeee re sie re sie sie Se SSS SSS SSS SSS SS TSS SSIS SS TSS SSIS TSS SSIS SSIS SS .SUFFIXES: -out .mid 

© 1995 Atari Corp. 

Confidential Information “FO Property of Atari Corporation 

26 April, 1995 

| 

Page 52 

Libraries 

| @ - q P ‘ij 4 

} | ‘ 4 

- 

: \ ; 

| 4 , 3 j q . 4 = _ jf 4 ‘ << 

4 

‘ = :- @ | a : a ] q A q 3 4 4 = ] a i ; Bo | Bo 4 od 

| . a 7 

1 

| 4 

| 

q a |} = 

-mid.out: parse $(PARSERFLAGS) -o $*.out $*.mid 

. SUFFIXES : -ser .out 

.out.scr: mac $(MACFLAGS) ~-oS$*.scr $*.out 

. SUFFIXES: .0 .S 

mac $(MACFLAGS) $* 

-SUFFIXES: 

-o} .das 

-das.oj: mac $({MACFLAGS) -o$*.oj $*.das 

FULSYN = $(SYNTHPATH)/fs5 **0** .0j2_ OBJS = driver.o synth.o $(MIDIFILE).scr SCORE = S$(MIDIFILE).scr EXEC = test.cof 

# EXECUTABLES 

$(EXEC): $(OBJS) $(FULSYN) aln $(ALNFLAGS) -o $(EXEC) $(OBJS) $(FULSYN) 

$aseaecsssSSSSSSSe SaaS SSS SSS SSS SSS SSS # Dependencies 

driver.o: driver.s synth.cnf $(SYNTHPATH)/fulsyn.inc 

synth.o: synth.s synth.cnt $(SYNTHPATH)/fulsyn.inc 

$(MIDIFILE).scr: $(MIDIFILE) .mid 

$(FULSYN) : $(SYNTHPATH)/£s02_50.das synth.cnf $(SYNTHPATH)/fulsyn.inc mac $(MACFLAGS) -o$*.oj $*.das $=saaSeeresssSSSSsSeesees Ss SSeS # EOF Ge ee 

* File: parse.cnf 26 April, 1995 Confidential Information “7®® Property of Atari Corporation 

© 1995 Atari Corp. 

Page 53 

\@uemme * Description: MIDI information file for the parse utility. f 

pw * Project: 

* Composer: ; * Date: FO | | * Format: Change the data in this file according to the @. following format. @ =, | | * n = max notepolyphony (default is 8 note polyhony} - * midi channel - 1: lowest_note - highest_note patch_number transpose value a * 

i... 

q a 

4 ; ALL RIGHTS RESERVED. :J : q; - ; Configuration for Fulsyn. 7 ; To save DSP memory, turn only those module on that are needed. 3 

**==> picture [66 x 14] intentionally omitted <==**

**----- Start of picture text -----**<br>
Libraries<br>**----- End of picture text -----**<br>


- PF on=8 ; 8 note polyphony 

- @ = 0: : 36-36 0 0 ; kick | 0: 42-42 1 0 ; clsdhat 3 0: 38-38 3 0 ; snare q 3: 43-55 6 0 :; bass 

## LULrrrt‘“SO.._—=sprCsCiCsCsC(wNCC(iONO”COONNiCCNCCCCNCNCCCNCNCOCCCCiCsCwCOCCitCC 

pn ; This is a simple sample program to play a tune on the synth code. 

- 4 f 

- r ; ; MODULE: SYNTH CONFIGURATION FILE _ : DESCR: THIS FILE CONTAINS THE FULSYN CONIFGURATION 

- , 3 ; (WHICH MODULES TO INCLUDE), GLOBAL VOLUME, SCLK, etc. Fg , WW ~—s;;, COPYRIGHT 1992,1993,1994 Atari U.S. Corporation | 4 ; UNAUTHORIZED REPRODUCTION, ADAPTATION, DISTRIBUTION, = 3 PERFORMANCE OR DISPLAY OF THIS COMPUTER PROGRAM OR 4 ; THE ASSOCIATED AUDIOVISUAL WORK IS STRICTLY PROHIBITED. 4 ; ALL RIGHTS RESERVED. 

] ON equ 1 | OFF equ 0 q FMSIMPLE_MOD equ ON q FMCMPLX_MOD equ OFF ; FMENV_MOD equ ON WAVEFM_MOD equ ON WAVEFM2_MOD equ ON WAVETAB MOD equ ON q SMPL8_MOD equ OFF Mr SMPL16_MOD equ OFF CSMPL16 MOD equ ON : SMPLENV_ MOD equ OFF a CSMPLENV_MOD equ ON 

Mr 

:; a ©1995 Atari Corp. Confidential Information ‘FER Property ofAtari Corporation 26 April, 1995 

Page 54 

Libraries 

f- , : 

| 

44 | : = | |j = - 4 

4 P 

q ae ee : @ | 4 | a | a _ | = Pe | @ | _ _ | | } = j Eo 4 oo | 3 mz | 8 . é _ q e : a 4 cS | a 

i : | : j | | . | : | 1i : 1 

- ; The following is for the note on/off modules. 

- ; This section does not need to be edited. 

**==> picture [557 x 657] intentionally omitted <==**

**----- Start of picture text -----**<br>
1 WAVEFM_NOTEFMCMPLX NOTE equequ  FMCMPLXWAVEFM MODMOD+ WAVEFM2 MOD :<br>FM_NOTE equ  FMSIMPLE_MOD + FMENV_MCD<br>. SMPL NOTE equ  SMPL8_MOD+SMPL16_MOD+CSMPLi6_MOD+SMPLENV_MOD+CSMPLENV_MOD<br>WAVETAB NOTE equ WAVETAB MOD<br>; SET GLOBAL & MIDI VOLUME<br>‘ MIDIVOLUMEGLOBALVOLUME equequ S7fff S$7fff<br>: ; SET SCLK<br>re<br>SCLKVALUE equ 19<br>pe<br>; EOF<br>) synths<br>q Fn nn mn nn nn ee nn nH ee<br>; ; This is a simple sample program to play a tune on the synth code.<br>;<br>; MODULE: SYNTH DATA FILE<br>; DESCR: THIS FILE CONTAINS THE PATCHES, SAMPLES, ENVELOPES,<br>; USER WAVEFORMS AND AN INITIALIZED VOICE TABLE.<br>| ; COPYRIGHT 1992,1993,1994 Atari U.S. Corporation<br>i ; UNAUTHORIZED REPRODUCTION, ADAPTATION, DISTRIBUTION,<br>: ; PERFORMANCE OR DISPLAY OF THIS COMPUTER PROGRAM OR<br>| ; THE ASSOCIATED AUDIOVISUAL WORK IS STRICTLY PROHIBITED.<br>: ; ALL RIGHTS RESERVED.<br>Jomo mote monn aa nn mn en<br>j oon nanan +a-- === =~ === == += +- ++ +--+ - 2 == === === ===<br>; INCLUDE FILES<br>aaaaaiateiata aaa eee teeteeteeeeateterieteetataiaaietaataaeemmmaamaemen<br>-include ‘jaguar.inc'<br>. include ‘fulsyn.inc’<br>. - include *synth.cnf'<br>Boro enn rcrn  R te a<br>; DATA SECTION<br>joann a a<br>-data<br>.even<br>FRR RK IH I KIRK RIK RK EK KKK KEK EEE KEKE KEE EKEEKHKE KE KEK KKK<br>pe EDIT AFTER THIS POINT ‘ +e<br>FREER RE EKER KKK EEE KER EE KKK EEE KEE KEE IK RE EKER KEKE KEKKK KKK KEE<br>26 April, 1995 Confidential Information FER Property ofAtari Corporation © 1995 Atari Corp.<br>**----- End of picture text -----**<br>


**==> picture [20 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
rs<br>**----- End of picture text -----**<br>


Page 55 

## Libraries 

YP nmnn f ; PATCHES i ge I I ; patches:: 7 de.w 1 ; NUMBER OF PATCHES P+ Patch 0 .include 'patches\\strlow.ptc’ ; strlow patch ’ ; uses ‘'sstrlow' sample 4 ; and 'estrlow' envelope 3 gee en a } + SAMPLES ; pe I | strlow_s: .incbin "samples\\synstrgs.cmp” j; sampie used in patch 0 | pen nn nn PF +++ START OF DSP SECTION +++ a ga q -DSP | TABS_COPY:: i de.l TABSSTART ; DO NOT EDIT THIS LABEL de.l TABSEND - TABSSTART ; DO NOT EDIT THIS LABEL 

**==> picture [1 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
;<br>**----- End of picture text -----**<br>


eR q ; INITALIZED VOICETABLE + A zero in the first field tells FULSYN that this is the last voice : 3; to be used! -ORG tablestart 

- TABSSTART: : ; DO NOT EDIT THIS LABEL de.l -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 3; voice 0 

- : de. -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice i 

- | dc.1 -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 3 voice 2 j de.1 -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 3 voice 3 | de.l -4,9,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice 4 dc. -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 3 voice 5 

- j dc.1l ~4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice 6 de.l 0,0,0,0,0,0,0,0,0,0,0,6,0,0,0,0,0,0,0,0 5 voice 7-LAST 

- : de.1 ~4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 3; voice 8 : de.l -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice 9 dc.l -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice 10 

- q dc.1 -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 ; voice ll 4 de.l -4,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,9,0,0 ; voice 12 j dc.l 0 

**==> picture [327 x 36] intentionally omitted <==**

**----- Start of picture text -----**<br>
* ga a<br>; ; USER WAVEFORMS<br>; pa<br>**----- End of picture text -----**<br>


pa I ©1995 Atari Corp. Confidential Information FER. Property of Atari Corporation 26 April, 1995 

j | 

Libraries 

; 3 . ) ¥ : 4 q 4 | 3 , 4 ’ 2 1 : | 4 4 | : 

| 

; 4 

j 

| 

## Page 56 

~ Oo ~ 

igen, 7 ENVELOPES ateaiasiaeiaibaiataieiaialatatatatetatatetatatetataetaaiaaatataetaaaamaamamataiaaamemeeeteeee 

strlow_e:: -include "slopeenv\\string5.env"” ; envelope used in patch 0 

9 RK KK He HH RK KI II TK TK KKK IK KEKE KEK KKK ERE K RK ERK K RRR EK iehel EDIT UP TO THIS POINT * RR He HR KK IKK HTK KIKI KEKE KEKE KEE KK ERE EEEKEARKAKKEK KKK KKK 

; have slop for sloppy loader ~de.l 0,0 TABSEND: : ; DO NOT EDIT THIS LABEL -de.l 0 end 

**==> picture [10 x 18] intentionally omitted <==**

**----- Start of picture text -----**<br>
a<br>**----- End of picture text -----**<br>


26 April, 1995 

Confidential Information TER Property ofAtari Corporation 

© 1995 Atari Corp. 

**==> picture [552 x 736] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||
|---|---|---|---|---|---|---|---|
|:|’ Libraries|Page|57|
|- EEPROMAccessLibrary|
|:|The Jaguar provides several options for game developers to store non-volatile game information such as|
|a|high scores, options, saved games, music/sound effect levels,|etc...|while the unit is powered down.|
|||Standard (Cartridge E°PROM|(128 byes)|ee|
|1|Standard Jaguar Cartridge PCB’s are currently equipped with a 128 byte E?PROM for non-volatile|
|4|storage. Developer Alpine boards also contain a compatible part for use in game testing. These parts are|
|@|tated for approximately|100,000 write cycles before failure though we have achieved a much higher|
|number of successful|writes in our|testing.|
|||i|In order to provide compatibilty with the parts we use in manufacturing, we supply tested code which|
|must be used to access the E2PROM. This code should not be modified in any manner unless prior|
|q|approval is|granted by|Atari Corp.|The JAGUAR\SOURCE\EEPROM directory contains EEPROM.S,|
|=|which has six functions used for reading, writing, and performing checksums on this data. Use of these|
|F|functions requires that a valid stack pointer has been set in A7. These functions are as follows:|
|: —||.|an|
|a (t=|ew|EPROM|acdatress to read from.|
|Register Usage|Preserves|all other registers.|
|}_§|PurposeReturns|dO.wThis function = Value reads read|one 16-bit word (address #0-62) from the E°-PROM. This function|
|=|pays no attention to the checksum and therefore has no|way to be sure the data is|
|S|valid. A call to eeValidateChecksum|will ensure that successive calls to|
|7|eeReadWord will|return valid data.|
|Se|an|
|3|di.w|E-PROM|address to write to.|
|dO.w__|Data to write.|
|||
|3|Register Usage|Preserves|all other registers.|
|4|Returns|do.w|0» Successful.|
|j|1|-> Write failed.|
|4|Purpose|This function attempts to write one 76-bit word (address #0--62) to the E*PROM. This|
|g|function does not update the checksum and will thus cause any subsequent calls to|
|4|eeReadBank or eeValidateChecksum to fail. The function eeUpdateChecksum|
|must be used after any series of eeWriteWord calls to make the checksum valid|
|4|
|]|again.|
|fr|a0.)|Address of a buffer 63 16-bit words in length to receive data from the|
|ge|E°PROM.|
|:|Register Usage|Preserves|all other registers.|
|7|do.w|04 ->— Successful.Checksum|invalid.|
|'|
|q|© 1995|Atari Corp.|Confidential|Information|PPR|Property ofAtari Corporation|26 April, 1995|

**----- End of picture text -----**<br>


Libraries 5 q OO CU = - , a ; | the g only 4 4 4 |g j q , a 4 the ] | 

2 : 4 q 1 : j 

7 : ‘ 

] 

**==> picture [592 x 462] intentionally omitted <==**

**----- Start of picture text -----**<br>
j Page 58 Libraries<br>| Purpose This function reads 63 16-bit words from the EPROM into a supplied buffer and<br>| validates the data against the stored checksum to ensure the data read is good.<br>eTCia NNCTi‘(‘i(C}RNVY’NRNRNRNAONNORONCNCriCiCCNCzCi(iyRO OO CU<br>2 a0.|__Address of a buffer containing 63 16-bit words to write to the E7PROM. =<br>Register Usage Preserves all other registers. -<br>Returns d0.w 0 -» Successful. , a<br>1 — Write failed. ; |<br>Purpose This functions stores 63 16-bit words supplied to it in the E-PROM, checksums the g<br>data, and stores the checksum at address #63. We recommend that this function only 4<br>be used when a large amount of data needs to be stored since this counts as 64 4<br>writes against the 100,000 rated limit. If you only change a couple of words, use 4<br>eeWriteWord(s) followed by eeUpdateChecksum. |g<br>j ecUpdateChecksumOU<br>Register Usage Preserves all other registers. j q<br>Returns d0.w 0 Successful. , a<br>Purpose 1 -» Checksum write failed. 4<br>This functions checksums the first 63 16-bit words from the E*PROM and stores the ]<br>checksum at address #63. |<br>7 Register Usage Preserves all other registers. : 4<br>Returns d0.w 0O- Successful.<br>Purpose 1 — Checksum invalid. | a<br>This function checksums the first 63 16-bit words from the E-PROM and compares :<br>: the checksum to the value stored at address #63. This function does not change any |<br>stored data. |<br>**----- End of picture text -----**<br>


**==> picture [41 x 342] intentionally omitted <==**

**----- Start of picture text -----**<br>
: 4<br>x<br>| a<br>: P ;<br>|<br>|<br>| g<br>7<br>: a<br>[=<br>: a<br>] Pa-<br>. 7<br>§ a<br>**----- End of picture text -----**<br>


We are currently in the design phase of a new cartridge PCB which will contain a 16k E7PROM. Thirdparties will be able to request this PCB to provide access to the greater amount of storage. Because this project is still under development, no further details are available yet. Atari will notify developers when this part becomes available. | CD-ROM NV-RAM Storage Cartridge =§g#=#§ |. Because CD-ROM titles do not normally have access to non-volatile storage, Atari will be making j scoresavailableand a Flash game ROMinformation. cartridgeThe asprot a c **o** colsnsumerfor productaccessing thatthis give end-userscartridge are thegiven optionin the to NV-RAM save high Cartridge Access Library section. 

**==> picture [77 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
© 1995 Atari Corp.<br>**----- End of picture text -----**<br>


26 April, 1995 

Confidential Information “7O® Property ofAtari Corporation 

| 

| 

, Libraries Page 59 Ceea a Cartridge Access Library Because CD-ROM titles do not normally have access to non-volatile storage, Atari will make available a } special NV-RAM cartridge as a consumer product. This will give end-users the option to save high scores, setup options, and saved game information for their CD-ROM games. This cartridge is accessed by your program through the NV-RAM cartridge library. 

| These calls are provided to allow developers writing CD-ROM based games to save game information | into a special cartridge containing non-volatile Flash ROM memory in an efficient and easy to use } manner. There will be 128K bytes available in NV memory in the first version of the hardware (later ! cartridges may include more or less memory, so developers should use the Inquire function to } determine the actual space available). This memory will be used and allocated in a file system-like } manner, so that multiple games may use the same non-volatile memory cartridge without conflict, and } so that different cartridge sizes may easily be supported. The NVM_Bios calls are thus much like the } GEMDOS or MS-DOS file system calls. | The length of each block of memory is some multiple of 512 bytes. Memory blocks must be given a | _ size when they are created, and cannot exceed that size later. The total number of memory blocks M depends on the size of the cartridge being used, but as long as you use the NVM_Bios calls you will be z able to deal with whatever is available. 

A memory block is uniquely identified by two strings: the application which created it, and a block| specific name (its "filename"). The application name is available so that users may quickly identify which applications are associated with which blocks of memory. Application names may be up to 15 characters in length, and file names may be up to 9 characters in length. Both application and file | names must use only characters chosen from the following 40 character set: 

. 

## ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:'. 

space 

There are eleven calls provided to access NV memory. When the calls are available, a magic cookie with the value ' NVM' (OxSF4E564D) will exist at address $2400, and a dispatcher will exist at $2404. To invoke a function, do a 68000 JSR to location $2404 with the opcode and parameters described on the following pages. 

**==> picture [536 x 140] intentionally omitted <==**

**----- Start of picture text -----**<br>
| All of the functions return a 32 bit value in dO, although in many cases only the lower 16 bits will be of<br>interest. If bit 31 of dO is set (i.e. if dO.1 is negative) then an error has occured. The following error<br>} codes are defined:<br>Error Name Code Description<br>ENOINIT | [-1_|] [the] [Initialize][ function] [has] [not yet][ been] [called]<br>ENOSPC [—-2__| there is not enough free space for the operation<br>EFILNE P__-3__| the file was notfound<br>aa<br>© 1995 Atari Corp. Confidential Information JER. Property ofAtari Corporation 26 April, 1995<br>**----- End of picture text -----**<br>


**==> picture [2 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


: Page 60 The following following functions are following functions are functions are are ) | Cc i 68000 Assembly q : Purpose . a t ‘ , q q { 1 q ' | q ‘ | [: a | 26 April, 1995 April, 1995 1995 

Libraries | 4 Error Name Code Description q - The following following functions are following functions are functions are are available: 4 Function Opcode P_intiaize | | [Open | Close 3 , | Cc ,”,,h”r””CC‘(Ci<lORONSCOVWQONSONCSCOCUiiin | @ 68000 Assembly pea work_area y 2 move.w __ #0,-(Sp) | = jsr NVM_Bios a adda. #10,sp | = Purpose Initialize must be called before any other NVM_Bios function. Its purpose is to a initialize the NVM_Bios system, and also to identify the current application to the 1 S NVM Bios. The application name (a null terminated string satisfying the rules listed a above) is passed as the parameter app_name. All subsequent Create and Open q o operations will use this application name for the memory blocks being created or _ opened. The second parameter (work_area) must point to a 16K, phrase aligned n buffer which the NVM Bios may use as a scratch buffer. Applications need not Poe preserve the contents of this memory between NVM_Bios calls (i.e. they can also poe use it for other purposes when not using the NVM Bios) but they must be aware that , the buffer will be modified by ali NVM_Bios calls. In other words, you can do what - 4 you want with the 16K between NVM_Bios calls, but every time you call NVM_Bios the 16K will be trashed. j It is legal to call Initialize more than once, indeed, this is the only way for applications ( to open another application's memory blocks or for an application to change the q location of the 16K NVM_Bios buffer. Please note that calling Initialize will invalidate j all currently open handles (returned by Create or Open). 4 All other NVM_Blos functions will return ENOINIT if called before the first call to Initialize. 7 26 April, 1995 April, 1995 1995 Confidential Information “FO® Property of Atari Corporation © 1995 Atari Corp. | 

The following following functions are following functions are functions are are available: 

Page 61 E Libraries — a ees F [68000 Assembly move.| file_size f | Example pea file_name 4 move.w _ #1,-(sp) q jsr NVM_Bios adda.| #10,sp y | Returns A non-negative handie on success q ENOINIT if the Initialize function has not yet been called ; ENOSPC if there is insufficient room to allocate the file ; ; | Purpose Create should be used to allocate a specified number of bytes from backup memory. j | The parameter file_name should point to a name for the memory block. If the current 4 application (specified by the Initialize call) already has a memory block with the same. 4 name, then that block will be deleted and a new one created (i.e. the new block wiil — replace the existing one). The file_size parameter should contain the size in bytes q required for the block. This size will be rounded up to the nearest multiple of 256 . 3 | before being used for allocation. : ' Note that multiple applications may have files with the same name, without affecting 4 | one another; Create will only delete an existing file if both the file name AND the 4 | application name match. : The file handle returned by Create must be used in any Read, Write, or Seek calls “ referring to this file. WARNING: do not make this call if there is an existing file handle (returned by a : previous Create or Open call) referring to a file with the same name as the new file : being created. Use the Close call to close all such file handles before re-creating the : file. : LLLLLL OEE : 68000 Assembly pea tile_name q4 Example move.w _ #2,-(Sp) : jst NVM_Bios | adda.| #6,sp | EFILNF if the application has no file with the given name 4 ] Purpose Instructs the Bios to attempt to access the blocks of memory owned by the current q application (as set in initialize) and whose file name is file_name. The file_name E parameter must point to a null terminated file name string of an existing file. As with 4 the Create call, Open will search only for files owned by the current application; it will 4 not open a file owned by a different application, even if the file names are the same. q The handle returned by Open must be used in any Read, Write, or Seek calls i referring to this file. 

1 

© 1995 Atari Corp. 

Confidential Information FER Property ofAtari Corporation 

26 April, 1995 

| Page 62 Libraries | eSr—“‘“COsOstsCOSOSOSOOCSOCOCOCOCOCOCOCOCOCOCisidsisC 

4 

| 9 7 = 4 | @ gs | 4 , 4 } 

7 Example move.w __-#8,-(sp) jsr NVM_Bios : adda.| #4,sp | EIHNDL if passed passed an invalid handle ; Purpose Used by an by an an application to to indicate that it is finished working with a finished working with a working with a with a a file previously opened by Open Open or Create. Create. After the the call to Close, the handle to Close, the handle Close, the handle the handle handle passed to close close 1 becomes invalid, and no further no further further Read or Write Write calls on that on that that handle will succeed. | So rrrC~—t—i—“C™OC—C—COQNCONCCSCis;s«C.:«CUi«Ci«iiéia#SC;C(CiCj 68000 Assembly Assembly pea file_name : Example pea app_name move.w #4,-(sp) isr NVM_Bios i adda.| #10,sp - EFILNF if no file no file file matching the given the given application name and file name name and file name and file name file name name is found found : Purpose Deletes a file, freeing the memory freeing the memory the memory memory associated with with it. Any application may may delete any aa determinedotherother application's by Searchfile, by Searchfile, Searchfile,file, Firstbyby passing and Searchin the and Searchin the Searchin thein the the Next)applicationin app_namename and andfile file_namenameapplicationin app_namename and andfile file_namenamein app_namename and andfile file_namename app_namename and andfile file_namenamename and andfile file_namename and andfile file_namename andfile file_namenamefile file_namename file_namenamename (as ' respectively. J Note that applications that applications applications should never delete files delete files files belonging to other applications to other applications other applications applications specifically requested to do so by the do so by the so by the by the the user . If an an application needs more needs more more space than . is available on the on the the cartridge, then it should should tell the the user and and offer him him or her her the of either aborting the current either aborting the current aborting the current the current current operation or of selecting of selecting selecting one or more files or more files more files files to delete from ‘ the cartridge. cartridge. 3 WARNING: do not make this make this this call if there there is an existing file handle an existing file handle existing file handle file handle handle (returned by a ‘ previous Create Create or Open Open call) referring to the file being deleted. to the file being deleted. the file being deleted. file being deleted. being deleted. deleted. Use the the Close 1 to close close all such file handles such file handles file handles handles before deleting the file. the file. file. 

| @ | 4 | ¥ , 4 ; ‘ . j i ; 2 2 j 7 j i 7 ; 8 _ foo Q - 

| 

— i 3 : q 4 d 

| : 1 E 4 

q 

**==> picture [465 x 112] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||
|---|---|---|---|---|---|---|---|---|
|68000 Assembly|move.w|__ #handle,-(sp)|
|Example|move.w|__-#8,-(sp)|
|jsr|NVM_Bios|
|adda.||#4,sp|
|EIHNDL|if passed passed|an|invalid|handle|
|Purpose|Used by an by an an|application to to|indicate that|it|is finished working with a finished working with a working with a with a a|file|previously|
|opened|by Open Open|or Create. Create.|After the the|call to Close, the handle to Close, the handle Close, the handle the handle handle|passed|to close close|
|becomes|invalid,|and no further no further further|Read|or Write Write|calls on that on that that|handle|will|succeed.|

**----- End of picture text -----**<br>


**==> picture [492 x 258] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||
|---|---|---|---|---|---|---|---|---|---|
|68000 Assembly Assembly|pea|file_name|
|Example|pea|app_name|
|move.w|#4,-(sp)|
|isr|NVM_Bios|
|adda.||#10,sp|
|EFILNF|if no file no file file|matching the given the given|application name and file name name and file name and file name file name name|is found found|
|Purpose|Deletes|a|file, freeing the memory freeing the memory the memory memory|associated with with|it.|Any|application may may delete any|
|determinedotherother|application's by Searchfile, by Searchfile, Searchfile,file,|Firstbyby|passing and Searchin the and Searchin the Searchin thein the the|Next)applicationin app_namename and andfile file_namenameapplicationin app_namename and andfile file_namenamein app_namename and andfile file_namename app_namename and andfile file_namenamename and andfile file_namename and andfile file_namename andfile file_namenamefile file_namename file_namenamename|(as|
|respectively.|
|Note that applications that applications applications|should|never delete files delete files files|belonging to other applications to other applications other applications applications|unless|
|specifically|requested|to do so by the do so by the so by the by the the|user|.|If an an|application needs more needs more more space than|
|is|available on the on the the|cartridge,|then|it should should|tell the the|user and and|offer him him|or her her the|choice|
|of either aborting the current either aborting the current aborting the current the current current|operation|or of selecting of selecting selecting|one or more files or more files more files files|to delete from|
|the cartridge. cartridge.|
|WARNING:|do|not make this make this this|call|if there there|is an existing file handle an existing file handle existing file handle file handle handle|(returned by a|
|previous Create Create|or Open Open|call)|referring to the file being deleted. to the file being deleted. the file being deleted. file being deleted. being deleted. deleted.|Use the the Close|call|
|to close close|all such file handles such file handles file handles handles|before|deleting the file. the file. file.|

**----- End of picture text -----**<br>


**==> picture [505 x 196] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||
|---|---|---|---|---|---|---|
|Readpect|
|68000 Assembly|move.|count,-(sp)|
|Example|pea|bufptr|
|move.w|__ handie,-(sp)|
|move.w|__ #5,-(sp)|
|jst|NVM_Bios|
|adda.||#12,sp|
|number|of|bytes|read|in|dO,|if successful|
|EIHNDL|if passed|an|invalid|handle|
|7|___-__|
|26 April, 1995|Confidential Information|“7O®|Property|of|Atari Corporation|© 1995 Atari Corp.|

**----- End of picture text -----**<br>


| | 

|q 

|:|Libraries|Page63||
|---|---|---|---|
|4<br>4<br>7<br>4<br>j|Purpose|TheRead callmay beused to fill a buffer pointedtobybufptrwithcountnumber of<br>bytesfromthe file specified byhandie (returnedfrom apreviousOpen orClose call).<br>Theread will begin atthe current position inthe file. This position is initialized to 0 by<br>Open orCreate, is incremented byReadand Write (bythenumber of bytes read or<br>written, respectively), andmay bechanged bySeek. Thegamecode must provide a<br>buffer largeenough to hold countnumberofbytes.<br>Ifsuccessful, the cail will return<br>thenumber ofbytes read. Attheend ofthe file (i.e.whenthe file's current position||
||||exceeds its size) 0 bytes will be returned byRead.||
||||ritCCCCCCwtC:«iSistStst—;ists«wtésSC.CXCidszaisCéiCi‘“CN:COtitOisC:CiCiCizsCi<ai‘izi.uiéCC||
|q<br>—<br>.<br>4<br>4|68000Assembly<br>Example|move.|<br>count,-(sp)<br>pea<br>bufptr<br>move.w<br>handle,-(sp)<br>move.w __#6,-(sp)||
|,<br>4<br>_<br>.<br>|<br>_—||jsr<br>NVM_Bios<br>adda.|<br>#12,sp<br>number ofbytes written in dO, ifsuccessful<br>EIHNDL if passed an invalid handle||
|q<br>q<br>‘<br>*|Purpose|The Write callmaybe usedto writecountnumber ofbytesfrom thefilespecified by<br>handle (returnedfrom a previous Open orClose call). Thewrite will begin atthe<br>current position inthe file. This position is initialized to<br>0byOpen orCreate, is<br>| incremented byReadand Write (by the number of bytes read orwritten,<br>respectively), and may bechanged bySeek. The number ofbytes actually written to<br>the file is returned. This may be lessthancount if, forexample, an attempt is made|||
|j||towritemore bytestothefilethanthespace allocated for it inCreate.||
|||Searchfirst|== = =<br>Opeede?||
||<br>4<br>4<br>j<br>;|68000Assembly<br>Example|move.|<br>search_flag,-(sp)<br>pea<br>search_buf<br>move.w __#7,-(Sp)<br>jsr<br>NVM_Bios<br>adda.|<br>#10,sp||
||||||
|‘||EFILNF if no files match the search||



a ©1995 Atari Corp. Confidential Information “7U® Property of Atari Corporation 26 April, 1995 

| Page 64 Libraries | Purpose The Search First call can be used in conjunction with the Seareh Next call to browse B i through the backup memory table of contents. This can be useful for displaying to | the user all of the games whose information is backed up on a given cart. It can also . » be used by a game to obtain application and file names to be used in the Delete call ] | tofinalmakeauthorityroom onon thisa cartridgetype of foraction.its own information. The game player must be given d4 The search_buf parameter should point to a word-aligned 30 byte buffer used as a : : structure as shown below: ; typedef struct 4 { _ long size; | 4 char app_name[16]; . | char _ file_name[10]; 4 | } NV_FILEINFO 3 : If the search is successful, the size field will be filled in with a long word giving the : ' total size of the file. The app _name field will be filled with a null terminated character & string giving the name of the application that created this file. The file_name field will 3 be filled with a null terminated string consisting of the name the application gave to F 4 F the file. These two strings constitute the app_name and file_name parameters for the i 4 Delete call. 4 The search_flag parameter must be either 0 or 1. if it is zero, then the search will 4 ; ‘ include all files on the cartridge, regardiess of which application created them. If it is Pd ‘ one, only files created by the current application (as specified by the last cali to - : Initialize) will be included in the search. The value of search_flag will be used in , ‘ subsequent Search Next calls as well. | - i Ssrrrtri‘CC—COCNCSCdistsés.:«CisCdsCiésYS=UisrisCrisiCisiiéiCtitia ' C Prototype int NVM_Bios( short opcode = 8, NV_FILEINFO *search_buf) ] q q 68000 Assembly pea search_buf ; i Example move.w __-#8,-(sp) | | bi jsr NVM_Bios _ adda.l _#6,sp ] . identical to Search First | Purpose To be used in conjunction with Search First to provide the caller with table of f 4 | contents information. This call can be made successive times until EFILNF is _ f returned in dO. This will mean that no other entries exist in backup memory. : 2 | See the entry for Search First for the definition of the NV_FILEINFO structure. ; a Serrrtr—“‘SCOCCC.UCCC.COCitsa;st«t;C«C«Ci«Ciés.:SUCiéaiCN‘(CO#w;WSCOiléCOCiiwsCtiwzésC'Ctidissicrrrtr—“‘SCOCCC.UCCC.COCitsa;st«t;C«C«Ci«Ciés.:SUCiéaiCN‘(CO#w;WSCOiléCOCiiwsCtiwzésC'Ctidissic TCU Prototype long NVM NVM _Bios( short opcode = 9, short short opcode = 9, short opcode = 9, short = 9, short 9, short short handle, long offset, short flag flag ) q 2 

Serrrtr—“‘SCOCCC.UCCC.COCitsa;st«t;C«C«Ci«Ciés.:SUCiéaiCN‘(CO#w;WSCOiléCOCiiwsCtiwzésC'Ctidissicrrrtr—“‘SCOCCC.UCCC.COCitsa;st«t;C«C«Ci«Ciés.:SUCiéaiCN‘(CO#w;WSCOiléCOCiiwsCtiwzésC'Ctidissic TCU Prototype long NVM NVM _Bios( short opcode = 9, short short opcode = 9, short opcode = 9, short = 9, short 9, short short handle, long offset, short flag flag ) q 2 

26 April, 1995 

Confidential Information APR Property of Atari Corporation 

© 1995 Atari Corp. Jn 

| 

|4<br>’|4<br>’|Libraries|Page65|
|---|---|---|---|
|||68000Assembly|move.w<br>flag,-(sp)|
|.<br>p<br>4<br>f<br>4<br>:|y||Example|move.|<br>offset,-(sp)<br>move.w __ handie,-(sp)<br>move.w<br>_— #9,-(sp)<br>jsr<br>NVM_Bios|
||<br>4<br>Pf<br>;<br>3|||adda!<br>#10,sp<br>the newfile position, ifsuccessful<br>EIHNDL if passed an invalid handie<br>:|
|Fd|||ERANGE ifthe offsetwould be past theend of file|
|j||Purpose|Resetsthe file position (used byReadand Write) forthe filewhose file handle (as|
|——|||returned byOpen orCreate) is handle to be at offset bytes from the beginning ofthe|
|,<br>|<br>,<br>4|||file (ifflag is 0) orfrom the current position inthe file (ifflag is 1}. SubsequentRead<br>or Write calls will begin their operations at this point (and will updatethe file position|
|;<br>||||as usual).|
||i||rlrt~—CO.UCOtCSCSCSsS;sSr«sS:«s—Srsi—SrsiaOiaéS$sSCiésiCiC:i:itsCiiSCiC;isiaC_CiézaK=(C||
|4||Prototype|int NVM_Bios( shortopcode= 10, long*totspc, long“freespc )|
|_||68000Assembly|pea<br>freespc<br>; Ptrto ‘freespc’ variablesomewhere in RAM|
|=<br>4<br>:||Example|pea<br>totspc<br>; Ptrto ‘totspc’ variable somewhere in RAM<br>move.w _#10,-(sp)<br>bsr<br>NVM_Bios|
|Pg|||adda.|<br>#10,sp|
|||Purpose|Inquires aboutthe amount ofspace available on the cartridge. The fotspce parameter|
|a|||points to a long word which is filled in withthe total amount of cartridge memory which|
|.<br>4|||may be used for applications (i.e. the size ofthe largest possible memory block,|
|=|||assuming it is the only memory block onthe cartridge). Thefreespe parameter points|
|rp<br>4|||to a longwordwhich is filled in with the amount of cartridge memory currently free|
|,<br>4|||(i.e. the size ofthe largest memory blockwhich could be created atthe presenttime).|
|;<br>4|||; (Note thattheamount offree memory is notthe only constraint on the Create call;|
|4<br>||||even ifthere is sufficient spacefor<br>amemory block, Create may return ENOSPC if<br>there is noroom left inthe cartridge's table ofcontents.)|
|m||UsingtheNV-RAMSimulatoer<br>=||



The NV-RAM Simulator allows you to use an Alpine board plugged into your Jaguar CD-ROM | development station to simulate a NV-RAM cartridge during the development process. It provides the _ same functions for accessing NV memory as described in the previous section. - The NV-RAM Simulator is normally located in the JAGUAR\NVRAMSIM directory. To use it, load @ @=—_the debugger and then type: { load nvmsim.db : The NVRAM BIOS will be installed into your system and then control will return to the debugger. At | this point you may load and execute your main program. © 1995 Atari Corp. Confidential Information FER Property ofAtari Corporation 26 April, April, 

26 April, April, 1995 

’ - Lf: , 4 {| ; | 4 = ' : : j I 4 , 4 | 7 gg | ’ ] ‘ P 4 |—a , 4 | 2 | q a , 4 

| 1 

ee errt—é—=étEEEWCCC”C;”*™tCOCOCNCiCNiszstsCdiézi(CO ‘(UNCsCisC If you hold down the "Option" key (and keep it held down) before typing the "load nvmsim.db" or “load nvmtest.db” command in the debugger, you will be presented with the Save Cartridge 1 Manager screen. This is a sample application which users will also be able to access in order to delete i files. (Please note that the existence of the Save Cartridge Manager does not excuse individual j applicationsfrom providing similar functionality themselves!!!). The Save Cartridge Manager uses the ; following keys: j up arrow/down arrow Selects files ‘ A,B,C To delete a file | OPTION To choose how to sort files : **OPTION +** 7+91 **To** save preferencescreate a (dummy) infilea file ; OPTION + *+# To erase all files ' OPTION + *+0+# To do a test of free memory “+ To exit the manager | Once the Save Cartridge Manager has run, the BIOS will be copied to RAM (at $2400). You can then i reset the machine and load and run your own application. The BIOS will remain in RAM until the j - machine is powered off. 

## Page 66 

## Libraries 

1 The Alpine board’s memory from $900000 to $91FFFF will be used to hold the cartridge data. A sample disk image (full of files containing random data) is included with the simulator. The file is called DISKIMG.IMG. To load this file, type "read diskimg.img 900000" while in the a debsim **u** latorgger. andThethe debuggersample casc **r** ipttridge NVMTEST.DBfiles in one eai **s** alsoy step. included. It will load both the NV-RAM | ' Keep in mind that the Alpine board’s memory switch must be set for “write enable” in order for the simulator to work. Also keep in mind that any program or debugger script that clears DRAM below $4000 will erase the simulator from memory. 

**==> picture [13 x 23] intentionally omitted <==**

**----- Start of picture text -----**<br>
ba<br>**----- End of picture text -----**<br>


| 

26 April, 1995 

Confidential Information FR Property ofAtari Corporation 

© 1995 Atari Corp. 

