| QSound For Jaguar Page I ) QSound™ForTheAtariJaguar | QSound is a patented, innovative process for generating a sound field that is not bound to the playback | speakers. It requires only traditional stereo playback equipment for reproduction, and provides enhanced audio imaging capabilities with startling contrasts. 

**==> picture [505 x 201] intentionally omitted <==**

**----- Start of picture text -----**<br>
| Using the QSound process, sound sources can be placed in "virtual space": an arc approximately +90<br>| degrees in front of the listener, well outside the speakers. The QSound pan positions which map this<br>| space are numbered0 (far left) to 32 (far right).<br>Left Speaker [*] J wento® Right Speaker<br>0 Yi JME<br>**----- End of picture text -----**<br>


For game developers, QSound provides a rich environment for audio interfacing. For example, enemy fire can be heard in QSpace before the enemy appears on the screen; missiles launched off an F-16 jet fighter can be heard to drop off the wing tip before they race off into the distance; when you drive or fly past an explosion, it can appear to move beyond the player; background music can be given extra ambiance and depth. 

## | UsigeScindFordaguar 

There are two ways of using QSound for Atari Jaguar games: 

1. For sounds which can be preprocessed and require no dynamic control of position, the QSystem H or QCreator program can be used!. The QSystem II is a sophisticated hardware & software post production mixing system which results in stereo output. QCreator is a software-only tool which runs under Microsoft Windows and allows developers to QSound process mono sound samples in AIFF, RIFF, and raw sample formats. The result is a stereo sample which will include the QSound effect when played. 

j 

Sounds processed with QCreator can be played at runtime with no further processing required. However, because the samples are 16-bit stereo they will take up more room than using 16-bit mono 

- 1 The QCreator program is available to Jaguar Developers from either QSound or Atari Jaguar Developer Support upon request. For more information about QCreator or to inquire about the Qsystem II, please contact QSound directly at the address given at the end of this section. 

- © 1995 QSound Labs Confidential PER. Information 25 April, April, 1995 

: 

25 April, April, 1995 

| Page 2 2 QSound For For Jaguar | samples processed processed at runtime. runtime. Note also that using lossy sound compression also that using lossy sound compression that using lossy sound compression using lossy sound compression lossy sound compression sound compression compression techniques on QSound on QSound QSound | processed files will probably result in the QSound effect being altered or lost completely. files will probably result in the QSound effect being altered or lost completely. will probably result in the QSound effect being altered or lost completely. probably result in the QSound effect being altered or lost completely. result in the QSound effect being altered or lost completely. in the QSound effect being altered or lost completely. the QSound effect being altered or lost completely. QSound effect being altered or lost completely. effect being altered or lost completely. being altered or lost completely. altered or lost completely. or lost completely. lost completely. completely. { Because they require no additional processing at runtime, pre-processed samples can be used in | conjunction with the Jaguar Synth & & Music driver. 

Page 2 2 QSound For For Jaguar 5 samples processed processed at runtime. runtime. Note also that using lossy sound compression also that using lossy sound compression that using lossy sound compression using lossy sound compression lossy sound compression sound compression compression techniques on QSound on QSound QSound Ig processed files will probably result in the QSound effect being altered or lost completely. files will probably result in the QSound effect being altered or lost completely. will probably result in the QSound effect being altered or lost completely. probably result in the QSound effect being altered or lost completely. result in the QSound effect being altered or lost completely. in the QSound effect being altered or lost completely. the QSound effect being altered or lost completely. QSound effect being altered or lost completely. effect being altered or lost completely. being altered or lost completely. altered or lost completely. or lost completely. lost completely. completely. yy q Because they require no additional processing at runtime, pre-processed samples can be used in 4 conjunction with the Jaguar Synth & & Music driver. 2. For sounds which which are to be panned dynamically at runtime, The QSound Q1 Q1 module has been been implemented on on the Jaguar Jaguar DSP. The Q1 Q1 module takes 16-bit monophonic monophonic sound samples and and 4 creates 16-bit stereo output with the sounds positioned in 3D 3D space using the QSound effect. ti Because the QSound module module must be running in the Jaguar DSP Jaguar DSP DSP to process the samples at runtime, ’ your ability to otherwise use the DSP DSP at thesame thesamesame time is limited. For example, the Jaguar Jaguar Synth & & . Music Driver cannot be used at the same time. - One advantage to using the Q1 module instead of pre-processed sounds is that the files will take up half as much room because you have mono samples instead of stereo. And although the sample eS program doesn't do it, lossy compression techniques can be used to further reduce the storage | } requirements. Or you could even use plain 8-bit mono samples as your starting point and expand 4 them to 16-bit before passing them to the Qi module. a It's entirely possible to use both options in the same program. For your title screen and option screens | you might have some preprocessed QSound effects built into samples that are played as part of a music ' score being done by the Jaguar Synth & Music Driver. Then during your game play, you could have the (iim QSound Q1 module loaded so that you could dynamically position your sound effects in 3D space. 4 Regardless of which options you choose, the starting point must be a monoponic sound sample. This : can be created or edited using whatever digital sound sampler & editor you choose. This can be & something like the utilities that come with many PC sound cards, or something more sophisticated. The i main requirement is that you must be able to create files in either the RAW format that you would link = in with your Jaguar program or files loadable by the QCreator program. 4 The implementation implementation of the dynamic Q1 module on the Atari Jaguar system can be viewed as a black box the dynamic Q1 module on the Atari Jaguar system can be viewed as a black box dynamic Q1 module on the Atari Jaguar system can be viewed as a black box Q1 module on the Atari Jaguar system can be viewed as a black box module on the Atari Jaguar system can be viewed as a black box on the Atari Jaguar system can be viewed as a black box the Atari Jaguar system can be viewed as a black box Jaguar system can be viewed as a black box system can be viewed as a black box be viewed as a black box viewed as a black box as a black box a black box black box box 4 with a single entry point: the QSound QSound function running in the DSP. DSP. The QSound module can QSound module can module can can processup @ to eight independently panned mono mono voices. Regardless of the number of inputs, the number of inputs, number of inputs, of inputs, inputs, the output is alwaysa alwaysa q stereo stream, which may may be mixed with mixed with with other stereo data before before it is played back through played back through back through through the I2S : 7 interface. , 4 — Note: There is no internal volume scaling of the input samples within the QSound module. It is the 4 responsibility of the caller to do the required volume scaling of voices to ensure that overflow doesnot my occur. . a. The QSound process QSound process process is dependent on the sampling dependent on the sampling on the sampling the sampling sampling rate. The current implementation current implementation implementation is for the for the default ; sampling rate of the DSP, of the DSP, the DSP, DSP, which is a shade under 22050 Hz a shade under 22050 Hz shade under 22050 Hz 22050 Hz Hz (SCLK set to #19). #19). If you you are running running at <a 25 April, 1995 1995 Confidential FOR Information FOR Information Information © 1995 QSound Labs 1995 QSound Labs Zi 

| | 

| 

: : 

| | 

2. For sounds which which are to be panned dynamically at runtime, The QSound Q1 Q1 module has been been implemented on on the Jaguar Jaguar DSP. The Q1 Q1 module takes 16-bit monophonic monophonic sound samples and and creates 16-bit stereo output with the sounds positioned in 3D 3D space using the QSound effect. Because the QSound module module must be running in the Jaguar DSP Jaguar DSP DSP to process the samples at runtime, your ability to otherwise use the DSP DSP at thesame thesamesame time is limited. For example, the Jaguar Jaguar Synth & & Music Driver cannot be used at the same time. - 

| The implementation implementation of the dynamic Q1 module on the Atari Jaguar system can be viewed as a black box the dynamic Q1 module on the Atari Jaguar system can be viewed as a black box dynamic Q1 module on the Atari Jaguar system can be viewed as a black box Q1 module on the Atari Jaguar system can be viewed as a black box module on the Atari Jaguar system can be viewed as a black box on the Atari Jaguar system can be viewed as a black box the Atari Jaguar system can be viewed as a black box Jaguar system can be viewed as a black box system can be viewed as a black box be viewed as a black box viewed as a black box as a black box a black box black box box with a single entry point: the QSound QSound function running in the DSP. DSP. The QSound module can QSound module can module can can processup to eight independently panned mono mono voices. Regardless of the number of inputs, the number of inputs, number of inputs, of inputs, inputs, the output is alwaysa alwaysa stereo stream, which may may be mixed with mixed with with other stereo data before before it is played back through played back through back through through the I2S interface. 

The QSound process QSound process process is dependent on the sampling dependent on the sampling on the sampling the sampling sampling rate. The current implementation current implementation implementation is for the for the default | sampling rate of the DSP, of the DSP, the DSP, DSP, which is a shade under 22050 Hz a shade under 22050 Hz shade under 22050 Hz 22050 Hz Hz (SCLK set to #19). #19). If you you are running running at 25 April, 1995 1995 Confidential FOR Information FOR Information Information © 1995 QSound Labs 1995 QSound Labs 

Page 3 

|| 

| 

QSound For Jaguar any other sample rate, then please contact QSound Labs and we will provide an appropriately adjusted y module for your desired sample rate. 

**==> picture [2 x 24] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


**==> picture [422 x 181] intentionally omitted <==**

**----- Start of picture text -----**<br>
mono input 0<br>pan position 0<br>. | QSound |<br>mono input 7<br>pan position 7 right<br>left ep<br>Other stereo data Stereo output to DAC<br>right an<br>**----- End of picture text -----**<br>


| 

Descriptions of the routine follows. For further information or technical help, please contact Buzz Burrowes at QSound. The file QSOUND.OT is a linkable object module containing the QSound routines. This file must be linked with your program, and at runtime, the routines must be loaded into Jaguar’s DSP. It has a single entry point which is documented below. See the documentation on the sample program for more information. The QSound module is designed to be completely position-independent. You can load it anywhere in | DSP memory where room is available. Usually, it follows with other DSP code supplied by you which | feeds samples to the QSound module. See the demo program for an example. 

**==> picture [513 x 207] intentionally omitted <==**

**----- Start of picture text -----**<br>
Summary: The QSound function is called every sample period in which at least one QSound voice is<br>active. Typically this means once per sample (typically 22050 times per second).<br>j ,<br>Input: 116 = return address<br>| 117 = number of QSound voices to process (1 to 8)<br>| r18 = Pointer to an array of structures which define the input sample and pan position for<br>each voice. The structures look like this:<br>| struct QSound_Voice /* Values use only low 16 bits of LONG */<br>; long sample; /* Sample to be processed */<br>long pan_position; /* values from 0 (left) to 32 (right) */<br>}120 = left channel of stereo output (32 bits) ready to be fed to Jaguar's I2S interface<br>122 = right channel of stereo output (32 bits) ready to be fed to Jaguar's 12S interface<br>© 1995 QSound Labs Confidential “78% Information 25 April, 1995<br>**----- End of picture text -----**<br>


25 April, 1995 

7 QSound For Jaguar ; i wilt q 

| | | | i | ; ' | 1 | ; | : 

Page 4 

i ; § | 5 ' 3 3 4 a a : P| P| = = py iq . 4 = | § a = j ‘ 4 ' , 4 : : 4 | ; | | a ] q Si 

i : | 

| 

Register Usage: | uses 112 through 127 [Notes: —_—*| Rlequires/uses about (140 + (27 * num_voices)) instructions. 

iCi‘<Cé.OCOwOCOOCUL 

Se,,,h”r”rt~—“.LULUCi‘iCtwtCrsiis 

; copy 16 bit inputs to #samples 

|; copy|16 bit|inputs to #samples|inputs to #samples|inputs to #samples|
|---|---|---|---|---|
|After:|load<br>movei<br>movei<br>jump<br>nop<br>move<br>shrgq<br>shrq<br>wee|QSound ptr,rs<br>; Get stored address where we put QSound module<br>#after,rl6é<br>; return address for QSound<br>#1,r17<br>; mumber of voices<br>T,(r5)<br>; call QSound module<br>#toQSound,rl18<br>; ri8 -> input samples/pan pairs<br>#16,r20<br>; outputs in 16 bits for I2S Interface<br>#16,xr22<br>; store results for processing at next I2S interrupt|||
|toQSound:<br>-ds.l||1|; <br>;|up to 8 consecutive 2*32 bit locations<br> voice<br>O0 sample|
||-ds.i|1|;|pan position for voice 0|
||-ds.1<br>-ds.l<br>.ds.l|1<br>1<br>1|;<br>; <br>;|voice<br>1<br>sample<br> pan position for voice 1<br> voice 2 sample|
||-ds.l|1|;|pan position for voice 2|
||-ds.l|i|:|voice<br>3 sample|
||.ds.l|1|;|pan position for voice 3|
|-|.ds.l|1|;|voice 4 sample|
||-ds.l|1|;|pan position for voice 4|
||-ds.l<br>-ds.l|1<br>1|; <br>;|voice 5 sample<br> pan position for vcice 5|
||-ds.l<br>-ds.1|1<br>1|; <br>;|voice 6 sample<br> pan position for voice 6|
||-ds.l<br>-ds.1.|1<br>1|; <br>;|voice 7 sample<br>panpositionforvoice7|



## HowToContactQSoundlabs = #=#=#=§= =. .......... | 

QSound Labs Inc. Tel: (403) 291-2492 2748 - 37 Ave NE. Fax: (403) 250-1521 Calgary, AB, Canada 

**==> picture [2 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
2<br>**----- End of picture text -----**<br>


25 April, 1995 

Confidential FOR Information 

© 1995 QSound Labs 

Page 5 

| QSound For Jaguar Buzz Burrowes |r QSound2521 Ripley Labs,AvenueInc. F Redondo Beach, CA 90278 

Tel: (310) 374-8017 Fax: (310) 374-0998 

CO —De eee ) | QSound technology is protected by patent and copyright laws. Its use on the Atari Jaguar system is restricted to, and subject to, the licensing agreement signed with Atari. | All third parties interested in using QSound in Jaguar applications should check with Atari regarding | this licensing agreement. 

**==> picture [529 x 499] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|
|QbEMOvasoindDemoProgram|
|}|The QDEMO program demonstrates how to use the QSound module to play back different samples and|
|||position them in 3D-space in real-time.|You use the joypad to control the location of the sounds in 3D-|
|}|space.|
|Below is|a|list of all the files which make up the QSound demo program.|In order to reduce the size of|
|||the archive containing the demo, the executable program|itself is not provided; the project must be built|
|||using the tools in your Jaguar developer’s kit.|
|:|Filename|DescriptionSound file used by the program (the helicopter).|This is a raw 16-bit mono sound sample|
|q|file (sample rate about 20khz).|Included at link stage by using|-ii option of ALN.|;|
|||This|is the code module for the demo program where things happen.|This copies the|
|ee:|reads the joystick and cooks the values for the QSPanner routine.|
|||
|with MAKE|utility to build executable program|file from source code and data files.|
|‘|OTERO S| Ts|th|MARE uty|to bul|executable|program|le rom|source|code|and|eta hos|
|file used by the program|(the explosion).|This|is a aw 16-bit mono sound sample|
|file (sample rate about 20khz).|Included at link stage by using -ii option of ALN.|
|4|sno|| Sunset|af|cag|wrm|on A|
|F.q ERO][TPHASER.SND|neeSound file used by the program|(theeae gunshot).|This isigen a raw 46-bit mono ae sound sample file|
|linker include file specifying names|of files to be linked|into demo program.|
|-ESERTOTNK|[SIN|interrate nt about 20khz). Includedfle specting|names at link offs stage to e|b|y usingInked -t|i|optionno deme of|ALN.rogran. ————|
|j|This file takes contro! after the startup code has initialized the system.|It creates an object|||
|routine|in|DEMO.S.|
|«= _|Tilist for|the ba|c|kgrounde picture,|installssme an ar obie|c|t listtrnmnme refresh routine, evo and then calls the|||
|MADMAC Source code file containing DSP|interrupt routines and demo program's interface|
|7|SOUNDING —t WRONGto QSound function.ince tt|cortaning|dadeaton|of ebels|GSOUND|GT mode —_}|
|5 a BSD-format object module containing @Sound|routines.|Linked with demo|
|program or with your own program to provide the QSound capabilities.|E|
|sonoOT|| Meee etinclud|e|file containing declarationsSe|ee cee of labels Saracens in QSOUND.OT module|
|||This file is actually in the WAGUAR\SOURCE|directory.|This is the screen displayed by the|
|4|startup code that is used by several of the sample programs in the Jaguar Developer's Kit.|
|p|©1995 QSound Labs|Confidential FER|Information|25|April,|1995|

**----- End of picture text -----**<br>


25 April, 1995 

| Filename Description : 1 | STARTUP.S Standard Jaguar Startup Code. This module contains all the code necessary to properly i | «q initialize the Jaguar hardware and display a simple startup picture. Then it passes control to the _ start label in the QDEMO.S module. (See the Sample Programs section for further 1 information on the Standard Jaguar Startup Code.) VALOGO16.PIC | Binary image of picture to be displayed by demo program. This is a raw image file : containing no header. The image itself is 320 pixels wide by 200 pixels tall, 16-bit Jaguar : RGB format. included at link stage by using -ii option of ALN. | VIDSTUFF.INC | MADMAC include file containing miscellaneous equates used by the demo program's object 3 j list setup 1 Below is a more in-depth description of some of the main files from this demo program. . Sahlrrrrtr——~<Ssrsi‘=iri‘“OSsCsCtrsiCrazCVrizszti;SséstswCsKSdisHhrlCULe This file is where the program execution begins. This is the standard Jaguar Startup Code responsible 4 for initializing the system. It sets up interrupts, sets the video registers correctly for either NTSC or q PAL, and does other related things that must be done properly at startup time for your program to a function. It also displays a startup screen. Once it is finished, it passes control to the _start label a somewhere in your program (QDEMO:S in this example). s Note that STARTUP.S has been modified slightly from the version in JAGUAR\STARTUP to allow _ the use of a different startup picture. This type of change is only one allowed in this file. Making 4 q changes to other portions of the file may result in errors which can prevent your program from ; ' functioning properly. 4 | This file is where the program execution begins after the startup code has initialized the system. It 4 basically delays for a few seconds so that we can look at the startup screen, then it creates an object list a : for our background picture, installs an interrupt handler to refresh the object list, and then sets the video mode to 320-pixel RGB mode. Finally, it clears the memory that will be used for our bitmap, and then 4 jumps into the gdemo function, located in DEMO.S. dl Note that the object list creation routine make_list is almost identical to the routine JnitLister in the STARTUP.S module. The only parts that changed were the labels for the address where the list a information is stored. : a This file contains a number of program-specific equates that describe the video and object list 4 q requirements of the program. (Such as the memory location to be used by the bitmap object we are | 4 using in our object list.) This is used by QDEMO:S. Mi 25 April, 1995 Confidential FAR Information © 1995 QSound Labs 

**==> picture [2 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
)<br>**----- End of picture text -----**<br>


Page 7 

QSound For Jaguar 

| 

$e (©1995 QSound Labs 

This file contains the readpad routine that we use to read the joypad controller. The joypad data is only : read by this routine, not interpreted. The readpad routine outputs one variable which describes the current joypad reading and another that indicates what’s changed on the joypad since the last time we read it (buttons being pressed or released, etc.). 

This file is essentially the same as the one used by the 3DDEMO sample program. 

j LLL LLLLL ; This is the main program-specific part of the source code. The gdemo routine starts off by blitting our | picture from ROM into RAM so that it can be displayed (displaying bitmaps directly from ROM is a big | waste of bus bandwidth). | Next it starts the main helicopter sound, and then jumps into a loop where it reads the joypad values (by calling the readpad function), and calls the interpad function. : The interpad function is responsible for interpreting the joypad values and taking the appropriate action: jt sets the pan positions of the sounds, and starts a gunshot and explosion sound if the *B’ bution is 

& pressed. LAL LLL LAL This file contains source code for the Jaguar DSP. The OSWrapper function enables the Jaguar 12S } interrupt, which is acting as the sample rate timer for our sound samples. Then it calls the QWave ; function. ; j The QWave function reads data from the sound samples being played, figures out the current pan positions, and then feeds this information to the QSound routine in the QSOUND.OT module, which then processes it. When an 12S interrupt occurs (about 22050 times per second), the processed samples } are output to the I2S interface so we can hear the wonderful 3-D sound effects that QSound is capable of producing. 1 Also contained in this file is the source for the DSP interrupt routines. In many other DSP applications, : } —_the 12S interrupt would grab the current set of samples and feed them to the I2S interface (i.e. play the ‘ ] sound). But because QSound has to pre-process each set of samples, we do thingsa little differently. ge OThe 12S interrupt simply sets a semaphore that the main QWave function uses as a flag to indicate that ge Owe are ready to hand one set of samples off to the 12S interface (i.e. play the sound). As soon as this iS ; | done, it sends another set of samples off to the QSound function to be processed. 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


Confidential “7% Information 

25 April, 1995 

| | | i | | i ] | | | 

Page 8 co 

QSound ForJaguar 

: 

rrts—C—CW;sCOUSCOU.CiC(CNOi®COW”CNCt;CiC.®'SCSCtéCC.CUGMn.S 

f 7 program. It pixel. i ‘ the ALN ALN a | OC the PHASER and and ; this won’t won’t = with QSound QSound i) ( , | a » ; © 1995 QSound Labs | 

This file contains declarations for the QSOUND.OT module (so you can figure out the length of the code before you copy it into the DSP). See DEMO.S for an example 

This is a raw binary file containing the picture which we display on screen during the demo program. It is an RGB picture with dimensions of 320 pixels wide, 200 pixels high, and 16 bits per pixel. It is included and assigned a starting label and an ending label by using the -ii function of the ALN ALN linker. 

## WOGSNDSCOPTERSND-&PHASERSND 

These files contain the three raw mono 16-bit samples that will be played and passed through the QSound module. Note that the order these are specified in the link is important, as the PHASER and and MIX3 sounds are sometimes played together as a single sound. If they aren’t consecutive, this won’t won’t work correctly. You may wish to substitute your own 16-bit mono sample files in crder to see the results with QSound QSound on the Jaguar. These files are included and each assigned labels by using the -ii function of the ALN linker. 

| 25 April, 1995 

Confidential FOR Information 

