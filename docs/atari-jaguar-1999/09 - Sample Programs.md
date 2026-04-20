Page I 

" 

Sample Programs So sete —s—SS == Fsampe ee ~=—C Programs This section describes the various sample programs that are included with the Jaguar development system which are not a part of the Jaguar Workshop series. Each subsection describes a particular program, and will discuss what the program does, what techniques it is supposed to illustrate, and to some degree how the code works. If you have not read the Jaguar Software Reference Manual already, you should do it before reading this section. Please note that the sample programs are often intended to illustrate a particular idea in an easy to understand way. In most cases, this will not be the fastest method, or use the least memory, because such optimization frequently makes it harder to understand what's going on. Once you understand the Jaguar hardware, you will undoubtedly find a number of ways to do the same thing faster and with less code.Atari is constantly creating new sample code, so in the event that there are changes or additions to the sample programs, there will be README.TXT files located in the SOURCE directory and/or within the specific subdirectory of the sample. You should also check the online services at least every couple of weeks to see what updates and additions are available. Please note that in order to reduce the size of the archives containing the sample programs, the executable program itself is not provided in most cases, the project must be built using the tools in your Jaguar developer’s kit. (This should serve as a useful reality check to be sure your installation is correct.) 

{ | | | | i | : | | | 

© 1995 Atari Corp. 

Confidential Information JER Property ofAtari Corporation 

16 May, 1995 

This program demonstrates how to set up a full-screen bitmap object and then uses the GPU program demonstrates how to set up a full-screen bitmap object and then uses the GPU demonstrates how to set up a full-screen bitmap object and then uses the GPU how to set up a full-screen bitmap object and then uses the GPU to set up a full-screen bitmap object and then uses the GPU set up a full-screen bitmap object and then uses the GPU up a full-screen bitmap object and then uses the GPU a full-screen bitmap object and then uses the GPU full-screen bitmap object and then uses the GPU bitmap object and then uses the GPU object and then uses the GPU and then uses the GPU then uses the GPU the GPU GPU to draw a draw a a j ' Mandelbrot fractal into it. Once the Mandlebrot set has been drawn, has been drawn, been drawn, drawn, a Julia Julia set is drawn, drawn, and the the ; , program then switches back and forth between then switches back and forth between back and forth between forth between between the two images. two images. images. ; ; The 68000 68000 is used to set up the parameters for the GPU, and then the entire screen used to set up the parameters for the GPU, and then the entire screen to set up the parameters for the GPU, and then the entire screen set up the parameters for the GPU, and then the entire screen up the parameters for the GPU, and then the entire screen the parameters for the GPU, and then the entire screen parameters for the GPU, and then the entire screen for the GPU, and then the entire screen GPU, and then the entire screen and then the entire screen then the entire screen the entire screen entire screen screen is drawn by drawn by by the GPU. GPU. ] 4 : As implemented, implemented, the whole screen whole screen screen is drawn in about 5 drawn in about 5 in about 5 about 5 seconds, and could be sped up bya could be sped up bya be sped up bya sped up bya up bya byaa factor of of . @ _ 100% or more with or more with more with with a little more optimization more optimization optimization (like using the DSP to calculate the DSP to calculate DSP to calculate to calculate calculate half the picture while the picture while the while the the | @ GPU calculates the other the other other half). | @ This example is normally found in the JAGUAR\SOURCEVAGMAND directory. Below is a list of all | 2 the files which are included. a Filename Description 3 o : CALCMAND.S | This is the actual Mandlebrot calculation code that runs in the Jaguar GPU. 4 a CRY.PAL This file contains data for a 256-entry CRY-mode color palette for palette-based objects. 4 . : JAGMAND.S This file takes control after the startup code has initialized the system. It creates an object F, list for the background picture, installs an object list refresh routine, and then calls the code | 4 : in MANDLE.S. Poo : MAKEFILE Used with MAKE utility to build executable program file from source code and data files. ‘ .: : MANDLE.S This uses the 68000 to set up the fractal parameters and then calls the GPU to calculate ] . the image. | e 5 STARTUP.RGB | This file is actually in the JAGUAR\SOURCE directory. This is the screen displayed by the | 3 = startup code that is used by several of the sample programs in the Jaguar Developer's Kit. a STARTUP.S Standard Jaguar Startup Code. This module contains all the code necessary to properly ’ 4 initialize the Jaguar hardware and display a simple startup picture. Then it passes control to} # : the_ start label in the JAGMAND.S module. (See the Sample Programs section for j further information on the Standard Jaguar Startup Code.) q 

: _ 

_ i q 7 : | 4 i j 

- Page 2 Sample Programs 4 JaguarMandelbrot/FractalDemo Ni,”rmrmrrCmr—r~—~—...CUi‘i~i*:COSOSCSSRSCOUG This program demonstrates how to set up a full-screen bitmap object and then uses the GPU program demonstrates how to set up a full-screen bitmap object and then uses the GPU demonstrates how to set up a full-screen bitmap object and then uses the GPU how to set up a full-screen bitmap object and then uses the GPU to set up a full-screen bitmap object and then uses the GPU set up a full-screen bitmap object and then uses the GPU up a full-screen bitmap object and then uses the GPU a full-screen bitmap object and then uses the GPU full-screen bitmap object and then uses the GPU bitmap object and then uses the GPU object and then uses the GPU and then uses the GPU then uses the GPU the GPU GPU to draw a draw a a j ' Mandelbrot fractal into it. Once the Mandlebrot set has been drawn, has been drawn, been drawn, drawn, a Julia Julia set is drawn, drawn, and the the ; , program then switches back and forth between then switches back and forth between back and forth between forth between between the two images. two images. images. ; ; The 68000 68000 is used to set up the parameters for the GPU, and then the entire screen used to set up the parameters for the GPU, and then the entire screen to set up the parameters for the GPU, and then the entire screen set up the parameters for the GPU, and then the entire screen up the parameters for the GPU, and then the entire screen the parameters for the GPU, and then the entire screen parameters for the GPU, and then the entire screen for the GPU, and then the entire screen GPU, and then the entire screen and then the entire screen then the entire screen the entire screen entire screen screen is drawn by drawn by by the GPU. GPU. ] 4 As implemented, implemented, the whole screen whole screen screen is drawn in about 5 drawn in about 5 in about 5 about 5 seconds, and could be sped up bya could be sped up bya be sped up bya sped up bya up bya byaa factor of of . @ _ 100% or more with or more with more with with a little more optimization more optimization optimization (like using the DSP to calculate the DSP to calculate DSP to calculate to calculate calculate half the picture while the picture while the while the the | @ GPU calculates the other the other other half). | @ 

This file is where the program execution begins. This is the standard Jaguar Startup Code responsible for initializing the system. It sets up interrupts, sets the video registers correctly for either NTSC or | PAL, and does other related things that must be done properly at startup time for your program to | function. It also displays a startup screen. Once it is finished, it passes control to the _ start label somewhere in your program (JAGMAND:S in this example). , Note that STARTUP.S has been modified slightly from the version in JAGUAR\STARTUP to allow 2 the use of a different startup picture. This type of change is only one allowed in this file. Making ' changes to other portions of the file may result in errors which can prevent your program from | functioning properly. 

\ 

16 May, 1995 

Confidential Information F@® Property of Atari Corporation 

© 1995 Atari Corp. 4 

. 

Page 3 

| Sample Programs & Kkoe This file is where the program execution begins after the startup code has initialized the system. It basically delays for a few seconds so that we can look at the startup screen, then it creates an object list for our background picture, installs an interrupt handler to refresh the object list, and then sets the video 1 mode to 320-pixel CRY mode. Finally, it clears the memory that will be used for our bitmap, and then jumps into the Mandle function, located in MANDLE.S. Note that the object list creation routine make_list is almost identical to the routine InitLister in the STARTUP.S module. The only parts that changed were the labels for the address where the list information is stored. OSLO LLL This contains the 68000 routine that sets up the fractal parameters (coordinates, zoom range, etc.) and tells the GPU to start creating the fractal image. a oe This contains the GPU routine that calculates the fractal image for each pixel of the picture, using the & 0 parameters (coordinates, zoom range, etc.) which are set up by the 68000. 

© 1995 Atari Corp. 

Confidential Information FPR Property ofAtari Corporation 

16 May, 1995 

7 Page 4 JagLine, JagSlant, JagBlock, JagSkew, JagShade 7 These are very simple programs which demonstrate how to do specific tasks using the blitter. Warning! Please note that note that that the current versions of of these programs are programs are are not intended as general examples ofJaguar general examples ofJaguar examples ofJaguar ofJaguarJaguar programming. They are intended as simple are intended as simple intended as simple as simple examples f of specific blitter operations, blitter operations, operations, and they take short cuts to this end. they take short cuts to this end. cuts to this end. to this end. this end. end. Do not not use these these : examples to obtain startup code or as a shellfor creating your own to obtain startup code or as a shellfor creating your own obtain startup code or as a shellfor creating your own startup code or as a shellfor creating your own code or as a shellfor creating your own or as a shellfor creating your own as a shellfor creating your own a shellfor creating your own shellfor creating your ownfor creating your own creating your own your own own programs. 

Sample Programs 7 im i not intended r 4 examples 4 use these these ’ i | up a a q . 4 up a narrow a narrow narrow 4 : 1] sets up a up a a : ’ . ; 4 / It sets up a up a a = Itsetsup | 4 —_— =.= contains % the files which which | = | ; 3 4 ne a ] é : = 1 2 program q a objects. = ] and JagSlant JagSlant [3 and data files. data files. files. 4 

| 

| ) | 

Warning! Please note that note that that the current versions of of these programs are programs are are not intended as general examples ofJaguar general examples ofJaguar examples ofJaguar ofJaguarJaguar programming. They are intended as simple are intended as simple intended as simple as simple examples of specific blitter operations, blitter operations, operations, and they take short cuts to this end. they take short cuts to this end. cuts to this end. to this end. this end. end. Do not not use these these examples to obtain startup code or as a shellfor creating your own to obtain startup code or as a shellfor creating your own obtain startup code or as a shellfor creating your own startup code or as a shellfor creating your own code or as a shellfor creating your own or as a shellfor creating your own as a shellfor creating your own a shellfor creating your own shellfor creating your ownfor creating your own creating your own your own own programs. 

| JagLine - This program demonstrates how to draw a horizontal line using the blitter. It sets up a a narrow bitmap object and then draws a single yellow line into the top of it. | _ JagSlant - This program demonstrates how to draw a diagonal line using the blitter. It sets up a narrow a narrow narrow bitmap object and then drawsa single yellow line into the top of it. | JagBlock - This program demonstrates how to draw a solid rectangle using the blitter. It sets up a up a a | narrow bitmap object and then draws a single yellow box into the top of it. , : JagSkew - This program demonstrates how to draw a skewed rectangle using the blitter. It sets up a up a a narrow bitmap object and then draws a non-shaded yellow polygon into it. : JagShade - This program demonstrates how to draw a shaded parallelogram using the blitter. Itsetsup a narrow bitmap object and then draws a shaded yellow 4-sided polygon into the top of it. 

**==> picture [496 x 236] intentionally omitted <==**

**----- Start of picture text -----**<br>
This example is normally found in the JAGUAR\SOURCE\BLIT directory. This directory contains<br>several demos which share a number of common source code files. Below is a list of all the files which which<br>are included.<br>Filename Description<br>BLITBLCK.S This is the code for JagBlock that calls the blitter<br>BLITLINE.S - This is the code for JagLine that calls the blitter<br>BLITSHAD.S This is the code for JagShade that calls the blitter<br>BLITSKEW.S This is the code for JagSkew that calls the blitter<br>BLITSLNT.S This is the code for JagStant that calls the blitter<br>CLEARBAR.S The routine in this file uses the biitter to clear the bitmap memory used by the program<br>CRY.PAL This file contains data for a 256-entry CRY-mode color palette for palette-based objects.<br>INTSERV.S This file contains the interrupt handling routines used by all the programs.<br>JAGLINE.S This is the main program file for JagBlock, JagLine, JagShade, JagSkew, and JagSlant JagSlant<br>LISTBAR.S The routines in this file set up the object list used by all the programs<br>MAKEFILE Used with MAKE utility to build executable program files from source code and data files. data files. files.<br>VIDEOINI.S The routines in this file set up the video display used by ali the programs.<br>**----- End of picture text -----**<br>


## Confidential Information ‘PER Property ofAtari Corporation 

4 

16 May, 1995 

© 1995 Atari Corp.4 

Page 5 

Sample Programs 

| | | i i tf f | } | | | t t t / 1 ' | | j 1 1 | i | | ' | 

; 

| | These files contain the code for the blitter for the individual programs. Only one file is used by each || ———program (see table above). ns | ‘This file contains a simple subroutine which uses the blitter to clear the memory used by the bitmap : j object we use to display our picture in all of these programs. It sets up a pattern containing all zeroes, : and then blits this pattern into the bitmap. ' —— nn | This file contains data for a CRY-mode color palette, which will be used by objects with 8 bits per pixel | less. This file contains the routine that installs our vertical blank interrupt, as well as the vertical blank A] interrupt service routine (ISR). The ISR simply calls the Lister function (contained in LISTBAR.S) .. which creates the object list. Note that re-creating it from scratch during each vertical blank is a terrible way to maintain your object | list; please don’t do it this way. It’s much more efficient to change only those fields of those objects | which get changed every frame by the object processor. For better examples of creating and maintaining an object list, see the programs in the \JAGUAR\WORKSHOP directory, which create | object lists of various sizes and complexity. For a specific example of an object list like those used by JagLine, etc., see the routines in the file MOU_LIST.S, located in the \JAGUAR\WORKSHOP\MOU directory. 

This file is the main source file for these programs. It performs program initialization, and then transfers control to the DoBlit function, which is different for each program (this routine is contained in the BLITBLCK.S, BLITLINE:S, BLITSHAD:S, BLITSKEW.S, and BLITSLNT:S files; each program uses just one of these). 

This file contains the Lister routine we use to create our object list, as well as the routines which save | and restore the fields of the object list which are modified during each frame by the object processor. Him ae i. This file contains the routine that detects the current video standard (NTSC or PAL) and sets up the video registers which control aspects of the video such as the size and position of the borders at the 1 edges of the screen. ] q © 1995 Atari Corp. Confidential Information FPR Property ofAtari Corporation 16 May, 1995 1995 

16 May, 1995 1995 

ve 

% = Sample Programs Page 7 QW ioypadReadingExample lm This program demonstrates how to read the Jaguar joypad controllers. It is quite simple; the current buttons pressed on the joypad are printed to the screen. Controller #1 is shown on the left side, and | Controller #2 is shown on the right side. This example is normally found in the \JAGUAR\SOURCE\OYTEST directory. 

|[:] 

© 1995 Atari Corp. 

Confidential Information “70% Property of Atari Corporation 

16 May, 1995 | 

' 

Page 8 Sample Programs 1 EEPROMExample§..§s == ccc CG 

] q | j ' : _ ‘ | 4 

: : | - | 

' 

4 i 

| 

This program demonstrates how to read and write information to the EEPROM ofa cartridge. 

The EEPROM is 128 bytes of non-volatile memory on a standard Jaguar cartridge that is normally used for storing the user's controller preference settings, high scores, etc. This program demonstrates how to access it. Note: This program demonstrates the exact method required for accessing the EEPROM. Use the code from this program as is, without change. 

This example is normally found in the \JAGUAR\SOURCE\EEPROM directory. 

. 

. 

16 May, 1995 

Confidential Information “FOR Property of Atari Corporation 

© 1995 Atari Corp. } 

| 

Page 9 

| Sample Programs AGE True Color Bitmap Display Example 

|\ f | 

L 

This program demonstrates how to set the system up for RGB mode instead of CRY mode, and creates a | 16-bit true color RGB bitmap object. It then draws a number of bands of color into the object. This | program uses only the 68000, and while it's not exactly slow, it could be done much faster using the _ GPU and/or Blitter. 

This example is normally found in the \JAGUAR\SOURCE\TESTRGB directory. 

) 

© 1995 Atari Corp. 

Confidential Information FPR Property of Atari Corporation 

16 May, 1995 

: : ; i : 

; ' Po | 1 

‘ 

: Warning! Please note that the current version of this program is not intended as a ; general example ofJaguar programming. It isa simple example ofa specific DSP , operation, and it takes short cuts to this end. Do not use this example to obtain 4 

Ve,hrrrtrtrtrtstr—S=«i‘COrQOCUOtCi(C(’N’TNNYNNCCSOUCésCOGMRL 

This program demonstrates how to playback a simple waveform using one of the samples in the DSP waveform ROM. Nothing is shown on screen, but you should hear a tone from your speakers. 

This example is normally found in the JAGUAR\SOURCE\SIMPLE directory. 

| 

16 May, 1995 

Confidential Information ‘FER Property ofAtari Corporation 

© 1995 Atari Corp. 

i 

Page 11 

| | ' L q 1 ‘ | |j | | 1 j 4 : ' | q { | 

Sample Programs nono oe | This program is a sort of Blitter recipe book written by Francois- Yves Bertrand. It uses the blitter to copy a bitmapped picture from the source bitmap to the screen. Then it allows you to plug values into the blitter registers to see what happens. This program is really as much a tool you can use to figure out what values to use with your own blitter | code as it is a sample program. Playing with this program as you read through the blitter sections of the | Jaguar Software Reference Manual - Tom & Jerry is really a great way to learn the Jaguar blitter. With this tool, you can program any of the blitter register and see the result directly on screen. The actual program uses two main objects: ~—@ The first one is an ATARI logo, 64 x 64, 16 bits per pixel. This is used as the source. }| 9 The second one is the destination buffer. It is 320 x 256, 16 bits per pixel, 3 layers (2 for double buffering and one for Zbuffer) i You can move around the register with the UP/DOWN keys or faster with 1/7 keys on paddle 1. You can change the value of a register with LEFT/RIGHT keys or faster with C/B keys. The only register you cannot change is the base register (for both Al and A2). If you set the DSTA2 register (so Al is the source and A2 the reception), the program swaps the Al base and A2 base. You will have to swap | manually all the other registers (PITCH,PIXEL SIZE...) to have the correct result on screen. | The source code for this program is not provided. While the program itself is interesting to play with, | = and useful as a tool to help figure out your own blitting routines, the source code is not really a good Jaguar programming example in general. 

This example is normally found in the \JAGUAR\BLITTER directory. 

q 

j 

© 1995 Atari Corp. 

Confidential Information FPR Property ofAtari Corporation 

16 May, 1995 

| | j 

Page 12 

Sample Programs 

: | 

‘CSR ay | = 4 

| | 

| 

} 

BPEGDecompressionExample§.§.=«sse ee,,rrt“(t™w~w™C—~C~COC;UCid«COWCO@SCOCi‘(CU.CUOwtCOi‘i‘ 

TESTBPEG is a sample program for the Jaguar that demonstrates how to take the files created with the BPEG image compression tools and use them in a program with the BPEG routine and tools. For further information, please see the Libraries section. 

This example is normally found in the JAGUAR\BPEG directory. 

**==> picture [7 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
ee<br>**----- End of picture text -----**<br>


16 May, 1995 

Confidential Information AR Property ofAtari Corporation 

© 1995 Atari Corp. } 

in 4 

3Sample Programs 

Page 13 

, 4 4 Warning! Please note that the current version of this program is not intended as a q j general example of Jaguar programming. It is a simple example of using the Jaguar q i Synth and Music Driver, and it takes short cuts to this end. Do not use this example 3 q to obtain startup code or as a Shellfor creating your own programs. 

4 ] This program demonstrates how to use the Jaguar Synthesizer and Jaguar Music Driver to play music in @eeSsyur programs. 4 j For further information, please see the Libraries section. 

] 

j This example is normally found in the \JAGUAR\MUSIC\SYNDEMO directory. 

: | A different example that uses a wider variety of patches for the synthesizer may be found in the p of \JAGUAR\MUSIC\MUSICDRY directory. 

| ;1 © 1995 Atari Corp. 

Confidential Information “F® Property of Atari Corporation 

16 May, 1995 

Page 14 

Sample Programs 

[ j } _ @ | 4 rf ' : 4 — 7 | | : | | |PF@ ] x | | **|** og3 1 ‘ 1 4 EB PO 

3D Rendering &TextureMappingDemo###§ 4. 

| | | . : : : | | | | a . : | , | 

Warning! Please note that the current version of this program is not intended as a general example ofJaguar programming. It is an example of using the 3D Graphics library, and it takes short cuts to this end. Do not use these examples to obtain startup code or as a Shellfor creating your own programs. 

## rrrtrts—COsCCQCUiaC(i‘C(NYNNYNRH.._.s—iéié(a‘i‘aéa‘i‘iéi;mt 

This program encompasses and demonstrates the Jaguar 3D Graphics routines supplied by Atari. The program drawsa fully light-shaded and texture mapped space fighter on screen. Using the joypad controller, you can control the fighter's position and orientation. 

**==> picture [237 x 172] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||
|---|---|---|---|---|---|
|Controller|Button|Movement|
|Rotates you|backward|
|Rotates|you to the|right|
|Rotates|you|to the|left|
|ChangesRotates you thecounter-clockwiselight shading|
|Rotates|you|clockwise|
||6's|Changes|[light]|intensity|
|||Reduces number of objects|
|||s8'9|S|:||TurnsIncreases on/offnumb obj|e|ctr of rotation objects|

**----- End of picture text -----**<br>


The number of objects on screen increases/decreases exponentially when you use the '7' and '9' keys; you can have 1 object (14), 8 objects (27), 27 objects (37), and soon. 

Whereisit? =4... .,. This example is normally found in the JAGUAR\3DDEMO directory. 7 

16 May, 1995 

Confidential Information FER Property ofAtari Corporation © 1995 Atari Corp. 

