: 1 AppendixA - Frequently Asked Questions About Jaguar Page 1 Pe Frequently Asked Questions About Jaguar @ AboutTheDeveloperPackage : F Q: Ido not have an Atari TOS based machine. A: Atari is creating new demo code and exampies but only have ST Software to work with my all the time, and it’s possible that there have been } developer package right now updates since you got your developer’s kit... Look j on the developer support BBS and private Jaguar | A: The current versions of both the PC/MSDOS Developer areas of Compuserve. (See Online @ and Atari versions are available online on Support section of the Getting Started chapter q | Compuserve and the Atari Software Development of the documentation.) ‘ | BBS. Or you can contact Jaguar Developer aw x $e Support to obtain the PC/MSDOS versions of the tools. Q: What am I supposed to use as a 3-D object editor? nee 

1 Q: I am not satisfied with the examples thatcame —_[A:][You][can][use][whatever][you][preter.][The] GF with my developer’s package. Is there more deme —_— conversion utility out of our 3-D package uses BB. code available? .3DS files from AutoCAD 3D-Studio v2.0 or from AutoCAD 3D-Studio v2.0 or AutoCAD 3D-Studio v2.0 or 3D-Studio v2.0 or v2.0 or or v3.0 P| running on on the PC. 

.3DS files from AutoCAD 3D-Studio v2.0 or from AutoCAD 3D-Studio v2.0 or AutoCAD 3D-Studio v2.0 or 3D-Studio v2.0 or v2.0 or or v3.0 running on on the PC. 

1 1 Q: I have trouble getting the debugger to transfer data, rather than using the port’s data lines. This information from my PC to my Alpine Board. allows them to do bidirectional communication on @® Either the debugger says “No Bi-directional a unidirectional port, but it is much slower $B Parallel Port Found” or it says “Error While because it cannot transfer as much information at 4 Reading FAST” during a transfer. once. 

| t. A: Either you do not have a bidirectional parallel In order to achieve acceptible performance, the @e port installed in your PC, or else you need to Jaguar debugger requires a true bidirectional 4 1 adjust the timing of the high-speed transfer. parallel port. The Jaguar Developer’s Kit = includes a PC I/O card that features such a port. { |F The JaguarAlpine board debugger via a bidirectional communicatesparallel withport the If you are seeing a message that says “no ie installed in your PC. Calling a port bidirectional paralle! port found” then either the : | “bidirectional” means that it is capable of either debugger could not communicate with the Alpine _ B® receiving or transmitting 8-bits of information ata because the parallel port was incapable of Be time. receiving information back from the Jaguar, or rf| Most inexpensive I/O cards for PCs are intended elseport it’sis tiedpossibleup forthat somethereason. Alpine board’sReset yourparallel “ae to be used for output only and do not feature Alpine board. If you still see the message when 1 }} bidirectionalaround this, someparallel ports.PC programsIn orderand hardware to work youhave runa bidirectional the debugger,port theninstalled. you probably don’t q | add-ons use the port’s contro] signals to receive 4 | © 1994 Atari Corp. Confidential Information FR Property ofAtari Corporation 26 April, April, 1995 

, 

26 April, April, 1995 

'' Page 2 AppendixA - Frequently Asked Questions About Jaguar : j If you have installed the card included with the causes the system to think these other programs : developer’s kit, make sure it is configured » are accessing RDBJAG's memory. RDBJAG | correctly for your system. If you need assistance needs to allow other programs access to its , : with this, please contact Developer Support. memory. This is controlled by the ‘global’ i memory protection flag in the program header. = q If you have a bidirectional port installed and are The most likely problem is that this flag is 4 seeing a message like “Error While Reading probably turned off and needs to be turned on. If ' FAST” during a transfer, then the timing of the you don't know how to do this, please contact ' debugger’s high-speed parallel transfer may Jaguar Developer Support. Alternately, you can fg require adjustment for your machine. The run MultiTOS with memory protection turned off. ' debugger has a variabie named “PPROT” which This can be done with the MultiTOS CPX in the | 4 allows you to adjust this. After loading the CONTROL PANEL accessory. You will then 3 a debugger, type the following: have to reboot for the changes to take ettect. ] pprot =n ' ; Q: I tried taking out the Alpine board and put in a = a Where ‘n’ is a number from 1 to 9. Experiment cartridge, but it would not run. Is the cartridge : a with different numbers until you find one that broken? : i works reliably. After you find one that works ’ 7 reliably, you can add this line to your RDB.RC A: Most likely not. In order to run a cartridge on : i file so that this adjustment is made automatically a development system, it is necessary to hold Pf 4 each time you run the debugger. down the 'B' button on controller #1 when you -_ : turn on the machine. This is because you need to i Another adjustment that has been known to help signal to the development console that the _ : is changing the ISA bus speed of your machine. debugging routines are not supposed to be & q This is typically done in your BIOS setup screen installed at startup, and that it should act like a ' : that is accessible when you first boot the system. standard retail version of the Jaguar. fs p “s* If the cartridge runs, but you hear a lot of static j Q Q: I have problems with running GULAM on my noise, then you must connect a 1k resistor ( ‘ Falcon030. between lines 4 and 5 of the header at the end of ; i the 10-connector ribbon cable that goes trom the ' 4 A: You should make sure that you do NOT try to development console and plugs into the back of d | run GULAM under MultiTOS, the multitasking the Alpine board (this is the STOP button cable). 4 7 extensions to the TOS operating system. If you This is only necessary for some systems with " : want to work under MultiTOS then you should serial numbers starting with less than K14... (See | use another shell, for example the UNIX C-Shell The Jaguar Development System ROMulator q ! style shell TCSH. TCSH is available online. chapter of the Technical Overview section of the i 4 F eae documentation.) | q “* 1 Q: The newest version of RDBJAG crashes under | @ i MultiTOS. Q: On my ATARI Falcon030 I cannot establish 4 ; communication between RDBJAG andthe Stubin i A: When running on 68030-up systems, the development console - Help! 4 MultiTOS features hardware memory protection. F RDBJAG installs itself into system interrupt L 3 | vectors. When other programs call interrupts, this | q 1 26 April, 1995 Confidential Information FR Property ofAtari Corporation © 1994 Atari Corp. q 

' AppendixA - Frequently Asked Questions AboutJaguar Page 3 ) IMMA: This is a problem only with older versions of variable. See the Configuration section of the @ RDBIAG. The current version of RDBJAG is Getting Started chapter for more information. B® available online. K&R “ee Q: My source code developed under the TOS | | Q: The command OD does not work with my based system does not assemble with the PC@ version RDBJAG. What is wrong ? based tools. @ A: The OD commanzd is actually a DB procedure A: While we intend to maintain backwards ® which is defined in the OD.DB script file. This compatibility to the highest possible degree, it 1s # script is normally loaded automatically by the sometimes not practical or possible to do this @ debugger through the RDB.RC startup script while at the same time adding new features. See @ (along with the scripts GPU.DB and FILL.DB). the text files in the JAGUAR\DOCS directory for BE These scripts implement a number of Jaguar information on changes to the tools. @ DSP/GPU-specific debugger commands. The a @. problem is most likely one of the following: | 4 Q: How frequently are the development tools M1) RDB.RC was not loaded at startup because it updated ? i | could not be located. The complete pathname for @ this script file must be contained in the RDBRC A: There is no particular set schedule for B® senvironment variable. See the Configuration updates. New versions are made available as soon pie section of the Getting Started chapter for more as they are ready. We are constantly improving q information. our tools, This includes expansion to other 4 platforms and strong improvements in user @ 2) The RDB.RC file has been edited and no interface. The MADMAC Assembler, ALN “GR longer loads the OD.DB script. linker, and RDBJAG/WDB debugger are updated 4 often, and the most recent versions are always @§ 3) The OD.DB script file could not be found. available online. @ This script must reside somewhere in the search 4 path specified by the DBPATH environment It would be a good idea to get into the habit of po checking the online areas at least once every week 4 or two to see what’s new and improved. 

a About Documentation Clarifications f+ Q: I want to program parts of my program (i.e. linked correctly, you should follow some major @e the user interface in the selection menus) in the guidelines: WE 68000 using the C compiler. How do | avoid ey uunexpected crashes? * Always link C-compiler code to be the last . | module(s) 4 A: Most problems with code written in C happen because C compilers do not know about Jaguar* Phrase align the end of every segment of ~ | specific requirements such as phrase alignment or assembly language module you write. This @ie double phrase alignment. To make sure that a file means separate alignment for text, data and q that contains 68k and GPU or DSP code gets bss segment of each single module. as | ©1994 Atari Corp. Confidential Information “JPR Property ofAtari Corporation 26 April, April, 

26 April, April, 1995 

1 ' Page 4 Appendix[A][-][ Frequently][Asked][ Questions][ About Jaguar] 1 The ALN linker has an option that can , automatically align the size of the segments A: The blitter can do this in 8 bit wide segments, _ inside each included module to a specified so you have to setup the blitter to do two blits of 8 q boundary. bit source width. | | xe . * Make sure that you phrase align ALL data you ! are using/generating from the C-module(s). A Q: As there are objects that must be two-phrase | way to achieve this is to build a customized aligned, is there an '.dphrase' feature in the Yo 1 malloc() routine that only gives back phrase assembler ? : aligned blocks of memory. Always generate a : the structures to work within these given A: Yes, MADMAC can do this. You can also tell ] i blocks. You may also use hard-coded adresses the linker to automatically align each segment of : a to structures that have to be accessed in phrase | €ach module in a variety of different ways, a ‘ mode. including single or double-phrase. But it is 4 4 . generally a good idea to make sure that your S 1 * It may work better if you define some of your objects are located correctly without requiring = { arrays and initialized data inside assembly these features, either by preallocating memory for 4 ] modules, and reference them as ‘extern’ in the objects and corretly adjusting them or by 4 : your C code. hardcoding their adresses. |] { a See the MADMAC documentation for more s 1 Q: I want to use the ‘character painting' feature ct information. , 4 the blitter to use a 16x16 bitmap for my font. . | Programming Questions 1 Q: How to save highscores in the EEPROM of i my cartridge? A: There are many different ways of speeding up = ; code. In general, do not spend more time than : A: One of the sample programs provided in the absolutely neccesary doing 68000 code. The = developer's kit demonstrateshow to do this. See more you can utilize the GPU, DSP, and Blitter, q the Sample Programs section for more the better your program will run. Here are some | information. basic guidelines: } i * Optimize all 68000 interrupt code to need the S q Q: I do not now how to setup sound. Where I find absolute minimum of time. q the documentation ? ’ * Try to keep the 68000 off the bus. For : j A: Refer to the sample program source code for example, don’t run 68000 code directly from S : SIMPLE. Also investigate the Jaguar Synthesizer ROM space. Accessing ROM takes as much | | | and Music Driver. Also look into the Jaguar as 10 times longer than accessing DRAM. 4 1 Console Hardware Release Notes section of the 4 : Technical Reference chapter. * Don't use the 68000 or even the GPU or DSP § | RK for memory copy operations, use the Blitter. 3 ‘ Q:to Mydo. code seems to be too slow for what want «Use the Blitter in phrase mode where possible. _ q |26 April, 1995 Confidential Information FER Property ofAtari Corporation ©1994 AtariCorp, Ti 

4 Appendix[A][-][ Frequently][ Asked][ Questions][AboutJaguar] Page 5 a my =6Use the GPU and the DSP for calculations one's header, and copy data from the bitmap to | where possible. You may have them both the line buffer (keep your bitmaps in DRAM, not | runnning at the same time. ROM!) This all has to take place in approx. 63.5 ' usec or less on NTSC systems (PAL gives you a @ = * You may start the Blitter and do calculations little more time, but we suppose and urgently in DSP or GPU until the blit is completed. suggest that you would want to have your Ei software running everywhere). The number @ * Becareful to interleave the instructions for depends to a big part on DWIDTH and on the 5 any GPU or DSP code you write so that you hardware configuration for RAM access as set in t avoid register wait states. MEMCONI (don't change this register!). | Please read the Jaguar Programming Tips & Please read the Jaguar Programming Tips & @ General Procedures section of Appendix B. General Procedures section of Appendix B. | Q: Does the Jaguar feature support for analog Q: I run out of time by using the object processor @ joysticks and other special controllers? for moving objects by just changing the XPOS g and YPOS fields of the objects. How to avoid § A: Yes, you'll find a sample program included that? @ with your development system. See also the @ Jaguar Controllers and Controller Ports A: Aside from any other optimizations of your “6 MD section of the Technical Reference chapter. object list that may be possible, you may simply be eating up too much bandwidth with an object a koe x list that contains too many moving objects. As ~@ Q: Thave set upa list of 50 objects to be general rule we would like to ask you to: @ displayed, but it does not run. * Use the Blitter to draw/move the objects if the | A: If you maintain the object list with a 68000 objects are static for more than ten trames - interrupt handler, you might be running out of @ itime during the interrupt routine because the nexi * Move the objects with the object processor if f interrupt occurs while you are still handling the your objects move faster than every ten @ so prrevious one. frames. @ You might be able to solve this by optimizing We sour 68000 code, although if this doesn't work, Q: I want to use the MMULT instruction in GPU @eSCéyou may need to move your object list update and/or DSP. How is the data organized if the B® routine to the GPU. (Which is going to be the second matrix is stored in RAM? 1 better solution in the long run.) i ' A: The organisation in RAM is word packed, as ; | The main limitation is not the number of objects in the registers. However, this instruction has Be Soyo have overall, it is the number of objects been designed for implementation of algorithms 4 1 which must be displayed in the same horizontal that operate on word packed data structures as - line. 8x8 matrices in discrete cosine transformations so you should not use it for general purpose matrix ff We The main restriction on the size of the object list calculations. You in general are better off if you ; ] is the time it takes for the Object processor to spare the time for packing and unpacking the data q ] scan all the objects for a given scanline, read each 

a 

© 1994 Atari Corp. 

Confidential Information FER Property ofAtari Corporation 

26 April, 1995 

| Page 6 Appendix A - Frequently Asked Questions About Jaguar q | and use an explicit sequence of IMULTN, : IMACN and RESMAC instead. - ue 4 | About Documentation Bugs And Additions sd | Q: My branch objects do not work as advertised. Q: I've created an object list that includes a GPU 3 Why? interrupt object, but instead of the interrupt : | occuring just on the scanline I've specified, it Py i A: There is a typo in the Jaguar Software appears to occur on every line. ] | ! Reference Guide before Rev 2.2, May 3rd 1994. a i The word ‘not’ is missing in the description of the A: There is a typo in the Jaguar Software if BRANCH object type. The LINK field contains Reference Manual before Rev. 2.3 on page 17. ‘ ‘ the phrase aligned address that is used if the The Graphics Processor Object does not have a & q branch is not taken. YPOS field. Bits 0-2 are the object type, and bits = eee 3-63 are DATA to be used by the GPU interrupt 4 q service routine. t 4 Q: Are the DSP timer divider registers JPIT2 and = ; JPIT4 write accessed at the same memory To work around this, simply use branch objects s q location? immediately before your GPU object so that the | | a . GPU object is called only for the scanline(s) you q A: No, that is a typo in the Jaguar Software desire. 4 4 Reference Guide before Rev 2.2, May 3rd 1994. an 4 , The location for JPIT2 write access is $F10002, vm a for JPIT4 it is $F10006. | | Abouthardwarefeatures§ = =i Q: In the demos, all object lists have two branch Q: My program code runs unreliably when I i . objects in the beginning. Why? switch the Object processor on and off during the ; run of my code. a Z A: The two branch objects are mandatory to keep a | the hardware happy. Unless your object list A: Never disable the Object processer once it is f | : contains only a single stop object, always include running. Your goal is probably to turn off video. ' these two branch objects at the beginning. You can do this by aiming the Object List pointer dl eee at a single STOP object. ' —_ | Q: Shading texture mapped surfaces using SCRSHADE does not work correctly. Q: Do the PWM DACs not work? : 7 A: The documentation states "SRCSHADE may A: Correct, do not use the PWM DACs. They are q j be used with GOURZ, not with GOURD". There not even connected in the Jaguar console. Use the j q is a bug in the blitter that requires GOURZ to be I?S-Bus for sound activities. Refer to the source os set. See the Blitter BUG List section of the files SIMPLE.S and SIMPLE.DAS, which are . E Technical Reference Chapter. part of the SIMPLE sound example program.. | 4 26 April, 1995 Confidential Information TR Property ofAtari Corporation © 1994 Atari Corp. { 

| 4 : . 1 Z | : ' 7 ‘ q j | q q 

7 

4 1 Appendix[A][-][ Frequently][ Asked][ Questions][ AboutJaguar] Page 7 Fae: Accessing Jaguar registers and On-Chip RAM _ really make sense. Refer to the GPU/DSP Bug sometimes has unpredictable results. What is List section of the Technical Reference Chapter. B going on? ae | A: Never access the On-Chip RAM in the Jaguar Q: I want my object list update routine to do as { | Chipset except by reading or writing longwords. little work as possible. Exactly which fields of Same holds for ALL 32 bit wide registers. theframe? objects need to be reinitialized before each 4 eae | Q: Every time I use the 68000 elr.| instruction to A: The following fields are changed by the object @ set registers in the Jaguar Chipset the result seems = processor and must be reinitialized after the end @ tobe unpredictable. Why is that? of a frame: @ =A: Never use the 68000 elr.] instruction for Bitmap or Scaled Bitmap: HEIGHT, DATA @ accessing long words in the Jaguar GPU & DSP Scaled Bitmap: REMAINDER # address space. This includes both hardware @ registers and internal RAM. As you can perform Note that there are some intcresting effects that } the same operation more efficiently and more can be achieved by not updating these fields after @ quickly using other instructions, there should be each frame. @ no reason to use cir.! anyway. . i q One such effect is that by arranging your data as a . Wii The problem has to do with the way this vertical strip of frames and by setting the - Fr’ particular instruction writes to memory, which is HEIGHT field to the height of this strip (number @ different from most other 68000 instructions. of frames * scanlines per frame), you can play | This problem can also happen with certain other back an animation automatically without updating ; f instruction and address mode combinations. the object's DATA pointer yourself. This works @ Please see the Hardware Bugs & Warnings because the object processor will keep updating ; chapter for more complete information about this the display and incrementing the DATA field as { problem and how to work around it. ~ long as the HEIGHT field is non-zero. (This ; | ae requires a branch object before the bitmap object = so that the proper number of scanlines are done q Q: Some sequences of GPU statements are not during each frame.) You don't have to update the working. Is this a hardware bug ? object until after the end of the last frame. : ] A: The current revision of the GPU chip has a @ few minor problems which mostly would appear only in cases where the running code would not 

Be QQ: The newer versions of RDBJAG cannot transfer data correctly to a Sylvester development system. . A: Boy, do you have an old system! If you are still working on a Sylvester, you should immediately "We contact Jaguar Developer Support to exchange it. The Sylvester is very outdated and should not be used My for development any more. 

r ©1994 Atari Corp. 

Confidential Information FOR Property of Atari Corporation 

26 April, 1995 

Appendix B B - Programming Programming Guidelines == = = #§.§§ =. =i that have proven to be effective and efficient. types of files of files files is strongly strongly recommended. The by Atari for all of our our sample programs and programs and and Filetype library. some people for raw binary ROM raw binary ROM binary ROM ROM image files, but executable program file. program file. file. Output from ALN Linker. ALN Linker. Linker. patch. This is a MADMAC MADMAC source code code file that that is | image of program of program program code, data, a picture, a picture, picture, or whatever. whatever. ; assembler creates .BIN files containing the creates .BIN files containing the files containing the containing the the ( assembled file(s). file(s). | | 

a 

**==> picture [559 x 763] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|.|
|'|Page 8|Appendix B B|- Programming Programming|Guidelines|
|Programming|Guidelines|==|=|=|#§.§§|=.|
|:|Below|is|a number|of guidelines|for Jaguar programming|that|have|proven|to|be|effective|and|efficient.|
|j|Filename|Extensions|
|i|The use of standardized filename|extensions|for various|types of files of files files|is strongly strongly|recommended.|The|
|||table below shows|the|standard|filename|extensions|used by|Atari|for|all|of our our|sample programs and programs and and|
|1|libraries:|
|i|Extension|Filetype|
|i|pS|compatible with the Jaguar 3-D Graphics|library.|
|if|This|extension|has|also|been|used|by some|people|for raw binary ROM raw binary ROM binary ROM ROM|image|files,|but|
|ayy|oO|thisDRI/Alcyonusage|format absoluteis|discouraged.|location executable program file. program file. file.|Output from ALN Linker. ALN Linker. Linker.|
|a|ASC|ASCII|version|of Jaguar Synth sound|patch.|This|is|a MADMAC MADMAC|source code code|file that that|is|
|A|driver.|
|i|Binary data.|This|could be a binary|image of program of program program|code,|data, a picture, a picture, picture,|or whatever. whatever.|
|'|The LTXCONV utility used with the GASM assembler creates .BIN files containing the creates .BIN files containing the files containing the containing the the|
|a|combined TEXT & DATA sections|of the assembled file(s). file(s).|
|a|using the SNDCMP|utility.|
|F|-.CRY|Madmac|source|code|file for|a CRY-format|graphics|image,|typically|converted|from|Targa|
|4|DSP|assembly|language|source.|This|extension|is|used|for files|that|contain|source|
|a|GPU|assembly|language source.|This|extension|is|used|for files that|contain source|
|4|3D|3D|object data|in MADMAC assembler source format.|Output from the 3DS2JAG|utility.|
|4|JAG|Jaguar JPEG compressed|graphics|image.|Created|by the JAGPEG|utilities.|(Note that|
|cS|JAGPEG|has|been|replaced|by the BPEG|package.|Also,|the 3DS2JAG|utility|that|
|7a|pT convertsused|the Autodesk.JAG|extension 3D Studio(it|has intosince sourcebeen codechangedformatto foruse the.J3D).Jaguar|3D libraries once also|
|i|-LTX|GASM|assembler|output|file.|The GASM|assembler|does|not|output|files|that|are|
|:i|A26|April, 1995|Confidential Information|F@®|Property ofAtari Corporation|© 1994|Atari Corp.|

**----- End of picture text -----**<br>


**==> picture [555 x 740] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|
|Page 9|
|j|Appendix|B|- Programming Guidelines|
|22|Extension|Filetype|
|~~|LZSS Compressed data file.|This is a binary|file containing raw LZSS-compressed|data.|It|
|is created by the LZJAG|utility.|This|is linked into your program, and then decompressed|
|A|||using the DELZJAG|routines.|
|t'|pw|MIDIPARSE scoreutility file. to createThis is|a|MIDImusic file sc|o|reutp|u|sat|b|yle a by MIDI the sequencer. Jaguar SynthYou & Music feed thes driv r.|e|files to the|
|68000/mixed|object module.|Object file created|after assembling a .S file with MADMAC|
|Some|of the conversion|utilities create MADMAC source code files that don’t always end|in|
|filename|extensions|of|.S, and they may also use the O filename extension after being|
|Ss||assembled.|
|Ou|DSP|(JERRY)|object module.|Object file created|after assembling a .DAS file with|
|t|MADMAC (Note that GASM does not create standard object modules.)|
|or|
|7|Some older projects have used an extension of .OD for DSP object code. However, the .OJ|
|=]|.OD.OT|extensionGPU (TOM)is objectpreferred. code.|Object file created after assembling a .GAS file with MADMAC.|
|.|(Note that GASM does not create standard object modules.)|
|!|or|||
|Some older projects have used an extension|of .OG for GPU object code.|However, the|
|4|.OG|OT extension|is|preferred.|
|t|Parsed MID! file, output by the PARSE and MERGE|utilities.|This|is really a MADMAC|
|4|source code file which|is normally assembled|into an object file using a “SCR extension|
|&|Jaguar Sound Tool Patch|File.|These are the binary patch files used by the Jaguar Sound|
|||Tool and the Jaguar Synth.|
|) ii|.ROM|Alpine Board/ROM Image File.|Created by FILEFIX utility, or saved from Alpine board|
|.|i|using the debugger.|
|’|Using the debugger, 4 ROM file can be loaded into Alpine board by "read <file>.rom|
|B|802000" or “fread <file>.rom 802000"|(FREAD uses faster I/O routines)|
|4|Using the debugger, a program can be saved to a ROM file from an Alpine board by “write|
|q|<file>.rom 802000[1FE000}"|for a 2 megabyte|(16 megabit) program|or “write <file>.rom|=|||
|=|802000[3FE000}"|for a 4 megabyte (32 megabit)|program.|
|68000/mixed|assembly language|source.|This extension|is used for files that contain|
|&|source either exclusively for the 68000 or mixed source for any combination|of 68000,|
|=|GPU,|and/or DSP.|
|||Smooth-format 16-bit CRY Cinepak film|(Note: This file extension|is also used|in some|
|g|cases to designate|object files containing|music data.)|
|||4|SCR|Compiled MIDI score file.|This is an object file, the same as CO files, except with a different|
|q|extension to highlight the idea that they contain MUSIC score information.|Files with an|
|{|||SCR extension are to .MID files as S files are to .O files.|
|Ss|Note: This file extension16-bit|RGBis alsoCinepak usedfilm for some Cinepak Movie Files (Smooth CRY-format).|
|-|||sag|—|Sineatv format|T6-bH|AGB Ginepak|fim|
|eS|SYM|Symbol Table File.|Created by FILEFIX utility.|This|is the same basic format as an|
|=|executable program|file, except with empty TEXT and DATA sections.|Only the symbol|
|,|||table|has|information|in|it.|
|=|TGA|Targa picture file.|The Targa format|is a popular format for 16-bit and 24-bit RGB true|
|-|-|color graphics images.|Can be converted into Jaguar CRY-format using the T@A2CRY|
|-|Binary image of a program's TEXT segment.|Created by the FILEFIX|utility.|The current|
|-_|or|version of FILEFIX produces files with a “.TX” extension.|However, older versions created|
|=|TXT|files with the|“.TXT” extension. Because the .TXT extension|is also used for ASCII text|
|-|||files, this was changed to avoid|conflicts.|
|9|(©1994 Atari Corp.|Confidential Information|JPR|Property ofAtari Corporation|26 April, 1995|

**----- End of picture text -----**<br>


' _. Page 10 Appendix B - Programming Guidelines g Extension Filetype 7 . Waveform definition. Used by the Jaguar Synth & Music driver. y a 5 Please do not use the filename extensions shown above for file types other than those shown. This can 3 : be a cause of great confusion. Perhaps the most common misuse of filename extensions is using ".ABS" a i for ROM image files that should have ".ROM"” extensions. | BasicTestingFordaguarPrograms i It is important that your Jaguar programs run at the proper address, start themselves correctly, and do 4 not try to write data at runtime into the ROM address space. With a development system, it is possible | : for a program to do any or all of these things, and you may not even realize it's a problem until you try | i to execute your program on a standard retail console. The earlier your programs avoid such problems, j ‘ the easier the task is. 4 i 7Below is a short basic test procedure that should be tried frequently with all programs destined to | | become a cartridge. It is by no means a complete and comprehensive testing procedure, but it will j au . : . > } d confirm the basic operation of your program. | i 1) Set the Alpine's memory protection switch to "Write Enable". a i 2) Download the code & data to the Alpine board. Make sure you are not downloading code or 1 a data directly to the console's DRAM (i.e. memory addresses from $200000-down). on i L 3) Set the Alpine's memory protection switch to "Write Disable”. 5 / 4) Turn off the Jaguar console. Wait for about 20 seconds. ; 5) Hold down the 'B' button of Joypad #1 and turn the console power on. j : 6) The standard Jaguar startup screen should appear with the Atari logo and spinning Jaguar cube. ! Release the 'B' button. Now press and release it again. . | 7) Your program should now start immediately. If it does not operate as expected, then you have a i | q problem that needs to be solved. This can include: trying to write to ROM, being at the wrong | address (your programs must start at $802000), or having bad or incomplete startup code. . ' 8) Hold down the ‘B’ button of Joypad #1 again, and hit the RESET button on the top of the Alpine Ss a board. You should see a repeat of steps 6 and 7. : 7 The steps above should be the first stage of your overall test procedure. Of course, once your program ' j q is known to pass this test, you need to subject it to a variety of more complete and more sophisticated . | tests. No Jaguar program should be released to the public without having first passed a comprehensive : testing procedure. yy ' 26 April, 1995 Confidential Information AR Property ofAtari Corporation © 1994 Atari Corp. : ] 

Page 11 

Appendix B - Programming Guidelines 

- | The following is a list of several tips for Jaguar programming. Some might seem obvious to experienced Jaguar programmers, but there are also some new tips that reflect newly discovered bugs or simply better methods of doing things. 

   - 1) In order to guarantee proper system initialization, every Jaguar program must start out with the standard startup code supplied in the JAGUAR\STARTUP directory of the standard Jaguar Developer distribution. 

   - 2) Every object list must start with two branch objects. The first one should branch to a stop object if VC <a_vdb, and the second should branch to a stop object if VC > a_vde. The a_vdb and a_vde variables are calculated by the video initialization routine shown below in item 3. 

## =) 

   - Use the blitter in phrase mode whenever possible (it is much, much faster). 

- 4) Because of a blitter bug, you must always set Al_CLIP to 0 prior to each blit, even if you aren’t enabling clipping in the B_CMD register. 

- , 5) Don't rebuild your entire object list every vertical blank. Only update the individual fields of the 

- 7 objects that need to be updated. e 66) The GPU and DSP may not be reliably stopped once they are running by anybody but themselves. This is a recently documented bug. GPU or DSP code which needs to run most of the time but be stopped occassionally should monitor a semaphore and shut itself down when the semaphore is given the “shutdown” value. (Alternately, a GPU or DSP interrupt could be used to tell the GPU or DSP to shut themselves down.) 

- = 7) The YPOS field of GPU Interrupt Objects was misdocumented as existing. This field does not exist. You can use branch objects to simulate the result of that field. 

- | 8) In order for GPU or DSP interrupts to be handled, those processors must be running. If no 

- |] program other program is running and you want interrupts to be handled, leave a small piece of & GPU or DSP code running that continuously checks a semaphore to determine whether it is OK gs to shut itself off. Keep in mind that as long as the semaphore is in internal RAM, this uses no bus bandwidth, so it shouldn’t affect the rest of the system at ali. Do not put either the GPU or 

- @ DSP into a tight (i.e. one line) infinite loop. | wD When copying data to GPU or DSP RAM or I/O registers, always copy long words. f 10) When the Jaguar console resets, the interrupt stack pointers of the GPU and DSP are in an 

- P undefined state. Always initialize these registers as needed. P 11) Avoid creating object lists at assembly-time which are used directly from ROM at runtime. The 

- P| bus access speed for ROM is much slower than for RAM (up to 10x slower), and the amount of , | time required to process your object list will increase dramatically, and some object lists may not. ; function at all. Always create your object list in RAM (or copy it to RAM before using it). 

- 4 E ©1994 Atari Corp. Confidential Information PPR Property ofAtari Corporation 26 April, 1995 

| Guidelines bus access 4 bandwidth 4 of the system) the system) system) y utilities, | j is to use to use use and then then (like a vertical a vertical vertical | the memory memory s more also a a it from from 5 rm ' or from from ; could be 4 a stack 4 basic steps 4 | registers. 4 ] it into a & : a stack. stack. If the the | § interrupt registers to to - | 3 8 of the of the the | ’ the semaphore semaphore &. If there there 4 next one off one off off j should get the get the the q © 1994 Atari Corp. 1994 Atari Corp. Atari Corp. Corp. 4 

_ Page 12 

Appendix B - Programming Guidelines 

I 12) Avoid displaying bitmapped graphics directly from ROM. Because of the greater bus access L times required for ROM, a bitmap object with data in ROM will use up the system bandwidth : available to the object processor (and therefore the bandwidth available to the rest of the system) the system) system) ~ very quickly. To save ROM space, compress the images using the JAGPEG or LZJAG utilities, i and then decompress them from ROM into a RAM buffer, from which they get displayed. | 13) | Use the GPU and DSP as much as possible, instead of the 68000. The optimal solution is to use to use use i the 68000 to get the program started and load some code into the DSP and/or GPU, and then then shut the 68000 down using the STOP instruction. 4 However, if you are using the 68000a lot, or are using it for time-critical routines (like a vertical a vertical vertical q blank handler), copy your code from ROM to KAM and execute it there. That way, the memory memory 4 accesses done by the 68000 to read instructions will hog less of the system bus, leaving more ' bandwidth available for the object processor, blitter, DSP, and GPU. Your code will also 1 execute more quickly. i 14) To save ROM space, compress your code using the LZJAG utility and then decompress it from from i ROM to the execution address in RAM. ‘ IdeasTOTry ar— rm i 1) If you havea lot of blit operations to be done, especially from different processors or from from | interrupts, rather than wait around for the blitter to be available each time, when you could be | . doing other processing, implement a GPU-interrupt routine that reads blit requests oft a stack a and sets up the blitter registers and starts the blit operation for you. Here are the basic steps a involved: § a) Define a structure that contains the values that need to be stuffed into the blitter registers. i: Also include a pointer to a semaphore variable. 4 b) When you need to do a blit, set up one of these structures, and stutf a pointer to it into a | variable. Clear your semaphore, and then force a GPU interrupt. 4 c) The GPU interrupt handier will grab the pointer to the structure and stuff it into a stack. stack. If the the i blitter is currently busy, the interrupt exits. If the blitter is currently free, the GPU interrupt g handler pops the pointer back off the stack, reads the structure, and stuffs the blitter registers to to q start the blit. The interrupt handler will then exit. | d) When the blit is completed, another GPU interrupt will occur (you must set bit 8 of the of the the G_FLAGS register to enable this). The interrupt handler will grab the pointer to the semaphore semaphore q for the just-completed bit, and stuff a value into it that indicates that the blit is finished. If there there 1 are any more blit requests waiting on the stack, the interrupt handler will grab the next one off one off off 1 the bottom of the stack and get it started. i - Of course, this is just a rough outline, so the details are glossed over a bit, but you should get the get the the 4 26 April, 1995 Confidential Information JER Property ofAtari Corporation © 1994 Atari Corp. 1994 Atari Corp. Atari Corp. Corp. 

Page 13 

4 | 

{ 

| 

Appendix B - Programming Guidelines basic idea. Steps c and d are done more or less invisibly to the processor and code that requested the blit in the first place. As long as your actual calculations aren’t affected by blits that aren’t completed yet, you’ll never have to wait for the blitter. Also note that using a GPU interrupt to put items onto the stack isn’t really necessary if all your blitter requests are coming from the GPU in the first place. 

© 1994 Atari Corp. 

Confidential Information JPR Property ofAtari Corporation 

26 April, 1995 

i 

™ = = ii ergo 

ret ero saan rene ; 

E. | | | | 

Appendix B - Programming Guidelines 

' ' 4 

| | 

| | 

I 

i. 4 ‘ | q 1 ‘ 1 | 4 4 : 4 4 | 

, . a q 

i q 

## Page 14 

## Jaguar Atari-Based Development System information 

**==> picture [70 x 51] intentionally omitted <==**

**----- Start of picture text -----**<br>
'<br>= @I€<br>**----- End of picture text -----**<br>


This section focuses on the differences between the standard PC/MSDOS-based development system and a development system based around one of the Atari computers. 

First of all, with only a few exceptions, the documentation for the tools applies to both the PC/MSDOS version and Atari TOS version. In those instances where there are differences, they are noted. 

## GeneralGuidelines = 

A standard component of MSDOS is the command line interpreter COMMAND.COM. On the Atari, there is no corresponding system shell; programs are normally launched through the GEM Desktop, part of the system's GEM graphic user interface. 

Without a full-blown integrated development environment of some kind, a command line interpreter is 4 essential for development work. Therefore, for the Atari we provide GULAM, a command line interpreter patterned after the UNIX C-Shell. GULAM is launched from the GEM Desktop like any other program, and once loaded takes over the system with its own text-based screen. GULAM uses j . customizeUNIX-stylethiscommandsto suit yourratherownthanpreferences.MSDOS-style,Pleasebutseesupportsthe GULAM-specificcommand namedocumentationaliases, so youforcanmore information. Also provided for the Atari is a version of MicroEMACS, a popular text editor. The GULAM shell : actually has a version of EMACS built-in, but the one we provide separately is more recent and more ; sophisticated. hs Currently we provide the GNU GCC cross GNU GCC cross GCC cross cross compiler that runs on on PC/MSDOS systems and generates : 68000 code. We do not currently provide the GNU GNU GCC compiler for the Atari computers. Hcewever, = the standard Atari version of GNU GCC GNU GCC GCC used for building programs programs for the Atari TOS computers can j also be used to generate code for the Jaguar. We consider it likely that developers who who prefer the ' based development system are going to already have the Atari version of GCC. GCC. However, if you you do 

Currently we provide the GNU GCC cross GNU GCC cross GCC cross cross compiler that runs on on PC/MSDOS systems and generates 68000 code. We do not currently provide the GNU GNU GCC compiler for the Atari computers. Hcewever, the standard Atari version of GNU GCC GNU GCC GCC used for building programs programs for the Atari TOS computers can also be used to generate code for the Jaguar. We consider it likely that developers who who prefer the Ataribased development system are going to already have the Atari version of GCC. GCC. However, if you you do not have the Atari version, and do want to work with it, let us know. Other Atari-based C compilers can also be used to gencrate 68000 code, provided they can output either DRI or COFF-format object modules. 

Please stay in touch with the Jaguar Developer Support people at Atari. We are looking forward to helping you to make your product a sottware experience that takes the utmost advantage out of the Jaguar's excellent hardware. 

Confidential Information FER Property ofAtari Corporation 

© 1994 Atari Corp. 7 

26 April, 1995 , 

Appendix D - Jaguar Development Standards 

: : : : 3 ; 

; { i 

: 

: . 4 

. 

A 

A 

## Page 15 

Gijaguar Development Standards| To insure consistency and to maintain the high quality of Jaguar software, the following standards must be adhered to by all developers: Please ensure that you contact Jaguar Developer Support before submitting code for Compatibility Coding if you have any questions regarding these guidelines. Items shown in italics apply to ttles published by Atari and must be adhered to by Atari-contracted developers, in addition to the other standards. 1) The title screen must contain all necessary copyright information: 

- ° The phrase “Licensed to Atari Corp.” must follow the copyright information on games licensed to Atari Corp. 

- ° The phrase “Licensed by Atari Corp.” must appear following the title screen on third: 

- party Licensee titles. 

- ° Programming credits may be included as desired, but they carinot replace or precede copyright information. 

° The title screen(s) must be the first visible screen(s). 

## » 

- 2) The “0” button should be used on the title screen to toggle game music oft and on, game sounds are unaffected by “0”. The default condition of the music (upon boot-up cr Restart) should be on. If the “0” button is not used in the game, it should be used to toggle game music off and on during all other game play screens as well. 

- If the music is toggled off[by][the][“0”][ button,][the][ music][ volume][slider][ should][ go][to][“0”][ volume][ as] well. Alternately, the volume slider can remain fixed at the current volume and the message “mute on” can be displayed. 

- 3) The Restart function of simultaneously pressing the “#" and “*” buttons should resct you back to the title screen. The order in which the buttons are pressed should not matter. Reset should occur immediately. 

- 4) When the Pause button is pressed, all game actions must immediatcly stop and the word “PAUSED” must be displayed in the center of the screen. When the button is pressed again, all game actions should immediately resume and the word “PAUSED” should be erased from the screen. The Pause indicator should be of such color and size that it is easily seen. It is helpful to game magazines if pressing the 1 and 3 keypad keys while paused removes the pause message to facilitate screen captures. 

- 5) Pause and Restart should be allowed anytime during a game with the exception that Pause is not necessary on the title screen. 

© 1994 Atari Corp. Confidential Information FPR Property ofAtari Corporation 

26 April, 1995 

i j , q ' ‘ ‘ ; ] | ' i i 

Page 16 

Appendix D - Jaguar Development Standards 

: | yy | q 

' 

: | i i ‘ : j q 3 : : : L : ; : 

ih) | q } : a ny ( 

- 6) We require a demo mode in all games showing some game action. This should be automatic from the title screen after a brief time of no user action and can also be an option on the option screen. Without a demo mode, retailers are much less inclined to have your game in the machine in their point-of-sale display. 

**==> picture [8 x 14] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


   - 7) Please ensure that any text you may display during the game can be read easily over all backgrounds. Either a contrasting color scheme or an outline around the text is recommended. 

   - 8) The “Completion of Game” logic should work as follows: When the game ends, there will probably be a “Congratulations” screen, or a high score screen. No matter what screen is shown, you must construct the end of the game so that the user cannot bypass any “Congratulations” text or High Score screen accidentally. Make the program work such that a Restart is required to return to the title screen from the “Congratulations” screen, OR implement a timer which ignores all input for a period of time (except timer wouldn’t restrict Restart) so that the user does not miss any valuable information. 

- _ 9) For normal “Game Over” screens, allow any fire button press to return you to the title screen. 10) For multi-player networked games, use the Modem/Networking developer guidelines. 

   - 11) | We recommend that the high score screen displays the current version number on the title screen during final testing. If there is no high score screen, the version number can be displayed in the “Pause” screen. This version number must be removed prior to release of software. 

The last digits of the top high score in the default high score table should be the version number of the software. 

- 12) Joystick port 1 is to be used for a one-player game. Joystick port 2 is to be used for the second player in a two-player game. See the Enhanced Joystick/Multi-player Adapter documentation for further details. 

- 13) The “B” button should be used as the primary action button; the “A” button should be used as the secondary action button. The “C” button should be used as the third action button. Ifa button is not used then it should be used as another “B” bution. There must be an option to allow users to reconfigure the default settings. 

## Buttons must be implemented this way. 

- 14) When the game is paused, pressing the “A” button should bring up a visual indicator and allow the user to adjust music volume via the joypad. Pressing the “B” button should bring upa visual indicator and allow the user to adjust sound effects volume. The “C” button can optionally be used to adjust a specific sound such as engines or voices. The indicator should be removed by the same button that brought it up. The volume level information should be saved when the high score or controller configuration information is written to the cartridge EEPROM. 

The visual indicator used for adjusting music volume or sound effects volume should be a horizontal bar. 

**==> picture [3 x 12] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


26 April, 1995 

Confidential Information TER Property ofAtari Corporation 

© 1994 Atari Corp. 

Appendix D - Jaguar Development Standards 

Page 17 

- W 15) The “Option” button should be used to take the user to the Option screen. There should be an option to reconfigure the default joypad controls. This should also be saved to cartridge. 

This option should be allowed during Pause also. 

   - 16) The stored information in the EEPROM should be cleared if the user simultaneously presses #, * and Option at the title or options screens. A message “Cartridge Memory Cleared” should then be displayed. 

- | 

- 17) The EEPROM data must be checksummed. If it is invalid or the EEPROM has timed out due to wear or failure, the default settings should be used. The game must never hang due to EEPROM fault. 

- 18) | We recommend using the keypad for passwords. 

- 19) The NTSC and PAL versions of a game must both be in the same cartridge. 20) Ifa game has a save game feature, it must be allowed only when the game is paused. A message “Game Saved” should be displayed below the paused message when the game save feature is activated. 

- 21) = Any game ofa graphically violent nature must contain a parental lockout code. Default ts “Tockout On”. The code must be changeable by a parent following instructions in the game manual. Under lockout no extreme violence is displayed. The code should be enterable only on the option screen. 

' 

i : 

EI SIESNSS i 

nt 

err reesceom sarmate—e 

pepe s= 

| 4 . Page 18 Appendix E - jaguar Software Software Experience Approved Manufacturer Production Manufacturer Production Production Guidelines | JAGUARSOFTWAREEXPERIENCE = # | Approved Manufacturer Production Guidelines | Mersion1.5,October?18,1994 

jaguar Software Software Experience Approved Manufacturer Production Manufacturer Production Production Guidelines = # 4 

| i : f } | i ' i [ i | i 4 : | 1 | | ’ 

c 

| ; | 

| | 

\ 

## 1) COMPATIBILITY CODING AND CONTENT VERIFICATION 

Publisher will send to Atari: 

1. Code on either floppy (for cartridges) or CD master (for CD ROM). ROM image (.ROM file, single contiguous file containing executing at $802000) on floppy must be ZIP'd and spanned across floppies using PK ZIP v2.64 or greater. 

2. Two sets of blank floppies. EPROMs (150ns or faster) or CD masters so we can return compatibility coded version of title. 

3. Completed Code Submission Form and affidavit of Content Descriptor (see section HI for information on Content Descriptor). 

4. Documentation of testing procedure and proof that the testing procedure has been adequately satisfied. 

5. Instructions to play submitted software. 

Atari will: 

- i. Review game to see if it adheres to the Jaguar Development Standards document guidelines (for fire button use, etc.) 

## 2. Pertorm a hardware compatibility verification. 

If code is accepted for compatibility coding, Atari will compatibility code and return it to the publisher. 

If code is rejected or other problems are found, an anomaly form will be faxed to the publisher. The publisher then can correct the problem(s) and resubmit code. If they need to resubmit code more than once, Atari will charge $250 per additional submission for re-review of the code and compatibility coding. 

**==> picture [12 x 10] intentionally omitted <==**

**----- Start of picture text -----**<br>
we<br>**----- End of picture text -----**<br>


## Option 1 

Please see Jaguar Product Style Guide for Atari recommended box design. 

| 

Appendix E - Jaguar Software Experience 

Approved Manufacturer Production Guidelines 

Page 19 

| UU Option 2 | 

: 

Publisher's custom designs are allowed with prior Atari approval. 

## General guidelines: 

| Themay Jaguarnot be logoobstructedmust appearby otheronartwork.the frontOtherof thelicensorbox in dimensionslogos (such noas JessQSound,than 2.5"wCinepak,x 1"h.etc.)It | appear on the back of the box. 

| | 

"Interactive Multimedia Cartridge" must appear across the bottom edge of the box front. 

| 

: 

The Jaguar Compatibility Assurance Hologram (see section IV) must be affixed to the front of the box 

| _ (Subject To Industry Rating System Proposal) | Atari will not censor content; publishers should make themselves aware of loca] laws concerning | entertainment media content. 

(CF Atari does reserve the right to withhold the use of the Atari and/or Jaguar logos to protect the goodwill of the Atari name and contradictory trademarks. The publisher must still properly use the Jaguar Compatibility Assurance Hologram and must adhere to all stipulations set forth in the Third-Party Licensing Agreement. 

Upon submission of the Software Experience tor compatibility coding and verification, the Licensee must also submit an affidavit stating that the Content Descriptor does accurately retlect the content of the Software. 

## EXAMPLE CONTENT DESCRIPTORS... 

- e General Audience Material @ (Graphic/Comical/Light) Violence e Adult-Oriented Themes e Adult Language e Adult/Sexual Situations 6 Partial Nudity e Sexual Themes e Explicit Sexual Themes 

| 

## Cartridges & CDs ("Product") 

@ Option 1 Atari will handle all manufacturing, based on Licensee's ROM or CD-ROM master and production-ready film. Atari will charge our cost, plus a 10% handling fee. 

, 

© 1994 Atari Corp. 

Confidential Information FER Property ofAtari Corporation 

26 April, 1995 

i | Page 20 Appendix E E - Jaguar Software Jaguar Software Software Experience Approved Manufacturer Production Manufacturer Production Production | Option 2Licensee can handle manufacturing themselves. The following services are available from i Atari-approved sources: iq Cartridge Shells (cost: approximately $0.32 each) Source: Stoesser Industries; Contact: Robert Stoesser; Phone 415-969-3252 qi Their supplied casings conform conform to Atari specifications. Publishers can order custom plastic colors, : have their own own logo appear in the molding of the of the the cartridge simply by purchasing a low-cost insert yf Stoesser. | ROMs Sharp; Contact: Paul McCartney; Phone: 408-452-6409 E Samsung; Contact: Lori Steinthal, I-Squared Mfgr. Rep.; Phone: 408-988-3400, x223 MX Macronix Inc.; Contact: Ray Mak; Phone: 408-453-8088 | Goldstar; Contact: Y. Kenneth Kim; Phone: 408-432-1331, x3603 ‘ Standard Cartridge PCB's : Atari will supply board layout information; Licensee must submit manufacturing samples to Atari q approval. We will also be happy to provide direct sources for PCB's. 

Appendix E E - Jaguar Software Jaguar Software Software Experience Approved Manufacturer Production Manufacturer Production Production Guidelines 

f | | 

‘ , . | i : ‘ | | 

+ = CD-ROM 4 WEA/Ivy Hill; Contact Atari for sales office for vour territory. | Cartridge Turnkey Service | Extron Manufacturing (contact: Thao Nguyen; phone 408-456-0180) has been designated as a fully 4 approved manufacturer under the provisions of the Jaguar Sottware License Agreement. || Publisher can create their own cart internal (PCB.. etc., ) design, but it must be submitted, registered and | approved by Atari prior to manufacturing to ensure compatibility with future revisions of Jaguar. This 1 ofwill these accommodatealternative Publishersdesigns may wishingalready to createbe availablespecialto cartsLicensees; with battery-backedcall for availability. up SRAM, etc. Some 4 All ROM and CD-ROM duplication must be performed by the Atari-approved vendors. Publisher shall { have the right to have a ROM or CD-ROM duplicator qualify as a Manufacturer under this Agreement | upon proof of the following: | 1. That manufacturer is properly licensed by Philips/Sony for CD-ROM, if applicable; | 2. That the manufacturer can maintain reasonable quality assurance standards. 3. That the manufacturer agrees to such reasonable security and reporting requirements to assure that compliance with the royalty provisions of the Jaguar Software License Agreement are ; implemented and verifiable by providing any information relating to production of Jaguar ROMs | or CD-ROMs when requested by Atari; and 

**==> picture [25 x 28] intentionally omitted <==**

**----- Start of picture text -----**<br>
raee<br>**----- End of picture text -----**<br>


Their supplied casings conform conform to Atari specifications. Publishers can order custom plastic colors, or have their own own logo appear in the molding of the of the the cartridge simply by purchasing a low-cost insert from Stoesser. 

Atari will supply board layout information; Licensee must submit manufacturing samples to Atari for approval. We will also be happy to provide direct sources for PCB's. 

**==> picture [20 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
i,<br>**----- End of picture text -----**<br>


4. That the manufacturer agrees to maintain Atari's intellectual property rights. 

**==> picture [2 x 21] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


j 

26 April, 1995 

Confidential Information TER Property ofAtari Corporation 

© 1994 Atari Corp. 

Appendix E - Jaguar Software Experience Approved Manufacturer Production Guidelines 

Page 21 

: “~~ however under no circumstances shall Atari have liability for the conduct of the manufacturer. Atari } ‘7 _ Atari shall reasonably assist any manufacturer advanced by Licensee to become a manufacturer, j shall inspect the manufacturing facilities prior to approval. Please allow 60 days for the approval process. 

4 Jaguar compatibility assurance holograms (see next section) must be affixed to the front of the point of | sale box. 

; 7 ; 

{ 

a 3. Holograms will be delivered generally within 3 working days. Po 4, Publisher will be billed for holograms and royalty at time of shipping, to be paid in accordance j & with the terms of your License Agreement. 

## | | V) COMPATIBILITY ASSURANCE HOLOGRAMS AND ROYALTY 

## se 

1. Jaguar Compatibility Assurance Holograms must be ordered from Atari via fax (1-408-7452088). Holograms are ordered on a by-title basis to track royalties via Atari-assigned serial numbers. 

2. Holograms are ordered in opening orders of a minimum 2000, reorders are in multiples of 1000. Holograms are 12 cents ($0.12) each. 

q 

© 1994 Atari Corp. 

Confidential Information FER Property ofAtari Corporation 

26 April, 1995 

Page 22 

Appendix F - Additional Documentation 

a 

(AdditionalDocumentation ——_—_ “a 

| 

2 ; = a 

| 

4 

: 

hy) 

The following additional documents are also included with the Jaguar Developer's Kit or are avauable separately: 

DB: The Atari Debugger 

; 

**==> picture [7 x 18] intentionally omitted <==**

**----- Start of picture text -----**<br>
a<br>**----- End of picture text -----**<br>


| 

26 April, 1995 

Confidential Information “7O® Property ofAtari Corporation 

© 1994 Atari Corp. 

