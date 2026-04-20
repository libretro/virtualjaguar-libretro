# *Development System 

| | | 

| 

The information in this documentation ts © 1994 Atari Corporation, All Rights Reserved except where otherwise noted. “y This Documentis ConfidentialInformation and the Property of Atari Corporation 

: | 

e 

) 

**==> picture [488 x 71] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>a@| Jaguar Developer Documentation<br>ee Table ofContents —<br>**----- End of picture text -----**<br>


## SS 

Introduction To The Atari Jaguar Development System . 

## Contacts At Atari 

Phone & Fax Numbers, Electronic Mail Addresses, General Mailing/Shipping Address 

## Online Support 

Who To Contact For What? 

## Setup & Installation 

Ifyou have problems 

Installation 

Configuation 

Running Your First Program 

How to Run A Cartridge In A Development System . a Overview of Jaguar Hardware & Architecture 

The Jaguar Development System 

A Sample Debugging Session , 

A Simple Sample Program a 

Jaguar and Memory 

Jaguar Video & Clock Speeds 

The Jaguar Blitter 

The Jaguar Development System ROMulator 

Jaguar Controller Support 

## Table of Contents 

Introduction 

Jaguar Video and Object Processor 

Object Processor Performance 

Memory Map 

- Object Definitions 

Description of Object Processor/Pixel Path 

O1994AunCopSSSNovember, 1994 

November, 1994 1994 

| | o 

## Jaguar Developer Documentation _ «Fable ofContents 

Color Mapping The CRY Color Scheme 

Graphics Processor Subsystem Memory Map 

**==> picture [1 x 3] intentionally omitted <==**

**----- Start of picture text -----**<br>
,<br>**----- End of picture text -----**<br>


## Graphics Processor 

Programming The Graphics Processor 

) 

Design Philosophy 

Pipe-Lining 

Memory Interface 

Arithmetic Functions 

Interrupts Program Flow Control Register File Blitter Programming The Blitter Address Generation DataBus InterfacePath Register Description . Address Registers Control Registers Data Registers Modes of Operation 

Jerry ‘ 

Frequency Dividers - Programmable Timers | Interrupts Pulse Width Modulation DACs . Synchronous Serial Interface Asynchronous Serial Interface 4 Joystick Interface , a General Purpose I/O Decodes a DSP Al Programming The DSP ’ ‘ Design Philosophy i11 November, 1994 

, 

**==> picture [26 x 153] intentionally omitted <==**

**----- Start of picture text -----**<br>
i<br>|<br>0)4;<br>]<br>'<br>**----- End of picture text -----**<br>


**==> picture [7 x 172] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>|<br>:<br>**----- End of picture text -----**<br>


**==> picture [5 x 25] intentionally omitted <==**

**----- Start of picture text -----**<br>
r<br>**----- End of picture text -----**<br>


ii 

© 1994 Atari Corp. 

**==> picture [20 x 34] intentionally omitted <==**

**----- Start of picture text -----**<br>
ai)<br>**----- End of picture text -----**<br>


**==> picture [426 x 63] intentionally omitted <==**

**----- Start of picture text -----**<br>
§ Jaguar Developer Documentation<br>- Table ofContents _<br>**----- End of picture text -----**<br>


**==> picture [53 x 29] intentionally omitted <==**

**----- Start of picture text -----**<br>
a<br>**----- End of picture text -----**<br>


Pipe-Lining 

Memory Map 

Arithmetic Functions Interrupts Program Flow Control Circular Buffer Management 

Register File 

## Appendices 

GPU & DSP Instruction Set 

, 

Writing Fast GPU & DSP Programs 

Data Organization - Big and Little Endian 

**==> picture [23 x 29] intentionally omitted <==**

**----- Start of picture text -----**<br>
 ]<br>YS<br>**----- End of picture text -----**<br>


## iTechnical Reference 

Jaguar Console Hardware Release Notes General Guidelines for Cartridges 

**==> picture [2 x 17] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


Specific Bits in Production Series Consoles 

Memory Map & Register List 

System Setup Registers 

GPU Registers 

| 

Blitter Registers 

Jerry Registers 

Joystick Registers 

DSP Registers 

| 

Jaguar Console Peripheral Specifications Video Ports 

RF And Composite 

Video Timings 

Video Connector 

DSP Port 

Multi-Console Games 

| & : a | 

Jaguar Network Jaguar Modem 

## Cartridge/Expansion Port 

## a 

SintAuailopo 

S—™”””SSCSCSCSE November, 1994 

; | } i 

| i | 4 

I 

j : S | 

## Jaguar Developer Documentation Table of Contents 

Controllers And Controller Ports Signals And Pinouts . Register Addressing Addressing - Digital Digital Inputs 

Register Addressing Addressing - Digital Digital Inputs 

**==> picture [1 x 27] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


Device Addressing 

ReadingA Jaguar Controller 

Standard Jaguar Controller Matrix 

4 Player Adapter 

6D Controller 

. 

Head-Mounted Trackers 

Rotary “Tempest” Controller Analog “Stick” and “Driving” Controllers Reading Bank Switching Controllers 

Audio Subsystem 

Cartridges & NVRAM 

GPU/DSP Bugs & Warnings 

Blitter Bugs & Warnings Object Processor Bugs & Warnings Miscellaneous Bugs & Warnings 

Jaguar CD-ROM Emulator Setup Step By Step Setup 

| 

The Jaguar CD-ROM _A Bit About CD-ROMs Some Defiitions Jaguar CD-ROM BIOS ; : Calling The CD-ROM BIOS : Function Reference , Jaguar CD-ROM Authoring Tool With Emulator be Creating[A][New][ Document] 7 Opening An Existing Document 7 Description ofthe Authoring Window a _. Current Item In The Window 

: | 

**==> picture [26 x 89] intentionally omitted <==**

**----- Start of picture text -----**<br>
'<br>i<br>) 4 :<br>: =<br>j 2<br>**----- End of picture text -----**<br>


| 

a @ 

r) SS 

**==> picture [459 x 241] intentionally omitted <==**

**----- Start of picture text -----**<br>
Jaguar Developer Documentation<br>Peo Table of Contents |<br>Saving A Document .<br>EditingACD-ROM Document<br>InsertingA Session<br>InsertingA Track |<br>Inserting A File<br>Editing A Filename<br>Adding Comments<br>Cut/Copy/Paste/Delete<br>Undo |<br>**----- End of picture text -----**<br>


**==> picture [8 x 33] intentionally omitted <==**

**----- Start of picture text -----**<br>
—<br>**----- End of picture text -----**<br>


Goto Session 

Goto Track 

Find/Find Next Preferences - Specifying Lead-In/Lead-Out For Sessions & Tracks 

Preferences - Specifying SCSI ID Preferences - How To Set The SCSI Identifier 

Preferences - CD-ROM Latency 

Emulating The CDROM 

Stopping The Emulation 

Restrictions On The Emulation 

Important Notes On Using The CD-ROM Emulator 

Log File Name | Preload Buffers 

CD-ROM Emulator Q&A The Jaguar CD-ROM: Programming, Procedures, and Guidelines 

The Jaguar Voice Modem Introduction 

Modem Interface 

Data Communications & Bandwidth 

Control Flow 

Call Hang Up 

Answer Sequence 

## Parsing The Received Data 

Call Waiting © 1994 Atari Corp. 

v 

11 November, 1994 

I, in 

## Jaguar Developer Documentation Table of Contents 

Comment Reference For Voice Plus Data Initiate-Report Software Reset Change Host Baud Rate to 19200 Set Data Packet Size Dial Number / Transmit DTMF Tone Poll DTMF Detector Report Handshake Status Set Voice Volume Set Voice Sampling Frequency Send Real Time Data Report Dial Tone Detector 

Unsolicited Reponse Reference Receive Real Time Data Packet Error Status Call Waiting Detected Line Lost 

**==> picture [7 x 20] intentionally omitted <==**

**----- Start of picture text -----**<br>
f<br>**----- End of picture text -----**<br>


Fanngn 

- #1 - Minimum Object List Update 

- #2 - Moving A Bitmap With The Object Processor #3 - Clipping A Bitmap Object With The Object Processor #4 - Scaling A Bitmap Object With The Object Processor #6 - GPU GPU Interrupt Object Processing Object Processing Processing #12 - Rotating A Bitmap A Bitmap Bitmap With The The Blitter 

| #6 - GPU GPU Interrupt Object Processing Object Processing Processing #12 - Rotating A Bitmap A Bitmap Bitmap With The The Blitter i Jaguar Mandlebrot/Fractal Demo i JagLine, JagSlant, JagBlock, JagSkew, JagShade i Joypad Reading Example Analog Joystick Example : EEPROM Example RGB True Color Bitmap Display Example Simple DSP Waveform Output 

**==> picture [7 x 20] intentionally omitted <==**

**----- Start of picture text -----**<br>
(<br>**----- End of picture text -----**<br>


Blitter Demo 

**==> picture [141 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
we<br>**----- End of picture text -----**<br>


“— liNovember,1994. 

©4994 Atari Corp. 

| Yd ) 

## |g” JaguarJaguar DeveloperDeveloper DocumentationDocumentatio pe Table of Contents 

Jaguar JPEG Decompression Example Jaguar Synth Demo 3D Rendering & Texture Mapping Demo 

3D Graphics 3DS2JAG Object/Texture Conversion Utility 

Transformation & Display Routines 

**==> picture [2 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


3D Demo program 

Jaguar JPEG Using The Compression Utilities 

Anatomy of a JAGPEG Image 

Subsampling 

Let's Compress Some Images DEJAG Decompression Routines 

To Use DEJAG 

Preparing DEHUFF.DAT With Locate 

TESTJPG Sample Program 

Excerpt From TEST.S 

Cinepak Video Decompression & Playback Networking 

Music 

The Jaguar Synth 

Jaguar Sound Tool User Guide 

The Jaguar Music Driver 

Parse Utility 

, 

Merge Utility SNDCOMP Utility 

Processing a MIDI File For the Atari Jaguar Introduction 

&- 

About The Jaguar Music System Terminology Procedure Summary 

Step by Step Procedure 

. 

More About Voicing Samples 

## Bio Aud Cope 

CE verb, 1994 

' 

os 

## Jaguar Developer Documentation pe Table ofContents 

Looping MIDI Files 

Example Files 

Using QSound for Jaguar 

The QSOUND.OT Module 

How To Contact QSound Labs 

QDEMO - The QSound Demo Program 

Introduction 

Cinepak Decompressor 68000 Module 

GPU Module Flags 

**==> picture [5 x 21] intentionally omitted <==**

**----- Start of picture text -----**<br>
(<br>**----- End of picture text -----**<br>


Auxiliary Data 

Jaguar Film Format Smooth Format 

## Chunky Format 

## Layout of CD-ROM 

Sample Playback Code 

Modules Supplied 

Memory Map 

Key Parameters 

) Key Variables } Utilities | Audio Playback ': Interrupt Handling Buffer Management : Frame Rate Control Code Walkthough Error Trapping 

## Jaguar Cinepak Utilities 

| 

. 

**==> picture [7 x 24] intentionally omitted <==**

**----- Start of picture text -----**<br>
(<br>**----- End of picture text -----**<br>


Movie To Film 

Converts a standard Quicktime movie to Jaguar Film Format viti 

11 November, 1994 

© 1994 Atari Corp. 

| L @ 

: | & 

## lal Jaguar Developer Documentation Py Tableof Contents 

**==> picture [1 x 31] intentionally omitted <==**

**----- Start of picture text -----**<br>
_<br>**----- End of picture text -----**<br>


## RGB-To-CRY 

Converts a Jaguar Film from RGB to CRY format 

Smooth To Chunky 

Converts a Jaguar Film from Smooth Format to Chunky Format 

FILM To AIFF 

Converts a Jaguar Film File into an AIFF File 

## Sample Jaguar Films 

References Trademark & Copyright Notice 

eeEE | [ (The main documentation for some tools is provided in separate sections) Madmac Macro Assembler Commandline Options Summary ofNew Assembly Directives Notes On Assembly Directives Miscellaneous Notes 

ALN Linker 

Commandline Options 

DB (WDB/RGBJAG) Debugger Debugger Messages | Commandline Options | GASM & LTXCONV | Utilities The AR68 program creates object module archive library files that can be used with the ALN linker. | AR68 Archive Utility | DUMP Utility | SIZE Utility | The SIZE utility analyzes an executable program an executable program executable program program file or object module or object module object module module file and and prints information information 

The SIZE utility analyzes an executable program an executable program executable program program file or object module or object module object module module file and and prints information information about the sizes and load addresses of the various program segments, and optionally a list of the symbols defined within the file. 

FILEFIX Utility Breaks down an executable program file into separate files for the TEXT, DATA, and symbol table segments, and outputs a script file to load them into the Alpine Board. 

## STRIP Utility 

Removes symbols from an executable program file 

| @104AunCop. 

a S~S”””SSSCSdi Nove ber, 1994 

## me Jaguar Developer Documentation 

FGREP Utility Fast General Regular Expression Parser. This program will search text files for a specified string pattern and tell you which files match or not. LS Utility , This is a UNIX-style list-files utility which has some options the standard ‘DIR' command does not. MAKE Utility This is a utility used to build your program files from your source code files by compiling only those files which have been changed since they were previously compiled. GULAM Shell The GULAM shell is a UNIX C-Shell clone for the Atari computer, which normaily has no standard - commandline shell. 3DS2JAG Utility The 3DS2JAG Utility converts AutoCAD 3D Studio objects into a format that can be used with the 3D Graphics libraries. (See the Libraries chapter.) PARSE Utility The PARSE utility converts standard MIDI files to work with the Jaguar Music Driver. (See the Libraries chapter.) SNDCOMP Uiility The SNDCOMP utility compresses digital sound samples. (See the Libraries chapter.) EY Appendices 7 as | Frequently Asked Questions About Jaguar About the Developer Package About Problems With the Development Software or System About Documentation Clarification H About Programming About Documentation Bugs & Additions ' About Hardware Features ti : Atari-Based Development System Information s Describes the difference between an Atari-based development system and a PC-based development system. ' Jaguar Development Standards Jaguar Software Experience Approved Manufacturer Production Guidelines Compatibility Coding And Content Verification Gift Box Content Descriptor! Manufacturing _ 1 Subject to Industry Rating System Proposal 11 November, 1994 x © 1994 Atari Corp 1994 Atari Corp Atari Corp Corp 

© 1994 Atari Corp 1994 Atari Corp Atari Corp Corp 

1 Jaguar Developer D ocumentation ri =——“—i:SFablee ofContents 

: 

/- 

Compatibility Assurance Holograms And Royalty Additional Documentation 

, 

Introduction 

The Command Line Command Line Options Using Madmac Interactive Mode Things You Should Be Aware Of Forward Branches Text File Format 

Statements 

Equates Symbols and Scope 

| 

Keywords Constants 

Strings Register Lists Expressions Types 

Unary Operators 

Binary Operators 

Special Forms 

Example Expressions 

Directives Notes On Assembly Directives 

## Macros 

## Parameter Substitution 

Macro Invocation 

Example Macros Repeat Blocks 

## 68000 Mode Addressing Modes 

| 

Branches © 1994 Atari Corp. 

xi 

11 November, 1994 

| 

i \ ah: a ‘ | 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


## Jaguar Developer Documentation - —-«* Table ofContents 

## Linker Constants OptimizatioA **n** ds Translations 

Jaguar GPU/DSP Mode 

Condition Codes 

Optimizations and Translations 

6502 Support Object Code Format 

## Error Messages 

When Things Go Wrong 

Warnings 

Fatal Errors 

Errors 

Introduction The Command Line Command Line Options 

( 

Using ALN Filenames And The Library Path 

Absolute Linking 

File Symbols 

File Formats 

## Alcyon Format Files 

## Alcyon Format Object Modules 

Alcyon (GEMDOS) Format Relocatable Executable Program Files Alcyon (GEMDOS) Relocation Information Alcyon-Format Absolute Object Modules (Jaguar Executable Program) Alcyon Format Archive Libraries Alcyon Symbol Format ) File Formats Formats BSD-Format Object Modules ; COFF-Format Absolute Executable Program Files ( 

## BSDICOFF File Formats Formats 

> DOINDEX- Archives and their Indices 

Duplicate Symbols In Modules 

Unused Modules In Libraries TiNovember,x.1996... ©1994 Atari Corp. 

© Jaguar Developer Documentation ; | Table of Contents 

j Error Messages 

1 

4a F ['See4 (This sectionsto the contains addendum thein main the Tools documentation section) for the DB Debugger (AKA “RDBJAG” and “WDB’). ld j DB: The Atari Debugger 

Expressions, Ranges, And Strings 

The Client, Breakpoints, and Checkpoints: An Overview 

Commands 

The Client, Breakpoints, and Checkpoints: Detail 

- | Symbols And Debugger Variables : Procedures, IF, GOTO, DEFER, and ALIAS 

Operating System Considerations 

1 

Remote Debugging 

Introduction 

{ 

Command Line 

Source Line Format 

. 

Name Spaces 

Identifers 

| 

: | 

Registers Labels 

Integer Constants 

Floating Point Constants Strings Expressions Addressing Modes 

| 

Error Reporting 

Instruction Optimization Code Safety Checks 

> Relocation and Linking ~ Macros 

Assembler Directives 

Ori Auad Cope 

—~S~S*di November, 1994 

‘ 

( 

| 

- Jaguar Developer Documentation so Table ofContents _ 

Fi November 1994 0 

”””—~™”—~™”S~S*C« 994 Atari Corp 

