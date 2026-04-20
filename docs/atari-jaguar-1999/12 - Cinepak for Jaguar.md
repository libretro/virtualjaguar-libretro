7 { Cinepak ForJaguar 

Page I 

j &' im - 

## bampCinepakForJaguar 

{ | This documents describes Cinepak forJaguar, a combination of utilities and code that hasbeen. | — developed to enable creation of high-quality video material which can be played back from the Jaguar = CD-ROM. Playback rates of 30 frames per second are possible even with full-screen (320x200), 16-bit @ per pixel images. In fact, even higher resolutions and/or frame rates are possible provided the overall data rate is reasonable. | | The Cinepak For Jaguar package is based upon Radius’ proprietary Cinepak video compression a technology!, which was specifically developed for this type of application; it consists of the following main elements: 1. Interface definition and linkable object code for the Cinepak decompressor. fF 2. Definition of a file format which interleaves audio and video in a manner suitable for playback on Jaguar, together with sample playback code which illustrates how to manage the periodic j access to the CD-ROM and maintain synchronization between audio and video. im 3. A utility to convert Cinepak-encoded QuickTime movies to the Jaguar Cinepak film format and perform necessary manipulations prior to recording on CD-ROM. 4. Three sample Jaguar films on CD-ROM. 

**==> picture [3 x 3] intentionally omitted <==**

**----- Start of picture text -----**<br>
1<br>**----- End of picture text -----**<br>


The Cinepak decompressor and the interface to it are discussed in the Cinepak Decompressor section. The Jaguar film format is discussed in the Jaguar Film Format section. The details of the sample player code are described in the Sample Playback Code section. The use of the film conversion utility is discussed in the Jaguar Cinepak Utility For Macintosh section. The content of a sample Jaguar CD-ROM containing Cinepak films is briefly described in the Sample Jaguar Films section. The layout of film data on a CD-ROM is discussed in the Using A Jaguar Cinepak Film With CD-ROM section. 

Decoding of the Cinepak bitstream and writing the decompressed pixel data to the frame buffer are handled almost entirely by the GPU in the Jaguar system. The 68000 plays a. minor role in parsing the bitstream and setting up pointers to various data structures. _ The Cinepak decompressor code consists of two object modules, codec.o and gpucode.og, for the & 0 68000 and the GPU, respectively. in addition, several flags must be defined, storage for auxiliary data i. must be reserved and the 68000 interrupt service routine must be used to coordinate bus activity . between 68000 and GPU. | 1 Cinepak was originally developed by SuperMac Technology, which merged with Radius, Inc. in 1994. i © 1995 Radius Inc. & Atari Corp. Confidential FAR Information 16 June, 1995 

16 June, 1995 

Page 2 

Cinepak For Jaguar 

g& . f F 4 = ; 4 | 7 P 4 . 4 | 4 - q 3 _ | @ = ; « | = 4 B E x fF fF OS ] ae 4 s | ae 4 . 

| | 

a j t 

| | | i ] ; | | 

| | | z 

> . 4 ‘ q § 

: | 

| 

In this section, we define the interface to the two code modules and briefly describe the operation of the flags. For an example of how these elements are incorporated in playing a Jaguar film, see the Sample Playback Code section. 

The codec.o module consists of approximately 700 bytes of 68000 code. There are three user callable functions, CheckKeyFrame, PreDecompress, and Decompress. The interfaces to these routines is specified below. 

## All the routines preserve all 68000 registers. 

All parameters used by these routines are passed on the stack. The return value is also returned on the stack. Cleaning up the stack upon return from any of these three routines is the responsibility of the calling program. , 

This routine is called to determine whether or not the current frame is a key frame.” 

**==> picture [359 x 69] intentionally omitted <==**

**----- Start of picture text -----**<br>
Stack Offset Size Description<br>4(a7) Return value. Must be set to 0 prior to entry. Will be<br>set to 1 upon exit if key frame is detected.<br>Address of start of frame.<br>Table 2.1 — 68000 stack setup before call to CheckKeyFrame.<br>**----- End of picture text -----**<br>


212 PrebecompressiyOE This routine is called to set up the tables needed to draw pixels on the display. = 

**==> picture [437 x 133] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||
|---|---|---|---|---|---|---|---|
|Stack|Offset|Size|Description|
|10(a7)|4|Return value.|Value|prior to entry|is|not important.|
|O =|returned|upon|successful|completion|
|non-zero|=|Error|occurred.|
|Y|6(a7)|~—s«| ~3—|4s Address of $3000 byte|auxiliary Cinepak data|buffer|(see section 2.4)|
|||2(a7)|~~|||4|sd|Address of start of frame|in Cinepak bitstream.|
|(a7)|Flag which|indicates video data type:|
|0 = Cinepak compressed-RGB|format|
|1|=|Atari CRY format|or expanded|RGB|
|Table|2.2 — 68000 stack setup|before|call to PreDecompress.|

**----- End of picture text -----**<br>


2 Cinepak generally relies upon frame differencing to compress video data; however, the encoder periodically inserts a key frame into the data stream. Such a frame can be decompressed without reference to any frames which precede it. A key frame may either occur naturally as a result of an abrupt change of scene, or can be injected into the data stream at a prescribed rate to aid random access or resynchronization with audio. 16 June, 1995 Property of “7@® of “7@® “7@® Atari Corporation Corporation © 1995 Radius, Inc. & Atari 1995 Radius, Inc. & Atari Radius, Inc. & Atari Inc. & Atari & Atari Atari Corp. 

Property of “7@® of “7@® “7@® Atari Corporation Corporation © 1995 Radius, Inc. & Atari 1995 Radius, Inc. & Atari Radius, Inc. & Atari Inc. & Atari & Atari Atari Corp. 

7 Stack Offset Size Description 16(a7) 4 Value prior to entry is not important. Returns: 0 = successful completion : 3 non-zero = error j |t2(a7)___|4 __ | Address of $3000 byte auxiliary Cinepak data buffer (see section 2.4) Address of start of frame in bitstream. Frame buffer address of top left corner of image. | [ B(a7y) [| 2 __| Bytes per row in frame buffer | Table 2.3 — 68000 stack setup before call to Decompress. { The latest version of Cinepak for Jaguar supports phrase interleaving for faster double or triple _ buffering schemes. If zero is passed as the phrase interleave factor, Cinepak will perform normally, j writing its data contiguously in memory. A phrase interleaving factor of one will cause one phrase to be } — skipped for every one written. A phrase interleaving factor of two will cause two phrases to be skipped | for every one written, and so on. This is done in a way that is compatible with similar features in the | Object Processor and Blitter. By interleaving the buffers which must be blitted back and forth, the . frequency of DRAM page faults drops signifigantly, increasing the available bus bandwidth. | This routine shuts down the Cinepak decompression code running in the GPU at the end of the current | frame. It takes no parameters. To restart Cinepak you must start from the beginning again. | ee ,rr,rrrrtr~—S—~<(i«w*”wsO~w™OCOWCWCSCSCOQUCOC(OCidszOisizC | The gpucode.og module consists of approximately 2200 bytes of relocatable GPU code. The labels DECOMP_S and DECOMP_E defined in the gpucode.og module are used to locate the beginning and ; end of the Cinepak GPU code so that it may be copied to the GPU’s internal RAM for execution. 

> | After the code has been copied over to internal GPU RAM, the GPU is started. The GPU code detects } the address at which it has been loaded by looking at the GPUOffset variable and then patches all | instructions and table values which are position-dependent. It then notifies the 68000 via the | GPU_READY flag (see Section 2.3) that it is ready to perform decompression tasks. 

**==> picture [516 x 41] intentionally omitted <==**

**----- Start of picture text -----**<br>
4 Cinepak For Jaguar Page 3<br>LS r—“(itw”r”rC—mrmCwr—CO~—~C‘CC;éaC.®CtCtCW<br>**----- End of picture text -----**<br>


## This is the routine that actually displays the pixels. 

| The Cinepak GPU code may be run from either register bank with some limitations. By default, Cinepak assumes it will run from Bank #0 and will set R31 to point to ten longwords of interrupt stack B. that it provides. As Cinepak requires registers RO-R27 (and R28-R31 are reserved for interrupts), if you run Cinepak in Bank #0, any interrupt code must preserve all Bank #0 registers. To run Cinepak in Bank | #1 you must perform the following steps: 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


ee Load the Cinepak GPU code into GPU RAM. 2. Load a small startup stub somewhere else in GPU RAM. © 1995 Radius Inc. & Atari Corp. Confidential PER Information 

16 June, 1995 

Page 4 

Cinepak ForJaguar 4 } 8 

' ‘ 

> 5. Using the information information in GPU_OFFSET, jump GPU_OFFSET, jump jump to the head of the Cinepak code. head of the Cinepak code. of the Cinepak code. the Cinepak code. Cinepak code. code. || a When these above steps are performed, Cinepak will harmlessly change R31 in Bank #1 and continue to j run from Bank #1. Interrupts (which must run in Bank #0) may then use RO-R27 of Bank #0 without fy saving them. gg | Once the system has been initialized, all GPU functions are invoked from within the routines in the j codec.o module; no attempt should ever be made by your code to directly access the GPU : decompression functions. = While the GPU is executing decompression functions, the 68000 is halted (a stop #$2000 instruction is | | ’ executed within codec.o). When the GPU finishes its task, it interrupts the 68000; the interrupt service [— ‘ routine sets a semaphore which is polled within codec.o to reawaken the 68000. This mechanism q ' provides a 5-10% improvement in performance by minimizing GPU/68000 bus contention, and should | a not be circumvented. = q The sample player program includesa utility subroutine named LoadGPU in the util.s file. This routine | 1 i copies the GPU code from gpucode.og into GPU memory (see section 5.5). The load address is offset | 4 | fromCINEPAK.INC the base ofincludeGPU memoryfile. This by theoffset constantis necessary value GPU_OFFSET,to avoid collisiondefinedwith the in GPU the application-specificinterrupt vectors. | ]= | Sample code for the GPU startup sequence appears in the module player.s (see Section 5), in the | i vicinity of label WaitGPU. = i Storage for two flag variables must be declared within the DRAM address space. These are defined in : 2 ' Table 2.4. The initial values of these flags are not important. | @ : Flag Size Description a £ | semaphore Cleared within codec.o upon invocation of GPU task. Set by interrupt service e GPUOffset 4 routineRelocation uponoffset completionof GPU ofcode.GPU task.Before you execute the GPU code from Fo| « ' gpucode.og, this variable must be set to the offset from the beginning of GPU zz internal RAM (G_RAM) where the GPU code has been loaded. 2 The sample player program sets this to the constant value GPU_OFFSET at 7 time GPU code is loaded. j Table 2.4 — Flags declared in DRAM address space. ° An additional flag is declared (internal to gpucode.og) within GPU internal address space and must be = accessed by the 68000, as defined in Table 2.5. 7 

| 4 

3. Have the startup stub provide interrupt stack space and store the location in R31. 4. Switch to the second register bank. 5. Using the information information in GPU_OFFSET, jump GPU_OFFSET, jump jump to the head of the Cinepak code. head of the Cinepak code. of the Cinepak code. the Cinepak code. Cinepak code. code. 

© 1995 Radius, Inc. & Atari Corp. 

16 June, 1995 

Property of P@® Atari Corporation 

**==> picture [517 x 249] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||
|---|---|---|---|---|
|;|Cinepak ForJaguar|Page 5|
|Flag|Size|Description|
|GPU_READY|4|Cleared by 68000 prior to GPU startup.|Set by GPU when|
|initialization|procedure|has|been|completed.|
|:|
|1|To account for GPU code relocation, you must add the value of|
|q|GPUOffset to this symbol|in order to get the correct address.|(For an|
|1|example, see the code immediately|before the WaitForGPU label in|
|q|the sample program's player.s|source|file.)|
|||Table 2.5|— Flag declared in GPU|internal address space.|
|ma|
|||The PreCompress and Decompress routines require storage space in DRAM for auxiliary|Gata|
|L|structures, distinct from the Cinepak data bitstream.|This puffer must be $3000 bytes in length and|
|F|reside on a long-word boundary.|Your Cinepak playback application must pass the address of a suitable|
|.|buffer each time these functions are called.|(Note that the same buffer may be used for both functions.)|

**----- End of picture text -----**<br>


The Cinepak bitstream is simply a source for a continuous stream of video; the bitstream contains no F information pertaining to time, frame rate, or synchronization of video with other media such as audio. | To provide a time reference and synchronization among different media, the Cinepak bitstream must be | embedded in some higher-level structure that is aware of time and the existence of media other than | yideo. The Jaguar film format has been devised to meet these requirements. 

- j The Jaguar film format exists in two flavors: , J) Smooth. This format is useful for playback of multiple low-resolution (for example, less than 160x100) films or a single film of higher resolution, provided in either case that the duration is 

- | very short (usually 3 or 4 seconds maximum). In this case, ali the film data could be stored and j played from ROM, or could be retrieved from the CD-ROM ina single brief access and loaded ] into DRAM for playing. =) Chunky. This format is designed for playback of longer films that cannot fit in DRAM all at once. Here, periodic access to the CD-ROM is required on a continuing basis, so some 

- : mechanism must be incorporated in the film structure for locating and identifying the film data : that are needed for display at a particular time. | The film formats are described in detail in the sections 3.1 and 3.2. 

- pAtari’s existing sample Cinepak player code only knows how to play Chunky-format Cinepak Films. . Ifyour program needs to play smooth films, the changes would needed would be minor. 

© 1995 Radius Inc. & Atari Corp. 

Confidential FER Information 

16 June, 1995 

Page 6 ; Cinepak For For Jaguar LoDlDdUDL”D”™L™rrrt~—r—.—CL.CWCUCUSCisCsSCisCistC Table 3.1 defines the structure of a smooth film at the highest level. 

Cinepak For For Jaguar 

ris‘iCCO'iUWW” | & F . |; 4 | fi j | & : q q 7 | 3 4 , 4 | ‘ _ r , 4 = 4 4 || @@ | =a | 7 E j ‘ . * j 7 

| | | | : / \ i i | | ; : | 

| 

**==> picture [437 x 94] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||
|---|---|---|---|---|---|---|---|---|---|
|Field|Size|Description|
|Frame Header|16|Global film|header|
|"FrameDescriptionAudio Description||||2020|_|| FrameAudio data size format and compressiondescription|type|
|Sample|Table|16 +|(n*|16) ||Index to film|samples which follow;|_n|is number|of samples|
|Film Samples|Audio blocks and video frames|
|Table 3.1 — Smooth film format.|

**----- End of picture text -----**<br>


The frame header identifies the ensuing data as a Jaguar film and gives the offset to the start of the film data: its structure is defined in Table 2.1. The frame description provides information about pixel resolution and the format of the compressed video; Table 3.3 describes this structure. The audio description contains information about the format of any audio data included in the film. This is discussed in Table 3.5. (Note that some older Jaguar Cinepak films may not include this field.) The sample table provides a time-based index to the ensuing audio and video data which form the actual content of the film; Table 3.7 defines the structure of the sample table. 

At the film sample level, the data stream is interleaved blocks of audio and video sample information; the time field of the sample record holds the key to the multiplexing scheme (see discussion following Table 3.8). The audio data itself uses the format defined by the film’s audio description atom. The video data stream is in the proprietary Cinepak format, which is interpreted by the Cinepak decompressor. 

## Loe eC 

lrrrrrtr—~—“itsOOCOCiCzSCdstszsSCsCisCOwiWCCCNCNCOiéCONOCOwsC®CC(CCiCwzé.C_CN = 

**==> picture [434 x 85] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||
|---|---|---|
|Field|Size|Description|
|||[_Header__|]|
|rAtomSize {44|__|__||SizeHuman of film readableheader, tag:plus FILM’ ensuing frame description and sample table|
|_|Table 3.2 — Structure|of frame header.|

**----- End of picture text -----**<br>


The frame header is a 16-byte structure comprised of four long-word fields. The Header field is a human-readable tag, ‘FILM’, which identifies the ensuing global data structure as a Jaguar film. The AtomSize field gives the offset in bytes from the start of the header to the beginning of the audio and video data records; this offset includes the size of the frame header itself, plus the sizes of the ensuing frame description and sample table structures. The Version and Reserved fields are not currently used; developers are free to use these as they wish. 

**==> picture [2 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


16 June, 1995 

Property of“JER Atari Corporation © 1995 Radius, Inc. & Atari Corp. | 

’ Cinepak For Jaguar 

Page7 

**==> picture [527 x 549] intentionally omitted <==**

**----- Start of picture text -----**<br>
Field Size Description<br>] |Header| _4 ___|Human readable tag: ‘FDSC'<br>: |—AtomSize_|4 _ | Size of frame description atom (=20)<br>j CType 4 Human readable compression type:<br>: ‘cvid' = Cinepak compressed-RGB format<br>j '$CRY' = Expanded Atari CRY format<br>‘$RGB = Expanded RGB format<br>j -—Wwiath[Height —[_| _ 4 _ _|_ Number of  dp xe s i sp l ay per lines line<br>j Table 3.3 — Structure of frame description atom.<br>1 The frame description is a 20-byte structure comprised of five long-word fields. The Header field is a<br>human-readable tag, ‘'FDSC', which identifies the structure as a frame description. The AtomSize field<br>| contains the size of the frame description atom (i.e. 20 bytes). The CType field contains a human-<br>} readable code which identifies the format of the compressed video; two modes are recognized:<br>] Value Meaning :<br>j | [‘evid']<br>‘'SCRY' _| CinepakCinepak compressed-RGBExpanded Atari CRY format format<br>. ‘$RGB Cinepak Expanded RGB format<br>Table 3.4 — Frame Description Atom CType values<br>| The Height and Width fields specify the vertical and horizontal resolution of the video in pixels.<br>ec lt‘ :COC;S]; zi‘i‘i##W’XYCX’ON’NYN’CUC#iét«<br>] Field Size Description<br>|Header| 4 __| Human readable tag: ‘ADSC’<br>Size of audio description atom (=20)<br>j AudioData Audio Data Description<br>{ .SCLK [4 __|SCLK timer value for audio playback<br>‘ Audiobritt | [4][|] [Drift] [rate][ value][ used] [adjust][ audio][ sample] [rate]<br>: Table 3.5 — Structure of audio description atom.<br>| The audio description atom is a 20-byte structure that defines the format of the audio data contained in<br>| the Cinepak film so that it may be played back properly. The Header field is a human-readable tag<br>|  ‘ADSC’ which identifies the structure as an audio description atom. The AzomSize field specifies the<br>size of the structure (20 bytes). z<br>**----- End of picture text -----**<br>


The AudioData field is a bitmapped flag that defines the data format of the audio, i.e. mono or stereo, i — compressed or non-compressed, 8-bit samples or 16-bit samples, and so forth. See Table 3.6 for a definition of the meanings of each bit. Note that the proper utilization of this information is the responsiblity of the Cinepak player application. 

I ©1995 Radius Inc. & Atari Corp. Confidential PER Information 16 June, 1995 

| Page 8 8 Cinepak For J tt 1 Bits Meaning PO |0=Mono,1=Stereo | 2-7 | Audio Compression Audio Compression Compression Type: 0 = uncompressed 1 = n® compression compression other values are reserved j Two's Complement audio flag Complement audio flag audio flag flag | Table 3.6 3.6 — Audio description flag Audio description flag description flag flag bits ' The SCLK field contains the value which should be used with the Jaguar’s SCLK timer to set the DSP | SCLKinterrupt field frequency will be forset to audio-1 ($FFFFFFFF)?. playback. In Jaguar Cinepak films which have no audio information, the | The AudioDrift field specifies a 32-bit value that can be used by the player program’s audio playback AudioDrift field specifies a 32-bit value that can be used by the player program’s audio playback field specifies a 32-bit value that can be used by the player program’s audio playback specifies a 32-bit value that can be used by the player program’s audio playback a 32-bit value that can be used by the player program’s audio playback value that can be used by the player program’s audio playback that can be used by the player program’s audio playback can be used by the player program’s audio playback be used by the player program’s audio playback used by the player program’s audio playback by the player program’s audio playback the player program’s audio playback player program’s audio playback program’s audio playback audio playback playback i code to account to account account for the difference between the difference between difference between between the audio audio data’s original sample rate and rate and and the actual playback playback : rate on the Jaguar. on the Jaguar. the Jaguar. Jaguar. This value value is added to an accumulator during each DSP sample added to an accumulator during each DSP sample to an accumulator during each DSP sample an accumulator during each DSP sample accumulator during each DSP sample during each DSP sample each DSP sample DSP sample sample rate interrupt. ' Whenaa carry is generated, generated, instead of proceeding to the next sample of proceeding to the next sample proceeding to the next sample to the next sample the next sample next sample sample as usual, usual, the current current sample is { reused instead. The audio drift rate is derived from derived from from the formula: formula: 

Cinepak For Jaguar 

4 ’ j a j - = 1 .i]& ; 3 ff | | fg > 

i _ | Gl | 4 | 4 : , — _ , 4 | 4 _ , 4 : 2 j ® ‘ q : = 

**==> picture [507 x 163] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 8 8 Cinepak For Jaguar<br>tt<br>Bits Meaning<br>PO |0=Mono,1=Stereo<br>2-7 | Audio Compression Audio Compression Compression Type:<br>0 = uncompressed<br>1 = n® compression compression<br>other values are reserved<br>Two's Complement audio flag Complement audio flag audio flag flag<br>Table 3.6 3.6 — Audio description flag Audio description flag description flag flag bits<br>**----- End of picture text -----**<br>


The AudioDrift field specifies a 32-bit value that can be used by the player program’s audio playback AudioDrift field specifies a 32-bit value that can be used by the player program’s audio playback field specifies a 32-bit value that can be used by the player program’s audio playback specifies a 32-bit value that can be used by the player program’s audio playback a 32-bit value that can be used by the player program’s audio playback value that can be used by the player program’s audio playback that can be used by the player program’s audio playback can be used by the player program’s audio playback be used by the player program’s audio playback used by the player program’s audio playback by the player program’s audio playback the player program’s audio playback player program’s audio playback program’s audio playback audio playback playback code to account to account account for the difference between the difference between difference between between the audio audio data’s original sample rate and rate and and the actual playback playback rate on the Jaguar. on the Jaguar. the Jaguar. Jaguar. This value value is added to an accumulator during each DSP sample added to an accumulator during each DSP sample to an accumulator during each DSP sample an accumulator during each DSP sample accumulator during each DSP sample during each DSP sample each DSP sample DSP sample sample rate interrupt. Whenaa carry is generated, generated, instead of proceeding to the next sample of proceeding to the next sample proceeding to the next sample to the next sample the next sample next sample sample as usual, usual, the current current sample is reused instead. The audio drift rate is derived from derived from from the formula: formula: DrifRate = A SourceSampleRate + (SourceSampleRate - JaguarSampleRate) The Jaguar sample rate is determined by: _ VideoClockRate = 26590906Hz (NTSC), 26593900Hz (PAL) {VideoClockRateVideoClockRate JaguarSampleRate = {|————. + 32————. + 32 + 32 32 | 2 x (SCLK+ x (SCLK+ (SCLK++ 1) 

4 {VideoClockRateVideoClockRate 4 JaguarSampleRate = {|————. + 32————. + 32 + 32 32 | 2 x (SCLK+ x (SCLK+ (SCLK++ 1) You can work backwards from the DriftRate value and the Jaguar Sample Rate to get the original : sample rate. You might do this, for example, in the event that you wanted to change the DSP code to perform linear interpolation to adjust the playback sample rate, rather than simply repeating samples. The formula for this is: JaguarSampleRate : SourceSampleRate = JaguarSampleRate +eee 2 +DriftRate || Note that older Jaguar Cinepak films may not contain an Audio Description Atom. If none is found, the player code should typically default to expecting 8-bit mono at a 22050 Hz (original) sample rate. 

3 This will only be true for films converted with versions of the Jaguar Cinepak Utilities dated June 1995 and later. 16 June, 1995 Property of “FO® Atari Corporation © 1995 Radius, Inc. & Atari Corp. 

. 

j| |Duration|Duration|| 4 | Duration of playback playback interval for sample for sample sample Table 3.8 — Structure of sample record. j The start field gives the starting address of the sample referenced by the sample record, relative to the f end of the sample table. The end of the sample table coincides with the end of the frame header (see | = Table 3.2). 

| The size field gives the size of the referenced sample in bytes. Adding the start and size fields of the | current sample record yields the value in the start field of the next sample record. 

| 

**==> picture [391 x 147] intentionally omitted <==**

**----- Start of picture text -----**<br>
: ; Cinepak For Jaguar<br>mm 81.4 SampletableAtom<br>: ] Field Size Description<br>Po |__Header_<br>= | 4 «| SizeHumanof sample readable table tag: 'STAB'atom<br>P| “Seale [4 __| Time scale of [fim]<br>fq Number of sample records in table<br>: Sample records 16* Count | Array of sample records<br>q ; Table 3.7 — Structure of sample table atom.<br>**----- End of picture text -----**<br>


**==> picture [29 x 15] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 9<br>**----- End of picture text -----**<br>


We sé audio and frames of video. The header is a human-readable tag, ‘STAB', which identifies the @e structure as a sample table. The atom size field contains the size of the sample table atom, which je 20s encompasses the ensuing sample records. 1 | The scale field provides the time scale for the fiim, in fractional units of a second, i.e. the unit of time is @e the reciprocal of the scale. A value of 600 is commonly used in QuickTime movies, as it is the lowest F common multiple of the common rates of 24, 25 and 30 frames per second. The MovieToFilm too] does ‘ q - not alter the time scale embedded in the QuickTime movie when a Jaguar film is created. The count field gives the number of sample records which immediately follow it; the sample record f structure is defined in Table 3.8. 

**==> picture [356 x 67] intentionally omitted <==**

**----- Start of picture text -----**<br>
Field ' Size Description _<br>Start of sample<br>Number of bytes in sample<br>Time at which to play sample<br>|Duration|Duration|| 4 | Duration of playback playback interval for sample for sample sample<br>**----- End of picture text -----**<br>


| The 31 least-significant bits in the time field of the sample record give the time at which the referenced sample is scheduled to be played, in the units specified by the scale field of the sample table. If the | — value is $7FFFFFFF that indicates that the referenced sample (block) contains audio, not video, which | should be played immediately following the end of the previous audio sample (block). 

> 4 The “sample” terminology is, unfortunately, somewhat ambiguous. In the context of a Cinepak film, it refers to a set of : data which may be either audio or video. In the context of audio, it conventionally refers to the 8-bit or 16-bit datum which is read or written to a DAC. Where possibility for confusion exists, we use the terminology "block" to indicate j the aggregate. ] © 1995 Radius Inc. & Atari Corp. Confidential “FER Information 16 June, 1995 

**==> picture [2 x 12] intentionally omitted <==**

**----- Start of picture text -----**<br>
i<br>**----- End of picture text -----**<br>


**==> picture [3 x 15] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


16 June, 1995 

Page 10 

Cinepak For Jaguar : (0) or not (1); or not (1); not (1); (1); this is a carry- is a carry- a carry- carry| 2 referenced sample, in units of the sample, in units of the in units of the units of the of the the j j addition of the time and duration of the time and duration the time and duration time and duration and duration duration : field of the next video sample of the next video sample the next video sample next video sample video sample sample 1 { | | eee | except that additional additional structures 4 for random access on a random access on a access on a on a a | @ Pf ] og -— _n is number of chunks number of chunks of chunks chunks | samples : -_ identical to those already defined those already defined defined : header atom size atom size size 4 4 q for chunky format films; chunky format films; format films; films; they 1 ] f 4 es | 3 ‘CTAB’ _ j } fo 4 in table table . 2 ; | a table (see Table 3.7). (see Table 3.7). Table 3.7). 3.7). The 4 and the chunk record, defined the chunk record, defined chunk record, defined record, defined | 4 | 7 Publishing Company, 1993, pages. Company, 1993, pages. 1993, pages. pages. 3 q j = © 1995 1995 Radius, Inc. Inc. & Atari Corp. Corp. | = 

1 | ) : | | I | q 

) 

jj 

The most significant bit of the time field indicates a shadow Sync sample (0) or not (1); or not (1); not (1); (1); this is a carry- is a carry- a carry- carryover from QuickTime that should be ignored by the sample player code.5 

The duration field of the sample record gives the play duration of the referenced sample, in units of the sample, in units of the in units of the units of the of the the time scale. For an audio sample (block), the duration is meaningless; addition of the time and duration of the time and duration the time and duration time and duration and duration duration record.fields of the current video sample record yields the value in the time field of the next video sample of the next video sample the next video sample next video sample video sample sample 

32 Chunky Pomel 2 eee The chunky format contains all the ingredients of the smooth format, except that additional additional structures are embedded in the data stream to partition it in time and provide mechanisms for random access on a random access on a access on a on a a CD-ROM disc. The highest-level structure is shown in Table 3.9. 

**==> picture [421 x 95] intentionally omitted <==**

**----- Start of picture text -----**<br>
Field Size Description<br>Frame header 16 Global film header<br>| Audio Description | 20 __[ Audio data format description<br> ___Chunk table __| 16 + (n* 16) | Index to chunk data which follow; _n is number of chunks number of chunks of chunks chunks<br>|__ Chunk data___[~_variable__| Time-sequential chunks of film samples<br>Table 3.9 — Chunky film format.<br>**----- End of picture text -----**<br>


The frame header, frame description, and audio description fields are identical to those already defined those already defined defined for the smooth format (see Table 3.2 and Table 3.3), except that the frame header atom size atom size size encompasses the ensuing chunk table. . The chunk table and chunk data fields are new fields especially created for chunky format films; chunky format films; format films; films; they are defined in Table 3.9 and Table 3.11, respectively. S21, Chunk Vekle Ati es | HeaderField [4Size |Human readableDescription tag: ‘CTAB’ [__ Seale | ___4 | Time scale of fim _ [Count[___4 ____ T Number of chunk records in table table Table 3.10 — Structure of chunk table atom. ; The chunk table bears a close resemblance to its counterpart, the sample table (see Table 3.7). (see Table 3.7). Table 3.7). 3.7). The differences are that the atom header ‘CTAB' identifies it as a chunk table, and the chunk record, defined the chunk record, defined chunk record, defined record, defined in Table 3.11, is a minor variation on the previously defined sample record. 

5 2-134For moreto 2-135. information, see the book Inside Macintosh: QuickTime, Addison-Wesley Publishing Company, 1993, pages. Company, 1993, pages. 1993, pages. pages. 16 June, 1995 Property of FOR Atari Corporation © 1995 1995 Radius, Inc. Inc. & Atari Corp. Corp. | 

; ‘Cinepak For Jaguar Page 11 Pe StatField | Size4 |StartofchunkDescription | | 1 Table 3.11 — Structure of chunk record. The chunk record is identical to the sample record (see Table 3.8), except that the duration field of the } latter is replaced by the sync pattern field. This 4-byte field specifies the pattern that is replicated to } form the sync marker for the chunk in the data stream. Field Size Description : rsync | _64.__| Sync Sync marker used to locate locate chunk within data stream data stream stream | | | Table 3.12 — Chunk — Chunk Chunk data format. format. 

Page 11 

Field Size Description : rsync | _64.__| Sync Sync marker used to locate locate chunk within data stream data stream stream | | | Table 3.12 — Chunk — Chunk Chunk data format. format. The chunk data element begins with 64-byte sync marker. This is followed by the sample table and film sample data for all film samples which fall within the time boundaries of the chunk. The structure of } the sample table is identical to that for the smooth format (see Table 3.7); however, the addressing of | film samples by the start field is local to the chunk. The zero base is the end of the sample table, in | analogy with the addressing for a smooth film. oe lmrrrrrt—<“—iws—s—s—s—s—s—s—O—C—C—OC—C~C~C~COCOCUC OwzSONCiCiCCC:ir«:«CNCUOCié'#UCO#ié#=(C.W ! Once you have created your film and converted it to the chunky Jaguar Cinepak format using the | — SmoothToChunky option of the Jaguar Cinepak Utilities program, you are ready to put the film onto a | CD-ROM disc so that it may be played on the Jaguar. We will presume for now that you are using just | one film per CD-ROM track. | The smooth format Jaguar Cinepak Film created by SmoothToChunky is used to create a track file using | the Jaguar CD Track Creator program (see the Jaguar CD-ROM chapter). This puts the correct | Jaguar CD-ROM track wrapper around your film data and gives youa track file that you can feed | directly to your CD-ROM mastering software in order to make a CD-ROM disc. & Unfortunately, some CD-ROM mastering software packages do not have the ability to take a raw binary file and use it to create a track. They may require that the file must look like an AIFF or WAV audio | file (even if that’s not really what kind of data it contains). The AIFF or WAVE file wrapper is removed prior to the data being written to the disc. The current version of the Jaguar CD Track | Creator has no option to add an AIFF or WAV wrapper to the files it creates; this must be done as an Rtvr | ©1995 Radius Inc. & Atari Corp. Confidential FO® Information 16 June, 1995 

| 

Page 12 Cinepak ForJaguar 1 : : additional step with a separate program. (The MKAIF tool supplied as part of the Jaguar sound & 7 | music package can be used for this purpose right now, but this feature will be added to future versions of the Jaguar CD Track Creator.) fy | eerrr—s—S—«..—.—.LUrC“C#Y)NYCRRRROSGYC”d”C'§&$$E$’NCNCSNC#aC@RS j An early approach to the AIFF requirements of CD-ROM mastering software was the FilmToAIFF { option of the Jaguar Cinepak Utilities program, which takes a Jaguar Cinepak Film and creates a new | § | file with an AIFF audio file wrapper around the original data. This option should no longer be used.6 -— : First, it only works with Jaguar Cinepak Film files, which isn’t the only thing you’ll need to put onto a i | Jaguar CD disc. Also, it presumes that there will only be one Cinepak film in each CD-ROM track, | 4 f whichit creates maydo notnot befollow the casethe ifstandardyou have Jaguara lot CD-ROM of small moviestrack specification, instead of a fewso bigit can o **n** es.ot be Finally,used to thecreatea files | j ' master CD-ROM disc ready for production. 4 i If your player code was originally set up to expect a film processed by FilmToAIFF, there are a few F : things to watch for when you change it over. First of all, FilmToAIFF has an option to put an extra 4 wrapper around the film data.” This places 56446 bytes of leader data (all “A” characters) before the fg j Jaguar Cinepak film data. Some older versions of Atari’s sample player program expect to find this data ‘ and use an offset value defined by the LEADER equate to skip ahead by this amount on each read from | | the CD. If you stop using FilmToAIFF, you should make sure that your player software no longer does this. Also, FilmToAIFF inserts a 64-byte sync header with all “1” characters immediately before your rp 4 Jaguar Cinepak film data. The player probably uses this to locate the start of the film. If this is the case, you must change it to look for the partition header created when you build a track file using the Jaguar 7 CD Track Creator program.’ _— See the Jaguar CD-ROM chapter for more information on CD mastering considerations. : j : ‘12 Other CD Mastering Considerations «= esa“ | Note that some older CD mastering software automatically inserts two seconds worth of silence at the 1 1 ' start of each audio track. This results in extra data at the start of the track. Some versions of the sample - | Cinepak player code include a SILENCE equate that is used to skip past this datain a similar mannerto | = the LEADER equate mentioned eariler. See the chapter Jaguar CD-ROM for more information. | @ | «- BSample'PlaybackCode eee | This section gives a comprehensive description of the sample code which is provided to demonstrate 2 | playback of Jaguar films from CD-ROM. The example is based on a film in the chunky format. The 2 smooth format, being a subset, would not be as illustrative. = 6 The FilmToAIFF option is still available in the current version of the Jaguar Cinepak Utilities program, but will is 7 probably be removed from future versions. ] x 8 See section 8.5 for more detailed information on the FilmToAIFF conversion. j > See the Jaguar CD-ROM chapter for detailed information on the Jaguar CD Track Creator program. 4 Fa 16 June, 1995 Property of F@® Atari Corporation © 1995 Radius, Inc. & Atari Corp. Ca 

Page 13 

| 

m j 

## Cinepak ForJaguar 

The sample code consists of the following source modules, in alphabetical order: { player.inc clear.s dspcode.das intserv.s lister.s memory.inc j player.s utils.s vidinit.s 

: A makefile is also provided to build the executable player code. Warning! Please note that the current version of the sample Cinepak player : programs is not intended as a general example ofJaguar programming. It is intended to specifically demonstrate the use of the Cinepak decompression code, and 1 : nothing else. Do not use this example to obtain startup code or as a shellfor creating your own programs. i { The system DRAM and ROM emulator memory map is shown in Table 5.1. Relevant symbol | definitions are contained in the module memory.inc. 

**==> picture [419 x 201] intentionally omitted <==**

**----- Start of picture text -----**<br>
Address Range Description<br>4 $0 - $OFFF Exception vectors, CD-BIOS<br>7 $4000 - $57BF* Player executable code<br>Se7C0"-SFFEF|Notused<br>: $10000<br>- $31BFF<br>S31000-S833FFF[Notused<br>| $34000 -$36FFF | Auxiliary Cinepak data<br>S57000837FFF[Notused<br>: $38000 - $137FFF | Film buffer (chunk table and film data)<br>; $138000 - $13803F Overflow (GPU fills beyond end of buffer)<br>SSS040-SiFFFFF [Notused<br>: : $800000 - $8FFFFF<br>7 $900000 - SOFFFFF | Debug history<br>* = Approximate address, may change with different versions of<br>: player program.<br>**----- End of picture text -----**<br>


Table 5.1 — DRAM and ROM emulator memory map. 

| 

| 

| 

| 

The memory map may be freely rearranged, or compacted if necessary; however, there are several restrictions: 

1. The base of the frame buffer (currently $10000) must be phrase-aligned. 

2. The base of the auxiliary Cinepak data area (currently $34000) must be long-aligned. 3. The base of the film buffer (currently $38000) must be long-aligned. 

© 1995 Radius Inc. & Atari Corp. 

ConfidentialFER Information 

16 June, 1995 

Page 14 14 Cinepak ForJaguar | egrrtrt~™.CSO_C(C‘i‘NYRYNRRRRRRAN_.U.«U«UC«wS‘‘NNHS|'rrtrt~™.CSO_C(C‘i‘NYRYNRRRRRRAN_.U.«U«UC«wS‘‘NNHS|' In this section, we we describe several key key parameters, defined in player.inc, player.inc, which either have major & impact on the behavior behavior of the the system or interact with similar parameters in the tools. , 4 The CBUF_SIZE equate controls the size of the the circular butfer which which is used to store the chunk table and film data. It is currently currently set at 1 MByte, although the size may be reduced, particularly for low- low= betweenresolutionreadorresolutionreadorreadoror short-durationandand write pointersfilms. uponThestartup HEAD_STARTmustfilms. uponThestartup HEAD_STARTmust uponThestartup HEAD_STARTmustThestartup HEAD_STARTmuststartup HEAD_STARTmust HEAD_STARTmustmust be adjusted equate, equate, alongwhichwithguaranteesCBUF_SIZE;a minimummaintainingseparationthewhichwithguaranteesCBUF_SIZE;a minimummaintainingseparationthewithguaranteesCBUF_SIZE;a minimummaintainingseparationtheguaranteesCBUF_SIZE;a minimummaintainingseparationtheCBUF_SIZE;a minimummaintainingseparationthea minimummaintainingseparationthe minimummaintainingseparationthemaintainingseparationtheseparationthethe _| 4 current ratio of 75% of 75% 75% should be be adequate. 1 The GPU_OFFSET GPU_OFFSET equate determines the offset from the offset from offset from from the base of GPU base of GPU of GPU GPU internal RAM RAM at which which the : Cinepak decompressor code code is loaded. loaded. During initialization, its value value is copied to copied to to the variable location , 4 GPUOffset, which the GPU code uses GPU code uses code uses uses to relocate portions of of its own own code and data. data. : The FILM_SYNC equate FILM_SYNC equate equate must correspond to the 4-byte correspond to the 4-byte to the 4-byte the 4-byte 4-byte partition sync marker that sync marker that marker that that is repeated repeated 16 times times . ) (for 64 bytes 64 bytes bytes total) immediately before immediately before before the film data begins. begins. The player code uses this to locate the player code uses this to locate the code uses this to locate the uses this to locate the this to locate the to locate the locate the the 4 beginning of the of the the film data after it is ready ready from the CD. CD. This sync marker sync marker marker is inserted inserted in front of the of the the 4 Jaguar Cinepak Cinepak film data by by the Jaguar CD Track Creator program when you create CD Track Creator program when you create Track Creator program when you create Creator program when you create program when you create when you create you create create the track files for files for for : j the CD.° CD.° The FilmToAIFF option of the of the the Jaguar Cinepak Cinepak Utilties program program always creates a sync a sync sync | pattern of of “1111”. 10 fi (MFi]The DRIFT_RATE DRIFT_RATE_RATE equate is used used to account for the difference between the sample sample rate of the of the the — originalsections audio3.1.3 data 5.6in in thefor more moreoriginalinformation.)QuickTime originalinformation.)QuickTime QuickTime movie and the and the the actual playback rate on on the Jaguar. Jaguar. (See i sections 3.1.3 and 5.6data 5.6in for more moreoriginalinformation.)QuickTime information.)QuickTime : ___ PLAYERS... PLAYERS... It seems to me that this information is other misleading or incomplete, incomplete, = etse we wouldn't be able to work with different different sized audio blocks. andwedo>> ae The AUDIO_LAG equate is a critical parameter in the calculation of when to start reading equate is a critical parameter in the calculation of when to start reading is a critical parameter in the calculation of when to start reading a critical parameter in the calculation of when to start reading critical parameter in the calculation of when to start reading parameter in the calculation of when to start reading in the calculation of when to start reading the calculation of when to start reading calculation of when to start reading of when to start reading when to start reading to start reading start reading reading ; ; data from the CD-ROM. CD-ROM. It is tied tied to the parameters parameters AUD_CHUNK and SAMP_RATE, and SAMP_RATE, SAMP_RATE, | @ which represent the the size of the audio of the audio the audio audio blocks in the the film data stream and data stream and and the audio sample audio sample sample _ rate, respectively. The AUD_CHUNK parameter AUD_CHUNK parameter parameter must correspond correspond to the kSoundChunkSize kSoundChunkSize |! Bo parameter in the MovieToFilm MovieToFilm tool. : The MAX DELAY equate limits how far the system can limits how far the system can how far the system can far the system can the system can system can can fall behind real-time display of video before behind real-time display of video before real-time display of video before of video before before it ‘ Starts skipping video skipping video video frames to catch catch up; it is currently currently set at 1/24 second. second. Because only key only key key frames are { . displayed during the catch-up process, catch-up process, process, the video will video will will appear jerky jerky while this is happening. happening. If this istoo istoo | - objectionable,should have problems with have problems withthe delay delay withcan bethe video videorelaxedfalling behind.)to the delay delay withcan bethe video videorelaxedfalling behind.)to can bethe video videorelaxedfalling behind.)to relaxedfalling behind.)to to 1/12 second. behind.) second. (Note that only only fairly high throughput films fe should have problems with have problems withthe delay delay withcan bethe video videorelaxedfalling behind.)to the video videorelaxedfalling behind.)to falling behind.)to 2 

Page 14 14 Cinepak egrrtrt~™.CSO_C(C‘i‘NYRYNRRRRRRAN_.U.«U«UC«wS‘‘NNHS|'rrtrt~™.CSO_C(C‘i‘NYRYNRRRRRRAN_.U.«U«UC«wS‘‘NNHS|' 

j In this section, we we describe several key key parameters, defined in player.inc, player.inc, which either have major impact on the behavior behavior of the the system or interact with similar parameters in the tools. ; The CBUF_SIZE equate controls the size of the the circular butfer which which is used to store the chunk table and film data. It is currently currently set at 1 MByte, although the size may be reduced, particularly for low- low| betweenresolutionreadorresolutionreadorreadoror short-durationandand write pointersfilms. uponThestartup HEAD_STARTmustfilms. uponThestartup HEAD_STARTmust uponThestartup HEAD_STARTmustThestartup HEAD_STARTmuststartup HEAD_STARTmust HEAD_STARTmustmust be adjusted equate, equate, alongwhichwithguaranteesCBUF_SIZE;a minimummaintainingseparationthewhichwithguaranteesCBUF_SIZE;a minimummaintainingseparationthewithguaranteesCBUF_SIZE;a minimummaintainingseparationtheguaranteesCBUF_SIZE;a minimummaintainingseparationtheCBUF_SIZE;a minimummaintainingseparationthea minimummaintainingseparationthe minimummaintainingseparationthemaintainingseparationtheseparationthethe j current ratio of 75% of 75% 75% should be be adequate. The GPU_OFFSET GPU_OFFSET equate determines the offset from the offset from offset from from the base of GPU base of GPU of GPU GPU internal RAM RAM at which which the i Cinepak decompressor code code is loaded. loaded. During initialization, its value value is copied to copied to to the variable location | GPUOffset, which the GPU code uses GPU code uses code uses uses to relocate portions of of its own own code and data. data. : The FILM_SYNC equate FILM_SYNC equate equate must correspond to the 4-byte correspond to the 4-byte to the 4-byte the 4-byte 4-byte partition sync marker that sync marker that marker that that is repeated repeated 16 times times | (for 64 bytes 64 bytes bytes total) immediately before immediately before before the film data begins. begins. The player code uses this to locate the player code uses this to locate the code uses this to locate the uses this to locate the this to locate the to locate the locate the the { beginning of the of the the film data after it is ready ready from the CD. CD. This sync marker sync marker marker is inserted inserted in front of the of the the i Jaguar Cinepak Cinepak film data by by the Jaguar CD Track Creator program when you create CD Track Creator program when you create Track Creator program when you create Creator program when you create program when you create when you create you create create the track files for files for for ; the CD.° CD.° The FilmToAIFF option of the of the the Jaguar Cinepak Cinepak Utilties program program always creates a sync a sync sync | pattern of of “1111”. 10 i (MFi]The DRIFT_RATE DRIFT_RATE_RATE equate is used used to account for the difference between the sample sample rate of the of the the . originalsections audio3.1.3 and 5.6data 5.6in thefor more moreoriginalinformation.)QuickTime movie and the and the the actual playback rate on on the Jaguar. Jaguar. (See : ___ PLAYERS... PLAYERS... It seems to me that this information is other misleading or incomplete, incomplete, | et etse we wouldn't be able to work with different different sized audio blocks. andwedo>> | The AUDIO_LAG equate is a critical parameter in the calculation of when to start reading equate is a critical parameter in the calculation of when to start reading is a critical parameter in the calculation of when to start reading a critical parameter in the calculation of when to start reading critical parameter in the calculation of when to start reading parameter in the calculation of when to start reading in the calculation of when to start reading the calculation of when to start reading calculation of when to start reading of when to start reading when to start reading to start reading start reading reading ; data from the CD-ROM. CD-ROM. It is tied tied to the parameters parameters AUD_CHUNK and SAMP_RATE, and SAMP_RATE, SAMP_RATE, : which represent the the size of the audio of the audio the audio audio blocks in the the film data stream and data stream and and the audio sample audio sample sample rate, respectively. The AUD_CHUNK parameter AUD_CHUNK parameter parameter must correspond correspond to the kSoundChunkSize kSoundChunkSize parameter in the MovieToFilm MovieToFilm tool. The MAX DELAY equate limits how far the system can limits how far the system can how far the system can far the system can the system can system can can fall behind real-time display of video before behind real-time display of video before real-time display of video before of video before before it Starts skipping video skipping video video frames to catch catch up; it is currently currently set at 1/24 second. second. Because only key only key key frames are j displayed during the catch-up process, catch-up process, process, the video will video will will appear jerky jerky while this is happening. happening. If this istoo istoo | objectionable,should have problems with have problems withthe delay delay withcan bethe video videorelaxedfalling behind.)to 1/12 second. behind.) second. (Note that only only fairly high throughput films | 9 See the Jaguar CD Mastering section of the Jaguar CD-ROM chapter for more information on the Jaguar CD Track | Creator tool. | 10 Atari recommends that you no longer use FilmToAIFF. See the Using A Jaguar Cinepak Film With CD-ROM section for more information. 

**==> picture [4 x 40] intentionally omitted <==**

**----- Start of picture text -----**<br>
7<br>]<br>:<br>**----- End of picture text -----**<br>


| | | | 

| 

Page 15 ] Cinepak For Jaguar The SILENCE and LEADER equates are used in computation of the time code for the beginning of each track, and must be consistent with how the CD is actually recorded. The SILENCE equate is used to | keep track of any extra blank space which may be placed at the beginning of a CD track by your CD | | mastering software.!! The ideal amount is zero, but some CD-ROM mastering software packages may } not give you any choice. The LEADER equate should be set to 0 unless you are using FilmToAIFF, in F which case you should set it to 24. (These values are based on a number of CD data blocks, which are | 2352 bytes each.) | The MARGIN equate causes the seek to occur ahead of the target, in order to guarantee that the data stream is valid at the actual point of interest. In the sample code, MARGIN is set to 16 blocks; this | value should not be tampered with. | The SYNC_SIZE parameter represents the number of bytes in the sync marker that is found before the | film header or a chunk of data within the film. This should always be 64.(MF2} | The SRCH_WIN parameter controls how many blocks into the input buffer the FindSync routine will look for the sync marker pattern before giving up and returning an error. Its value is closely linked to that of MARGIN and should not be changed. 

**==> picture [566 x 349] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|
|weir|ee|
|he|
|||
|Table 5.2 lists several key variables in the system (declared near the end of|player.s), and describes their|
|function.|
|P|||
|1|Variable|Size|Description”|a|
|subroutine.|
|Set|if time slip exceeds maxDelay.|Cleared when next key frame|is encountered.|
|:|
|\—saapeies|||aT|Size ofstarts immedicircul r|a|telybuffer following (CBUF_END- chunk table. oBufBase),|
|GetCDWritePtr|subroutine.|
|||—spavmonis|||Flag indicates Cinepak compressed|AGB|color format|(0)|or Ata|CRY|format)|
|time,|below which the next CD-ROM|read|activity|is|initiated.|
|4|[serine|| —*|[Bio|sos|et|Snaracnny eames|
|5|SetNextGroup|subroutine.|;|
|||Value must be computed because time scale of film|is not known|until run time.|
|||| -Segaarser|[Tost|in bytes|from star|of fim|on|CD-ROM|to frst|audio|or video dete|
|buffer contents.|Computed|in SetNextGroup|subroutine.|
|4||[Tae|in Scnenampaumauee.|

**----- End of picture text -----**<br>


**==> picture [421 x 43] intentionally omitted <==**

**----- Start of picture text -----**<br>
OO 11 See the Jaguar CD Mastering section of the Jaguar CD-ROM chapter for more information.<br>| © 1995 Radius Inc. & Atari Corp. Confidential FOR Information<br>**----- End of picture text -----**<br>


16 June, 1995 

7 

‘ i 

' | 1 j | 

**==> picture [606 x 724] intentionally omitted <==**

**----- Start of picture text -----**<br>
j Page 16 Cinepak For Jaguar =<br>q Variable Size Description , YF<br>( playPhase 2 Flag keeps track of activity while CD-ROM is playing:<br>1 0: no activity; 1 2<br>1 1: playing initiated; F<br>| 2: sync for next group of chunks detected Py<br>4 3: inhibit further play (end of film) 4<br>| PNextGroup 4 | Pointer to chunk record of first chunk in group that will be played after expiration of 4<br>semaphore Semaphore used to awaken the 68000 after GPU has finished decompression task. ; 4<br>Cleared by the 68000 when GPU task is initiated. Set upon receipt of GPU =<br>time interrupt by the 68000. | 4<br>||timeiner 4 |\ver32-b48-bit i tcalonal time time inincrem Qi6 m format. e ntngin Q16seniceraaine,Set format. to zeroIt whenis the filmratio playingonesof the timeis started.scale ofUpdated thm e saainafilm duringto the f|7<br>vertical interval tick rate. This increment is added to time during vertical interval<br>interrupt service routine. gg<br>Table 5.2 —- Key variables in system. _<br>Several utility routines are provided with the system to hide non-essential details and streamline the 4<br>main code. These routines are all contained in the module utils.s. {<br>Parameter passing to and from these routines is done via registers; the stack is not used. Table 5.3 4<br>summarizes the interfaces to the utility routines, along with their functions.<br>Routine Input Output Function 4<br>FindSyne dO: sync pattern a0: address following end | Searches data stream beginning at q<br>a0: starting address of sync, or 0 if sync not | (a0), until sync pattern, input in dO, is rr 4<br>found within located. | a<br>SRCH_WIN bytes F<br>- | GetCDWritePtr Updates CDWritePtr location with fog<br>current position of CD-ROM ; 4<br>GetTimeCode | d0: data offset from dO: time code in mmiss:bb | Converts byte offset to time code. + 3<br>LoadDSP Copies DSP program from DRAM to | #m<br>LoadGPU None Copies GPU Cinepak decompressor | 2.<br>code from DRAM to GPU internal _<br>: memory and calls CD-BIOS to load =<br>support code. Initializes GPUOfiset,| Tn<br>needed for later access to GPU _—<br>LongDivide [d0: unsigned 16-bit | di: unsigned 32-bit Performsmemory. {ong division, taking correct| j Se<br>d1: divisor quotient account of overflow (quotient q =<br>unsigned 32-bit exceeds 16 bits). q P<br>dividend ] a<br>ReadCDData | dO: data offset from Performs housekeeping on CD-ROM | jf ‘.<br>start of media hardware, sets up write pointers, | .<br>a0: starting computes time code for seek and ; ,<br>destination initiates CD-ROM playback. 4 Po<br>address 4 =<br>16 June, 1995 Property of FO® Atari Corporation © 1995 Radius, Inc. & Atari Corp. P<br>**----- End of picture text -----**<br>


Page 17 | Cinepak For Jaguar ' Routine Input Output Function - in circular buffer. Adjusts value of 1 filmChunks. : ne pNextGroup for next group of chunks Snapshot None Dumps 64-byte record of key emulator address space. | Table 5.3 — interfaces to utility routines. mea2 | Audio playback is handled entirely by the DSP (see module dspcode.das), although it does use some | information which is set up by the 68000 (in player.s). The player code looks at the film header for an | audio description atom (see section 3.1.3). If one is found, then the information for the audio format is | extracted and saved into variables for the DSP code to use. If no audio description is found, the player ‘ assumes that any audio data in the film will be mono, 8-bit samples in two's-complement format, with a | playback sample rate of 21.867 kHz and original sample rate of 22250 kHz. | Two locations in DSP internal memory are used to pass parameters between the 68000 and the DSP, as } shown in Table 5.4. , 4 Location Size Description : MTSE ARGS | 4 [Byte countin audio block : Table 5.4 — Locations used to control audio playback. | When the 68000 encounters an audio block in the circular buffer, it loads the starting address of the | block into location DSP_ARGS+4, then the the byte count into location DSP_ARGS. The code which | does this is located just following the SampleLoop label in module player:s. : The DSP polls the byte count location. When it sees a nonzero value, it reads the value, writes back a | zero and reads the starting address of the audio data. On a sample rate interrupt, the DSP reads a byte | from the audio buffer, writes it to the DACs and decrements its copy of the byte count. Because of the - forward bias of audio in the film data stream (see Section 5.8), the DSP receives a continuous supply of f audio data even if the video begins to lag behind schedule. However, should the byte count reach zero, a onull (silence) samples are written to the DACs until the 68000 next updates the parameters at me 20CODSP_ARGS.. @ =e A third DSP internal memory location, AUDIO _DRIFT, is loaded with either the DriftRate parameter @ from the audio description atom (see Section 3.1.3) if one is found, or otherwise from the DRIFT_RATE Me = equate defined in the player.inc file (see Section 5.3). This must happen before audio playback is M initiated. This value is used to adjust for the differences, or “drift”, between the original sample rate of F the audio data and the interrupt frequency at which it will be played back. After every sample is written to the DACs, the AUDIO_DRIFT value is added to an accumulator. Whena carry is generated, it } — means that the error between the two sample rates has accumulated to a full sample, and an input sample 4 © 1995 Radius Inc. & Atari Corp. Confidential “FER Information 16 June, 1995 1995 

| 

16 June, 1995 1995 

'q | { 

Page 18 

lnCinepak For Jaguar 

4 } 4 . 

’ | | Ff i 4 =_ 4 : 

q 

i| 

in the circular butfer the circular butfer circular butfer butfer is the most difficult the most difficult most difficult difficult technical aspect of aspect of of { ’ . 4 | the process. The read read pointer for the video video data being being used by by the the circular buffer, buffer, consuming data as as it goes. goes. Meanwhile, the — CD follows along behind follows along behind behind it. Whenever the read pointer reaches the read pointer reaches read pointer reaches pointer reaches | a beginning and the consumption of data continues without and the consumption of data continues without the consumption of data continues without consumption of data continues without of data continues without data continues without continues without without ‘ reaches the end of the buffer, end of the buffer, of the buffer, the buffer, buffer, the write process write process process is suspended. suspended. q the ratio of the combined video/audio ratio of the combined video/audio of the combined video/audio the combined video/audio combined video/audio video/audio data rate to the playback rate to the playback the playback playback | q high-quality film, the combined rate might be 250 kBytes/sec; combined rate might be 250 kBytes/sec; rate might be 250 kBytes/sec; might be 250 kBytes/sec; be 250 kBytes/sec; 250 kBytes/sec; kBytes/sec; with a a | @ this translates to a duty cycle of roughly 70%. a duty cycle of roughly 70%. duty cycle of roughly 70%. cycle of roughly 70%. of roughly 70%. 70%. ; 4 much lower than the compressed than the compressed the compressed compressed video data rate, the audio the DSP, DSP, advances at a much slower rate than the video read a much slower rate than the video read much slower rate than the video read slower rate than the video read rate than the video read than the video read the video read video read read = be dramatic dramatic differences in audio throughput in audio throughput audio throughput throughput rates depending on depending on | 2 16-bit stereo audio at 22 kHz requires 4 times as much 22 kHz requires 4 times as much kHz requires 4 times as much requires 4 times as much 4 times as much times as much as much much | = . | = in the data stream, the data stream, the audio pointer will periodically jump ahead audio pointer will periodically jump ahead pointer will periodically jump ahead will periodically jump ahead periodically jump ahead jump ahead ahead : q . For this reason, this reason, reason, the audio audio pointer has a rather jagged trajectory has a rather jagged trajectory a rather jagged trajectory rather jagged trajectory jagged trajectory trajectory 4 7 lies within within an envelope having the same slope as the trajectory having the same slope as the trajectory the same slope as the trajectory same slope as the trajectory slope as the trajectory as the trajectory the trajectory trajectory j 7 it by by a constant amount, constant amount, amount, as shown. shown. ] bs original sample rate of 22250 Hz and a playback sample sample rate of 22250 Hz and a playback sample rate of 22250 Hz and a playback sample of 22250 Hz and a playback sample 22250 Hz and a playback sample Hz and a playback sample and a playback sample a playback sample playback sample rate of 21867 of 21867 21867 is only only q . 4 a Property of“FER of“FER“FER Atari Corporation © 1995 Radius, Inc. 1995 Radius, Inc. Radius, Inc. Inc. & Atari Corp. 3 o 

| 

is dropped to compensate for the error. However, because the difference between the sample rates is fairly small}? there is no discernible impairment in audio quality. 

## ae CCTCt—s—~s—OC—C=COCNSSCNONOWSCONCCONCCOCCSC‘ié‘éCOUMg,. _ The code for setting up and servicing interrupts to the 68000 is all contained in the module intserv.s. 

On the vertical interval interrupt, the 68000 must refresh the object list for the object processor and increment the time variable. The object list refresh is very compact: only those data in the list which have been destroyed by the object processor need to be reconstructed; the remaining values survive from initialization. The time update is straightforward, except that a carry to the upper 16 bits must periodically be handled. 

On a GPU interrupt, the 68000 must set the semaphore flag to awaken the main decompression task. 

Management of the read and write pointers in the circular butfer the circular butfer circular butfer butfer is the most difficult the most difficult most difficult difficult technical aspect of aspect of of film playback. 

Figure 5-A illustrates the essentials of the process. The read read pointer for the video video data being being used by by the decompression code advances through the circular buffer, buffer, consuming data as as it goes. goes. Meanwhile, the write pointer for data coming from the CD follows along behind follows along behind behind it. Whenever the read pointer reaches the read pointer reaches read pointer reaches pointer reaches the end of the buffer, it is reset to the beginning and the consumption of data continues without and the consumption of data continues without the consumption of data continues without consumption of data continues without of data continues without data continues without continues without without interruption. When the write pointer reaches the end of the buffer, end of the buffer, of the buffer, the buffer, buffer, the write process write process process is suspended. suspended. 

The duty cycle for CD-ROM access is the ratio of the combined video/audio ratio of the combined video/audio of the combined video/audio the combined video/audio combined video/audio video/audio data rate to the playback rate to the playback the playback playback rate from CD-ROM. For a typical high-quality film, the combined rate might be 250 kBytes/sec; combined rate might be 250 kBytes/sec; rate might be 250 kBytes/sec; might be 250 kBytes/sec; be 250 kBytes/sec; 250 kBytes/sec; kBytes/sec; with a a double-speed CD-ROM (~350 kBytes/sec), this translates to a duty cycle of roughly 70%. a duty cycle of roughly 70%. duty cycle of roughly 70%. cycle of roughly 70%. of roughly 70%. 70%. 

Because the audio sample rate is typically much lower than the compressed than the compressed the compressed compressed video data rate, the audio read pointer, which is controlled by the DSP, DSP, advances at a much slower rate than the video read a much slower rate than the video read much slower rate than the video read slower rate than the video read rate than the video read than the video read the video read video read read pointer. Note, however, that there can be dramatic dramatic differences in audio throughput in audio throughput audio throughput throughput rates depending on depending on the audio format. For example, uncompressed 16-bit stereo audio at 22 kHz requires 4 times as much 22 kHz requires 4 times as much kHz requires 4 times as much requires 4 times as much 4 times as much times as much as much much data throughput as 8-bit mono. . | Since audio and video are multiplexed in the data stream, the data stream, the audio pointer will periodically jump ahead audio pointer will periodically jump ahead pointer will periodically jump ahead will periodically jump ahead periodically jump ahead jump ahead ahead to the next block of audio in the buffer. For this reason, this reason, reason, the audio audio pointer has a rather jagged trajectory has a rather jagged trajectory a rather jagged trajectory rather jagged trajectory jagged trajectory trajectory in buffer-time space; however, it always lies within within an envelope having the same slope as the trajectory having the same slope as the trajectory the same slope as the trajectory same slope as the trajectory slope as the trajectory as the trajectory the trajectory trajectory | of the video pointer, but offset from it by by a constant amount, constant amount, amount, as shown. shown. 

12 For example, the difference between an original sample rate of 22250 Hz and a playback sample sample rate of 22250 Hz and a playback sample rate of 22250 Hz and a playback sample of 22250 Hz and a playback sample 22250 Hz and a playback sample Hz and a playback sample and a playback sample a playback sample playback sample rate of 21867 of 21867 21867 is only only about 1.7%. 16 June, 1995 Property of“FER of“FER“FER Atari Corporation © 1995 Radius, Inc. 1995 Radius, Inc. Radius, Inc. Inc. 

**==> picture [1 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


Page 19 

2 

| 

**==> picture [534 x 574] intentionally omitted <==**

**----- Start of picture text -----**<br>
Cinepak For Jaguar<br>| 12 | 13 |<br>i © 8) &<br>Y// wvia<br>A Ke Of<br>©<br>:<br>' Qal : Rya) / ~ Ce ©) &/<br>Ey nN 4 RS Qe<br>3 ee 7 a et<br>|<br>ae” SAS » e/<br>; «\* Q 7 ~\ » Qf<br>j Figure 5-A — Pointer trajectories vs. time in circular buffer.<br>| Referring to Figure 5-A, we define four times of interest:<br>| t = zero-based time at which writing of CD-ROM data is initiated;<br>; t] = time interval required to fill circular buffer;<br>12 = zero-based expiration time for current video data in circular buffer;<br>13 = lag between audio read envelope and trajectory of video read.<br>, The heuristics of the buffer management process are as follows:<br>® Writing must be initiated Jate enough that the write pointer does not cross the tail end of the<br>; audio read envelope;<br>: ® Writing must be initiated soon enough that there is sufficient backlog of fresh data in the circular<br>_ buffer at the time the video read pointer is reset.<br>F In terms of the above-defined time values, these constraints translate to:<br>| t+tl]>2+68t< 12<br>4<br>| | Solving both inequalities for 12 - t and rearranging, we obtain the concise result:<br>0<12-t<tl-B<br>1 |<br>: | The most conservative design strategy is to split the difference, conservative design strategy is to split the difference, design strategy is to split the difference, strategy is to split the difference, is to split the difference, to split the difference, split the difference, the difference, difference, i.e.<br>**----- End of picture text -----**<br>


The most conservative design strategy is to split the difference, conservative design strategy is to split the difference, design strategy is to split the difference, strategy is to split the difference, is to split the difference, to split the difference, split the difference, the difference, difference, i.e. 12-t = (t1 - 3)/2 

r Be Csithis is the approach which has been taken in the sample player code. . Cae combination (t1 - t3)/2 is referred to as deltaTime in the sample code (see also Table 5.2). The | ae is computed halfway between labels CalcDest and ClearWindow in player.s. The comparison ae —Cetweeen 12. - tand deltaTime is made just after label CheckCDPlay, once it is determined that playPhase Bi ©1995 Radius Inc. & Atari Corp. Confidential FOR Information 16June, 1995June, 1995 1995 

16June, 1995June, 1995 1995 

Page 20 20 Cinepak For[Jaguar] 1 | | The mechanics of transferring CD-ROM data to the circular buffer are all managed by the GPU | interrupt service routine, which is loaded by an initial call to the CD-BIOS routine CD_init; this call is x | made as part of the LoadGPU subroutine in module utils.s (see Table 5.3). Subroutine ReadCDData 4 q takes care of all the overhead associated with setting up the BIOS calls to access the CD-ROM, : including specification of an "end-of-buffer" address. When the write pointer has advanced to this address, the transfer of data is automatically suspended until the next call to ReadCDData, no further gs intervention by the playback code is required. = | SOFrameRateControl———— isi‘iéiéiS The mechanism for frame rate control is fairly simple. The sample record (see Table 3.8) contains a fd | field which indicates the scheduled time for the sample. The clock time, maintained by the vertical P| interval interrupt, is compared with the scheduled time and the system waits until the two times are the 4 ] same. The code for doing this appears in player.s at label KillTime. Ss If the display of video falls behind schedule by an amount greater than maxDelay, then the catchUp flag ‘ : is set and frames are skipped until the next key frame is encountered. When this occurs, the catchUp 4 flag is cleared, the key frame is displayed and normal operation resumes. This code appears sixtocight #m : instructions on either side of label LookForKey in player.s. j : Under most circumstances, most circumstances, circumstances, there is ample ample processing power power in the system to play full-screen video at 24 24 or even even 30 frames frames per second, so the catch-up mode mode will seldom be activated. However, there may be may be be _ situations in which which developers will will also want want to use some some portion of the GPU of the GPU the GPU GPU processing bandwidth bandwidth for | 3 purposes other than video decompression; other than video decompression; video decompression; decompression; in these these cases, the catch-up mechanism catch-up mechanism mechanism is essential. essential. f 4 

| Page 20 20 

| | q 

i 

: Under most circumstances, most circumstances, circumstances, there is ample ample processing power power in the system to play full-screen video at 24 24 or even even 30 frames frames per second, so the catch-up mode mode will seldom be activated. However, there may be may be be situations in which which developers will will also want want to use some some portion of the GPU of the GPU the GPU GPU processing bandwidth bandwidth for purposes other than video decompression; other than video decompression; video decompression; decompression; in these these cases, the catch-up mechanism catch-up mechanism mechanism is essential. essential. | eeTT ertCti—C(CN.LCtiCOCO ‘(‘(‘RASCOCUCOQR In this section, we give a complete walkthrough of the sample code in player.s, highlighting major | points of interest along the way. Before beginning, we define in Table 5.5 the use of several dedicated 68000 registers; this will clarify some of the explanations as we progress. All other registers are available for scratchpad computation. Register Use |d4| Pointer to compressed frame data [dS [Counter for samples remaining in chunk Counter for chunks remaining in circular buffer |a3__| Pointer to current sample record in circular buffer q |a5___| **Pointer to** startcurrent of **c** urrenthunk record chunk inin chunk circular ta **b** ufferle **q** Table 5.5 — Dedicated 68000 registers in film player code. j Between the start of the code and the label WaitGPU, the system is initialized. Much of the code used j here -- especially in subroutines -- is either identical to, or a close derivative of early versions of generic 16 June, 1995 Property of “7% Atari Corporation © 1995 Radius, Inc. & Atari Corp. | 

{ 7 | 3 4 4 7 am 4 7 4 | @ | 4 = q = **q** == j a j < e eS 

| 

| | | 

Page 21 

| Cinepak ForJaguar aguar sample code distributed by Atari. Note, however, that some aspects of this code are no longer 

considered to be good examples of general Jaguar programming. The Lister subroutine has been modified to store certain entries in the object list in memory for | subsequent use by the vertical interrupt interrupt service routine. The USE_CDROM switch, set at assembly time, allows assembly of code that bypasses ail access to CD-ROM; this is useful during development for testing short (three- or four-second) films by | downloading them into memory from the hard disk.[the][ first][ access][ to][ the][ CD-ROM][ occurs.][Data][ from][ the][ CD-] |[After][ the][ GPU][ has][ finished][ initialization,] | ROM will be read into memory starting at location FILM_BASE. At label _ClearWindow, we allow the | write pointer to advance beyond the end of the sync search window, then call FindSync to locate the | start of the film. At label CheckFilm, we verify that the frame header tag (see Table 3.2) follows the | film sync. | At labels RelocTable and CopyCT, the entire chunk table is moved from wherever it happened to land in | the buffer to location FILM_BASE. Next, the mediaOffset variable is computed, since the byte offset for all subsequent accesses to CD-ROM data will be relative to the end of the chunk table. Following this, cBufBase and cBufSize are determined: the size of the chunk table is subtracted from the total | available memory and whatever is left is allocated to the circular buffer. The cType field in the frame description atom is tested and the video is switched to CRY if the CRY tag is found. | The value of dest is computed at label CalcDest. In the sample code, the film is centered on the display; | developers will obviously want to adapt this for their own purposes. After this, the filmChunks variable | js initialized by copying the value from the Count field of the chunk table (see Table 3.10). Next, three key time variables are computed: timelncr, maxDelay and deltaTime. Finally, register a5 is set to point to the first chunk record (see Table 5.5). We are now ready to look for the first chunk in the circular buffer. The search begins at cBufBase, with } async pattern given by $c(a5). At label .ClearWindow, we again wait to ensure that the write pointer has advanced beyond the end of the search window before calling FindSync. Upon returning from FindSync, we verify that the sample table header tag (see Table 3.7) follows the chunk sync. | At label .ChunkOK, register a4 is set to point at the start of the chunk and a3 to point at the sample table for the chunk. A call to SetNextGroup is made to determine which chunk will be the target of the next | access to CD-ROM. | — Two final steps are required before we are ready to play the film. At label WaitToFill, we allow the | write pointer to get far enough ahead that the read pointer will not catch up to it. At label WaitForTick, we restart the vertical interval time clock at zero, since all time references in the film file are zero-based. I Label ChunkLoop is the top of the outer program loop. Register d5 is loaded from the Count field of the sample table (see Table 3.7). The AtomSize field of the sample table is added to the base address of the sample table in a3 to determine the address of the first data sample in the chunk, this is transferred to d4. Next, a3 is adjusted to point to the current sample record. 

| 

© 1995 Radius Inc. & Atari Corp. 

Confidential 7FO® Information 

16 June, 1995 

| Page 22 22 ’ Label SampleLoop ' ROM emulator address | should be commented ' record | : | currentAtAt labelstimeDoVideovariable. and KillTime,If weAt labelstimeDoVideovariable. and KillTime,If we labelstimeDoVideovariable. and KillTime,If wetimeDoVideovariable. and KillTime,If weDoVideovariable. and KillTime,If wevariable. and KillTime,If we and KillTime,If we KillTime,If weIf we we 

Page 22 22 Cinepak For Jaguar | Label SampleLoop is the top of the inner program loop. The call to Snapshot generates atime history in { ROM emulator address space which is very useful for doing post-mortems during development; it a should be commented out or deleted in production versions of the code. The Time field of the sample ,- record is tested to determine whether the sample is audio or video. If it is audio, the arguments 4 specified in Section 5.6 are passed to the DSP and a branch is taken to the end of the sample loop; = otherwise, the program falls through to process video. Pd currentAtAt labelstimeDoVideovariable. and KillTime,If weAt labelstimeDoVideovariable. and KillTime,If we labelstimeDoVideovariable. and KillTime,If wetimeDoVideovariable. and KillTime,If weDoVideovariable. and KillTime,If wevariable. and KillTime,If we and KillTime,If we KillTime,If weIf we we are ahead the Time of schedule, field of thewe samplewait the Time of schedule, field of thewe samplewait Time of schedule, field of thewe samplewait of schedule, field of thewe samplewait schedule, field of thewe samplewait field of thewe samplewait of thewe samplewait thewe samplewaitwe samplewait samplewaitwait until recordtime is has read advanced and comparedto recordtime is has read advanced and comparedtotime is has read advanced and comparedto is has read advanced and comparedto has read advanced and comparedto read advanced and comparedto advanced and comparedto and comparedto comparedtoto the scheduled with the scheduled with the with the the : 'j value; otherwise, we check check to see how how far behind behind schedule we we have fallen. If the the slip exceeds exceeds the time _ specified by maxDelay, by maxDelay, maxDelay, we begin begin the catch-up process described in Section 5.9; otherwise, we we proceed i to display display the frame. The stack setup for the call to CheckKeyFrame CheckKeyFrame is specified specified in Table Table 2.1. - The call to ForceDelay ForceDelay at label DisplayFrame DisplayFrame can be be conditionally assembled to simulate the catch-up process during development; during development; development; there is no other no other other use for ForceDelay. Next the the stack is set up up for the call _ to PreDecompress PreDecompress (see Table 2.2). Following the the return, an error check is performed on the check is performed on the is performed on the performed on the on the the return { 3 value. At label StartDecomp, StartDecomp, the stack is prepared for the prepared for the for the the call to Decompress Decompress (see Table 2.3); error Ss checking is likewise likewise performed upon upon return. ; | All of the code which manages of the code which manages the code which manages code which manages which manages manages the dynamics of writing dynamics of writing of writing writing to the the circular buffer buffer (excluding the the initial 1 ' write) appears between between labels CheckCDPlay CheckCDPlay and NextSample. NextSample. The playPhase playPhase variable, described in 4 Table 5.2, is the key to controlling this mechanism: : @ When playPhase is 0, the CD_ROM is not playing and the only task is to check the difference —— between the expiration time and the clock time and compare this difference with deltaTime. Note rr | that the expiration time is recovered trom the Time field of the chunk record which is addressed by 7 PNextChunk. If it is time to start filling the buffer, the CD-ROM is given a seek address determined | 7 by the Start field of the chunk record pointed to by pNextChunk, playing is initiated with a write 4 destination of cBufBase, and playPhase is set to 1; otherwise, a branch is taken to NextSample. 4 i. @ When playPhase playPhase is 1, the CD-ROM CD-ROM is playing, playing, and the only the only only task is to to locate the start of the next of the next the next next ‘ | group of chunks of chunks chunks in the circular buffer. Before calling FindSync, FindSync, a test is performed performed to see see if the the | @ write pointer has has progressed beyond beyond the end of the sync search window. end of the sync search window. of the sync search window. the sync search window. sync search window. search window. window. If the the test fails, the | 4 program does does not wait, but branches to NextSample; branches to NextSample; to NextSample; NextSample; this is to avoid needless needless delay in the the middle of of | 7 a loop that must execute loop that must execute that must execute must execute execute in real time. If the the test passes, passes, the following following actions are taken: ‘ . . - The sync search is begun at cBufBase, with a sync pattern specified by the SyncPattern field a : of the chunk record addressed by pNextChunk, . | - Error checking is performed; . ’ : - The nextBufAddr variable is set at the sync location in the circular buffer and SetNextGroup { be is called to determine which chunk will be the target of the subsequent access to CD-ROM; a - playPhase is set to 2. q Z June, 1995 1995 Property ofPER ofPERPER Atari Corporation © 1995 Radius, Inc. & Atari Corp. ¢ 

: 

| | j i : . 

currentAtAt labelstimeDoVideovariable. and KillTime,If weAt labelstimeDoVideovariable. and KillTime,If we labelstimeDoVideovariable. and KillTime,If wetimeDoVideovariable. and KillTime,If weDoVideovariable. and KillTime,If wevariable. and KillTime,If we and KillTime,If we KillTime,If weIf we we are ahead the Time of schedule, field of thewe samplewait the Time of schedule, field of thewe samplewait Time of schedule, field of thewe samplewait of schedule, field of thewe samplewait schedule, field of thewe samplewait field of thewe samplewait of thewe samplewait thewe samplewaitwe samplewait samplewaitwait until recordtime is has read advanced and comparedto recordtime is has read advanced and comparedtotime is has read advanced and comparedto is has read advanced and comparedto has read advanced and comparedto read advanced and comparedto advanced and comparedto and comparedto comparedtoto the scheduled with the scheduled with the with the the value; otherwise, we check check to see how how far behind behind schedule we we have fallen. If the the slip exceeds exceeds the time specified by maxDelay, by maxDelay, maxDelay, we begin begin the catch-up process described in Section 5.9; otherwise, we we proceed to display display the frame. The stack setup for the call to CheckKeyFrame CheckKeyFrame is specified specified in Table Table 2.1. 

The call to ForceDelay ForceDelay at label DisplayFrame DisplayFrame can be be conditionally assembled to simulate the catch-up process during development; during development; development; there is no other no other other use for ForceDelay. Next the the stack is set up up for the call to PreDecompress PreDecompress (see Table 2.2). Following the the return, an error check is performed on the check is performed on the is performed on the performed on the on the the return value. At label StartDecomp, StartDecomp, the stack is prepared for the prepared for the for the the call to Decompress Decompress (see Table 2.3); error checking is likewise likewise performed upon upon return. 

All of the code which manages of the code which manages the code which manages code which manages which manages manages the dynamics of writing dynamics of writing of writing writing to the the circular buffer buffer (excluding the the initial write) appears between between labels CheckCDPlay CheckCDPlay and NextSample. NextSample. The playPhase playPhase variable, described in Table 5.2, is the key to controlling this mechanism: 

- @ When playPhase playPhase is 1, the CD-ROM CD-ROM is playing, playing, and the only the only only task is to to locate the start of the next of the next the next next group of chunks of chunks chunks in the circular buffer. Before calling FindSync, FindSync, a test is performed performed to see see if the the write pointer has has progressed beyond beyond the end of the sync search window. end of the sync search window. of the sync search window. the sync search window. sync search window. search window. window. If the the test fails, the program does does not wait, but branches to NextSample; branches to NextSample; to NextSample; NextSample; this is to avoid needless needless delay in the the middle of of a loop that must execute loop that must execute that must execute must execute execute in real time. If the the test passes, passes, the following following actions are taken: 

**==> picture [1 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


16June, 1995 1995 Property ofPER ofPERPER Atari Corporation 

| _ Cinepak ForJaguar Page 23 b Once playPhase has reached 2, there is nothing further to be done until the count (a7) of chunks |y below).currently in the circular buffer is exhausted. This situation is handled following label ResetBuffer (see | Atlabel NextSample, the Size field of the current sample record is added to the address (d#) of the | current sample to obtain the address of the next sample, and the pointer (a3) to the sample record is ; advanced to the next record. The counter (d5) for the number of samples in the current chunk is | decremented, and if not exhausted, a backward branch is taken to SampleLoop. If the sample count (d5) is exhausted, the counter (d7) for the number of chunks remaining in the buffer { is decremented. If there are no chunks left, a branch is taken to ResetBuffer, otherwise, the Size field j of the current chunk record is added to the address (a4) of the current chunk to obtain the address of the | next chunk in the buffer, and register a3 is set to point at the sample table for the next chunk. At this | point, a test is made for an empty chunk (no video or audio scheduled) and a backward branch is taken | to either ChunkLoop (not empty) or NextChunk (empty). | At label ResetBuffer, d7 is reloaded from the buffChunks variable, which is set either in SetNextGroup f or a few instructions below. If the value loaded is zero, the film is finished and we branch to Done. For a nonzero value, a5 is advanced to the next chunk record, a4 is loaded from nextBufAddr, a3 is set ‘ up to point to the sample table for the first sample in the new chunk, and playPhase is reset to zero. L Next, the filmChunks variable (maintained by SetNextGroup) is tested to see if there are any chunks W beyond those about to be processed that must be loaded from the CD-ROM. If so, a backward branch is } taken to ChunkLoop. | If not, playPhase is set to 3 and buffChunks is set to zero. The first action inhibits any further access to ; the CD-ROM; the second causes the program to terminate when the current group of chunks has been + exhausted. A backward branch is then taken to ChunkLoop to finish playing the film. There are several error conditions related to CD-ROM data integrity which are checked by the 68000 } and trapped via an illegal instruction. When the trap is taken, register dO will contain an error code, j according to the condition which caused the trap. Table 5.6 summarizes the traps and condition codes. Code Condition 1 No error; playback completed normally 1 Sync pattern pattern not found within search window found within search window within search window search window window 4 ‘FILM' tag tag not found found at start of film header start of film header of film header film header header |$33333333_|$33333333_|_| ‘STAB' tag not found tag not found not found found at start of sample table start of sample table of sample table sample table table 4 Data error detected by PreDecompress error detected by PreDecompress detected by PreDecompress by PreDecompress PreDecompress 

f These traps are useful for development and experimentation. They should never occur during playback | of a finished Jaguar film. | © 1995 Radius Inc. & Atari Corp. Confidential FER Information 16 June, 1995 

**==> picture [337 x 107] intentionally omitted <==**

**----- Start of picture text -----**<br>
Code Condition<br>No error; playback completed normally<br>Sync pattern pattern not found within search window found within search window within search window search window window<br>‘FILM' tag tag not found found at start of film header start of film header of film header film header header<br>|$33333333_|$33333333_|_| ‘STAB' tag not found tag not found not found found at start of sample table start of sample table of sample table sample table table<br>Data error detected by PreDecompress error detected by PreDecompress detected by PreDecompress by PreDecompress PreDecompress<br>|$55555555_| Data error detected by Decompress ;<br>Table 5.6 — Error codes and conditions.<br>**----- End of picture text -----**<br>


**==> picture [2 x 81] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>|<br>|<br>**----- End of picture text -----**<br>


16 June, 1995 

Page 24 Cinepak For Jaguar . &SampledaguarFilms§ .=§ =... wt t—i(i‘éi@ Three sample Jaguar films are provided on CD-ROM for demonstration purposes; any of the three | a films can be played using the sample code without modification. The film material has been approved - for distribution and can be freely used for demonstration or evaluation. . Table 6.1 summarizes the characteristics of the three sample films: | 1 Excerpt from "Jaws" “Escape" sequence Excerpt from "Back 7 [Resolution from Star Wars To the Future 3" Pd | 288 x 136 288 x 216 288 x 216 Pixel depth Hebits |e bits febits Sid | Color format Cinepak RGB Cinepak RGB Cinepak RGB f 4 24 fps 24 fps 24 fps | Compressed video rate {220 kB/sec 260 kB/sec 280 kB/sec 4 Audio sampie rate 22251.5 Hz 22251.6 Hz }22249H2 | a Film duration [2:33 min «dO min _————~«*d¢TOB min ——SCS~* I | 

| : | 

: | 

: 

| 

Table 6.1 — sample Jaguar films. 

Allby CD-ROMsthe sample player are single-sessioncode. with the film data recorded on track zero. This is the format expected 

**==> picture [12 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
a<br>**----- End of picture text -----**<br>


j 

16 June, 1995 

Property ofFER Atari Corporation 

© 1995 Radius, Inc. & Atari Corp. 

Page 25 

Cinepak For Jaguar 

| Cinepak is a registered trademark of Radius, Inc. Jaguar is a registered trademark of Atari Corporation. QuickTime, Macintosh and MPW are registered trademarks of Apple Computer, Inc. Think C isa | registered trademark of Symantec Corporation. CoSA and After Effects are registered trademarks of The Company of Science and Art. 

© 1995 Radius Inc. & Atari Corp. 

Confidential AUR Information 

16 June, 1995 | 

1 | : 4 

Page 26 

Cinepak For Jaguar 

: |g 4 1 | _ o ; : rg | 4 j F 4 SC ] : a } 4 a [a 

| | | j : 1 

“ os 

The Jaguar Cinepak Utility program runs on the Apple Macintosh under System 6.1 or later (older versions of System/Finder may work, but have not been tested). The QuickTime extensions must also be loaded. When you run the program, you’ll see a screen that looks like this: 

**==> picture [485 x 300] intentionally omitted <==**

**----- Start of picture text -----**<br>
" € File Edit Convert Utilities<br>Figure 8-A — Jaguar Cinepak Utilities Screen<br>We’ll assume that you know how to run programs and generally use the Macintosh computer. If this<br>isn’t true, please look through your Macintosh user’s manual before attempting to run the Jaguar<br>Cinepak Utility.<br>**----- End of picture text -----**<br>


The program displays a console window where messages from the various conversion functions will appear, as well as a menu bar at the top. The menus and the items they contain are described below. 

rrrrrrrtrtr—~—“O™C—CisOCCCCCs«CstSSstSstCéit‘(Cié‘ia‘NRCNCNCCOCC=CNwiCC™CDSS 

The File menu has just a single choice that allows you to quit the program. 

## ee 

lrrr—r—S~S<i‘i‘OONNONOiNtOONNCNNNOONCNONCNOiCNOC;sCCCOCCNCCCCCC. 

The Edit menu has the standard list of choices for Cut, Copy, Clear, and Paste. However, these options are not yet functional in this version of the utility. In future versions, they will allow you to edit the text shown in the console window. 

eedlrrrt—“N..CCiCNi‘iNOOWiNCCNNNNNONCN.;d§CCUGSFUS 

There are six conversion options available in the Convert menu. The first four are: Movie To Film, RGB To Cry, Smooth To Chunky, and Film To AIFF. These described in additional detail below. 

16 June, 1995 

Property of PO® Atari Corporation © 1995 Radius, Inc. & Atari Corp. | Ps 

Page 27 

j 

| Cinepak ForJaguar |[These] The last two choices are Convert[A][ QuickTime][ Movie][ and][ Convert][ QuickTime][ Movie][ Batch.] J options allow you to combine the individual conversion steps represented by the top four menu choices. | This is discussed in further detail below. ' There is also a Utilities menu with options for displaying information about Jaguar Cinepak Film files F and QuickTime movie files. These are discussed further detail below. 

| mm | The Movie To Film function allows you to convert a standard QuickTime Cinepak movie to the smooth | Jaguar film format. Selecting this menu item ieads to a dialog box that allows you to select the input file and the output file and conversion options as shown below: 

**==> picture [338 x 225] intentionally omitted <==**

**----- Start of picture text -----**<br>
r ¢ File Edit Convert Utilities<br>Convert Quicktime Movie to Cinepak Film File Quicktime Movie to Cinepak Film File Movie to Cinepak Film File to Cinepak Film File Cinepak Film File Film File File<br>input: [sash:Cinepak Movies:DL2S16Sc.movie Movies:DL2S16Sc.movie | (Browse) (Browse)<br>Assume RAW RAW sudio data is two's complement complement format<br>{i.e. movies created by CoSa After Effects} by CoSa After Effects} CoSa After Effects} After Effects} Effects}<br>Enter desired audio chunk size, in 1/100ths<br>|<br>of a second (from second (from (from 10 to to 100)<br>16-bit Sound Compression: Sound Compression: Compression: ® No Compression No Compression Compression<br>© Scale Scale 16-bit to 8-bit (lossy)<br>O Square Square Root (lossy)<br>Figure 8-B — Movie To Film — Movie To Film Movie To Film To Film Film dialog<br>**----- End of picture text -----**<br>


: Convert Quicktime Movie to Cinepak Film File Quicktime Movie to Cinepak Film File Movie to Cinepak Film File to Cinepak Film File Cinepak Film File Film File File input: [sash:Cinepak Movies:DL2S16Sc.movie Movies:DL2S16Sc.movie | (Browse) (Browse) Assume RAW RAW sudio data is two's complement complement format {i.e. movies created by CoSa After Effects} by CoSa After Effects} CoSa After Effects} After Effects} Effects} Enter desired audio chunk size, in 1/100ths | { of a second (from second (from (from 10 to to 100) : 16-bit Sound Compression: Sound Compression: Compression: ® No Compression No Compression Compression © Scale Scale 16-bit to 8-bit (lossy) ] O Square Square Root (lossy) : ! Figure 8-B — Movie To Film — Movie To Film Movie To Film To Film Film dialog j The input file must be an existing QuickTime Cinepak movie. You can type in the name of the file ] yourself, or you can click on the Browse button at the end of the Input field and the standard Macintosh | file selector will appear and allow you to select the desired filename. In the event that the Output field is blank when you Browse for the input field, the input filename you select will be used to guess at the ee = sdesired output filename. You may either use the guess directly or edit it as required. eS CThe output file name may be specified by typing in a name or by selecting the Browse button and using the standard Macintosh file selector that appears. Any existing file with the same name as the output ; ae ile will be overwritten. If you use the file selector to enter the output filename, you will be given a F warning, but not if you simply type it in. Note: Using a filename extension of “.SRG” is recommended. 

| 

‘ 

© 1995 Radius Inc. & Atari Corp. 

Confidential FOR Information 

16 June, 1995 

Page 28 Cinepak For Jaguar The Assume RAW Audio Data... checkbox allows you to inhibit the conversion of “Raw” audio tracks in = : the source QuickTime movie to the “Two’s Complement” format needed for proper playback on the ; Jaguar.}3 | = Audio data from the source movie is placed into the destination file in chunks interleaved with the video data. The length of each audio chunk is specified by the Enter Audio Chunk Size... edit box. This value 4 is specified as n/100ths of a second, and should ordinarily be about 3/4 the size of the chunk size you = will later specify in the Smooth To Chunky conversion process. The default size is 75/100ths of a g second. Note that the actual amount of data placed into the audio chunk depends on the format of the & audio data. If you use 16-bit stereo audio it will take 4 bytes per sample, versus 1 byte per sample for8bit mono. t | : Assuming an audio chunk size of 75/100ths of second, and video running at 24 frames per second, the ' audio will be placed into the destination file in the following way: the first audio chunk will be placed | | in the destination file immediately after the first frame of video. The second audio chunk will be : inserted after video frame #10. The remaining audio chunks will be inserted every 18 video frames. = This forward temporal bias in the audio stream means that the audio will play interrupted, as we will Pg always have a little more audio remaining in the buffer than we have video, even in cases where the ; ; video playback starts to lag behind real time. = You may specify audio chunk sizes from 10/100ths to 1 second. If you later specify chunk sizes less |P| than 1.0 seconds long in Smooth To Chunky, you should reduce the audio chunk size accordingly. . However, please note that changing the audio chunk size to less than 3/4 of the chunk size later | specified in Smooth To Chunky may affect the audio playback of the movie. If you have problems, try , | increasing the audio chunk size. | @ If the source QuickTime movie has a 16-bit audio track, then you have the option of compressing the 4 audio data. There are two ways to do this. The first method is to simply scale the 16-bit samples to 8- ye bit. The second method uses a special square root compression algorithm. Each 16-bit audio sample is 4 I converted to an 8-bit encoded value as follows: : q 8-bit encoded value = sqr(original sample value / 2) 4 The 8-bit encoded values are then placed into the destination film file. During playback, these encoded j 4 : sample values are expanded back to 16-bit. This compression method is still lossy (i.e. the output is not | 4 | quite the same as the input), but the results are usually more pleasing to the ear than simply scaling 16<7. bit values to 8-bit. a 13 QuickTime movies typically specify either a “RAW” audio track or a “Two’s Complement” audio track. The “Raw” ] q : ' type is normally the binary-offset format that is the default audio format used by the Macintosh. However, “Raw” also 4 EB 1 means the actual data format is not precisely defined, and some “Raw” audio tracks may not require conversion. This is 1 Do the case with movies created by Adobe (CoSA) After Effects, for example. Selecting the Assume RAW Audio Data... q - checkbox will inhibit the conversion of “Raw” audio tracks. : = QuickTime movies that specify a “Two’s Complement” audio track will normally not be converted regardless of the . se! checkbox setting. However, if you hold down the Shift+Command keys on the keyboard when selecting the menu a - choices Movie To Film, ConvertA QuickTime Movie, or Convert QuickTime Batch, these tracks will be converted if the j * checkbox is not selected. (Remember, the checkbox says “the audio is already Two’s Complement, leave it alone.”) F o | 16 June, 1995 Property of“FPR Atari Corporation © 1995 Radius, Inc. & Atari Corp. | = 

Page 29 ]} Cinepak For Jaguar A QuickTime Movie : The actual Movie To Film conversion process is also accessed through the Convert and Convert QuickTime Batch options. |mn ! The RGB To CRY function expands Cinepak-compressed RGB video data in a smooth format Jaguar } Cinepak film to either CRY or RGB uncompressed. The movie’s smooth film structure is not changed. ’ © Ente Edit Convert Utilities . ’ Convert a Cinepak film from compressed RGB format into _ Jaguar-specific CRY format. Please enter the input filename (an , AGB-format Cinepak film) and the output filename (a CRY-format : : Cinepak film). , | rDisableleave data AB->cAYin expandedConversion,RGB format. = : Butput: [sash:Cinepak Movies:012$16Sc.scq | | eee 3 ; oS Figure 8-C — RGB to CRY dialog 4 | thea smooth-format Jaguar film from the Cinepak compressed-RGB color format to either the Atari @ ~—CJaguar CRY format, without altering the smooth film structure. Selecting this menu item will lead to a B® dialog box where you can select the input file, output file, and conversion options. q : The input file must be an existing Jaguar Cinepak film in compressed-RGB format previously converted with Movie To Film. You can type in the name of the file yourself, or you can click on the Browse a button at the end of the Input field and the standard Macintosh file selector will appear and allow you ‘Be sito elect the desired filename. In the event that the Output field is blank when you Browse for the input field, the input filename you select will be used to guess at the desired output filename. You may } — either use the guess directly or edit it as required. ; : The output file name may be specified by typing in a name or by selecting the Browse button and using . we Oitthe standard Macintosh file selector that appears. Any existing file with the same name as the output file will be overwritten. If you use the file selector to enter the output filename, you will be given a | warning, but not if you simply type it in. Note: Using a filename extension of “.SRG” is recommended for movies with RGB video, or “.SCR” for movies with CRY video. Ss The RGB To CRY function first decompresses the proprietary Cinepak RGB color data to a non| P| compressed RGB format. Checking the Disable RGB->CRY Conversion... checkbox disables the final vO conversion of this data to CRY mode. increases the amount of data needed | Note that the decompression operation performed by RGB To CRY , | to represent each frame of video, so various entries in the header and sample table are also adjusted to ’ ( © 1995 Radius Inc. & Atari Corp. Confidential JER Information 16 June, 1995 

q 

Page 30 Cinepak For Jaguar reflect the change. The increase in size of the resulting film is typically about 10%, so there is minimal s penalty in either storage or CD-ROM access requirements. cok iq Cinepak films using non-compressed RGB or CRY video will consume about 10-15% less GPU ' processing bandwidth on playback than the same film using compressed-RGB video. The reason is that gs the processing step which converts from compressed to expanded RGB is bypassed (having already a been done off-line). For certain highly complex movies where the frame rate may fall slightly short of | 24 fps, developers may wish to take advantage of this time savings in order to squeeze maximum 4 performance out of the system. : The actual RGB To CRY conversion process is also accessed through the Convert[A][ QuickTime][ Movie] . and Convert QuickTime Movie Batch QuickTime Movie Batch Movie Batch Batch options. Ce eee . . FF The Smooth To Chunky menu item converts a Jaguar film from the smooth file format to the chunky Smooth To Chunky menu item converts a Jaguar film from the smooth file format to the chunky To Chunky menu item converts a Jaguar film from the smooth file format to the chunky Chunky menu item converts a Jaguar film from the smooth file format to the chunky menu item converts a Jaguar film from the smooth file format to the chunky item converts a Jaguar film from the smooth file format to the chunky converts a Jaguar film from the smooth file format to the chunky a Jaguar film from the smooth file format to the chunky Jaguar film from the smooth file format to the chunky film from the smooth file format to the chunky from the smooth file format to the chunky the smooth file format to the chunky smooth file format to the chunky file format to the chunky format to the chunky to the chunky the chunky chunky ‘ f format. Selecting this menu item will lead to a dialog box where you can select the input menu item will lead to a dialog box where you can select the input item will lead to a dialog box where you can select the input will lead to a dialog box where you can select the input lead to a dialog box where you can select the input to a dialog box where you can select the input a dialog box where you can select the input dialog box where you can select the input box where you can select the input where you can select the input you can select the input can select the input select the input the input input file, output output g i file, and conversion options. conversion options. options. - " ¢€ file Edit Convert Utilities 1 | Convert a Cinepak fitm from the smooth temporal format (output by | q 4 : <Movie To Film> or <RGB to CRY» into the chunky format that is recommended for playback from CD-ROM. = F. Please enter the input filename ond the output filename. : : j j HIspecify chunk uration [1.0 f 4 Input: [sash:Cinepak Movies:DL2516Sc.srg ; 4 { Figure 8-D — Smooth To Chunky dialog 1 1 4 The input file must be an existing smooth-format Jaguar Cinepak film previously created by either | ‘ 4 Movie To Film or RGB To CRY. You can type in the name of the file yourself, or you can click on the _ 4 Browse button at the end of the Input field and the standard Macintosh file selector will appear and 4 | allow you to select the desired filename. In the event that the Output field is blank when you Browse for the input field, the input filename you select will be used to guess at the desired output filename. ; 4 You may either use the guess directly or edit it as required. | | The output file name may be specified by typing in a name or by selecting the Browse button and using: q | | : the standard Macintosh file selector that appears. Any existing file with the same name as the output | & file will be overwritten. If you use the file selector to enter the output filename, you will be given a warning, but not if you simply type it in. Note: Using a filename extension of “.CRG” is recommended | a for movies with RGB video, or “.CCR” for movies with CRY video. j S | 16 June, 1995 Property of “AER Atari Corporation © 1995 Radius, Inc. & Atari Corp. s 

: . and Convert QuickTime Movie Batch QuickTime Movie Batch Movie Batch Batch options. Ce eee . . The Smooth To Chunky menu item converts a Jaguar film from the smooth file format to the chunky Smooth To Chunky menu item converts a Jaguar film from the smooth file format to the chunky To Chunky menu item converts a Jaguar film from the smooth file format to the chunky Chunky menu item converts a Jaguar film from the smooth file format to the chunky menu item converts a Jaguar film from the smooth file format to the chunky item converts a Jaguar film from the smooth file format to the chunky converts a Jaguar film from the smooth file format to the chunky a Jaguar film from the smooth file format to the chunky Jaguar film from the smooth file format to the chunky film from the smooth file format to the chunky from the smooth file format to the chunky the smooth file format to the chunky smooth file format to the chunky file format to the chunky format to the chunky to the chunky the chunky chunky f format. Selecting this menu item will lead to a dialog box where you can select the input menu item will lead to a dialog box where you can select the input item will lead to a dialog box where you can select the input will lead to a dialog box where you can select the input lead to a dialog box where you can select the input to a dialog box where you can select the input a dialog box where you can select the input dialog box where you can select the input box where you can select the input where you can select the input you can select the input can select the input select the input the input input file, output output i file, and conversion options. conversion options. options. 

4 F. 

. | Cinepak For Jaguar Page 31 ) Checking the Specify Chunk Duration checkbox wiil cause an edit box to appear where you can specify p the chunk duration, in seconds, of each chunk of data that will be placed into the destination file. If this } option is not invoked, a default value of 0.25 seconds is used. 

| 

: 

j The actual Smooth To Chunky conversion process is also accessed through the Convert[A][QuickTime] | Movie and Convert QuickTime Movie Batch options. **..** : **.** : i . @@#& «2... : Warning! The FilmToAIFF option was designed in response to certain CD Mastering j software packages which could not accept a raw binary file and use it to create a CD j track. Note that the files written by FilmToAIFF do not follow the standard Jaguar CD 4 track format specification. Using FilmToAIFF is no longer recommended. Use the j Jaguar CD Track Creator program instead. See the information in the Jaguar CD Mastering section of the Jaguar CD-ROM chapter, as well as section 4.1.1 of this 4 chapter for additional information. 

| The Film To AIFF menu item converts a Jaguar film file to an AIFF file, suitable as input to CD-ROM | recording software which accepts audio files in this format. Selecting this menu item will lead to a | dialog box where you can select the input file, output file, and conversion options. 

## Page 31 

The Smooth To Chunky function takes a smooth-format Jaguar Cinepak film and converts it into a @ **e** = chunky-format Jaguar Cinepak film. This essentially takes audio chunks and frames of video from the “= smooth file and places them into chunks of a particular playback duration. j See sections 3 to 3.2 for further information on the smooth and chunky Cinepak film formats. 

| j A chunk never begins with audio; an audio block which happens to fall on a chunk boundary is always | _ incorporated as the final data element in the earlier of the two chunks. : | The global chunk table is inserted near the beginning of the file, following the frame description atom. Ses Synchronization blocks and local (i.e. intra-chunk) sample tables are inserted at the beginning of the ; 1 film data for each chunk. This is so that the data for each individual chunk may be reliably located when @e—srreading from CD-ROM. 

Developers are free to experiment with the chunk duration. On the low end, the value is limited by the } — increase in the length of the chunk table, which must be stored in its entirety in DRAM. On the high end, the duration is limited by increasingly inefficient use of DRAM buffer space. Incomplete chunks at the end of the circular buffer constitute wasted storage; the fraction of wasted buffer space will increase with progressively larger chunk durations. 

| 

{ 

© 1995 Radius Inc. & Atari Corp. 

Confidential APR Information 

16 June, 1995 

q | 

Page 32 

Cinepak ForJaguar 

| . +» | j | 

q 

4 

i] 

| 

g = ' I = 1 <7 » j i 4 f 3 { j + 4 | j - , 4 | | _ : ' rr y4 } 4 ; 7 ' a 4 7 

: 

: 

: 

|| 

**==> picture [247 x 121] intentionally omitted <==**

**----- Start of picture text -----**<br>
"_€ file Edit tonuert Utilities<br>Convert Film to AIFF File.<br>Please enter the input filename and the output filename.<br>J Add Wrapper around film data?<br>Output: [sash:Cinepak Movies:DL2$16Sc.aiff<br>**----- End of picture text -----**<br>


Figure 8-E — Film To AIFF dialog 

The input file must be an existing Jaguar Cinepak film in either smooth or chunky format created by Movie To Film, RGB To CRY, or Smooth To Chunky. You can type in the name of the file yourself, or you can click on the Browse button at the end of the Input field and the standard Macintosh file selector will appear and allow you to select the desired filename. In the event that the Output field is blank when you Browse for the input field, the input filename you select will be used to guess at the desired output filename. You may either use the guess directly or edit it as required. 

The output file name may be specified by typing in a name or by selecting the Browse button and using the standard Macintosh file selector that appears. Any existing file with the same name as the output file will be overwritten. If you use the file selector to enter the output filename, you will be given a warning, but not if you simply type it in. Note: Using a filename extension of “AIFF” is recommended. 

There is also a checkbox for an option that is used to cause the film data to be "wrapped" by the header/sync and tailer data structures defined in Table 4.1 before the AIFF file header is added. 

, 

This tool is included primarily as a convenience to those developers using CD-ROM mastering software which cannot do this conversion or which do not accept raw data files as input. [MF3} Developers who choose to use or adapt Film To AIFF should be aware of three work-arounds in the code which have been introduced to compensate for bugs in the driver software that was used in creating the sample CD-ROM: 

e The header and tailer sizes are increased by two bytes each to preserve long alignment of the film data on the recorded medium: (see referenceso to HACK_SIZE in: the definitionsous of HEAD_SIZE and TAIL_SIZE); 

- \ SYNC_SIZE is omitted from the computation offileSize; e The numSampleFrames field of commonChunk does not correctly account for the number of channels (=2) and the number of bytes per sample (=2). 

16June, 1995 

Property of FOR Atari Corporation 

**==> picture [40 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
| a<br>**----- End of picture text -----**<br>


© 1995 Radius, Inc. & Atari Corp. 

Page 33 

| 

Cinepak For Jaguar The latter two work-arounds are needed to prevent spurious failure of the recording process and the F attendant destruction of a CD-ROM. 

The actual Film To AIFF conversion process is also accessed through the Convert[A][ QuickTime][ Movie] | and Convert QuickTime Movie Batch options. mann me es | The ConvertA QuickTime Movie menu item brings up a dialog that combines the functionality of the | separate Movie To Film, RGB To CRY, Smooth To Chunky, and Film To AIFF functions into one place. | Please see the documentation for those functions before using Convert[A][ QuickTime][ Movie.] The options in the ConvertA QuickTime Movie dialog correspond to the options in the separate Movie | To Film, RGB To CRY, Smooth To Chunky, and Film To AIFF dialogs with just a few exceptions, as detailed below. 

**==> picture [510 x 302] intentionally omitted <==**

**----- Start of picture text -----**<br>
First, the options currently selected affect the output filename that is automatically created when you<br>Browse the input filename. For example, if you have RGB Compressed and Smooth Film selected, the<br>| output name will have an extension of “.SRG”. But if you have CRY Non-Compressed and Chunky<br>Film selected, the output name will have an extension of “.CCR” instead.<br>’ ¢ File Edit Convert Utilities<br>P ;<br>Convert Quicktime Movie to Jaguar Cinepak Film Fite<br>! i| Output:RAL audio date [is] [2's]  complement: [(0] 16-bit Sound Compression: :<br>@ No Compression f|<br>Audio chunk size, in 1/100ths O Scale 16-bit to 8-bit Qossy)<br>of a second (from 10 to 180): [75 | O Square foot (tossy) ;<br>j Cinepak Film Format: Chunk Video Data Format:<br>j © Smooth Film Ouration: @ RGB Compressed<br>@Chunky Film (seconds) OCRY Non-Compressed<br>© AGB Non-Compressec a<br>4<br>; File Format:<br>@ Raw Cinepak Film Data pe<br>i<br>: O AIFF File w/o wrapper { Cancet- j<br>j O AIFF File w/wrapper<br>: Figure 8-F — ConvertA QuickTime Movie dialog<br>**----- End of picture text -----**<br>


{ : | 

| In the event you want to change the options after having selected the input filename, you can force the | dialog to recreate the output filename to match the new options by clicking on the “?” button next to the | output filename field’s Browse button. J Just because the choices are all in one dialog does not change the fact that there are still up to four : | | separate conversion steps involved. When you exit the dialog, Convert[A][ QuickTime][ Movie][ will][ call][ the] | Movie To Film conversion as well as whichever of the three other conversion steps are appropriate for | the options you have selected. 4 I ©1995 Radius Inc. & Atari Corp. Confidential FO® Information 16 June, 1995 ; 

Page 34 

Cinepak For Jaguar 

: 

ui abt : | ’ 5 Ss 

; beginning of the the conversion process. | Holding down down the SHIFT+COMMAND SHIFT+COMMAND keys when selecting when selecting selecting the ConvertA QuickTime Movie menu ConvertA QuickTime Movie menuA QuickTime Movie menu QuickTime Movie menu Movie menu menu item will cause the Raw Audio Data Raw Audio Data Audio Data Data is Two’s Complement checkbox Two’s Complement checkbox Complement checkbox checkbox setting to affect QuickTime QuickTime movies | with the “twos” audio format as well well as movies movies with the “raw” audio format. | 87 Convert QuickTime MovieBatch = = The Convert QuickTime Movie Batch menu item brings upa Convert QuickTime Movie Batch menu item brings upa QuickTime Movie Batch menu item brings upa Movie Batch menu item brings upa Batch menu item brings upa menu item brings upa item brings upa brings upa upaa file selector which selector which which allows you you to select the filename of a text of a text a text text file containing a a list of QuickTime of QuickTime QuickTime movie files to be converted. be converted. converted. This file may may be arbitrarily long and can and can can therefore allow you you to process dozens dozens or even hundreds even hundreds hundreds of QuickTime QuickTime movies at once. 

Convert QuickTime MovieBatch = Cd The Convert QuickTime Movie Batch menu item brings upa Convert QuickTime Movie Batch menu item brings upa QuickTime Movie Batch menu item brings upa Movie Batch menu item brings upa Batch menu item brings upa menu item brings upa item brings upa brings upa upaa file selector which selector which which allows you you to select the filename of a text of a text a text text file containing a a list of QuickTime of QuickTime QuickTime movie files to be converted. be converted. converted. This file may may be & arbitrarily long and can and can can therefore allow you you to process dozens dozens or even hundreds even hundreds hundreds of QuickTime QuickTime movies g once. a line in the batch the batch batch file must specify must specify specify a list of desired of desired desired options and the source filename. You may also j specify the destination filename, but if none none is specified, one will be created based on on the conversion = options selected. The available command command line options are: { - Option Description -afn} Specify audio chunk size. {n} is the chunk size in n/100ths of a second. The default a 2 value is 75. Must be in range of 10 to 100. f | -c{n} Chunk duration in seconds for chunky movies. The {n} value should bea floating point 4 number. The default is 1.0. Note that this number affects your CD-ROM buffer size 4 requirements: longer chunk durations require a larger buffer. . 4 -emp{n} Compress 16-bit audio (if that's what is in the source movie) {n} must be one of: ; ; 0 = Nocompression (default) | 4 1 = Simple 16-bit to 8-bit scaling = -f{n} 2File= Square-Rootformat. {n} represents16-bit to 8-bit the desiredcompressionfile format. In most cases [; @ oan | 01 == Raw AIFF Cinepak w/o wrapper film (defautt) =j 2 = AIFF w/wrapper -_ -film{n} . Specify Cinepak film format. {n} must be one of: , 3 0 = Smooth (suitable for small RAM-based movies, not.really for CD-ROM) 4 -twos Specify1 = Chunky that “RAW”(default, audio designed tracks forin CD-ROM source QuickTime playback)movie are Two's complement ,_ 4 format and do not need conversion. Note that if the QuickTime source movie has the . 4 “twos" flag set on the audio tracks, this conversion is deselected unless you hold down | the SHIFT+COMMAND keys when selecting the Convert QuickTime Batch menu item —— (in which case it uses the -twos flag). The default for this option is off. p -_ 

1 

| 

Any intermediate files required between the source and final destination will be created and deleted as needed. You will typically need to have approximately 2.2 times as much free disk space available as the size of your source movie. Please note that the amount of free disk space is not checked prior to the beginning of the the conversion process. 

Holding down down the SHIFT+COMMAND SHIFT+COMMAND keys when selecting when selecting selecting the ConvertA QuickTime Movie menu ConvertA QuickTime Movie menuA QuickTime Movie menu QuickTime Movie menu Movie menu menu item will cause the Raw Audio Data Raw Audio Data Audio Data Data is Two’s Complement checkbox Two’s Complement checkbox Complement checkbox checkbox setting to affect QuickTime QuickTime movies with the “twos” audio format as well well as movies movies with the “raw” audio format. 

Each line in the batch the batch batch file must specify must specify specify a list of desired of desired desired options and the source filename. You may also specify the destination filename, but if none none is specified, one will be created based on on the conversion options selected. The available command command line options are: 

**==> picture [3 x 15] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


**==> picture [6 x 15] intentionally omitted <==**

**----- Start of picture text -----**<br>
Zin<br>**----- End of picture text -----**<br>


16 June, 1995 

Property of 7O® Atari Corporation 

© 1995 Radius, Inc. & Atari Corp. 

| 

Page 35 ] Cinepak For Jaguar p | vin}Option VideoDescription mode. {n} represents the desired video mode and must be one of: 0 = RGB compressed (default) { 4 = CRY Expanded 2 = RGB Expanded |[These][ options][ allow][ you][ to][ select][ the][ same][ items][ as][ the][ various][ conversion][ dialog][ boxes.][A][typical] } batch file might look like this: 1 of This is a comment in my batch file... . P § This is another comment. tf This is the last (third, actually, in a series of three) comment. | ea37 -filml -c0.5 -v0 -f0 "sash:Cinepak Movies :DL2S16Sc .Movie" p add -filme -c0.6 -vl -cmp2 -f0 "sash:Cinepak Movies :DL3S16Sc .Movie”" a60 -filml -c0.75 -v2 -f1 “sash:Cinepak Movies:DL4S16Sc .Movie" 

| Note that any line in a batch file that starts with "#" or "//" is ignored and may be used as 2 comment. | Blank lines are also ignored. 

| The first line in the example that would be processed specifies an audio chunk size of 37/100ths of a S second (-a37), a chunky format film (-film1), a chunk size of 0.5 seconds (-c0.5), RGB-compressed video (-v0), and a Raw Cinepak data file (-f0). This command would cause the file"sash:Cinepak Movies: DL2$16Sc.crg" to be created - from the source file "sash: Cinepak Movies:DL25 16Sc.Movie". (Remember, if not otherwise | specified, the name of the destination file is always generated automatically based on the conversion options selected.) In a batch file, all command line options are persistent from one line to the next unless changed. If one ; command line in the batch file sets up certain options, they remain in effect until changed by another command line. For example, the second example shown above specifes 16-bit audio compression using | the square root method (-cmp2). The third command line example does not specify any ".cmp" option, | s0 the "-cmp2" from the previous command will carry over. | — This option is essentially a batch file version of the Convert A QuickTime Movie option, and therefore | similar rules apply. In particular, please note that that the individual functions Movie To Film, RGB To | CRY, Smooth To Chunky, and Film To AIFF are called by the batch file processor to perform whatever fF conversions are required. | — When doing batch file processing, the disk-space availability check done by the individual menu choices | and dialogs is NOT PERFORMED. So make sure you have sufficient disk space before attempting a } batch conversion. Try to ensure that you have about as much free disk space as the total disk space of your source files, plus the size of your largest file. (i.e. if you have 5 files totaling 10mb, and the largest | file is 2mb, then you need about 12mb free disk space total. However, keep in mind these are rough estimates and give yourself as much room as possible. 

© 1995 Radius Inc. & Atari Corp. Confidential ‘JPR Information 

16 June, 1995 

| Page 36 . 

Cinepak For Jaguar 

. | AN | § |Z s a 

od 

| 

The ShowFilm Info menu item brings up a dialog where you can select a Jaguar film file and select one i of three different degrees of verbosity. = r @ file Edit Coavert Utilities ] | Display information about a Cinepak Fiim © Fite Details - ‘ O File Details, Chunk Details — © File Details, Chunk Details, Sample Details F Input: [|sash:Cinepak Movies:DL2S16Sc.srg | E : j Figure 8-G — Show Jaguar Cinepak Film Info dialog j : To get just the basic information about a Jaguar Cinepak Film, select the File Details radio button. To 1 4 also get the the details for each chunk of the Jaguar Cinepak Film, select the File Details, Chunk Details | 4 radio button. To get the maximum amount of information, including the details of each block of sample § 4 data in the Jaguar Cinepak Film, select the radio button File Details, Chunk Details, Sample Datails. fg The specified film file will be analyzed and the requested information about the contents will be 3 q dumped to the screen. To pause the screen output, hold down the mouse button, and release it when you b want to continue. (The information printed is identical to the FILMINFO tool available for MSDOS.) ; 4 8.9 Show QuickTime Movieinfo = ' toThe ShowQuickTimeselect a QuickTime Moviemovie. InfoThis menu will itemcause bringsinformation up a standardabout Macintoshthe movie, Filesuch selectoras the movie and allowslength, you ]| 77 . 16 June, 1995 Property of FER Atari Corporation © 1995 Radius, Inc. & Atari Corp. } 7 

I WARNING: Please keep in mind that each movie can take up to several minutes at a time to convert. Large movies can easily take an hour or more. So before you start processing a batchfile with ahundred commands, remember that it could easily take several days to finish. Make sure that that you have a good understanding of the process and always run a reality check using just one or two movies first. 

Also, the batch file processing feature removes the necessity of you, the user, having to sit at the computer and guide each file through the conversion process, but it does not reduce the time required to convert each file. Because there is currently no facility for breaking out of the middle of a batch job, it is suggested that you try converting just a few movies at a time until you get a feel for how long the process is going to take. The time required for each of the conversion steps is directly related to the size of the file you are converting, with the exception of CRY-expanded or RGB-expanded video output, which will also depend on the compression ratio of the original video data. 

**==> picture [2 x 25] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


**==> picture [3 x 60] intentionally omitted <==**

**----- Start of picture text -----**<br>
.<br>|<br>**----- End of picture text -----**<br>


Cinepak For Jaguar 

Page 37 

& 

P 

frame size, number of video frames per second, type of audio tracks, audio data format, and so forth, will be printed into the console window. 

pee © 1995 Radius Inc. & Atari Corp. Confidential 7O% Information 16 June, 1995 

