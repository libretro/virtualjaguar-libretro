Technical Overview Page I eS of Jaguar Hardware & Architecture 

S\@Gverview 

P 

If you are new to the Jaguar, we recommend that you look at the first few pages of the Jaguar Software Reference Manual section for a basic overview of the Jaguar hardware and system architecture. After you've taken a look at that, come back to this section for an overview of the developer's kit and some | more specific information about certain aspects of the system. 

## i 

eT © 1995 Atari Corp. Confidential Information “FPR Property ofAtari Corporation 10 April, 1995 

| | | : 

| | 

## ‘Page 2 Technical Overview «“‘FhedaguarDevelopmentSystem What follows is a brief description of the tools in the Jaguar Development system. Detailed instructions | and explanations are found in specific documentation for each item. 

The Jaguar Development system consists of a set of hardware and software components intended to make writing software for Jaguar the most efficient and rewarding experience it can be. This goal can only be approached, never reached. Asa result, all of the components of this system will be enhanced as time goes by; some will be deleted, and others will be added in the future. It is essential to the success of this effort that we hear your comments on how this system can improve (keep those cards and letters coming!!). 

The hardware components of the system are a development Jaguar machine that connects to your existing PC/MSDOS computer with 80386 or better CPU!. The development system comes with an I/O card for your PC that features an 8-bit bidirectional parallel port. This is used to interface with the Alpine board that plugs into the Jaguar development console. If your PC already has an 8-bit bidirectional parallel port, you can probably use it instead of the card we supply. However, please note that most inexpensive I/O cards do not have such ports. 

The Jaguar development console is a modified version of the standard Jaguar retail machine. It comes with an ROMulator that holds your programs and emulates a ROM cartridge (aka "the Alpine board"), and other optional components (documentation is included with those components). 

The software components are many. In the Jaguar development machine, there is a debugging stub in ROM which communicates with the host computer via the Alpine Board interface card. It is designed to take a minimum amount of system resources. The software under development need not depend on the stub for ANY services, yet the debugging environment is quite complete and powerful. 

The main tools are: the Atari debugger DB; Sottware development tools such as the MADMAC Macro | Assembler, ALN Linker, and GNU GCC compiler. There are also Jaguar specific debugging aids, , of the extensive sample code and library code. Together these provide a set of tools that allow full use capabilites of the Jaguar system (see A Sample Debugging Session). Most of the tools are commandline-oriented; you pass them a commandline, they do what they're told, !s. exceptionand then.they to this quit. ruleInis most the Atari cases, debugger you don't “Db”. actuallyDb interactis a full withfeatured them symbolic debugger while they are running. with aliasesThe ° #4 and procedures that has been in use in the Atari computer development environment for many years. It has been updated and enhanced with numerous new features and special debugging aliases and _ procedures for the Jaguar development system. There are two variations; RDBJAG (Remote DB for Jaguar) features a simple terminal style interface, while WDB (Windowed DB) features a semi-graphic user interface using the mouse, windows, and pull-down menus. 

> _ 1 Instead of a PC system, any Atari TOS computer can also be used for development. The choice of TOS computer depends on the uses that the machine will need to perform beyond simply running the development system software. For best performance and greatest flexibility in a pure debugging environment, an TT030 system with the TTM195 19" monochrome monitor is recommended. 10 April, 1995 Confidential Information FER Property ofAtari Corporation © 1995 1995 Atari Corp. 

© 1995 1995 Atari Corp. | 

| | 

L 

| q | j | | | 1 : | | 

Technical Overview — Page 3 | ob:[e][ Object][ processor][ in][ Jaguar][ is][ an][ unfamiliar][ mechanism][to][ most][ programmers][ and][ this][ can be][a][bit] of a hurdle when starting to program the system. To overcome this problem, we provide a heavily documented routine which is used by several of the sample programs included with the developer's kit. Please see the examples in the JAGUAR\WORKSHOP directory after you have installed your developer’s kit disks. A very useful tool for the Object processor programmer is OD, a script procedure for the DB debugger that translates an object list into English and will warn about common mistakes. 

The Jaguar GPU is a high performance custom RISC processor that was optimized to give maximum performance when programmed in assembly language in graphics applications. The instruction set is general purpose with specific instructions added to do matrix multiplication and simple floating point math. Db has a GPU disassembler and register dump as well as a GPU single step facility for GPU debugging (See Debugging the GPU). The GPU should not be a difficult system facility to master since its instruction set was designed with the programmer in mind. The DSP is very similiar to the GPU in both design and instuction set, the main difference being some extra instructions for sound processing. The MADMAC macro assembler provided in the developer's kit is capable of generating code for the - GPU and DSP as well as the 68000. Older versions of the developer’s kit also provided the GASM macro assembler for GPU/DSP, but this has been made obsolete by newer versions of MADMAC.. _,[The][ ALN][ linker][ is][ used][ to][ link][ your][ object][ modules][ and][ libraries][ compiled][ or][ assembled][ from][ different] | .) source code files and create an executable file ready to be run on your Jaguar. 

There is also a set of programmer utilities included in the system. These include a MAKE utility, a file hex DUMP utility, a version of GREP (the UNIX search utility), and a variety of object module & executable file information utilities. These are documented individually in the Tools section. 

A text editor is not provided with the system because we expect that you will probably already have an editor that you are familiar with and would be unlikely to want to switch. However, if you do need an editor, you may wish to investigate the following fine editors to see which will best suit your needs: 

|MSDOS-basedProgrammer'sEditors<br>Brief-BorlandInternational<br>MultiEdit -American Cybernetics<br>MicroEMACSv3.12 -Shareware (Available<br>onlineonCompuserve&othersystems)|TMicrosoftWindows-basedProgrammer'sEditors<br>Visual SlickEdit -MicroEdgeSoftware<br>| CodeWright -PremiaCorporation<br>|MicroEMACSforWindowsv3.12 -Shareware<br>(Available onlineonCompuserve&other<br>isystems)|
|---|---|



The choice of an editor is often a very personal one and nothing in the Jaguar Development System insists on the use of any particular one. The list above is simply a sampling of those used by programmers at Atari, and there are undoubtedly other fine editors not listed here. 

©1995 Atari Corp. Confidential Information FER Property ofAtari Corporation 10 April, 1995 q 

| . t E | ’ : F : | 7 | 

| 

Page 4 Technical Overview _ _A&SampleDebuggingSession “ To help you become acquainted with the debugging environment, we will load in a program that uses : both the 68000 and the GPU and take a look around. The program that we will use is JAGMAND, a : very simple Mandelbrot set generator. This is the same program that we used in the Getting Started section to verify that the system was working correctly, so we already have built the executable. Change to the \JAGUAR\SOURCE\JAGMAND directory and start the debugger from the shell by ~ typing "rdbjag" (pressing return is implicit here, this instruction will not be repeated). 

: : | 5 ' i 

We won't go into details about how the sample program itself works, as this is explained elsewhere. 

First we load the program into memory in the Alpine board. The debugger uses the first part of system memory for variables, stack, and added GPU specific code. Therefore, all RAM below $4000 is reserved. All cartridge-based Jaguar programs must start at $802000. 

To load in the program we type "aread jagmand. cof". This loads the sample program into memory at the locations specified by the executable (as specified by the commands given to the linker). A map of the memory space used is also displayed. An alternative to the AREAD command is the LOAD command, which loads and executes a script file which can in turn Joad binary data into the Jaguar’s memory by using the READ or FREAD commands. 

At this time we can look at our program by typing "1 802000". This will disassemble (or list) the 68000 code starting at address $802000. (Note that the debugger uses hexidecimal notation by default.) If you first set the program counter using the command ""xpc 802000", you can trace one instruction at a time using the "t" command, or execute a subroutine with the "tw" command. Try this for the first few instructions and subroutines. 

At this point, let's set a breakpoint at the label “start”. This is done by typing "b -_ start". Before the breakpoint is reached, the program’s startup code has been executed. This startup code initializes the Jaguar hardware correctly, sets up an object list, and displays a simple startup screen. i Type "g 802000" to begin execution at the start of the program (or, if you traced some of the program ‘ already you can just type "g") and run until the breakpoint is reached. When the breakpoint is reached 1 the internal state of the 68000 is displayed and the debugger waits for another command. At this point the memory starting at the listbuf \abel contains the object list created by the startup code for the startup picture. Type "od .listbuf" to see a display of the object list that is being used. It should be noted that object lists should be viewed before video processing is started because the object processor changes values in the objects during processing. These are restored each frame by interrupt software, but looking at an active object list with "od" will not give correct data for the data pointer or the object height fields. 

**==> picture [2 x 3] intentionally omitted <==**

**----- Start of picture text -----**<br>
;<br>**----- End of picture text -----**<br>


Type “g ,.Mand1le” to skip past the 68000 code that copies the GPU code to GPU RAM. This will take a few seconds, because the program hasa short delay so that the startup screen may be seen. Note that the debugger will print the message "Press Control-C to stop waiting" on screen. a 

| 

**==> picture [1 x 17] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


18 April, 1995 

Confidential Information “FPR Property ofAtari Corporation 

©1995 Atari Corp. 

Page 5 

Technical Overview 

*e, Ors is because the Jaguar system did not respond quickly to the "g" command and return control to the “| ~~ debugger. 

| 1 | t 

; ‘ B; ii AA za .. | ' 

Now let’s look at some code in the GPU. To do this type "1g £03000". The address used here is the location of the start of GPU RAM. To see the values in the GPU registers type "xg". At this point the GPU may be single-stepped by setting the GPU program counter by typing "setgpc £03000" and then typing "tg" a number of times. Although nothing terribly interesting is likely to be learned, let's give it a try. 

Next we run our program by typing "g". There are a few interesting things to note at this stage. First, the Mandelbrot computation is REALLY quick (despite this, there is AT LEAST[a][factor][of][ two][ times] more performance that can be squeezed out of the system). Second, the debugger again printed the message "Press Control-C to stop wa iting". However, once the program completed one pass over the Mandelbrot set it is stopped in a rather brute force, but effective, way. It executed an illegal instruction. This got the debugger's attention and control is returned to the debugger. Despite this, there is an interrupt happening once a frame stil] running to fix up the object list. 

To leave the debugger type "q". This will sever the communications at the computer side but leave the development system ready for more commands. Type "rdb7jag" and the stub should "check out ok". a ) Ifpress for some the reset reason button the stub on the and debugger Alpine Board. fail This to communicate, will get the attention type the of “wait” the debugger command whenever in Db andit is "Waiting..." . 

**==> picture [29 x 21] intentionally omitted <==**

**----- Start of picture text -----**<br>
©<br>**----- End of picture text -----**<br>


© 1995 Atari Corp. 

Confidential Information “PO® Property ofAtari Corporation 

18 April, 1995 | 

Page 6 

Technical Overview 

## ASimpleSampleProgram 

We have looked at the JAGMAND sample program twice now. Aside from drawing the Mandelbrot set fractal, this program also points out many of the features and characteristics of both the Jaguar and the developmentthe blitter to clearsystem.the screen.While it is in many ways very simple, note that the JAGMAND. program does use 

There are a number of very mundane things that must be considered when writing a Jaguar program. In no particular order these include: 

- 1) Where in memory will the various segments be? 

The debugger in the development system takes up the lower 16K of memory. Programs should therefore use no RAM lower than $4000. The rest of RAM is yours to do with as you please. The ROMulator should be used to hold the program's text and data segments. The first part of ROMulator memory is also reserved, this time for the security code. Cartridge-based programs must always start at $802000. 

## 2) Where is the 68000 stack? 

Keeping in mind the restrictions mentioned above, you can put the stack anywhere in RAM above $4000 you want. Probably the best place is at address $1FFFFC. This is 1 long word away from the end of RAM. 

- 3) How do you set up video, clear interrupts, and initialize memory at startup time? 

We supply a standardized startup routine that initializes the entire system and then jumps to your program code. This is contained in the JAGUAR\STARTUP directory. The JAGMAND program includes the STARTUPS file, containing this startup code. 

## 4) Setting up an object list. 

The choice of object list structure is quite complex and depends greatly on what your goals are. Since there is no good general solution we give a VERY simple one here. A single full screen object. This uses an unscaled bit mapped object. The object is the height of the screen. 

## 5) Putting stuff in the object to be displayed. 

The JAGMAND program draws a Mandelbrot fractal into the bitmap displayed by the object. Of course, your program is going to draw whatever is appropriate for it. 

**==> picture [1 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
'<br>**----- End of picture text -----**<br>


18 April, 1995 

Confidential Information FR Property ofAtari Corporation 

© 1995 Atari Corp. 

## Technical Overview 

Page 7 

## We JaguarandMemory 

This document describes the memory map of the Jaguar (Tom and Jerry) development system. 

Main system RAM in Jaguar in 64 bits wide. It consists of a singie 2-megabyte bank starting at memory j location $00000000. The rest of the system memory map consists of hardware registers. These registers include the internal high speed SRAM for holding GPU and DSP programs and data. This starts at $00F00000. . 

The GPU, DSP and blitter internal registers are 32 bits wide and MUST be read and written as such. When accessing these memory locations with the 68000 CPU they must be read and written as 32 bit entities. This is especially important with regard to GPU and DSP internal SRAM. Transfers to (and from) this memory, to pass parameters between CPU and GPU for example, must be made at long word boundaries. Please note, to clear a long in internal GPU/DSP RAM space use the move instruction, because the clr.] instruction will not be reliable. (Please see the Hardware Bugs & Warnings section for further information about this subject.) 

The last kind of memory in the development system is the ROMulator, described later in this section. 

| | j 

| 

C—O eee eeeeeSEB.— O00 , ©1995 Atari Corp. Confidential Information “FER Property ofAtari Corporation 

1 18 April, 1995 q 

Page 8 

Technical Overview 

_ " 

## AheJaguarBliter ####=#= ##§+.==+—=s— ww 

The Jaguar Blitter is a very powerful piece of the Jaguar graphics system. This document will introduce the major functional parts and show some of the many ways in which they can work together. 

The programming model of the blitter consists of: 

1) Two address generators. 2) A Logical Function Unit. 3) A Pattern Data register. 4) A Gouraud Shading unit. 5) A Z-buffer unit. 6) A Collision detection system. . 

The two address generators are easy to use because they work in pixel units, not address units. This greatly simplifies the coding tasks for blitter use. 

The basic concept used in both address generators is the "window". A window is a rectangle of memory whose width is taken from the list of allowed widths (see BLIT.INC for the allowed widths). The maximum allowed height of a window is 4096. If no outer loop is used, the window width is not relevant and the maximum sized blit allowed is 32767 pixels. 

There are two address generators Al and A2. ; 

Al has the ability to traverse its window in tractional steps with complete independence in x and y. The inner and outer loops are controlled independently and the outer loop increment may also contain independent, fractional x and y values. These features combine to allow arbitrary rotation, skewing and scaling of rectangular areas. 

A2's special ability allows it to repeat a source pattern over a larger destination by masking the pixel offsets. The masks can be any power of two size up to 215. 

The Logical Function Unit takes the source and destination and produces an output based on the logical or'ing of the four possible minterms. Four of these combinations are of particular use: 

Destination <= Source Destination <= (Source) | (Destination) Destination <= (Source) & (Destination) Destination <= (Source) “ (Destination) 

A complete listing of these is given in the system include file BLIT.INC. 

The Pattern Data register is where the blitter gets its data without the need for reading source data. This is used, for example, in drawing lines. 

i—, 

10 April, 1995 Confidential Information FR Property ofAtari Corporation 

© 1995 Atari Corp. 

| | , | | | | | : 

| j 1 { : | | : : : 

uid 

1 Technical Overview Page 9 The Gouraud Shading Unit is one of the most powerful features of the Blitter. It allows the automatic P shading of CRY pixels. (See the description of the CRY color model in the Jaguar Software Reference Manual for more information). The Gouraud shader uses the Pattern Data register as the source with | the added capability of adding a constant (fractional) intensity to each pixel. This allows the generation of a smoothly shaded line with no explicit computations done at the pixel level. 

| ‘In the same way that shading is handled in hardware, a line produced by the blitter can also have az value automatically provided for each pixel and the blitter can be instructed to suppress writing of pixels with z values that correspond to 3d point that should not be visible. 

Note: Gouraud shading and Z mode are only available with 16 bit pixels. 

Another important concept to understand in the Jaguar blitter is phrase mode. The inner loop increment -used by the blitter is controlled by the first few bits of the FLAGS register for each address generator. These modes are fairly self explanatory, except for phrase mode. 

In phrase mode the blitter reads and writes 64 bits of data at a time. The blitter handles all fringe cases and data alignment automatically in 8 and 16 bit per pixel. For smaller numbers of bits per pixel, pixel mode should be used. Note: BOTH address generators must be in phrase mode. It cannot be half set. There are two extra complexities when dealing with phrase mode. It is possible that the first data write | ; @ requires an extra phrase read. This happens whenever the data for the first write is not contained in the first data read. Consider for example a 16 bit per pixel blit: 

(The vertical bars are 64 bit phrase boundaries) 

Source: | abcd| | Destination: ABCD 

The blitter needs two source reads to get all of the data for the first data write. This extra read is caused . by setting the SourCe ENable eXtra (SCRENX) bit in the B_CMD register. Other situations also require this bit to be set. For example: 

**==> picture [86 x 81] intentionally omitted <==**

**----- Start of picture text -----**<br>
Source:1 ft |<br>abcd<br>Destination:<br>ABCD<br>**----- End of picture text -----**<br>


The other extra complication involves the STEP value used in the outer loop. Since the blitter always advances to the end of a phrase the STEP size is not always the width of the blit. An example should make the general principles clear: © 1995 Atari Corp. Confidential Information FER. Property ofAtari Corporation 10 April, 1995 

**==> picture [2 x 25] intentionally omitted <==**

**----- Start of picture text -----**<br>
1<br>**----- End of picture text -----**<br>


10 April, 1995 

Page 10 eee 

Technical Overview 

, 

**==> picture [258 x 82] intentionally omitted <==**

**----- Start of picture text -----**<br>
Source:<br>bo ft J |<br>abcdefgh<br>Destination: ;<br>ABCDEFGH<br>**----- End of picture text -----**<br>


**==> picture [6 x 24] intentionally omitted <==**

**----- Start of picture text -----**<br>
(<br>**----- End of picture text -----**<br>


In both cases the STEP goes from the end of the third phrase to the beginning of the data. In this case this gives a STEP of -10 for the source and -9 for the destination. 

Also remember that if SCRENX is set an extra phrase worth must be subtracted from the source STEP value. 

Phrase mode also has an effect on Gouraud shading. Since the blitter writes four pixels at once all four pixels must be placed in the Pattern data register and the value of the intensity increment must be multiplied by four. This means the maximum intensity increment that will work in phrase mode is 31. 

Since the intensity addition saturates and the increment is signed there are a few cases that will fail. These all share the following characteristic: The first pixel to plot is not on.a phrase boundary and the extrapolated value for the first pixel falls outside of the allowed values. Software authors need to beware of this condition. It should either be rigidly excluded or a switch to pixel mode is needed. 

10 April, 1995 

Confidential Information “AOR Property ofAtari Corporation 

© 1995 Atari Corp. 

Page 1] 

## Technical Overview 

SO ie Jaguar Development System Stubulator & ROMulator The Stubulator is what we call the version of the Jaguar console that is used as part of a Jaguar Development System. Also known as a Jaguar Test Station, it is essentially a standard Jaguar console which has been modified to use a special debugging version of the boot ROM, and which has an extra cable attached which connects to the ROMulator board to handle the stop button interrupt. 

**==> picture [536 x 305] intentionally omitted <==**

**----- Start of picture text -----**<br>
Reset Button on:<br>Stop Button \ Write Disable/Enable<br>TO | Ett<br>TITTT || Bee<br>atte<br>5 —— B BERR<br>O ry OO00o<br>\ Cartridge Port<br>LED Connector<br>i Figure 1, The ROMulator Board (front)<br>The ROMulator, also known as the Alpine Board, serves two purposes. First, it allows the Jaguar<br>console to communicate with your computer via a parallel port or seria! connections. Second, it<br>contains 2 or 4 megabytes of battery backed-up static RAM? which is used to emulate a ROM cartridge.<br>Hereafter, we will refer to the RAM memory on the ROMulator as ROM in order to distinguish between<br>| it and the RAM inside the Jaguar console. i<br>**----- End of picture text -----**<br>


**==> picture [259 x 44] intentionally omitted <==**

**----- Start of picture text -----**<br>
)<br>Stop Cable Connector Pin 1<br>a<br>**----- End of picture text -----**<br>


6 Figure 2, The ROMulator Board (back) 2 The standard Alpine board shipped with the Jaguar Developer System contains two megabytes of static RAM. However, four megabyte (32 megabits) Alpine boards are also available upon special request. Contact Jaguar Developer Support if your project requires more than two megabytes (16 megabits) of ROM space. 

| | | | : | | ] | 1 | ' q 

j | ' 1 j | 

Page 12 

Technical Overview 

## The Alpine board has a variety of components you should become familar with, as highlighted in figure #1 and figure #2. The table below briefly describes each one. 

**==> picture [506 x 657] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|Component|Description|
|Stop|Button|When|pressed,|this|button|generates|a|non-maskable|68000|interrupt|in the|
|Stubulator.|The|debugger|stub|handles|this|interrupt|and|stops|the|current|
|process,|and|then|passes|control|to the|debugger.|If a|program|is|severely|
|crashed,|the|68000|has|been|disabled,|or a|program|has|aitered|the|interrupt|
|vector,|then the|stop|button|may|have|no|effect.|This|is|rare,|but|it|does|
|happen|occasionally.|
|Reset|Button|This|button|generates|a|hardware|reset|of|both|the|Jaguar|console|and|the|
|Alpine|board.|The|current|program|is|halted,|and|one|or more|of the|following|
|[are]|[taken:]|
|}|[actions]|
|||1)|The debugger stub initializes|itself to use memory|in the $800000 to|
|$801FFF|range|of the|Alpine|board.|If the Alpine|board|is|write|protected,|
|or|if|a ROM|cartridge|is|plugged|in,|then|it|proceeds|to|action|#2.|
|2)|The|debugger|stub|initializes|itself to|use|memory|in|low DRAM|(below|
|$4000)|in|the|console.|Then|it|proceeds|to|action|#3,|#4,|or|#5.|
|3)|The|cartridge|port|is|checked|for a|32-bit|cartridge.|If found,|then|the|.|
|68000|starts|executing|code|at|$802000.|If|not,|it|proceeds|to|action|#5.|
|4)|The|cartridge|port|is|checked|for|an|8-bit|cartridge.|If found,|thenthe|—|
|68000|starts|executing|code|at|$802000.|If|not,|it|proceeds|to|action|#5.|
|5)|The|debugger|stub|displays|the|“Jaguar|Development|System”|screen|and|
|running|on|your|computer.|
|||attempts to communicate via the Alpine board with the debugger interface|
|I||The|exact|combination|of|actions|depends|on|which|buttons|of joypad #0|are|
|pressed|when the|reset|button|is|pushed.|If|no|buttons|are|pressed,|then|
|actions|#1,|#2,|and|#5|are|taken.|If the|‘B’|button|is|held|down,|then|actions|
|#3|and|#5|are|taken.|!f|the|‘C’|button|is|held|down,|then|actions|#4|and|#5|
|are|taken.|
|Neither|console|RAM|or Alpine|board|SRAM|is|cleared|by|this|reset.|
|However,|interrupts|are|cleared.|
|Write|Enable|/ Disable|Switch|||This|switch|allows|you to|control|if the|RAM|on the Alpine|board|may|be|
|written|to.|If|this|is|set|to|“Write|Disable”,|then|the write|lines|of the|memory|
|chips|are|physically|disconnected|so|that the|memory|contents|cannot|be|
|altered.|
|PateyBattery|.|Thispoweris isused turnedto|offmaintain or|whenthe thecontents Alpineof board the|staticis notRAM pluggedwhenin.the|console|
|RPLED|Thispluggedis|litin, when and the consolethe|Write-Disableis turnedswitch on.is|set,|and|the|Alpine|board|is|
|eeeSerial|/|MID!NeseConnector|||ThisAineis ibthe|connectionaaapten nnnused for|either a|serial|link to your|hostencomcomputer|oror the|
|eeeParallel|Port|Connector||mRThisGrestonais|the|connectionparaieiponused|to communicate|with|your|host|computer's|bi-|
|erStop|Cablecene femeserConnector|[connectsThis|is where tothe the hnstop|cabletard,coming morroout|of th a|developerStop|hatonto Jaguarbe console furconak|
|18 April, 1995|Confidential Information|FPR|Property ofAtari Corporation|© 1995 1995|Atari Corp. Corp.|

**----- End of picture text -----**<br>


© 1995 1995 Atari Corp. Corp. 

Page 13 13 | The | and | coming || that . 

Technical Overview Page 13 13 The Alpine board plugs into the Jaguar console in the same manner as a standard Jaguar cartridge. The front of the Alpine board, as shown above, faces the front of the console (where the power switch and controller connectors are located). A Jaguar Test Station should also have a 10-pin ribbon cable coming | gut of the back. This is the stop cable which connects to the back of the Alpine board. Make sure that the red-striped wire of the ribbon cable always goes to pin 1 connector on the Alpine. 

Newer releases of the Alpine board come with a 32MHz crystal, and a header fitted in space J4. (J4 is marked as the Serial / MIDI connector in figure 1.) Only those Alpines with those components can be used with the MIDI add-on board. If your Alpine is an older mode] and you need to use the Jaguar MIDI board, contact Atari Developer Support for modification instructions or to arrange an exchange. 

The ROMulator memory starts at $800000, the same address space used by a cartridge, and is treated by the system as 32-bits wide. In order to emulate a ROM cartridge, the ROMulator memory may be write protected. This is accomplished using the WRITE DISABLE/ENABLE switch at the top of the board. The ROMulator is write protected when the LED in the bottom left corner is ON. Just as with a real cartridge, all static code and data must start in ROM and get copied to the console's **a** , RAM by the program as needed. No writes to ROM space should be done by game code. This may be tested by the following steps: | 1) Load a program into the ROMuiator using the debugger. 2) Turn the switch to WRITE DISABLE. 

| | | | 1 | | | 

3) Turn the machine off for a few seconds, then on again. 

| 

4) Run the program and make sure it functions normally. 

oe,,,rrr~—‘“C i;i*™wstsis—~—~—~—C—~—C—CrC The debugger stub also uses a section of the ROMulator space. To leave room for the security code that will be in each cartridge, the first $2000 of the ROMulator (from $800000 to $801 FFF) is NOT to be used by your programs. The restriction on the use of the first 16K of RAM ($0000 to $3FFF) is also still in effect. 

The debugging stub normally tries to use memory in the ROMulator, but it can optionally use DRAM if | necessary. The sign-on message shown by the debugger indicates how the stub is using memory. There Cc) are two possible reasons for the stub to not use the ROMulator: | 1) The ROMulator is not present or damaged in some way. | 2) The ROMulator is write-protected AND the stub is NOT ALREADY loaded. | © 1995 Atari Corp. Confidential Information “FER Property ofAtari Corporation 18 April, 1995 April, 1995 1995 

| ' | 

18 April, 1995 April, 1995 1995 ‘ 

Page 14 

Technical Overview 

mi) 

This allows the system to be reset with a write protected ROMulator and still work. If the stub reports that it is running from DRAM, the ROMulator data has probably been disturbed. 

To force the stub to use DRAM, you can hold down the ‘A’ button of controller #1 while turning on the Jaguar's power or pressing the ROMulator reset button. Normally, however, this should not be necessary. 

wibtAddOnBeardee The MIDI Add-On board is a special add-on board that connects to the serial port of an Alpine board and allows you to feed MIDI data to a special version of the Jaguar Synthesizer. This effectively turns the Jaguar into a stand-alone synthesizer which can be controlled by an external keyboard, sequencer, or by a computer equipped with a MIDI port and MIDI software. This allows you to preview your music on the Jaguar itself. 

| 

**==> picture [155 x 164] intentionally omitted <==**

**----- Start of picture text -----**<br>
MID! Connectors<br>In Out Thru<br>|<br>| Pin 1<br>Connector JP4<br>**----- End of picture text -----**<br>


| Figure 3, Jaguar MIDI development board _ \ To connect the Jaguar MIDI board, simply connect one end of the supplied 10-pin ribbon cable to a connector JP4 on the MIDI board and connect the other end to the Serial port / MIDI connector of the 4 Alpine board. Make sure that the red-striped wire of the ribbon cable goes to pin 1 at both ends. Once the Jaguar MIDI board is connected, it can be used with the Jaguar Sound Tool (the patch editor for the Jaguar Synthesizer). See the documentation for the Sound Tool for further information. 

| 

10 April, 1995 

Confidential Information FAR Property ofAtari Corporation 

© 1995 Atari Corp. 

——_ 

Page 15 

Technical Overview 

: : 

## 2 @jaguarControlierSuppot 

The Jaguar supports a variety of different controller types beyond the joypad that comes with every console. In order to insure that controllers are correctly supported, we urge developers to pay close attention to the Jaguar Controller & Controller Ports Specification section of the Technical Reference chapter. 

| 

© 1995 Atari Corp. 

Confidential Information “AAR Property of Atari Corporation 

10 April, 1995 

