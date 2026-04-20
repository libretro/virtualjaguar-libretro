| 1 | i i | ' j | : ‘ |] 1 | | q 1 | : | : ] : i 1 

Page I E Jaguar CD-ROM ian The Atari CD is a low cost, high capacity data storage device capable of storing 746.9 megabytes of H data. The Atari drive is double speed (=353 kb/sec.). The uncorrectable error rate is less than 1ini0O . All errors are flagged by the system so damaged blocks may be re-read. | There are a few differences between the Jaguar CD and other systems that you may be familiar with. E These fall into two areas: performance and arrangement. - | The Jaguar CD subsystem is high performance. For example, a MPC (Multimedia PC) has a minimum | performance requirement that states that, “The drive must be capable of maintaining a sustained transfer | rate of 150 kb/sec, without consuming more than 40% of the CPU bandwidth in the process.” This data | rate is half that of the Atari CD and the Jaguar will sustain the full 352800 bytes/sec. rate. This high + performance level is achievable because of Jaguar's very large bus bandwidth. j All data on the disc is accessed directly, not via a file system with a directory structure. The data is | arranged in a “raw” format compliant with Red Book except that Jaguar discs may be multi-session | (defined by the Orange Book standard). There is a table of contents on the disc which may have up to 99 entries each referencing a single track (for more information about CDs, see the section below titled A _ Bit About CD-ROMs). P’ Data on the disc is referenced via the time stamp of the data. Time stamps assume single speed play and | start at the beginning of the disc. The minimum addressable data unit on the disc is a frame. Each frame | js 588 longs (2352 bytes). There are 75 frames per second at single speed. Any position on the disc is | accessible via a time stamp of the format mm:ss:ff (mm = minutes; ss = seconds; ff= frames). Reading data from a CD is an inexact process. When a command is sent to the CD to request data | starting at a particular time code, the mechanism cannot guarantee that the data being sent is coming | from the exact location requested. It is important to recognize that the data that is written into memory } will not start at the exact beginning of the requested frame. In order to guarantee that the data you want | will be contained in the data read we suggest that you start reading six frames before the first block you | actually want and search for your partition marker’ in memory for 31 frames (72,912 bytes) from this | point. Please note that while this amount is sufficient for most ‘gold’ discs, we have found that some | writer software induces additional skew which may need to be compensated for by additional preseeking. Manufactured discs are guaranteed to be well within the tolerances given. It should be noted that the data from the CD maintains long alignment only. This means that graphics data cannot be guaranteed to have a particular phrase alignment. This phrase alignment must be i accounted for in your code, or else the data needs to be moved. | In order to allow for changes in CD vendors and changes in data transfer mechanism, it is essential that ") all access to the CD and its associated controls be via the CD BIOS. The BIOS is meant to be as 

1 A partition marker is a 64 byte block of data consisting of 16 repetitions of the same longword. Partition markers are covered in more detail in the section Jaguar CD-ROM Programming Procedures and Guidelines. © 1995 Atari Corp. Confidential Information PER Property ofAtari Corporation 16 May, 1995 

: Page 2 2 Jaguar | unobtrusive as possible. A detailed description of the BIOS can be found as possible. A detailed description of the BIOS can be found possible. A detailed description of the BIOS can be found A detailed description of the BIOS can be found detailed description of the BIOS can be found description of the BIOS can be found of the BIOS can be found the BIOS can be found BIOS can be found can be found be found in the section The the section The section The The Jaguar CD- CDFundamentally, CDs are a constant CDs are a constant are a constant a constant constant linear velocity (CLV), velocity (CLV), (CLV), single-data-track optical media with one data optical media with one data media with one data with one data one data data ' surface. The single data track is in the form form of a a spiral about a mile long. Absolute position information ! is contained contained in a time time code recorded within the data. The time code can be resolved time code can be resolved code can be resolved can be resolved be resolved resolved to a a single sector of of 4 2352 bytes, of which, all may be data, or 2048 2048 data bytes and the remainder remainder for an an additional layer of of 7 error correction. correction. Atari Jaguar CDs CDs are recorded in CD-DA “raw data” format, CD-DA “raw data” format, “raw data” format, data” format, format, with Motorola byte- byte| ordering, so there are 2352 bytes per sector, or block. block. The total capacity of a Jaguar CD a Jaguar CD Jaguar CD CD is 746.9 : megabytes. j The logical logical logical organization of a standard CD divides of a standard CD divides a standard CD divides standard CD divides CD divides divides of a standard CD divides a standard CD divides standard CD divides CD divides divides a standard CD divides standard CD divides CD divides divides standard CD divides CD divides divides CD divides divides divides the disc into four types of regions: disc into four types of regions: into four types of regions: four types of regions: of regions: regions: disc into four types of regions: into four types of regions: four types of regions: of regions: regions: into four types of regions: four types of regions: of regions: regions: four types of regions: of regions: regions: of regions: regions: regions: lead-in, tracks, pauses, and lead-out. The lead-in area is about 10000 sectors long, near the inner diameter of the CD. diameter of the CD. of the CD. the CD. CD. diameter of the CD. of the CD. the CD. CD. of the CD. the CD. CD. the CD. CD. CD. ; The Table of Contents (TOC) Table of Contents (TOC) of Contents (TOC) Contents (TOC) (TOC) Table of Contents (TOC) of Contents (TOC) Contents (TOC) (TOC) of Contents (TOC) Contents (TOC) (TOC) Contents (TOC) (TOC) (TOC) is repeated endlessly repeated endlessly endlessly repeated endlessly endlessly endlessly within the Q subcode Q subcode subcode Q subcode subcode subcode of this region. this region. region. this region. region. region. Following the ‘ lead-in is the the the first pause pause pause region, which must be which must be be which must be be be 150 or 225 or 225 225 or 225 225 225 sectors long. After the the the first pause comes pause comes comes pause comes comes comes the ' first track, which which which is a data data data region. If the CD the CD CD the CD CD CD has more than one one one track, every track must be be be separated by a 1 pause region of 2 or 3 2 or 3 or 3 3 2 or 3 or 3 3 or 3 3 3 seconds. After the the the last track comes comes comes the lead-out region which which which contains primary primary primary 

Page 2 2 Jaguar CD-ROM unobtrusive as possible. A detailed description of the BIOS can be found as possible. A detailed description of the BIOS can be found possible. A detailed description of the BIOS can be found A detailed description of the BIOS can be found detailed description of the BIOS can be found description of the BIOS can be found of the BIOS can be found the BIOS can be found BIOS can be found can be found be found in the section The the section The section The The Jaguar CD- CD5 Fundamentally, CDs are a constant CDs are a constant are a constant a constant constant linear velocity (CLV), velocity (CLV), (CLV), single-data-track optical media with one data optical media with one data media with one data with one data one data data { ’ surface. The single data track is in the form form of a a spiral about a mile long. Absolute position information 4 is contained contained in a time time code recorded within the data. The time code can be resolved time code can be resolved code can be resolved can be resolved be resolved resolved to a a single sector of of 7 2352 bytes, of which, all may be data, or 2048 2048 data bytes and the remainder remainder for an an additional layer of of 4 error correction. correction. Atari Jaguar CDs CDs are recorded in CD-DA “raw data” format, CD-DA “raw data” format, “raw data” format, data” format, format, with Motorola byte- byteordering, so there are 2352 bytes per sector, or block. block. The total capacity of a Jaguar CD a Jaguar CD Jaguar CD CD is 746.9 8 megabytes. | The logical logical logical organization of a standard CD divides of a standard CD divides a standard CD divides standard CD divides CD divides divides of a standard CD divides a standard CD divides standard CD divides CD divides divides a standard CD divides standard CD divides CD divides divides standard CD divides CD divides divides CD divides divides divides the disc into four types of regions: disc into four types of regions: into four types of regions: four types of regions: of regions: regions: disc into four types of regions: into four types of regions: four types of regions: of regions: regions: into four types of regions: four types of regions: of regions: regions: four types of regions: of regions: regions: of regions: regions: regions: lead-in, tracks, ' . pauses, and lead-out. The lead-in area is about 10000 sectors long, near the inner diameter of the CD. diameter of the CD. of the CD. the CD. CD. diameter of the CD. of the CD. the CD. CD. of the CD. the CD. CD. the CD. CD. CD. fa The Table of Contents (TOC) Table of Contents (TOC) of Contents (TOC) Contents (TOC) (TOC) Table of Contents (TOC) of Contents (TOC) Contents (TOC) (TOC) of Contents (TOC) Contents (TOC) (TOC) Contents (TOC) (TOC) (TOC) is repeated endlessly repeated endlessly endlessly repeated endlessly endlessly endlessly within the Q subcode Q subcode subcode Q subcode subcode subcode of this region. this region. region. this region. region. region. Following the s lead-in is the the the first pause pause pause region, which must be which must be be which must be be be 150 or 225 or 225 225 or 225 225 225 sectors long. After the the the first pause comes pause comes comes pause comes comes comes the | & first track, which which which is a data data data region. If the CD the CD CD the CD CD CD has more than one one one track, every track must be be be separated by a | = pause region of 2 or 3 2 or 3 or 3 3 2 or 3 or 3 3 or 3 3 3 seconds. After the the the last track comes comes comes the lead-out region which which which contains primary primary primary 4 data all set to zeros to zeros zeros and an an alternating P subcode P subcode subcode channel bit. q . Multi-session CDs appear logically as a set of up to 40 standard CDs arranged as sequential annular Ld rings on the disc. Independent of the number of sessions on the CD, the total number of tracks must vi always be 99 or less for the entire disc. In theory, each session could have up to 99 tracks, for a total of : : up to 3960 tracks, but this structure is not yet officially supported by Philips and Sony. The track | 2 number limitation is usually overcome with a “logical block-logical file” structure that is built in 1 ,. software on top of the physical track structure. 2 er..——C—CUCUCT ERE..——C—CUCUCT ERE ERE rc wrLDVc Absolute Time — The time codc Time — The time codc — The time codc The time codc time codc codc information in the Q Subcode Q Subcode Subcode that ranges continuously from continuously from from 00:00:00 { PS to a maximum maximum of 73:59:75, 73:59:75, beginning at the the start of the of the the first pause pause region on the disc. be Area or Region — Region — — A physical portion of the CD's the CD's CD's data carrying carrying surface that is 2D 2D ring-shaped like a @ flattened doughnut. doughnut. j : Channel Frame — The fundamental Frame — The fundamental — The fundamental The fundamental fundamental packet size of 588 bits that size of 588 bits that of 588 bits that 588 bits that bits that that is transmitted transmitted on the high-frequency the high-frequency high-frequency : signal sent by by the laser playback head’s output playback head’s output head’s output amplifier. The packet contains 24 bytes packet contains 24 bytes contains 24 bytes 24 bytes bytes of primary data primary data data oe and 1 byte of secondary data of secondary data data (1 bit each, P through each, P through P through through W subcodes) as well well as all of the overhead of the overhead the overhead overhead data bits a required to form form the packet. packet. Po | : theFinalizelead-in —that The includesprocessFinalizelead-in —that The includesprocesslead-in —that The includesprocess —that The includesprocessthat The includesprocess The includesprocess includesprocessprocess theof making main TOCaof making main TOCa making main TOCa main TOCa TOCaa recordableat theat the the inner diameter. CD CD readable An by An by by unfinalizedstandard CD CD players willstandard CD CD players will CD CD players will CD players will players will will generallyinvolves writing beinvolves writing be writing be be Ve . unplayable, except on CD ROM on CD ROM CD ROM ROM players specifically designed for this situation, such as Jaguar Jaguar and Photo CD CD players. | ‘16May,1995 ‘Confidential Information Information FP™ Property of Atari Corporation ©1995 AtariCorp. | 

' | | 

The logical logical logical organization of a standard CD divides of a standard CD divides a standard CD divides standard CD divides CD divides divides of a standard CD divides a standard CD divides standard CD divides CD divides divides a standard CD divides standard CD divides CD divides divides standard CD divides CD divides divides CD divides divides divides the disc into four types of regions: disc into four types of regions: into four types of regions: four types of regions: of regions: regions: disc into four types of regions: into four types of regions: four types of regions: of regions: regions: into four types of regions: four types of regions: of regions: regions: four types of regions: of regions: regions: of regions: regions: regions: lead-in, tracks, pauses, and lead-out. The lead-in area is about 10000 sectors long, near the inner diameter of the CD. diameter of the CD. of the CD. the CD. CD. diameter of the CD. of the CD. the CD. CD. of the CD. the CD. CD. the CD. CD. CD. The Table of Contents (TOC) Table of Contents (TOC) of Contents (TOC) Contents (TOC) (TOC) Table of Contents (TOC) of Contents (TOC) Contents (TOC) (TOC) of Contents (TOC) Contents (TOC) (TOC) Contents (TOC) (TOC) (TOC) is repeated endlessly repeated endlessly endlessly repeated endlessly endlessly endlessly within the Q subcode Q subcode subcode Q subcode subcode subcode of this region. this region. region. this region. region. region. Following the lead-in is the the the first pause pause pause region, which must be which must be be which must be be be 150 or 225 or 225 225 or 225 225 225 sectors long. After the the the first pause comes pause comes comes pause comes comes comes the first track, which which which is a data data data region. If the CD the CD CD the CD CD CD has more than one one one track, every track must be be be separated by a pause region of 2 or 3 2 or 3 or 3 3 2 or 3 or 3 3 or 3 3 3 seconds. After the the the last track comes comes comes the lead-out region which which which contains primary primary primary data all set to zeros to zeros zeros and an an alternating P subcode P subcode subcode channel bit. 

er..——C—CUCUCT ERE..——C—CUCUCT ERE ERE rc 1 Absolute Time — The time codc Time — The time codc — The time codc The time codc time codc codc information in the Q Subcode Q Subcode Subcode that ranges continuously from continuously from from 00:00:00 4 to a maximum maximum of 73:59:75, 73:59:75, beginning at the the start of the of the the first pause pause region on the disc. 1 Area or Region — Region — — A physical portion of the CD's the CD's CD's data carrying carrying surface that is 2D 2D ring-shaped like a 4 flattened doughnut. doughnut. Channel Frame — The fundamental Frame — The fundamental — The fundamental The fundamental fundamental packet size of 588 bits that size of 588 bits that of 588 bits that 588 bits that bits that that is transmitted transmitted on the high-frequency the high-frequency high-frequency ' signal sent by by the laser playback head’s output playback head’s output head’s output amplifier. The packet contains 24 bytes packet contains 24 bytes contains 24 bytes 24 bytes bytes of primary data primary data data 1 and 1 byte of secondary data of secondary data data (1 bit each, P through each, P through P through through W subcodes) as well well as all of the overhead of the overhead the overhead overhead data bits ' required to form form the packet. packet. || theFinalizelead-in —that The includesprocessFinalizelead-in —that The includesprocesslead-in —that The includesprocess —that The includesprocessthat The includesprocess The includesprocess includesprocessprocess theof making main TOCaof making main TOCa making main TOCa main TOCa TOCaa recordableat theat the the inner diameter. CD CD readable An by An by by unfinalizedstandard CD CD players willstandard CD CD players will CD CD players will CD players will players will will generallyinvolves writing beinvolves writing be writing be be unplayable, except on CD ROM on CD ROM CD ROM ROM players specifically designed for this situation, such as Jaguar Jaguar and ( Photo CD CD players. 

‘16May,1995 ‘Confidential Information Information FP™ Property of Atari Corporation 

| Jaguar CD-ROM 

Page 3 

r Index — A pointer in the track that is currently playing. This sometimes used for accessing specific } parts of tracks, independently of time code. | Lead-in — The region of the CD near the inner diameter that contains the table of contents, usually[as][“TOC”.] |[abbreviated] 

Mode — The type of track (audio, ROM, CD+G, Karaoke, CDI, etc.) that is presently being read. Open/Closed Session — The process of making a session valid after recording data in it on a recordable CD involves writing a lead-in and lead-out for it, called “closing” it. While the session is open, data can be appended to the session. An open session can not be accessed by Jaguar's CD Module. Pause —A region of the disc that must contain only digital zeros of primary data while the P Subcode in the secondary data channel is set to all ones. Some software refers to this as “Track Lead-in.” 

## Program — The main data region, or regions of a CD. 

Relative Time — The time code information in the Q Subcode that ranges continuously from 00:00:00 

Sector or Block — The smallest addressable unit of primary data storage, 2352 bytes, that can be read from the disc without post-processing of the data. 

Session — A session is an area of a CD that has at least one complete set of region types. i.e. lead-in, pause, track (the data), and lead-out. A standard audio CD has a single session, usually with multiple tracks and pauses between the lead-in and lead-out. There can be as many as 99 sessions on a single multi-session CD (in fact only about 40 sessions will fit on a disc). 

Subcode Data Channel — The serial secondary data read from the disc at 1/192 of the rate of the primary data, both of which are combined within the main channel. There are 8 subcodes within the secondary channel, identified as P through W. The Q Subcode contains the position information of the primary data channel sectors. The position information is in a time-based format of : 

## minutes:seconds:frames 

Subcode Frame — The subcode channel information extracted from one sector of the CD. The subcode frame rate is 75 per second at 1X speed playback and 150 per second at 2X speed playback. 

Table of Contents — The directory of the CD read from the Q subcode channel. Each program on the disc is listed according to its position on the disc. There can be as many as 99 items in the TOC. Special information items about the disc and its manufacturer can also be found here. Track Number — The number of a program (audio selection for example) on the CD. 

| 1 : / | | | ] | i i y | 4 : q 4 | ' ; i | | | 1 I i 

© 1995 Atari Corp. Confidential Information PO® Property of Atari Corporation 

16 May, 1995 

Page 5 : | | you ! a | , | | / q so A7 A7 q | | q : |a q' ; certain j of q may be be z | for and and q should BIOS | | 

| ; - 2.3. TheWhatcall's registersuse. are used for input. | 4.5. WhatWhat registersregisters areare used used changedfor byoutput. the cail. registers areare used used changedfor byoutput. the cail. are used used changedfor byoutput. the cail. for byoutput. the cail. output. the cail. 

| Jaguar CD-ROM FgaguarCDROMBIOS: | The Jaguar CD BIOS provides hardware transparent access to the Jaguar CD subsystem. ITIS | REQUIRED THAT ALL ACCESS TO THE CD BE THROUGH THE BIOS. The BIOS gives you control over all major aspects of the CD system. The BIOS allows single or double speed operation, a choice of data paths into the system, a data transfer function and other features. For more information on the CD subsystem, see section 1 and the sample source code CD_SAMP.S and CD_ASAMP:S. | CC ccrummmmmmammmmmmmmmmmane. ccatng he eR ROM BIOS! 9 | To call the CD-ROM BIOS, you load the proper values into the appropriate registers, then do a 68000 | jsr CD_routine call for the CD-ROM BIOS routine you want to call. The addresses of the routines are | defined in the CD.INC include file. Each CD BIOS call may require up to 64 bytes of stack space so A7 A7 | should be configured properly prior to calling any CD BIOS routine. | The CD-ROM BIOS is installed automatically in a retail Jaguar CD-ROM system. In a development | CD-ROM system, however, you must manually load the CD-ROM BIOS into DRAM. A debugger script (CDBIOS??.DB)’ is provided for this purpose. ~~ The following is a list of the CD BIOS calls. Each block gives: 1, The name of the call (and what version it is available in). 

- 4.5. WhatWhat registersregisters areare used used changedfor byoutput. the cail. 

| ——ore ee The CD.INC file defines an error variabie named err_flag, which will receive an error code from certain | CD BIOS routines. A value of zero indicates no error; non-zero values indicate an error. The contents of err_flag are valid only after a CD BIOS function which is documented as setting it. However, it may be be changed by other CD BIOS functions. Proper error checking is mandatory when using the Jaguar CD-ROM. Failure to properly check for and and | handle error conditions may prevent your product from obtaining final production approval. You should always check err_flag after those CD BIOS calls that set it. Additionally, your program should have some kind of timeout mechanism to prevent the situation where it endlessly waits for a CD BIOS call to return (which could happen if other errors have not been properly handled). 

2 Different versions of the CD BIOS may be distinquished by the last two digits of the filename. For example, CDBIOS43.DB would be a DB script that would load version 4.3 of the CD BIOS. © 1995 Atari Corp. Confidential Information “JER Property ofAtari Corporation 

‘ 7 

| 

15 June, 1995 

| Page 6 Jaguar CD-ROM | 23 DebuggingwiththeCO-ROMBIOS =#=§...sa j Two versions, revisions 2.x and 4.x, of the CD-ROM BIOS are currently distributed by Atari Jaguar | Developer Support. If you have revision 1.0, you should download the two newer versions from Compuserve or the Atari Software Development BBS. Developer CD systems with the Butch 1 chip can i only use revision two of the BIOS. Butch 2 systems can support either (you have a Butch 2 system if | your CD system is in a modified production-level case). it | When debugging a CD title you should format your data on a CD-R disc or the emulator as specified in F section 6. The CD-BIOS must be soft-loaded prior to making any CD-BIOS call using the command ] ‘load cdbiosxx.db’ where ‘xx’ is the version number of the BIOS you want to load3. 

q eh | @ - || |4 oo j . 2 a q Bo p 1 a : | = be . _ 3 | . : i ; a | ‘ 4 : - _ _ r | @ 

‘ 1 1 } \ i | 1 j 

: ; 

| 1 ] j : | ' | 

4 

To debug, you will need a copy of the disc’s table of contents. To create a copy, load the CD-BIOS and execute a short 68000 program such as the following: 

**==> picture [149 x 216] intentionally omitted <==**

**----- Start of picture text -----**<br>
- include “jaguar.inc”<br>-include "“cd.inc”<br>68000<br>.text<br>move.1 #$70007,D_END<br>jsr CD_setup<br>move .w #0,da0<br>jsr CD_mode<br>lea $2C00,A0<br>jsxr CD_getoc<br>illegal<br>-end<br>**----- End of picture text -----**<br>


This program sets up the CD hardware, cails CD_getoc to read the table of contents at $2C00 and then ends on an illegal instruction. Now you can use the debugger command ‘write toc.dat 2C00{[400]’ to store the TOC to disc. This step needs to be performed each time the data on the disc changes. 

Now, you can create a simple debugger script such as: 

load cdbios40.db read toc.dat 2c00 aread bootcode.cof 

This will load the CD BIOS rev 4.0 , the Table of Contents, and your bootcode to the correct location so you can begin debugging. Your bootcode program should be the same (and at the same location) as you 

- 3 Depending on your system setup, it may be necessary to switch to the directory containing the CD-ROM BIOS files, typically JAGUAR\CDROM, prior to Joading the debugger and issuing this command. 

- 15 June, 1995 1995 Confidential Information PR Property ofAtari Corporation © 1995 Atari Corp. 

15 June, 1995 1995 

4 

| Jaguar CD-ROM Page 7 | rd will have the CD Boot ROM load your code. This bootcode must be <64k and is responsible for the loading of other code/data segments. 

{ | | 4 { { | { ( | i ' q : | | | | 

; B 

| j q : j : i 

You should never place a CD_getoc call in your main code as the CD Boot ROM will load the table of contents on a booting disc at $2C00 automatically. ga Reading Data with the CD-ROM BIOS Data is normally read from a CD by calling one of three forms of CD_init (CD_init, CD_initf, and CD_initm) followed by any number of CD_read calls. With the current hardware, each form of CD_init loads a piece of GPU interrupt code which handles interrupts redirected from Jerry’s 1S interrupt. This may change as new versions of the CD hardware are produced. 

Warning! The CD-BIOS GPU code does not distinguish between which interrupts actually came from Jerry and which came from other sources. For this reason, you should never enable other interrupts in the JINTCTRL register when a handler from any version of CD_init is active, otherwise they wili be mistaken for interrupts from the CD interface. . Following is a brief description of the variants of CD_init: if ; CD_init ~ Average speed, does not automatically locate data‘, uses no (non-interrupt) registers. CD_initf — Fastest read, does not automatically locate data, uses more registers. CD _initm — Slowest read, locates data, supports circular buffers, uses no (non-interrupt) registers. When reading data at double-speed these interrupts occur approximately every 90 psecs. Due to interrupt overhead the required maximum latency is reduced to = 54 psecs. If the Object Processor is used extensively, this number may be reduced. This means that no processor that has priority over the GPU must take control of the bus for longer than this period of time. Specifically, 68000 vertical-blank handlers are a likely cause of problems. Preferably, use the GPU for object-list update, etc... or, if you must, use only a tiny handler in the 68k. 

If you do not wish to use the GPU for CD reading you can also use the DSP. To do this, you must install a DSP I’S interrupt handler, call CD_jert appropriately, and set SMODE to $14 (SMODE is set to the default of $15 by the Boot ROM and should be restored when done). This method eliminates the need for any form of CD_init. When a CD_read call is executed your handler can now extract data from the CD. CD data transfers using the DSP are, however, subject to infrequent unreported data errors. Data whose integrity is required to be perfect should be checksummed. 

To play Red Book audio you need a very simple interrupt handler that reads the incoming data from the CD and outputs it to the DACs (see the file INOUT.DAS in \JAGUAR\CDROM) for an example. You 4 The CD_init and CD_initf routines do not guarantee that a data read will begin exactly at a specified time code. We recommend that CD reading begin six blocks ahead of where data is needed and that your buffer is searched for 31 blocks worth of memory. The CD_initm routine does, however, automatically search for data tagged by partition markers and locates the data in memory automatically. © 1995 Atari Corp. Confidential Information “JER. Property ofAtari Corporation 15 June, 1995 

15 June, 1995 

| Page 8 Jaguar CD-ROM CD-ROM | can then call CD_read with the “Just Seek” bit Seek” bit bit set and the timecode of your and the timecode of your the timecode of your timecode of your of your track. Audio will be played Audio will be played will be played be played played by your interrupt handler but no data will be stored by any installed version of CD_init. CD_init. | 25CommandAcknowledge = tt C*@“ 4 Several CD BIOS functions give you the option of waiting for an acknowlege that the command the command command 1 completed or returning immediately. The only only restriction to the “return immediately” mode immediately” mode mode is that that a CD_ack must be used prior to any subsequent CD BIOS command. subsequent CD BIOS command. CD BIOS command. BIOS command. command. With the CD_read commandin CD_read commandin commandin seek : mode, this delayed acknowledge is implied by implied by by the command command so you must alsodoaCD_ack priortoany alsodoaCD_ack priortoany priortoany } CD BIOS command that follows. This structure gives gives you the flexibilty to perform other calculationsor do other processing while a command command takes place. | 2.6 Error Recovery Procedure for CD Read Operations, i To retry a CD read operation that fails (ie. CD_pér returns returns an error result) while running in double: speed mode, the following steps should be performed: should be performed: be performed: performed: 1. Switch to Single-Speed Single-Speed using CD_mode. CD_mode. { 2. Switch to Double-Speed using CD_mode. CD_mode. | 3. Reexecute the CD_read. CD_read. This should make error recovery reliable under under ali circumstances where circumstances where where it is actually actually possible (i.e. the | ; disk isn't actually damaged or defective). { | oe,rrrti‘CeOCOCtr~COwzsCNCNCC.CUCiéCdCNCizssC.tirizCisiONisCONCNOCO_iéCUG,rrrti‘CeOCOCtr~COwzsCNCNCC.CUCiéCdCNCizssC.tirizCisiONisCONCNOCO_iéCUG j ee8484 Error code code in global err_flag: 0 indicates no error, error, error, non-zero indicates error , j |PurposePurpose =| If any any call uses the the the “return immediately” option, CD_ack may be used to wait for the may be used to wait for the be used to wait for the to wait for the wait for the for the the may be used to wait for the be used to wait for the to wait for the wait for the for the the be used to wait for the to wait for the wait for the for the the to wait for the wait for the for the the wait for the for the the for the the the | **|** | requested action to complete. action to complete. complete. action to complete. complete. complete. Note: Any call that does not “return immediately” uses this Any call that does not “return immediately” uses this call that does not “return immediately” uses this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this Any call that does not “return immediately” uses this call that does not “return immediately” uses this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this call that does not “return immediately” uses this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this “return immediately” uses this immediately” uses this uses this this immediately” uses this uses this this uses this this this | call to wait for completion. to wait for completion. wait for completion. for completion. completion. to wait for completion. wait for completion. for completion. completion. wait for completion. for completion. completion. for completion. completion. completion. This means that err_fiag is set. j Se r—~—“ i™OC:iC:SCS:i‘CCNONONONC®COWO®CONO®NOCOCONOCiiész.CimCGTCCNONONONC®COWO®CONO®NOCOCONOCiiész.CimCGTCCNONONONC®COWO®CONO®NOCOCONOCiiész.CimCGT j Note: This call should never be used by a bootable CD-ROM. call should never be used by a bootable CD-ROM. should never be used by a bootable CD-ROM. never be used by a bootable CD-ROM. used by a bootable CD-ROM. by a bootable CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. used by a bootable CD-ROM. by a bootable CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. by a bootable CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. bootable CD-ROM. CD-ROM. CD-ROM. It isfor debugging purposes only. isfor debugging purposes only.for debugging purposes only. debugging purposes only. purposes only. only. isfor debugging purposes only.for debugging purposes only. debugging purposes only. purposes only. only.for debugging purposes only. debugging purposes only. purposes only. only. debugging purposes only. purposes only. only. purposes only. only. only. | | AO.L The address address of 1024 byte buffer for returned 1024 byte buffer for returned byte buffer for returned buffer for returned for returned returned multi-session TOC TOC | : I5June, 1995 Confidential Information AR Property ofAtari Corporation ofAtari CorporationAtari Corporation Corporation © 1995 1995 Atari Corp. Corp. 4 

Jaguar CD-ROM CD-ROM fh the “Just Seek” bit Seek” bit bit set and the timecode of your and the timecode of your the timecode of your timecode of your of your track. Audio will be played Audio will be played will be played be played played ’ . but no data will be stored by any installed version of CD_init. CD_init. CommandAcknowledge = tt C*@“ functions give you the option of waiting for an acknowlege give you the option of waiting for an acknowlege you the option of waiting for an acknowlege the option of waiting for an acknowlege option of waiting for an acknowlege of waiting for an acknowlege waiting for an acknowlege for an acknowlege an acknowlege acknowlege that the command the command command : immediately. The only only restriction to the “return immediately” mode immediately” mode mode is that that a ? to any subsequent CD BIOS command. subsequent CD BIOS command. CD BIOS command. BIOS command. command. With the CD_read commandin CD_read commandin commandin seek @% is implied by implied by by the command command so you must alsodoaCD_ack priortoany alsodoaCD_ack priortoany priortoany follows. This structure gives gives you the flexibilty to perform other calculationsor a command command takes place. | Procedure for CD Read Operations, i dR that fails (ie. CD_pér returns returns an error result) while running in double3 steps should be performed: should be performed: be performed: performed: = to Single-Speed Single-Speed using CD_mode. CD_mode. { | Double-Speed using CD_mode. CD_mode. | & the CD_read. CD_read. o recovery reliable under under ali circumstances where circumstances where where it is actually actually possible (i.e. the | = or defective). { ; ,rrrti‘CeOCOCtr~COwzsCNCNCC.CUCiéCdCNCizssC.tirizCisiONisCONCNOCO_iéCUG : code in global err_flag: 0 indicates no error, error, error, non-zero indicates error , o call uses the the the “return immediately” option, CD_ack may be used to wait for the may be used to wait for the be used to wait for the to wait for the wait for the for the the may be used to wait for the be used to wait for the to wait for the wait for the for the the be used to wait for the to wait for the wait for the for the the to wait for the wait for the for the the wait for the for the the for the the the — requested action to complete. action to complete. complete. action to complete. complete. complete. Note: Any call that does not “return immediately” uses this Any call that does not “return immediately” uses this call that does not “return immediately” uses this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this Any call that does not “return immediately” uses this call that does not “return immediately” uses this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this call that does not “return immediately” uses this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this “return immediately” uses this immediately” uses this uses this this immediately” uses this uses this this uses this this this | ize call to wait for completion. to wait for completion. wait for completion. for completion. completion. to wait for completion. wait for completion. for completion. completion. wait for completion. for completion. completion. for completion. completion. completion. This means that err_fiag is set. Poe r—~—“ i™OC:iC:SCS:i‘CCNONONONC®COWO®CONO®NOCOCONOCiiész.CimCGTCCNONONONC®COWO®CONO®NOCOCONOCiiész.CimCGTCCNONONONC®COWO®CONO®NOCOCONOCiiész.CimCGT never be used by a bootable CD-ROM. used by a bootable CD-ROM. by a bootable CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. used by a bootable CD-ROM. by a bootable CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. by a bootable CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. bootable CD-ROM. CD-ROM. CD-ROM. It isfor debugging purposes only. isfor debugging purposes only.for debugging purposes only. debugging purposes only. purposes only. only. isfor debugging purposes only.for debugging purposes only. debugging purposes only. purposes only. only.for debugging purposes only. debugging purposes only. purposes only. only. debugging purposes only. purposes only. only. purposes only. only. only. | The address address of 1024 byte buffer for returned 1024 byte buffer for returned byte buffer for returned buffer for returned for returned returned multi-session TOC TOC | Confidential Information AR Property ofAtari Corporation ofAtari CorporationAtari Corporation Corporation © 1995 1995 Atari Corp. Corp. 4 

oe,rrrti‘CeOCOCtr~COwzsCNCNCC.CUCiéCdCNCizssC.tirizCisiONisCONCNOCO_iéCUG,rrrti‘CeOCOCtr~COwzsCNCNCC.CUCiéCdCNCizssC.tirizCisiONisCONCNOCO_iéCUG : 

ee8484 Error code code in global err_flag: 0 indicates no error, error, error, non-zero indicates error , |PurposePurpose =| If any any call uses the the the “return immediately” option, CD_ack may be used to wait for the may be used to wait for the be used to wait for the to wait for the wait for the for the the may be used to wait for the be used to wait for the to wait for the wait for the for the the be used to wait for the to wait for the wait for the for the the to wait for the wait for the for the the wait for the for the the for the the the — **|** | requested action to complete. action to complete. complete. action to complete. complete. complete. Note: Any call that does not “return immediately” uses this Any call that does not “return immediately” uses this call that does not “return immediately” uses this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this Any call that does not “return immediately” uses this call that does not “return immediately” uses this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this call that does not “return immediately” uses this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this that does not “return immediately” uses this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this does not “return immediately” uses this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this not “return immediately” uses this “return immediately” uses this immediately” uses this uses this this “return immediately” uses this immediately” uses this uses this this immediately” uses this uses this this uses this this this | call to wait for completion. to wait for completion. wait for completion. for completion. completion. to wait for completion. wait for completion. for completion. completion. wait for completion. for completion. completion. for completion. completion. completion. This means that err_fiag is set. Poe Se r—~—“ i™OC:iC:SCS:i‘CCNONONONC®COWO®CONO®NOCOCONOCiiész.CimCGTCCNONONONC®COWO®CONO®NOCOCONOCiiész.CimCGTCCNONONONC®COWO®CONO®NOCOCONOCiiész.CimCGT Note: This call should never be used by a bootable CD-ROM. call should never be used by a bootable CD-ROM. should never be used by a bootable CD-ROM. never be used by a bootable CD-ROM. used by a bootable CD-ROM. by a bootable CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. used by a bootable CD-ROM. by a bootable CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. by a bootable CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. a bootable CD-ROM. bootable CD-ROM. CD-ROM. bootable CD-ROM. CD-ROM. CD-ROM. It isfor debugging purposes only. isfor debugging purposes only.for debugging purposes only. debugging purposes only. purposes only. only. isfor debugging purposes only.for debugging purposes only. debugging purposes only. purposes only. only.for debugging purposes only. debugging purposes only. purposes only. only. debugging purposes only. purposes only. only. purposes only. only. only. | 

i Page 9 | | | | | disc | this i for in i 

‘ ! : : i] q 

**==> picture [515 x 286] intentionally omitted <==**

**----- Start of picture text -----**<br>
t+ Jaguar CD-ROM<br>& ve Returns TOC data in buffer located in DRAM at the location pointed to by AO.L.<br>=.—lrCC +3 - Maximum track number.<br>a.LLrLrLrC*C +4 - Total number of sessions.<br>t -_lrC 45+6 - - Start Start of of last last lead-out lead-out time, time, absolute minutes. absolute seconds.<br>Cf +7 - Start of last lead-out time, absolute frames.<br>| £2| Format for the track records that follow:<br>ee +1 - Absolute minutes (0..99), start of track.<br>SCs 42 - Absolute seconds (0..59), start of track.<br>CC +3 - Absolute frames, (0..74), start of track.<br>i +7 - Track duration frames.<br>Purmose = The Fetumned buffer will contain 8-byte records, one for each track found on the CD in<br>| track/time order. The very first record (corresponding to the “Oth” track) has overall disc<br>| information.<br>**----- End of picture text -----**<br>


**==> picture [532 x 328] intentionally omitted <==**

**----- Start of picture text -----**<br>
esAOL The address of a long aligned block of GPU RAM 224 bytes long.<br>Purpose =| This call loads support code into the GPU to support CD_read. At the present time this<br>~~. only registers R28 to R31 in Bank #0 (which are the same as those normally reserved for<br> - interrupts to be processed and this primary process must define the interrupt stack in<br>Hesphies Cn CO nim<br>Burmese This call is a version of CD_init that is about 30% faster but uses more registers. This call<br>loads support code into the GPU to support CD_read. the Peso time this uses the<br>| tor GPu interrupts to be processed and this primary process must define the interrupt<br>v | stack in R31.<br>**----- End of picture text -----**<br>


; 15 June, 1995 

© 1995 Atari Corp. 

Confidential Information “PPR Property ofAtari Corporation 

| | po 

(“es | Ss ’ | 2 &. 

Jie 

: 

. 

: 

/ 

a 

| 

## 275° CDiniim CDHIOSRevsoup) 

**==> picture [502 x 31] intentionally omitted <==**

**----- Start of picture text -----**<br>
AO.L. The address of a long aligned block of GPU RAM 336 bytes long.<br>fRegisterUsage [A100<br>**----- End of picture text -----**<br>


**==> picture [491 x 42] intentionally omitted <==**

**----- Start of picture text -----**<br>
oe and circular buffers. At the present time this uses the DSP interrupt in the GPU. The ISR<br>| the same as those normally reserved for interrupts). Note that there must be a primary<br>**----- End of picture text -----**<br>


## eerrrtrsr——..LCi‘<‘‘OCOCOUONiNiC«CVCCCNédsCCiaCrOiéCSCGR 

Purpose ——_[ This call alows CD data to flow to the 'S port on Jerry. This allows audio datato go into | 

pat == [DoW Speed/mode desired: 

FRetume ©——_[ Error code in err_flag. : [essed either audio or data. Note: When in audio mode, the CD mechanism may alter data or 

## apa | DOW O= Retum immediately. 

This call mutes the CD. It functions only in audio mode. 

**==> picture [2 x 15] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


15 June, 1995 

Confidential Information “7O® Property ofAtari Corporation 

©1995 Atari Corp. 

| 

Page 11 

{ | 1 | | q q q | / ir | | 1 

| | | | : j 

i 

**==> picture [267 x 63] intentionally omitted <==**

**----- Start of picture text -----**<br>
@ Jaguar CD-ROM<br>a pom<br>Cee Dow Oversample by 2°(00).<br>**----- End of picture text -----**<br>


**==> picture [527 x 271] intentionally omitted <==**

**----- Start of picture text -----**<br>
4 | No return value in any registers.<br>: SCs Note: This call will only perform the functions that the mechanism can actually do. !f the<br>Bf | =____| mechanism cannot perform the oversampling requested it will do the next best that it can.<br>. oversample factor. Whatever software is handling Jerry had better be able to handle it.<br>eo  ,,<br>PREETI] BOW O= Return immediate.<br>to [No return value in any registers<br>. Pumese | This call pauses the CD. When in data mode, data will still be sent but it will not be<br>' _ sensible. When in pause mode, the CD will not advance along the disc. This means that,<br>1 when in pause mode, a CD_read call will fill the buffer with nonsense.<br>: CD_upaus<br>**----- End of picture text -----**<br>


## er tisids.CCC 

Register Usage | FRetems [AOL Address of last written data ._ A1.L Approximate address of most recenterror. —_Purpose “This call returns the address of the last longword of memory that was written to. If no data . hes been read, this value will be one longword prior to the start of the read buffer (often a | the position of the last detected read error since the start of the last CD_read command. Aico =| Section 2.6, Error Recovery Procedure for CD Read Operations 

**==> picture [6 x 33] intentionally omitted <==**

**----- Start of picture text -----**<br>
i<br>**----- End of picture text -----**<br>


© 1995 AtariCorp. 

Confidential Information FER Property ofAtari Corporation 

15 June, 1995 

F JaguarCD-ROM @& : 

: | 

4 

| @ a =. a 1 | **8** | 4 p 4 _ , S& _ . POR _ 

| a : | | ‘ | ] | 1 j : ; j | | 

4 pe 1 | q q : ] } q 

## Page 12 

## ee 

rrs—“i‘ONONOC‘i'OCiriséSC®dszaCNiaCCNOON”CisCCtisCsisCCCziCéstizstsrstsL«C‘<Céi‘s’RCNWCT#C 

**==> picture [493 x 306] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|
|put]|AOL|Beginning|of destination|data|buffer.|
|The|remaining|bits|are:|mm:ss:ff|(mm|=|minutes,|ss|=|seconds,|ff =|frames).|
|ee|aligned on a 2“*'|boundary.|The minimum|functional|size for|‘N’|is|3.|If the circular|
|a|pointer exceeds the value|in A1|even|if a circular buffer|is|defined.|(CD_initm|
|pe]|No|return value|in any|registers|
||Purpose:=|..]|This|call transfers|data from the CD,|starting|at a given time code. The manner|in which|
|FEos |1|transfout th|e|rred, positionbut theof the next CD|will addresscontinue to to be advanc writt|e|n to.at theIf thecurrent “Justsp S|ee|d.k” bitA CD_ack is set, no may data is|
|Peed|follow only|if the|“Just Seek”|bit|is|set.|
|Seeatss|=——[|CD_uread|
|||Section|2.6,|Error Recovery Procedure|for CD Read|Operations|

**----- End of picture text -----**<br>


**==> picture [374 x 250] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|
|CD_init|Type|Description|_|
|CD_init|Datareached. is readThe into timecode the specifiedspecified bufferto|read until from the endshould of thebe buffer6 framesis|
|prior to that|actually|needed|and the|partition|marker|indicating|the|
|start|of the|data may|be|anywhere|within the|first 31|frames|
|(72,912|bytes)|of the|buffer.|
|CD_initm|Incoming|data from|the CD|is|scanned|for a|partition|marker|
|consisting|of the|longword|specified.|Once the|partition|marker|is|
|identified,|data|immediately|past|the|partition|marker|will|be|read|
|into the|buffer.|Though|data|is|automatically|located|correctly|in|
|memory|by|this|call,|more system|resources|are|used.|Note:|If|the|
|partition|marker|is|not found,|this|call|will|look|‘forever.’|
|This|call|also supports|circular|buffers. When|enabled,|data|will|be|
|read|into the|circular|buffer|indefinitely|or|until CD_uread|is|called.|
|If CD_jeri|has|been|called|and SMODE|has|been|set|to $14 to|
|allow data to flow to the|I°S|port,|you|may|install|a custom|
|interrupt|handler that|will|read|data from|the CD|and|use|it|as|
|necessary.|

**----- End of picture text -----**<br>


**==> picture [4 x 30] intentionally omitted <==**

**----- Start of picture text -----**<br>
j<br>bi<br>**----- End of picture text -----**<br>


15 June, 1995 

Confidential Information “JER Property ofAtari Corporation 

© 1995 AtariCorp. 

| 1 i | | | 4 q 1 i ti | i 1 | | ; ' } : | ' i j 

i y 

s © 1995 Atari Corp. 

1 

## | Jaguar CD-ROM Page 13 Sc dD —~—iCCis 

This call must be used to initialize the CD system before any other calls can be made. 

**==> picture [540 x 560] intentionally omitted <==**

**----- Start of picture text -----**<br>
s co  Llhdllrrrtsts”r—CC.UOCtCiéC..<br>| fee [DoW 0 Retum immediately.<br>__<br>i D1.W Sessiontospinupon.<br>Pb | No return value in any registers.<br>4 Paposs This call sets the CD drive to a specific session. Note: This call is not actually required for<br>fF of[reading data in another session.<br>7 oe ., ...<br>[RET| DOW 0 = Return immediately<br>ee 1 => Wait for completion.<br>: [ee [|]  No [ return][ value][ in][ any][ registers.]<br>- rpose. = { This call stops the CD<br>' DO, 01, Ad<br>i No return value in any registers.<br>Purpose This call allows a different disc to be inserted into the Jaguar CD without a reset<br>4 a occurring. This call should only be made after a CD_stop with the “wait for completion” -<br>a a flag set, followed by the display of a graphic asking that the user insert a new disc. When<br>the a new CD is inserted, its Table of Contents will be read at $2C00 and control will be<br>| ee returned to the program. Do not assume anything about the state of the CD after this call.<br>: a This means, for example, that CD_mode should be reissued to place the CD in the state<br>j | you require. See the section Jaguar CD-ROM Programming Procedures & Guidelines<br>ees| for more information.<br>ff] DOW => Return immediately.<br>ee 1 => Wait for completion.<br>**----- End of picture text -----**<br>


Confidential Information “PO® Property of Atari Corporation 

15June, 1995 

> 2 : _— . | | 2@ 

| | 

Page 14 

Jaguar CD-ROM 

| | | : ; , 

Es 4 Pm 

= 

| 

| 

**==> picture [255 x 13] intentionally omitted <==**

**----- Start of picture text -----**<br>
This call unmutes the CD. It functions only in audio mode.<br>**----- End of picture text -----**<br>


eerC—<—~—srsCSsrsSstCszs—.SaCO‘(RYOYCNCNONO.O.OCCaCisCiziz.C;€® 

pe | DOW 0 Return immediatoy 

oo No return value in any registers. This call undoes the actions of CD_paus. 

SeLldlrrrr—“(eOOOOOOCONCCCCsa.saistrst;stCriCNRCNNRNCCiézéCSAl 

[‘RegisterUsage {D0 PRetwns= =—=—=—_—_|_ Error code in err_flag. 

Purpose This call stops data recording started with a CD_read call. The disc will not be stopped by peasec ss os] this call, only the data transfer. This call is used to cause early termination of a data Le transfer in case of an error, or to disable the CD data transfer when it is no longer needed {and the resources it uses are required for other purposes. 

15 June, 1995 

Confidential Information “7O® Property ofAtari Corporation 

© 1995 Atari Corp. 

Page 15 

> 'Elaguarjaguar CD-ROM CD-AOMEmulatorSetup tic This document provides the information you wil: need to connect your Jaguar CD-ROM Emulator to } your Jaguar Development System. Before proceeding with the setup of your Emulator, verify that you } have the following items ready to use: 1 1. An Atari Falcon030 Computer with mouse and AC power cord. . 2. A Jaguar Development System. | 3. A Jaguar Developer CD. 4. Three-header connector. | 5. A Falcon030 to Jaguar adapter card with ribbon cable. 6.7. AASCSI Falcon030 Monitor hard disk drivePort (not to supplied VGA connectorby Atari).adapter. 8.9. ASCSIA VGA cable monitor with with a high-density VGA cable (notSCSI supplied connector.by Atari). Note that the SCSI hard disk drive must be supplied by you. Not all SCSI drives will work in this application, due to variations in the speed, hard drive buffer size and caching strategies among different & drives. Atari strongly recommends the Connor Peripherals CFP1060S or CFP1080S, both of which are P 35” one-third height drives with a storage capacity of approximately 1 gigabyte. Use of drives other , than these may give unusable results. 

1. Connect the AC power, video monitor and mouse. Attach the AC power cable to the connector | marked “Power” on the back panel of the Falcon030. Plug the AC cable into a properly grounded | electrical outlet. Plug the Falcon030 Monitor Port VGA connector adapter into the Falcon030 back | — panel connector marked “Monitor”. Connect your VGA monitor cable to this adapter. Plug in the j Falcon030's mouse to the connector with the mouse symbol, which is located underneath the Falcon030, | near the right front edge of the unit. There is also a joystick connector in the same area — do not plug the | | mouse2. Power-upinto that. the Falcon030 and check software installation. Turn on the Falcon030 using the power switch on the back panel, near the AC power cord. On you VGA monitor, you should see a black and | | white low-resolution display of the boot-up sequence in which the Falcon030 checks itself. At the end of the boot sequence the screen resolution will increase and the desktop will be displayed. The open | window will have the CD-ROM Authoring and Emulator software “CDROM.PRG” as the last item in the list of files displayed. You are now finished with this part of setup, so'turn off the Falcon030. t ft7 3. Connect your SCSI hard drive and verify accessibility. Attach a SCSI cable to the port on the | back panel of Falcon030 marked “SCSI”. Since this is a high-density SCSI connector, you may require | the© 1995 adapter cableAtari Corp. to connectConfidential to your SCSI Information drive. FER After youProperty have of attachedAtari Corporation your drive, turn on the15 June, 1995 

4 | | | | | | | 1 i | | | 1 | q i | | ! { : i \ i i i ‘ '{ i | / 

15 June, 1995 

Jaguar CD-ROM [i VGA ; 3 | the ’ the opposite opposite | 3 Attach = “DSP”. q with three three | ‘ : plugging 3 plugged . protruding the CD-ROM CD-ROM Pe connector to to 2. j o e 7 j a | 2 7 _ : 

| 

. 

| | 

: | : | 

= 1 : 1 

| q 

Page 16 - Falcon030, and watch for your SCSI drive to show up in the list of devices displayed on the VGA monitor during boot-up. Turn off the Falcon030. 

4. Ensure that the ribbon cable is attached to the Falcon030 to Jaguar connector. Connect the ribbon cable to the Falcon030 to Jaguar Interface connector. The red stripe should be on the opposite opposite side of pin #1 of the connector. If you had an older system, this is the reverse of the old setup. Attach the Falcon030 to Jaguar Emulator adapter card to the Falcon030 back panel connector marked “DSP”. 

**==> picture [602 x 330] intentionally omitted <==**

**----- Start of picture text -----**<br>
| 6. Connect the CD-ROM and Falcon. The CD development system contains a simple PCB with three three | ‘ :<br>ribbon cable connectors as shown in Figure 3-A. All three connectors are keyed to prevent plugging 3<br>them in incorrectly. The cable attached to the Falcon030 to Jaguar connector should always be plugged .<br>; into the grey connector oriented differently from the two black connectors. The ribbon cable protruding<br>| from the CD-ROM unit should be connected to the black connector on the outside to use the CD-ROM CD-ROM Pe<br>unit normally and disable emulation. Connect the cable from the CD-ROM to the inside connector to to 2.<br>a emulate and disable the onboard mechanism. j o<br>Connect to CD-ROM e<br>| for normal operation. 7<br>| | Connect to CD-ROM j a<br>[| tor emulation. | 2<br>a ae |<br>,<br>ae to Jaguar connector. 7<br>/ | Connect to Falcon030 _<br>a a5 | :<br>: ae =<br>**----- End of picture text -----**<br>


Figure 3-A — Three-Header Connector 

That's it. The setup is done. If any of the above steps could not be accomplished, despite having allthe bits and pieces and following the instructions, please contact Jaguar Developer Support. 

To start using the Authoring Tool, turn on the Falcon030, wait for the desktop to appear and press the F1 key (or double-click on the file "CDROM.PRG"). Follow the Jaguar CD-ROM Authoring Tool With Emulator Users Guide to create a CD-ROM Table of Contents based on your SCSI drive's files. 

4a : 15 June, 1995 Confidential Information AR Property ofAtari Corporation © 1995 Atari Corp. | 

Page 17 

| 

HE Jaguar CD-ROM yGOr Jaguar CD-ROM Authoring Tool WithEmulator | | The Jaguar CD-ROM Authoring Tool with Emulator provides a simple yet comprehensive user interface for creating sessions and tracks for a CD-ROM, and emulating the real hardware. To create tracks, the user specifies the files constituting a track. The data files can be audio/video data or | executable code. 

: | : | | i | | : 4 i | : 4 { j 4 ' 

4 4 "| Ai if 

| This software emulates CD-ROM by reading data from a large MS-DOS formatted SCSI hard disk drive. The SCSI identifier for the drive must be specified to the emulator. Failure to do so may result in the emulator refusing to initialize. Please refer to the section How to set the SCSI identifier. 

! Fe lw | —-To[ create][a] new document, choose "New" from[ the][ File] Menu. The Authoring Too][ will][ create][ a] new document and will ask for a Title for the document. The window will show only one row saying “End of CD-ROM...”, since you have not specified any files yet, as shown in Figure 4-A. 

. 

**==> picture [320 x 219] intentionally omitted <==**

**----- Start of picture text -----**<br>
CO ROM File Edit Search Options<br>This is a Test COROM Title... g<br>7 Sessions = @,<br>-qunber of Sesslons 20, Tracks = 0, Files=@<br>unber_of Sessions = Ss TracksTrask =are@, Files_1 tangth= @ [Coment |<br>End of CORON...x |<br>{<br>| |<br>1| — =. 5<br>‘Figure 4-A — Creating a new CD-ROM Table of Contents Document<br>**----- End of picture text -----**<br>


To open an existing document, choose “Open” from the File Menu. A file selector box will be presented in which you can select the document you want to open. Clicking on "OK" will open the document you just selected. The Authoring Tool will check for the validity of the files constituting the tracks in the document and update the position/length for each of them. 

Page 18 JaguarCD-ROM fi -@3 ‘Description Of The Authoring Window nc cc Ge CDROM File Edit Search Options i» ec; 4 Nunber of Sessions = 2, Tracks = 5, Files = 72 [8:82:88 | 88:14:71 = | I” PBALL.CDR_[pney.cOR |, ~——6:23215660 || 68:06:00BB: 87:61 | GB;81:61 | ThisThis isis another9 sanple sanpleconnent..t.conné:| | 44 BAT.CDR 00497 | 88:87:64 |eoseoras) —RC“‘COCCC*SS | T_T BSKULL.CDR | £6948 | 88:12:32 | 88:88:87 4 | Teupele.cor | 73844 | 88:12:39 | 88:88:32 i ba e | | Track # 3 88:16:71 | | 10548 /00:20:71 | oerepras{ be BUGGY COR 31596 |88:25:04 | 00:00:16 | 8 Figure 4-B — A CD-ROM Table of Contents Document _ The Authoring Window is divided into various parts, as shown in Figure 4-B. The top row of the " window contains the “Title” (user specified) for the document. The second row contains the total | 3 number of sessions, tracks and files used in this document. The next row contains the column headings, | ‘ arranged as follows : og © The first column contains the current session number, current track number or filenames used to create the track. The tracks in a session are indented two characters inside the session to which they _ belong, and the files are indented further by two characters inside the track to which they belong. ? © The second column contains the length of the files in bytes. The entries for session or track in this _ column are empty. fe * The third column contains the start of the item on the CD-ROM in terms of it's time code position, am also referred to as it's "time-stamp". ¢ The fourth column contains the length of the item in terms of time code. | ° The fifth and last column contains the user specified comments for each item. a4CurrentitemintheWindowCurrentitemintheWindowWindow = 0 The CD-ROM document opens in a window and presents itself in a hierarchical CD-ROM document opens in a window and presents itself in a hierarchical document opens in a window and presents itself in a hierarchical opens in a window and presents itself in a hierarchical in a window and presents itself in a hierarchical a window and presents itself in a hierarchical window and presents itself in a hierarchical and presents itself in a hierarchical presents itself in a hierarchical itself in a hierarchical in a hierarchical a hierarchical hierarchical structure of of Sessions/Tracks/Files. The “cursor” “cursor” is a row-window, row-window, indicated by a thick border around the current by a thick border around the current a thick border around the current thick border around the current border around the current around the current the current current j 

| 

q 

{ : : j 1 | | 1 / j : : 4 : | 

| : ] ' 

> a4CurrentitemintheWindowCurrentitemintheWindowWindow = 0 The CD-ROM document opens in a window and presents itself in a hierarchical CD-ROM document opens in a window and presents itself in a hierarchical document opens in a window and presents itself in a hierarchical opens in a window and presents itself in a hierarchical in a window and presents itself in a hierarchical a window and presents itself in a hierarchical window and presents itself in a hierarchical and presents itself in a hierarchical presents itself in a hierarchical itself in a hierarchical in a hierarchical a hierarchical hierarchical structure of of Sessions/Tracks/Files. The “cursor” “cursor” is a row-window, row-window, indicated by a thick border around the current by a thick border around the current a thick border around the current thick border around the current border around the current around the current the current current j row, as shown in Figure 4-B. Most of the editing operations work on the current row, depending upon whether it is a session or track or file. 

| 

15 June, 1995 

Confidential Information FR Property ofAtari Corporation 

© 1995 Atari Corp. | 

@& = Jaguar CD-ROM Page 19 Eq isSavingADocument @ _snorder to save a document choose “Save” or “Save as” from the File Menu. For “Save As”a file f selector dialog will appear and prompt you for the output path and filename. 

i i j | j | fi / 1 | i i 1 | q i i t a : 4 i4 4 iq ; q y 4 q 

| 

. 4 ' 

7 

S| @e~@ 

#6RditingACDROMDocument In the CD-ROM document, each session should have at least one track and each track should have at least one file. While editing a CD-ROM document, if the Authoring Tool finds that there are no files in a track or there are no tracks in a particular session, it will enter the required items automatically. If a new track is entered, then the subsequent tracks are renumbered. The default filename entered is “Untitled”. This is true for all editing operations. 

47 (lasertingASession — i In order to insert a new session in the document at any position, choose “Insert Session” from the Edit Menu, as shown in Figure 4-C. This command inserts a new session before the current item. This function is disabled if it is not possible to insert a new session at the current row. A session should contain at least a track and each track should contain at least a file. 

**==> picture [534 x 387] intentionally omitted <==**

**----- Start of picture text -----**<br>
CD ROM File Search Options<br>(Nunber oy ETiteees o|<br>of Sessi fo 2|Se | |<br>; Peete ay Connent i |<br>| [ Session #e | delete (bell ea<br>GEM tosent Session (F3) dastkem |<br>: TRABY.COR | Insert Track | [F2) Bi@iG1 {This is a sanple connent...}<br>"[BALL.COR |InsertFile CF1) Bragsa3 | This is another sanple conn ea<br>rack& 2 b----mnnnnnnn---<br>BSKULL.COR| Add Comments...nnn ooCFS] no progie7|2843 | a<br>BUBBLE.COR| casz rile Nene... tray pigessz|<br>BUBBLS.COR | __ 40548 | 08:20:73 a<br>Figure 4-C — Inserting a New Session<br>In order to insert a new track in the document at any position, choose “Insert Track” from the Edit<br>. : -<br>/ ‘ 6 Menu. This command inserts a new track before the current item. This function is disabled if you can<br>not enter a new track at the current row. A track should contain at least one file.<br>: © 1995 Atari Corp. Confidential Information “FR Property ofAtari Corporation 15 June, 1995<br>**----- End of picture text -----**<br>


i 

15 June, 1995 

| Page 20 Jaguar CD-ROM | 8 InsettingAFie| 

i , { 

| | ‘| | : | : j 1 ' j 

: 1 

| i : : j ; :' ' | 

In order to insert a new file at any position, choose “Insert File” from the Edit Menu. This command inserts an “Untitled” file before the current row. This function is disabled if you can not enter a file at the current row. . 

> 410EditingAFilename##§ == = The Authoring Tool always enters an “Untitled” file of length zero when you create a new file.In order to edit this filename, use the cursor keys to make it the current item. Moving the mouse pointer over to the filename and clicking on it will also make it the current item. Now, choose “Edit Filename” from the Edit Menu to select a new filename. A file selector box will appear showing you the disk structure of the current SCSI drive being used. You can traverse through sub-directories and files on the disk and select the filename you want for the current item. The Authoring Tool will update the time code stamps for each item in the window. 

In order to provide some description for each item constituting the CD-ROM, the user can specify a description up to 64 characters long. To enter the description for a particular item, make that item the current item and choose “Add Comments” from the Edit Menu, as shown in Figure 4-D. A dialog box will appear where you can type the description you want for the item. This dialog box will also appear if you double click the mouse over the “comments” area for any item. 

**==> picture [318 x 217] intentionally omitted <==**

**----- Start of picture text -----**<br>
CO ROM File Edit Search Options _<br>This is a Test Title... 0:<br>“Hunber of Sessions = 2, Tracks = 5, Files = 72 a<br>| Size | Start | Length | Comment 1 |<br>i__| BABY. COR 319688 This is a sample comment... —<br>WM BALL. cor 6232 } 06:07:61 | 00:00:83 | This isanother sample comme<br>— | Hee...Fri ENTER CONKENTSaaaTO SE ADDED ne iSpigiS|i<br>LBURPLE.COR | 76916 06:80:33 | Si<br>12] f<br>Figure 4-D — Entering Comments<br>**----- End of picture text -----**<br>


**==> picture [2 x 14] intentionally omitted <==**

**----- Start of picture text -----**<br>
}<br>**----- End of picture text -----**<br>


7 

15 June, 1995 

Confidential Information FR Property ofAtari Corporation 

© 1995 Atari Corp. 

Page 21 

Jaguar CD-ROM mura a 

i ' ] | : | | | | { | ‘ 4 ' ‘ \ :\ 

| | ait pieterencas = Specifying Léad-in/lead-out for Sessions & The Jaguar CD is a multi-session “Orange Book Standard” CD. The whole CD and each session within : it contains certain specific regions. In order to specify length of such regions to the emulator, choose | “Preferences” from the Options Menu. These regions may be lead-in/lead-out for sessions or the pause | eo regions around the tracks, etc... 

i 

4 The Authoring Tool provides common editing operations like Cut/Copy/Paste/Delete to make editing a + CD-ROM document easy. In order to cut, copy or delete items from the document, first select the items 4 and then choose “Cut”, “Copy” or “Delete” from the Edit Menu. “Cut” will copy the items to the me clipboard and delete them from the document, “Copy” just copies the items to the clipboard and - “Delete” deletes the items from the document. If the clipboard contains CD-ROM document | information already, you can paste that information to the document. The information added from the | clipboard will go immediately before the current item. During these operations, if the Authoring Tool | — finds that any of the sessions are emply, it will enter a track for you. If any of the tracks are empty, it ~~ will enter an untitled file in those places for you. The Authoring Tool always updates the time code # _sstamps for each item after each editing operation. / | Ce | — Inorder to undo the last editing operation, choose “Undo” from the Edit Menu. { gaaeoie Session ee ae inorder to move to a specific session, click on “Goto Session” from the Search Menu. 

j mene ee q In order to move to a specific track, click on “Goto Track’ from the Search Menu. 

CMC You can also find a particular item by using this option. 

© 1995Atari Corp. 

4 

Confidential Information “JPR Property ofAtari Corporation 

15 June, 1995 

**==> picture [602 x 729] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 22 Jaguar CD-ROM a<br>| 418Preferences—SpecifyngSCSiiD§. «el<br>CD-ROM hardware emulation is performed by reading data from a large SCSI drive. Before this can be 4<br>done,dialogthe box. SCSIFailureidentifierto do sofor the may driveresult must bein the emulatorspecifiedrefusingto the emulatorto initialize. by means of the :Preferences || @2<br>419Preferences-HowtosetthesCslidentiier§ §.§.. ss, s§- s SaZ<br>The identifier of a SCSI device defines the number of the device set on its jumpers. The emulator , |<br>expects this identifier to be specified through the preferences dialog box, and the emulator will use this<br>identifier to access the data on that device. Sometimes, for an encased SCSI device, this identifier can , 8<br>be set by means of a rotary dial on the back of the case. In other SCS] modules, the ID can be set with 1 ]<br>dip switches. Consult the owner's manual of the SCSI sub-system or drive you are using. i o3<br>3S<br>| 420Preferences-—CD-ROMLatency=<br>Different latency periods can be specified to the emulator by choosing “CD-ROM Latency” from the fe<br>Options Menu, as shown in Figure 4-E. In our experience, these values are very ‘worst-case’. You | o<br>should probably set all of these values to zero since the existing defaults do not properly represent a %<br>| production CD. If you are doing timing critical stuff you should burn a real disc to conduct your tests<br>on.<br>Ca CD ROMaFile Edit Searchcm OTS$iatisTALL Cine  Taniameasurements areSonain SeMunberOeof milliseconds 2 a 3Pog::<br>: Initial Spi (Single Session, 10 Tracks)...ecssesseevees4088) |p<br>4 |_Humbe u bach piditvonel session, Addi csccrcvreceerererescuveees 568 as 4 i<br>Each additional track, add....scscsereesseenereseeeeees1868 fi<br>| ' it Stop disc and park the Readseccessesesvcreveeeeresereevees<br>Te From middle of disc, addscssccccssereseserseesesesseres1080 [psd |<br>Fron outside edge, addsssiscversvseareecevseesseceenees2808 fT Es)<br>rt £068 bane<br>: i) Pause to ready for next Conmand....sssesvesereeeserseereres 1880 frm. ]<br>; | Ttrél uUnpause to start of data flow. .ciesecessrsesseessereeneenss 167 - j<br>ql i] Short seek within 1 minute span lacated in 1st 37 minutes.. 258 |<br>q | Short seek within 1 minute span located in 2nd 37 minutes.. 375 fs i<br>: ,— Long seek Kithin @ to 20 minutes..c.ssecsesevereserererses 588 [>i q<br>j | Sess} Long seek Within 21 to 40 minutessiseresscsecerseeseeeeeees 625 Fo<br>: TTpy| bong seek within AL to GB minutesscssecssererserversereses 758 | }<br>: LL Long seek within 61 to 74 MiMUtES...secererersresecrseeeees 1808 a<br>' i Long seek within 6 to 74 minutes (Single Session)......... 1508 ae :<br>i io" tp For each additional session, adds.csssvccsserseeeeeeess 250 fo fee) 4<br>j<br>{ : Long seck within 6 to 35 minutes (Single Session).......5. 759 |<br>_ For each additional session, add...sseceseeereerseerees 258 fe<br>: ie] OL<br>| Figure 4-E — Editing the Latency Table<br>: The default Jaguar CD-ROM Latency table is as follows: j<br>I<br>jOperation Latency Time Recommended<br>' initial Spin up - single session, 10 tracks es eee<br>b. Each additional track, add<br>jj<br>: 15June, 1995 Confidential Information AR Property ofAtari Corporation © 1995 Atari Corp. §<br>**----- End of picture text -----**<br>


| i { i i i \ : | | j | | | | j , 1 | j ]4 | : | : | ] j 

| &, 

**==> picture [546 x 236] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 23<br>Bi j 43“?™@ JaguarOperationCD-ROM Latency Time Recommended .<br>WPS’ | Stop disc and park the head p tsecs: | secs. |<br>| a. From middle of disc, add ee ee<br>: b. From outside the edge, add PBeces. | secs.<br>| Pause to ready for next command paosecs._[  Osecs. |<br>7 Unpause to start of data flow y—47esees: | secs.<br>a Short seek within 1 minute span, located in 1st 37 minutes 3+/4-se6s- [_Osecs. |<br>: Short seek within 1 minute span, located in 2nd 37 minutes [aieeces. | __Osecs.<br>Long seek within 0 to 20 minutes [—aeccs, | secs. __—<br>4 Long seek within 21 to 40 minutes Peesces, | secs.<br>3 Long seek within 41 to 60 minutes 3/4-sees- O secs.<br>Long seek within 61 to 75 minutes eee ee<br>Long seek from 0 to 74 minutes raeces, | Osecs.<br>a. For each additional session crossed, add [——daecss, | _Osecs.<br>| Long seek from 0 to 35 minutes —~“sirsess. | Osecs. |<br>a. For each additional session crossed, add [——Hrsecs. | secs.<br>**----- End of picture text -----**<br>


ne After your various sessions and tracks of the CD-ROM have been specified, this function will emulate | the Jaguar CD-ROM. To start, choose “Emulate CD-ROM” from the File Menu. The emulator will install various drivers and start emulating the CD-ROM by monitoring the Jaguar Console data requests a and respond by sending data to the Jaguar Console, as if the Falcon030 were a Jaguar CD-ROM drive. | ume aaa 

To stop emulation, press the “Esc” key. 

goa Restrictions‘ On The Emulation ee » Data rate is always 95% of doubl / 7.4" > (4, ond vs. 352800). * No CDerrors will occur. ka; oO_ ae4 i .[ine][real][hardware][in][all][cases,][so][the latency] ° _[cveut] aa ;- itadeq . t- Ae ©. 4 by you for your own disc structure's performance profile. ce on Using THE;CD-ROM Emulator Although the emulator allows you to specify multiple files per track, we suggest that you use one file per track. This way the emulator will give the best performance, when you compare it to an actual CDROM drive. The reasons for this are as follows: 

© 1995 Atari Corp. Confidential Information JER Property ofAtari Corporation 

15 June, 1995 

Page 24 

. 

Jaguar CD-ROM 

7 

i 

| 

|Z : ¢ | @ | # Ee [ | Es | | BS | : a 

| 

| | } 

| | 

1 

The CD-ROM emulator allows you to add multiple files on each track on the CD. In order to do this, the emulator adds zeroes at the end of each file, so that the files are a multiple of 2352 bytes. This is done internally, and it does not effect the files on your SCSI drive. 

In order to get the best performance from the emulator on the Atari Falcon030, version 2.0 of the emulator does this padding process differently. First the emulator adds zeroes at the end of each file so that the length of file is a multiple of 16K, and then it adds zeroes further so that the files are now a multiple of 2352 bytes in length. Again, this is done on the fly by the emulator as the files are sent tothe Jaguar and it does not effect the files themselves on the SCSI drive. . 

Note that a lot of emulated space on CD-ROM is wasted in order to get the best performance from the emulator. This does not mean that your file layout on tracks should waste this kind of space. Thisisthe reason you whould use only one file per track in practice. Therefore, the user should layout different files on a track and create one big file and specify it as one track to the authoring and emulation system. The version 2.0 accepts the old ‘. TOC’ files from version 1.0. This ‘.TOC’ format is a native format for the emulator. 

## 425 logfileName = s—i“(tw—CCtCee Oe - PreloadBuffes = = = «=—sisisi fOptons Menu) Ee 

These two menu items are not functional yet. The file name entered in Log File Name and the values entered in Preload Buffer Size dialog boxes are ignored. 

1 

15 June, 1995 

Confidential Information “FOR Property ofAtari Corporation 

© 1995 Atari Corp. | 

| | ; : j \ i | i ‘ ' I { | | | | | | 1 if | i ; i 4 ; ; : ‘ ; t | i ; : | 

; #0 Jaguar CD-ROM Page 25 ya 5.CD-ROMEmulatorQ&A } There are some common questions that arise even after reading the installation instructions delivered iE swith the CD-ROM Emulation system. We want to address these in this document. g What external hard drive are wesupposed tobuy? Pe ee 1 Answer || The SCSI hard drives we have tested and know work are the Conner Peripherals CFP1060S 4 and CFP 1080S. Using other drives not tested by Atari may not give acceptable results. | An external drive with its own case and power supply is most convenient, which is why we Z include a SCSI-I! cable with the emulation system. How do| prepare and connect the drive for the Emulation System?. a 4 You must format the CD data drive on an Adaptec 1542 SCSI Controller in an MS-DOS based . computer. Set the disk up with a single partition using the Adaptec tools. Now format it under E MS-DOS (you need MS-DOS 5.0 or later to deal with partitions of this size). Do NOT use q DoubleSpace or other real-time disk compression utilities!! : Other SCSI! cards may work, provided they create and use the exact same partition setup as the Adeptec. However, other cards have not been tested by Atari, so proceed at your own risk. | After formatting, copy some of the files that you want to access as CD data to the drive. Switch - 9 your PC and CD data drive off. Disconnect the CD data drive from the PC. Connect it to the y i Ealcon030 emulation computer. Now proceed as detailed in section 3. i] Question |] It looks like | can access the PC formatted drive even from the Falcon030desktop (| can read a and copy file from and to it) - is. something wrong? ees Don't even try to access the PC drive from anywhere except the File Selector dialog within the CD Emulator (see section 4.10). The hard disk partition scheme used by the Falcon030 is very close to that used by MSDOS, but it is not identical. Reading from the PC formatted drive on the Falcon030 will corrupt the internal memory structures of the Falcon030's operating system, which will in turn cause other errors and system crashes. It may even allocate al! of the system's memory in a desperate attempt to make sense out of the PC drive’s directory structure. This can lead to the failure to allocate memory as you start CDROM.PRG so that when you try to access the directory of the external drive you will see: “Error in Fileselektor Box! (Internal Error Number -3000)" Do not install a desktop icon on the Falcon030’s desktop for accessing the emulation data drive. In the event you do accidentally read the PC drive (even just the directory), you should reboot the Falcon030 immediately to avoid problems. Likewise, attempting to write to the PC formatted drive from the Falcon030 will result in a corrupted disk structure, which will require that you repartition and reformat the drive, and then recopy all of your data files to it. Why may | get the message “Internal Error Number 4000"? ee |Z 0 rAnswer_ || You may have set the wrong SCSI ID in the Emulator Software. 

© 1995 Atari Corp. 

Confidential Information “JER Property ofAtari Corporation 

15 June, 1995 

Page 26 Jaguar CD-ROM Ee Question |]! read data from the emulatorbutwhen | do a memory dump |[see][ a][ region][full][of][ the][ hex][ value] pO | |] SDEAD. oy a oe eee This is the emulators default return value for areas of the virtual disc that don't have any data : ; associated with them such as the lead-in, lead-out regions and prior to and after valid tracks on a. the disc. What is the current distribution of the CD-oriented tools? : 4 As Atari is constantly improving and updating the Jaguar Developer Tools, you should : : periodically check for new revisions on the Atari Software Development BBS or the Jaguar : | Developer's area on Compuserve (See Online Support in the Getting Started section). . j Question || |have problems getting the technical information for the Conner drive, such as Jumper settings poe ; and so on. Where can | get these? . 7 4 & | Answer Call the Automated Conner HelpFax for all your possible drive information requirements. From — | any touchtone phone in the world you can call this number. The machine asks for the number of — q a FAX machine you want to get the information faxed to and directly faxes to that machine. The ; : number of this Automated Conner Fax Service is: (408) 456-4903. { i Adaptec also has an info faxback service for their SCSI controller cards at (408) 945-6776, and a BBS for software updates at (408) 945-7727, Parameters 9600bps, &N1. : Question || | can access the code, but the CD_read routine just stops working after transferring 5-20 { : kilobytes of.information. What might be wrong? : It is likely that you are using a 68000 based vertical blank, interrupt handler that consumes too ; much time. j The time you have within the 68000 Vertical Blank Interrupt (VBL) must be significantly shorter a than the time between two interrupts coming from the CD. Make it short. Do not build object lists j { within the 68000 VBL (this is generally true, not only for CD). q | The Falcon030 crashes everytime | do.a CD_read. What's wrong? ce { : Versions of the CD emulator through v2.02 seem to have a bug where if you try to read data q ; from before the start of the first track or after the data in the last track, the emulator will crash. j | 4 We are working on this problem to remedy it. For now, add padding tracks as necessary to q 1 access your data. : 

q ; 

i1ever15 June, 1995 —— Confidential Information“7O® Property ofAtari Corporation © 1995 Atari Corp. ; 

Page 27 

| ! | | i | | yj | | | { q . | | ’ 4 : 1 :{ { 

j & Jaguar CD-ROM 

| programming, Procedures, and Guidelines : This is a “living document.” Many of the details are subject to change but there are no expected changes @ that will cause overall structural changes or require changes in game code. @ The Jaguar CD format is raw data and multi-session. Session #0 of a Jaguar CD is an audio-orly } session. It must contain only standard “Red Book” 2udio. This may be used to store future product 4 information, sound track music, etc... No CD title that contains anything other than “Red Book” @ = audio in Session #0 will be compatibility encoded. Atari will probably take the first track(s) for our Gown information. If you have no Red Book audio for your CD title, then you should test and submit = = your CD with at least one “dummy” track in Session #0. : All developer code starts with the next session, Session #1. The first track located in this session will be the boot track. The last track of the last session will contain data used by the Atari authentication code. | The size of this track will be quite small, about 300k or jess. | cnn byfae The beginning of each data track (i.e. Session #1 and above) you provide must contain a specific Atari , a format data header and tailer. The track header must consist of 16 long-aligned repetitions of the ASCH B block ‘ATRI’ (64 bytes) followed by the string: 4 ATARI APPROVED DATA HEADER ATRIx This string is exactly 32 bytes in length. The last character is a special byte that increments for each track. Your first data track (i.e. the boot track) should have an ASCII space character in that position (0x20). In your second data track, this byte should be an ASCII exclamation point (0x21). In your third data track, this bytes should be an ASCII quote character (0x22), etc... Each track must also end with a specific track tailer. The track tailer must consist of the string shown below followed by 16 long-aligned repetitions of the ASCII block ‘ATRI’ (64 bytes). ATARI APPROVED DATA TAILER ATRIx The last byte of the track tailer string should be the same as the last byte of the track header string for the same track. No data should precede a track header or followa track tailer. 

' 

a © 1995 Atari Corp. 

Confidential Information JPR Property ofAtari Corporation 

15 June, 1995 

Page 28 28 Jaguar CD-ROM Ef 61 TheBootiack = § =§ . £=—@ The boot track has two additional Motorola (MSB-LSB) style longwords that must follow immediately #3 after the track header. The first is a long word that indicates the target address of your startup code and , the second should indicate the length of your startup code in bytes. Your startup code should follow : 4 immediately after these two longwords. The CD Boot ROM will load a maximum of 64k of code at the , 2 location you specify in DRAM and transfer control to the 68000 at the start of this code. Your boot : track may contain data beyond this 68k which will be your responsibility to load, however, the system 2 will only load up to 64k. § When control is passed to your code, the results of a CD_getoc call will be in memory at 0x2C00. Your : code must not call CD_getoc again. Use the table of contents to determine the first track in Session #1. The track number and timecode of all subsequent tracks should then be calculated as an offset to this. 4 Do not reference absolute track numbers in your code because the layout of your CD is certain to : i change after compatibility encoding. . 62CDTrackandSessionlayout=#§= =... § Atari will master your CD using a two second lead-in period at the beginning of each track. The track | . Thestart starttimestimes found ofin everythe table-of-contentstrack, however, willwillchangeaccountasfora resultthis andof thispointprocessto the beginningso you should of yournot relydata.on fF. absolute timings to find your data. You should add a dummy track to your last session to simulate where the compatibilty encoding data : will be placed. This track should be 156,192 bytes in length and may contain any dummy data. Please — note that the final size of compatibility encoding data may vary due to the layout of your CD. zz The first session will have at least as many tracks as you asked for (Atari will probably add one), your _ tracks will be at the end of the session. For example, if you give us a CD for compatibility encoding in (am the following format: . ' Session Track Contents q Developer Audio #1 | Po 8 | Developer BootDeveloper Game CodeData M1 ._ . Pe#5 | Developer Game Data #2 f #6 Developer Game Data #3 _ Developer Game Data #4 | #8 [Dummy End Track (required) ! Atari will master a CD and return it to you in the following encoded format: Session Track Contents , Pet== — __| maybe more Atari maybe more Atari more Atari Atari audio tracks... tracks... j 

> | . 

7 | | 

{ 

' 

7 

7 

7 

| | | j : 

## Page 28 28 61 TheBootiack = § =§ . 

**==> picture [289 x 87] intentionally omitted <==**

**----- Start of picture text -----**<br>
Session Track Contents<br>Pet== — __| maybe more Atari maybe more Atari more Atari Atari audio tracks... tracks...<br>|<br>2 [Developer Audio #1<br>[#8<br>__[ Developer Audio #2<br>Confidential Information FR Property ofAtari Corporation<br>**----- End of picture text -----**<br>


q 

15 June, 1995 

© 1995 Atari Corp. ] 

Page 29 

| gap. 

' 

**==> picture [517 x 358] intentionally omitted <==**

**----- Start of picture text -----**<br>
. Jaguar CD-ROM<br>Session Track Contents<br>Developer Boot Code<br>Developer Game Data #1<br>| #6__| Developer Game Data<br>ee Developer Game Data #3<br>Developer Game Data #4<br>Co ee Atari Compatibility Encoding Data<br>| —— a<br>| One goal of the Jaguar CD is to remove the “slow” stigma from CD-ROM. Using a small number of<br>/ sessions will minimize Startup time.<br>| At startup, disc authentication takes place. During authentication your code will be scanned for partition<br>| markers that separate your data into blocks of a managable size. Partition markers are sixteen<br>| consecutive and identical longwords that are long-aligned relative to the beginning of the track. Each<br>| track header and tailer, for instance, contains a marker using 16 longwords of ‘ATRI’. Do not use a<br>sequence of ‘ATRY, 0x00000000, or OxFFFFFFFF for a partition marker.<br>| We recommend that you break-up any tracks containing more than 1Mb of data with partition markers<br>ss so that a partition marker occurs approximately between every chunk of data between 128k and 1Mb in<br>size. This will ensure that the authentication process is reasonably quick. The worst-case authentication<br>delay will be no shorter than the time it takes to read the data between the two headers with the longest<br>**----- End of picture text -----**<br>


| oummannay ee | ‘The best way to minimize loading delay is to plan ahead. Design your software so that there is enough time to load new data in the background. This technique, used in the Cinepak demos, allows continuous | streaming of data many times larger than DRAM with no loading delays. The latest release of the CD - BIOS contains a new CD_initm cali that enables a special version of CD_read that reads continuously into a circular buffer with no extra programming. Designing both the game play and the programming to avoid loading delays will be a significant effort but it will be well worth it. 

| : | : : 4 i : ! F | | i 4| i 4 ; : : F 

The following diagram is a sample of how a boot track and subsequent code/data tracks should be laid out: 

©1995 Atari Corp. Confidential Information FER Property ofAtari Corporation 

15June, 1995 

Page 30 eee 

JaguarCD-ROM {i 

Po 

Ss { | _ _ q 2 j ‘ ; ; { ' : 4 ] | I 1 | E 4 j ] 

| 

: 3 ' ' | 

| : 

4 : 

|,|First Trackof<br>=Le<br>eeeee<br>Session #1<br>OT<br>| ATRIATRIATRIATRIATRIATRIATRIATRI<br>3<br>| ATRIATRIATRIATRIATRIATRIATRIATRI<br>|<br>SotTrackHeader<br>j| ATARI APPROVED DATA HEADER ATRI-<br>:<br>Addresstoload<br>—<br>———_—<br>.<br>BootCode<br>|<br>aa SizeofBootCode<br>00004000<br>|<br>00008000<br>;|First Trackof<br>=Le<br>eeeee<br>Session #1<br>OT<br>| ATRIATRIATRIATRIATRIATRIATRIATRI<br>3<br>| ATRIATRIATRIATRIATRIATRIATRIATRI<br>|<br>SotTrackHeader<br>j| ATARI APPROVED DATA HEADER ATRI-<br>:<br>Addresstoload<br>—<br>———_—<br>.<br>BootCode<br>|<br>aa SizeofBootCode<br>00004000<br>|<br>00008000<br>;|First Trackof<br>=Le<br>eeeee<br>Session #1<br>OT<br>| ATRIATRIATRIATRIATRIATRIATRIATRI<br>3<br>| ATRIATRIATRIATRIATRIATRIATRIATRI<br>|<br>SotTrackHeader<br>j| ATARI APPROVED DATA HEADER ATRI-<br>:<br>Addresstoload<br>—<br>———_—<br>.<br>BootCode<br>|<br>aa SizeofBootCode<br>00004000<br>|<br>00008000<br>;|
|---|---|---|---|
||||oo j-—— BootCode<br>Boct Code (Max 64k)<br>i!<br>|<br>:<br>Other Program Code/<br>ee<br>Datamayfollow. Boot<br>'<br>Other Code or Data (Optional)<br>iv<br>cone isresponsiblefor<br>i<br>loading.|
||||| ATARI APPROVED DATA TAILER ATRI<br>;<br>Boot<br>TrackTall<br>' ATRIATRIATRIATRIATRIATRIATRIATRI<br>ot<br>Track<br>Tater|
||Second Track of||- ATRIATRIATRIATRIATRIATRIATRIATRI<br>—<br>ee|
||Session #1|TTT|TERT TTTST<br>| ATRIATRIATRIATRIATRIATRIATRIATRI<br>|<br>7 TrackHeader<br>| ATRIATRIATRIATRIATRIATRIATRIATRI<br>||
||||' ATARI APPROVED DATA HEADER ATRI!<br>SST oom<br>Program Data/Code<br>' Program Data or Code (about 1Mb)<br>-<br>(mustbe long-aligned)|
||||:<br>Partition Marker (sample|
||||| GAMEGAMEGAMEGAMEGAMEGAMEGAMEGAME<br>..“__ 4.character sequence)|
||||’ GAMEGAMEGAMEGAMEGAMEGAMEGAMEGAME|
||||aaa<br>Program Data/Code<br>More Program Data/Ccde<br>a<br>(mustbelong-aligned)|
||||" ATARI APPROVED DATA TAILER ATRI!<br>| ATRIATRIATRIATRIATRIATRIATRIATRI 7 MackTailer|
|||||ATRIATRIATRIATRIATRIATRIATRIATRI<br>||



66UsingRedBookAudio= = = Titles designed for use with Jaguar CD may optionally use Red Book audio as in-game music. Normally this music should be placed on Session #0 so that the user may listen to it in a normal CD player. Optionally, ‘secret’ game audio may be placed on later sessions so that the game can restrict accessto this track until game/level completion etc... Placing Red Book audio on sessions after Session #0 will prevent playback on an audio CD player. 

If your title requires little or no CD access once your code is loaded, you may also, optionally, provide the user with an option to insert another Red Book audio disk for playback during gameplay.The procedure for using multiple discs within a game is contained in the following section. 

| 

15 June, 1995 

Confidential Information “7O® Property of Atari Corporation 

© 1995 Atari Corp. | 

’ 

Jaguar CD-ROM 

Page 31 

&.ig 87 | Accessing Additional CD-ROM Dises ) Despite the large amount of data capable of being stored on CD-ROMs, some titles are beginning to appear which require multiple discs. In addition, some games with minimal data requirements may offer the user the choice of inserting Red Book audio discs which can be used to replace in-game audio. The Jaguar CD-ROM BIOS contains a call (CD_switch) which automates the process of accepting a new CD and re-reading the disc’s Table of Contents. Please examine the flowchart below which demonstrates the disc switching process. 

| | | | 

| 

© 1995 Atari Corp. 

Confidential Information “FPR Property ofAtari Corporation 

15 June, 1995 

; 

**==> picture [600 x 663] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 32 Jaguar CD-ROM | =<br>|i | WaitCall for CD_stop completion’ in —=<br>mode. | | @<br>| | a Do | io: iB<br>1| Display graphic;  requesting i; Call :: | i i A +o | i ; 7 | | :: .ae<br>' | the correct [disk] . [ from][ the] [rm] i ' [CD_][ switch.] :: |i Wait for lid to open. tiii Wait for lid to close. ' | : : oe<br>' : H i DG @<br>' . { . c ae<br>\ : No Was a CO inserted? : : a<br>; : : i Yes : j Ae<br>: :<br>N 6: | i riot<br>Parse TOC at s2c00 : S developer : la | Read Table of Contents | > |<br>ct C i to $2C00. rf or 4<br>. : code.} : 1 : | itt<br>}|‘<br>i<br>' -— TOC is multi-session but isn't requested disk — EF<br>4iLoad ne de/data H 3<br>a<br>a |} TOC is requested multi-session disk. —— wee e/data as i ;<br>i | i : desired. ' 3<br>|:<br>’| — TOC is single-session (audio) <7 4 4<br>i beg Nl De you support audio? q<br>Yes 4<br>‘ | Allowand user begin to playback. select track, CONTINUE- } |:<br>' | |<br>ji : 1<br>**----- End of picture text -----**<br>


q 

Confidential Information aN Property ofAtari Corporation 

**==> picture [1 x 17] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


15 June, 1995 

© 1995 Atari Corp. 

Page 33 

Jaguar CD-ROM 

: | L q 4 '4 F I : 4 I 4 | ' | q j | ; ’ i : | | : q : 

| 

## Un rn 

Now that you’ve read all about the format of a track on a Jaguar CD, you are ready to master your first disc. The first thing you need is a computer system with a CD-Recordable Writer. Next, you need a CD Mastering software package and some idea of how to use it. Finally, you need data to put on your CD. There are many CD-Recorder/Players and CD mastering software packages to choose from today. are many CD-Recorder/Players and CD mastering software packages to choose from today. many CD-Recorder/Players and CD mastering software packages to choose from today. CD-Recorder/Players and CD mastering software packages to choose from today. and CD mastering software packages to choose from today. CD mastering software packages to choose from today. mastering software packages to choose from today. software packages to choose from today. packages to choose from today. to choose from today. choose from today. from today. today. At Atari we use a Phillips CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec we use a Phillips CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec use a Phillips CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec a Phillips CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec Phillips CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec CD Recorder connected to a 486-based PC machine using an Adaptec Recorder connected to a 486-based PC machine using an Adaptec connected to a 486-based PC machine using an Adaptec to a 486-based PC machine using an Adaptec a 486-based PC machine using an Adaptec 486-based PC machine using an Adaptec PC machine using an Adaptec machine using an Adaptec using an Adaptec Adaptec SCSI host adapter. host adapter. adapter. We have not tested other recorders or platforms, they may work just fine for have not tested other recorders or platforms, they may work just fine for not tested other recorders or platforms, they may work just fine for tested other recorders or platforms, they may work just fine for other recorders or platforms, they may work just fine for or platforms, they may work just fine for they may work just fine for may work just fine for work just fine for just fine for fine for creating Jaguar CDs, but require different configurations. Jaguar CDs, but require different configurations. CDs, but require different configurations. but require different configurations. require different configurations. different configurations. configurations. Note that some developers have reported that some developers have reported some developers have reported developers have reported have reported reported problems using some of the new generation of less-expensive CD recorders to create multi-session discs using some of the new generation of less-expensive CD recorders to create multi-session discs some of the new generation of less-expensive CD recorders to create multi-session discs of the new generation of less-expensive CD recorders to create multi-session discs the new generation of less-expensive CD recorders to create multi-session discs new generation of less-expensive CD recorders to create multi-session discs generation of less-expensive CD recorders to create multi-session discs of less-expensive CD recorders to create multi-session discs less-expensive CD recorders to create multi-session discs CD recorders to create multi-session discs recorders to create multi-session discs to create multi-session discs create multi-session discs multi-session discs discs (a Jaguar CD requirement). Jaguar CD requirement). CD requirement). requirement). 

] There are many CD-Recorder/Players and CD mastering software packages to choose from today. are many CD-Recorder/Players and CD mastering software packages to choose from today. many CD-Recorder/Players and CD mastering software packages to choose from today. CD-Recorder/Players and CD mastering software packages to choose from today. and CD mastering software packages to choose from today. CD mastering software packages to choose from today. mastering software packages to choose from today. software packages to choose from today. packages to choose from today. to choose from today. choose from today. from today. today. At Atari we use a Phillips CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec we use a Phillips CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec use a Phillips CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec a Phillips CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec Phillips CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec CDD-522 CD Recorder connected to a 486-based PC machine using an Adaptec CD Recorder connected to a 486-based PC machine using an Adaptec Recorder connected to a 486-based PC machine using an Adaptec connected to a 486-based PC machine using an Adaptec to a 486-based PC machine using an Adaptec a 486-based PC machine using an Adaptec 486-based PC machine using an Adaptec PC machine using an Adaptec machine using an Adaptec using an Adaptec Adaptec SCSI host adapter. host adapter. adapter. We have not tested other recorders or platforms, they may work just fine for have not tested other recorders or platforms, they may work just fine for not tested other recorders or platforms, they may work just fine for tested other recorders or platforms, they may work just fine for other recorders or platforms, they may work just fine for or platforms, they may work just fine for they may work just fine for may work just fine for work just fine for just fine for fine for creating Jaguar CDs, but require different configurations. Jaguar CDs, but require different configurations. CDs, but require different configurations. but require different configurations. require different configurations. different configurations. configurations. Note that some developers have reported that some developers have reported some developers have reported developers have reported have reported reported problems using some of the new generation of less-expensive CD recorders to create multi-session discs using some of the new generation of less-expensive CD recorders to create multi-session discs some of the new generation of less-expensive CD recorders to create multi-session discs of the new generation of less-expensive CD recorders to create multi-session discs the new generation of less-expensive CD recorders to create multi-session discs new generation of less-expensive CD recorders to create multi-session discs generation of less-expensive CD recorders to create multi-session discs of less-expensive CD recorders to create multi-session discs less-expensive CD recorders to create multi-session discs CD recorders to create multi-session discs recorders to create multi-session discs to create multi-session discs create multi-session discs multi-session discs discs (a Jaguar CD requirement). Jaguar CD requirement). CD requirement). requirement). The CD mastering software used most often at Atari is CeQuadrat’s WinOnCD Pro and Easy CD Pro y3.0 from InCat Systems. These packages both run under Microsoft Windows’ and allow you to make discs in different formats such as CD-DA (Digital Audio), ISO 9660 CD-ROM, and CD-XA. a Atari has not had any success creating Jaguar discs with the current version of Corel CD Creator. ft ma 7 requires Windows WAV sound files as input for creating tracks on an CD-DA disc and won’t work with y. 0 raw binary files. See section 7.1.1 for more information on this situation. | 74a rw veakon mastering sotiwaré won't Work With Binary Riles. 

A Jaguar CD looks very much like a standard audio CD, except that it is multisession. In most CD Mastering software programs, you specify “Audio” or “Raw” as the track type. Unfortunately, some CD mastering software packages, such as Corel CD Creator, do not have the ability to create a “Raw” track, and do not allow you to create an audio track from a raw binary data file. They require that the file must look like an AIFF or WAV audio file, even though the AIFF or WAVE file wrapper is removed prior to the data being written to the disc. Atari supplies a tool known as the Jaguar CD Track Creator that is used to create a track file for CD mastering from the Jaguar program and data files you specify (see section 7.2 for more information). However, the current version of this tool has no option to add an AIFF or WAV wrapper to the files it creates; this must be done as an additional step afterwards. The MKAIFF tool included in the Jaguar Developer’s Kit as part of the Jaguar Sound & Music tools can be used for this purpose right now, but this feature will be added to future versions of the Jaguar CD Track Creator program. An early approach to this problem was the FilmToAIFF option of the Jaguar Cinepak Utilities program. However, this only works with Jaguar Cinepak Film files, which isn’t the only thing you'll . b. Vvr needoption to no put longer onto a beJaguar used. CDFor disc. moreThere informati are **o** thern see the problems Cinepak For Jaguar as well, and we recommend chapter. that this 

5 Ip fact, we are currently mnning them under the beta release of Windows 95 (build 4.00.347). ol995 Atari Corp. Confidential Information FER Property ofAtari Corporation 

i 

15 June, 1995 

Page 34 

Jaguar CD-ROM 

7[s] 

The best solution is to select a CD Mastering package that doesn’t have any restrictions regarding what 7 type of files can be used as source data. See section 7.1 for information about the CD mastering package used by Atari. P | eeddd C—rt~—”—CN—C~COCUOSCzsCOtSRSCSON Note that some CD-ROM mastering software automatically inserts two seconds worth of silence (150 g blocks at 2352 bytes each = 352800 bytes) at the start of each audio track it creates. If your CD-ROM | @ mastering software does this, you should turn this feature off if possible. If you can’t turn it off, you | @ should consider getting. a new CD-ROM mastering* software package. Until you do that, you will have Reae to account for this extra data whenever reading data from the CD. | « 7.2dJaguarCDIrackCreator§=#= =... Cf In order to put your data into the proper format for creating a CD track, Atari supplies the Jaguar CD _ Track Creator program. This program runs under Microsoft Windowsé and allows you to create track = ? files suitable for mastering a Jaguar CD disc. Figure 7-A shows what the program looks like on screen Pe when you run it. fk : ee 1 JaguarCOTrack Creator ee | ; | en lr , | pre | neg (0 | j Figure 7-A — Jaguar CD Track Creator : The Jaguar CD Track Creator takes care of all the dirty work of merging all of your data files together and creating a track file with the proper header and tailer (as described in section 6.1). You provideita ] j list of files, and it combines them into a single large file, separated by a 64-byte partition sync marker of 4 : your choosing, complete with the proper track header and tailer information. If you specify track #0, it : also inserts the fields for the load address and size of your boot code. | 6 It has been tested with Windows 3.1, Windows For Workgroups v3.11, and Windows 95 beta 4.00.347. | ; 15 June, 1995 Confidential Information FER Property ofAtari Corporation © 1995 Atari Corp. 4 

Page 35 

) | , 

. | i | | | 

§) Jaguar CD-ROM § TheJaguar CD Track Creator takes two different categories of files as input. The first category consists of the files that contain your Jaguar program code, graphics, music, sound effects, and so forth. The second category is a batch file that lists all of the files from the first category that must be merged together into a CD track file. Clicking on the Browse button next to the Batch Filename edit box at the top of the window will bring up a standard Windows file selector dialog and allow you to select the name of your batch file that contains the list of files that wiil be used to construct your track, along with the partition sync marker codes that will be used for each file. Optionally, you may simply type in the filename. The batch file is an ASCII file that has one or more lines of information (separated by CR/LF) with the name of your data file, a Tab character (ASCII 9), and a 4 letter code that will be repeated 16 times to create a 64-byte partition sync marker that will delineate the beginning of this particular file within the track (see Figure 7-C and Figure 7-D). At runtime, your code will search for this 64-byte block and know that the desired data comes immediately afterwards. Section 7.2.1.2 shows a sample batch file. In this example, we are creating a file for our boot track. &. ae boot code is contained in the file GAJAGUAR\PROJECT\BOOTCODE.BIN, so the first line of the a batch file contains this filename followed by a <TAB> and then the 4 letter partition sync marker “CODE”. This is followed on the second line of the batch file by the file name for our title screen data, G:AJAGUAR\PROJECT\TITLESCR.RGB, which is followed by a <TAB> and the four letter partition sync marker “SCRN”. Finally, the last line of the batch file specifies the last file of the track, our music score which is contained in G:\JAGUAR\PROJECT\MUSIC.DAT, and a partition syne marker of “MUSC”. ASL LLL MALL LALA G:\ JAGUAR\PROJECT \BOOTCODE.BIN CODE Gs \ JAGUAR\PROJECT\TITLESCR.RGB SCRN G:\ JAGUAR\PROJECT \MUS IC.DAT MUSC Figure 7-B — Contents of sample batch file 

The Track Filename, Header Filename, Structure Filename, and Log Filename fields specify the filenames that will be used to create your output files. These fields are filled in automatically when you Browse your input Batch Filename, using derivatives of the batch filename. You can also type in the L ~ filenames or use the file selector by selecting the Browse button next to the desired field. 

: 

| 

© 1995 Atari Corp. 

Confidential Information JER Property ofAtari Corporation 

15 June, 1995 | 

i. 

**==> picture [606 x 719] intentionally omitted <==**

**----- Start of picture text -----**<br>
. _ Page 36 Jaguar CD-ROM ‘<br>ee  r—~—r—<=<R—SSrS—S—saia—SsSsiStS<Arrrrrrrsrsriaias i rLC ,<br>The Track Filename field specifies the name of the raw track file that will be created. This file is ready j<br>— to pass to your CD mastering software to create a CD track.? The file created will have the structure '<br>1 shown in Figure 7-C if you have specified track #0. '<br>| ! ATRIATRIATRIATRIATRIATRIATRIATRI | Track Header<br>| ATRIATRIATRIATRIATRIATRIATRIATRI /<br>ATARI APPROVED DATA HEADER ATRI! |<br>;<br>{ Address to load _antfa ~F Size; of Boot Code: 4 :<br>|<br>BootCode =‘! 90004000 «= | ~=—— 00008000—Ss¥ |<br>i|<br>-——- Beot Code ia<br>Boot Code (Max 64k) ;<br>nl Bie talk | CODECODECODECODECODECODECODECODE | characterPartition Marker sequence for 2nd repeated file in boo16 t  trackimes) ((4 ; |<br>may follow the {| CODECODECODECODECODECODECODECODE |<br>: boot code, but Sennen EEE Program Data/Code taken from 2nd file |<br>j the boot code | 7 — specified for track (Size must be long- :<br>: is responsible * Program Data or Code (about 1Mb) / aligned} §<br>for loading it. Le ;<br>| ATARI APPROVED DATA TAILER ATRI! | -—~ Track Tailer 4<br>| ATRIATRIATRIATRIATRIATRIATRIATRE<br>| | ATRIATRIATRIATRIATRIATRIATRIATRI | | =<br>Figure 7-C — CD Boot Track Structure iz<br>| Track #0 is handled specially because of the requirements of the boot code. First note that the boot code §<br>| block does not have a partition sync marker in front of it (such as the “CODE” marker preceeding the E<br>{ next program data/code block). This is because the boot code is loaded for you automatically by the :<br>system, and must always be at a specific offset from the track header anyway, so there’s really no need :<br>for your program to have a specific partition marker for this particular data. |<br>If you have specified a track other than #0, the track file structure will be as shown in Figure 7-D. The ,<br>| main difference is that there are no fields for the load address and code size of your boot code and that gq<br>| the first file is not treated specially, so it gets the partition sync marker specified in your batch file. |<br>While it’s true that the partition sync marker is not absolutely required for the first chunkof data in a<br>track, because you could use the track header instead, it is included because it makes it easier for your a<br>program to deal with all of your code and data files in the same way, regardless of their position within<br>a track. :<br>7 See section 7.1.1 for additional information which may be relevant. 1 :<br>1 15 June, 1995 Confidential Information FR Property ofAtari Corporation © 1995 Atari Corp. q<br>**----- End of picture text -----**<br>


: oe : , : i | i | a 4 ' 

**==> picture [540 x 265] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 37<br>@ Jaguar CD-ROM<br>- | ATRIATRIATRIATRIATRIATRIATRIATRI | /——~— Track Header<br>aP| | ATRIATRIATRIATRIATRIATRIATRIATRIATARI APPROVED DATA HEADER ATRI!  [/<br>EEE nanan Partition Marker for 1st file (4 character sequence<br>a. | CODECODECODECODECODECODECODECODE |/ repeated 16 times)<br>| CODECODECODECODECODECODECODECODE<br>: oo4 Program Data/Code taken from 1st file specified<br>i<br>: ] | Program Data or Code (about 1Mb) a for track (Size must be long-aligned)<br>Pjq { i Partition Marker for 2nd file (4 character sequence<br>4 | CGAMEGAMEGAMEGAMEGAMEGAMEGAMEGAME A repeated 16 times) .<br>| GAMEGAMEGAMEGAMEGAMEGAMEGAMEGAME<br>i _— Program Data/Code taken from 2nd file specified<br>: | More Program Data/Code 4 for track (Size must be long-aligned)<br>!<br>i<br>| ATARI APPROVED DATA TAILER ATRI! ——— Track Tailer<br>| ATRIATRIATRIATRIATRIATRIATRIATRI 4<br>! ATRIATRIATRIATRIATRIATRIATRIATRI !<br>{<br>**----- End of picture text -----**<br>


Figure 7-D — CD Track File Structure 

The Header Filename field defines the name of a file that will be created by the Jaguar CD Track Creator with definitions corresponding to the order of the files within the track. If the C Language Output option of the Options menu is selected, the file created will be a C language header file. See Figure 7-E for a sample C language header file created from the sample batch file in section 7.2.1.2. 

**==> picture [328 x 67] intentionally omitted <==**

**----- Start of picture text -----**<br>
#define FILE_ G: \JAGUAR\PROJECT \ BOOTCODE 0<br>#define FILE_ G: \JAGUAR\PROJECT\TITLESCR i<br>#define FILE_ G: \ JAGUAR\PROJECT\MUSIC 2<br>Figure 7-E — Sample C Language Header File<br>**----- End of picture text -----**<br>


If the Assembly Output option of the Options menu is selected instead, the file created will be a Madmac , assembly language include file. See Figure 7-E for a sample Madmac include file created from the sample batch file in section 7.2.1.2. 

Ll, 

A. 

FILE_ G: JAGUAR\ PROJECT\BOOTCODE equ 0 FILE_ G: \JAGUAR\PROJECT\TITLESCR equ i FILE_ G: \ JAGUAR\PROJECT\MUSIC equ 2 Figure 7-F —- Sample Madmac Assembly Language Include File | 

: 

**==> picture [2 x 24] intentionally omitted <==**

**----- Start of picture text -----**<br>
i<br>**----- End of picture text -----**<br>


© 1995 Atari Corp. 

Confidential Information “FER Property ofAtari Corporation 

15 June, 1995 

| 4 Page 38 38 JaguarCD-ROM 722.3 Structure Filename (.Cor"S})) The Structure Filename Structure Filename Filename field defines the name of a source code the name of a source code name of a source code of a source code a source code code file that will be created by that will be created by will be created by be created by created by by the Jaguar Jaguar F CD Track Creator with Track Creator with Creator with with an array of structures containing of structures containing structures containing containing information about the the files placed placed into the : a track file. There will be one element be one element one element element in the array for each each file placed into the track file. In “C”, the 4 structure is defined defined as: 2 typedef struct { E int track; a long block_offset; . long length; a long marker; | #8 } FILEDATA; - The track field indicates the track number where the file is located. The block_offset field indicates the {| 4 offset, in CD blocks, from the beginning of the track to where the file data is located. The length field , oF specifies the length of the file data in bytes. The marker field specifies the 4 byte partition sync marker ie used for this file. ; 8 If the C Language Output option of the Options menu the C Language Output option of the Options menu C Language Output option of the Options menu Language Output option of the Options menu Output option of the Options menu option of the Options menu of the Options menu the Options menu Options menu menu is selected, selected, the file created will will be a C language C language language : source file containing containing an array of FILEDATA structures. of FILEDATA structures. FILEDATA structures. structures. See Figure 7-G for 7-G for for a sample C language sample C language C language language f o8 source file created from the sample batch batch file in section 7.2.1.2. _ FILEDATA fd[] = { { 0x01, 0x00000000, 0x0004BA04, 0x57494E47 }, /* FILE_ G:\JAGUAR\PROJECT\BOOTCODE CODE */ { 0x01, 0x00000083, 0x0000EA04, 0x46494C32 }, /* FILE_ G:\JAGUAR\PROJECT\TITLESCR SCRN */ 1 1 { 0x01, 0x0000009D, 0x0000009C, 0x3344534F } /* FILE_ G:\JAGUAR\PROJECT\MUSIC MUSC */ { Figure 7-G — Sample C Language 7-G — Sample C Language — Sample C Language Sample C Language C Language Language Structure File | | ] If the Assembly Output option of the Options menu the Assembly Output option of the Options menu Assembly Output option of the Options menu Output option of the Options menu option of the Options menu of the Options menu the Options menu Options menu menu is selected instead, selected instead, instead, the file created will be created will be will be be a Madmac Madmac ] assembly language source file. See Figure Figure 7-E for for a sample Madmac include sample Madmac include Madmac include include file created from created from from the ; sample batch batch file in section 7.2.1.2. j fd:: dc.w $01 4 dce.1 $60000000,$0004BA04,$57494E47 ; FILE_ G:\JAGUAR\PROJECT\BOOTCODE CODE j de.w $01 de.1 $00000083,$0000EA04,$46494C32 ; FILE_ G:\JAGUAR\PROJECT\TITLESCR SCRN 4 dce.w $01 : de.1 $0000009D,$0000009C,$3344534F ; FILE_ G:\JAGUAR\PROJECT\MUSIC MUSC : Figure 7-H — Sample Madmac Assembly — Sample Madmac Assembly Sample Madmac Assembly Madmac Assembly Assembly Language Structure File Pe eee eeldllC—~<“‘OCCOCOCOCOiwitCUMRldllC—~<“‘OCCOCOCOCOiwitCUMR , 4 155 |The Log FilenameThe Log Filename Log Filename Filename field specifies the filename filename of a file file that will be created will be created be created created as a log of the the entire track creation process. This file contains basically basically the same information about each file used to create the track as what what is shown shown in Figure Figure 7-G, except in in a more human-readable more human-readable human-readable text format. format. j 15 June, 1995 1995 Confidential Information Information TR Property ofAtari Corporation ofAtari CorporationAtari Corporation Corporation © 1995 1995 Atari Corp. Corp. 4 

Page 38 38 

| The Structure Filename Structure Filename Filename field defines the name of a source code the name of a source code name of a source code of a source code a source code code file that will be created by that will be created by will be created by be created by created by by the Jaguar Jaguar _ CD Track Creator with Track Creator with Creator with with an array of structures containing of structures containing structures containing containing information about the the files placed placed into the | track file. There will be one element be one element one element element in the array for each each file placed into the track file. In “C”, the structure is defined defined as: | typedef struct 

| If the C Language Output option of the Options menu the C Language Output option of the Options menu C Language Output option of the Options menu Language Output option of the Options menu Output option of the Options menu option of the Options menu of the Options menu the Options menu Options menu menu is selected, selected, the file created will will be a C language C language language : source file containing containing an array of FILEDATA structures. of FILEDATA structures. FILEDATA structures. structures. See Figure 7-G for 7-G for for a sample C language sample C language C language language source file created from the sample batch batch file in section 7.2.1.2. : FILEDATA fd[] = { 7 { 0x01, 0x00000000, 0x0004BA04, 0x57494E47 }, /* FILE_ G:\JAGUAR\PROJECT\BOOTCODE CODE */ : { 0x01, 0x00000083, 0x0000EA04, 0x46494C32 }, /* FILE_ G:\JAGUAR\PROJECT\TITLESCR SCRN */ 1 { 0x01, 0x0000009D, 0x0000009C, 0x3344534F } /* FILE_ G:\JAGUAR\PROJECT\MUSIC MUSC */ ‘ Figure 7-G — Sample C Language 7-G — Sample C Language — Sample C Language Sample C Language C Language Language Structure File q If the Assembly Output option of the Options menu the Assembly Output option of the Options menu Assembly Output option of the Options menu Output option of the Options menu option of the Options menu of the Options menu the Options menu Options menu menu is selected instead, selected instead, instead, the file created will be created will be will be be a Madmac Madmac ; assembly language source file. See Figure Figure 7-E for for a sample Madmac include sample Madmac include Madmac include include file created from created from from the sample batch batch file in section 7.2.1.2. fd:: dc.w $01 dce.1 $60000000,$0004BA04,$57494E47 ; FILE_ G:\JAGUAR\PROJECT\BOOTCODE CODE 1 de.w $01 f de.1 $00000083,$0000EA04,$46494C32 ; FILE_ G:\JAGUAR\PROJECT\TITLESCR SCRN : dce.w $01 de.1 $0000009D,$0000009C,$3344534F ; FILE_ G:\JAGUAR\PROJECT\MUSIC MUSC Figure 7-H — Sample Madmac Assembly — Sample Madmac Assembly Sample Madmac Assembly Madmac Assembly Assembly Language Structure File Pe eee eeldllC—~<“‘OCCOCOCOCOiwitCUMRldllC—~<“‘OCCOCOCOCOiwitCUMR , 155 |The Log FilenameThe Log Filename Log Filename Filename field specifies the filename filename of a file file that will be created will be created be created created as a log of the the entire track creation process. This file contains basically basically the same information about each file used to create the : track as what what is shown shown in Figure Figure 7-G, except in in a more human-readable more human-readable human-readable text format. format. j q 15 June, 1995 1995 Confidential Information Information TR Property ofAtari Corporation ofAtari CorporationAtari Corporation Corporation © 1995 1995 Atari Corp. Corp. 4 

## 722.3 Structure Filename (.Cor"S})) 

{ Jaguar CD-ROM Page 39 aa cL rrr—“—™—s—CC—”—C”CCOC;siNCSOSUCUsCOCdidsCCCCts 7 | The bottom of the main screen shows a number of options for the track being created. . | ee ,,rrr~—rs—wCiCS aCRCCSCSCQCS@RSCNCCsCisCi;szC®”tC ‘Bs This field specifies the track number of the track being created. The track number is placed into the | track header and tailer information (see section 6.1). : If you specify track #0, the program recognizes the first file in your batch list as being your program’s @ boot code. The track file created follows the format shown in Figure 7-C. Also, the Boot Code Load/ExecAddress and Boot Code Size fields become visible. 

' : : | . 4 

j 

7 i | H 

: yess EadaitrackPadding 

**==> picture [316 x 25] intentionally omitted <==**

**----- Start of picture text -----**<br>
rrr—“—™—s—CC—”—C”CCOC;siNCSOSUCUsCOCdidsCCCCts<br>**----- End of picture text -----**<br>


For any track number other than zero, the track file created follows the format shown in Figure 7-D. The Boot Code Load/Exec Address and Boot Code Size fields are removed from the screen. 

## 70S Best Code Wadiexec Adress 

. 

, 

When you have specified track #0, this field allows you to specify ihe desired load address for the code in the first file listed in your batch. 

“Wp jg, When a track other than #0 is specified, this field is not available. 

This field allows you to specify the desired amount of extra padding information that will be added at ' the end of the track. | Le rt—rtia_ONrsCtisC@CiC ia‘NCOWwiCNRSCNCSCCSCCSCs«CNCtiészs ia‘NCOWwiCNRSCNCSCCSCCSCs«CNCtiészs 

## rt—rtia_ONrsCtisC@CiC ia‘NCOWwiCNRSCNCSCCSCCSCs«CNCtiészs ia‘NCOWwiCNRSCNCSCCSCCSCs«CNCtiészs 

When you have specified track #0, this field allows you to specify the length of the code and data in the boot code contained in the first file listed in your batch. This value is placed into the boot track header that the program creates for the tile (see section 6.1). 

; 

When a track other than #0 is specified, this field is not available. 

## ol ,,rmmrtrtr~—~COCOCOWCOUCCCCCCCCCtCUt 

| 

The menu bar of the Jaguar CD Track Creator allows you to set options that contro] how the program operates, begin processinga track file, or quit from the program. 

f= 

i © 1995 Atari Corp. Confidential Information “7O® Property ofAtari Corporation 15 June, 1995 

Page 40 

Jaguar CD-ROM 

rrrtr—~<C Srrsssaia<a‘NCRrSCiCNCis‘CNi‘(CO‘(SNOOOCCCONNCSCNCSCszCSCSCNsCrCSCSCaiakzaAXZ 

: | 

= 

{ j 

: : 

: 1 | 4 | | | F 

4 j : 

: ‘ 1 : 4 

| ' ' 

: 

**e** 

Figure 7-I shows the program’s File menu. 

Figure 7-1 — The File Menu 

The Do Batch item in the File menu causes the Jaguar CD Track Creator to start processing the specified batch file (see section 7.2.1.1) and create the desired output files. A status window will be shown to display the ongoing status of the track creation and information about any errors that may occur. 

The Exit item in the File menu causes the Jaguar CD Track Creator program to quit. 

Figure 7-J shows the program’s Options menu. 

**==> picture [160 x 75] intentionally omitted <==**

**----- Start of picture text -----**<br>
0 _ daguar CO)<br>eet! Output Track Data i<br>ae Output Header & Structure File<br>a 4 © Language Output :<br>**----- End of picture text -----**<br>


Figure 7-3 — The Options Menu 

When checked, the Output Track Data item in the Options menu causes the Jaguar CD Track Creator to merge the source files specified by your batch file into a new track data file suitable for CD mastering, as described in section 7.2.2.1. When unchecked, the track data file is not created. 

When checked, the Output Header & Structure Files item in the Options menu causes the Jaguar CD Track Creator to create the files described in sections 7.2.2.2 and 7.2.2.3. When the menu item is unchecked, these files are not created. 

When checked, the Output Log File item in the Options menu causes the Jaguar CD Track Creator to create the log file described in section 7.2.2.4. When the menu item is unchecked, this file is not created. 

The status of the C Language Output and Assembly Output menu items determine what file format is used to create the files described in sections 7.2.2.2 and 7.2.2.3. Only one item can be checked at a time. 

**==> picture [11 x 15] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


§ 

15 June, 1995 

Confidential Information “7% Property of Atari Corporation 

©1995 Atari Corp. 

