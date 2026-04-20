Page 1 

|| | | | | 

{ | : 

| Technical Reference s Waguar Console Hardware ReleaseNotes — = This document describes the Jaguar console hardware as far as software development is concerned. It is — acompanion to the Jaguar Software Reference Manual - Tom & Jerry.[_] Ce | General Guidelines For So | Do not ever write to any of the following registers. The BOOTROM (in a.standard retail cosole) or the | STUBULATOR (in a development console) will set them up. Especiallythe: settings in CLK2,;CLK3 and HP registers must be correct to make the hardware workat all and preventdot craw] in particular. a rMEMCONT—[$F00000 SSCS rMEMCON2 | $Fo0002 | FOLKY | SFi0o10 a a MEEK SF10012 SS —*dB | SF000% | berks srtooia (aka CHROMA DI [WBE [srooowz Lap sroome SVS [Foods rasFo00 **s** a t rHBE_-‘([$roogs2 FEE | $EQ004C @ The VMODE register and object piocessor Will be initialized.and started after reset by the bootcode. Then the only object in the object list will bé:a stop object, which willeffectively display a blank screen and send the correct video synchfonisation sigitals #6’ the monitor or TV. This also allows the phase locked loop to settle, which takes'about a segond[at] start-up.[Do] not ever turn video off again![(i.e.] by writing a zero to VMODE !!) AEP “ CHEE 

Audio is mute after reset. You have'to turn it on by setting bit 8 in register JOYSTICK. 

Jaguar cartridges normally contain a 128 byte serial EEPROM to be able to save highscores and other user specific information. For informationhew'to access the EEPROM refer to EEPROM.ZIP of your developer Software Or from.our BBS. EEPROM cartridges currently use bit 0 of JOYSTICK. Do not rely on the readable statusof JQ'YSTICK bit 0 - it is random. 

1 

© 1995 Atari Corp. 

Confidential Information ‘JER Property ofAtari Corporation 

26 April, 1995 

Page 2 

Technical Reference J i 

| : 

| | | | 

| | | OR | 

FDO 

WO 

i 

| 

| 

## MemoryMap/Registerlist 

The tables below show the Jaguar hardware register list. For each item in this list, we show the equate as given in the JAGUAR.INC include file (or other appropriate include files), the name of the register as given in the Jaguar Software Reference Manual, the address of the registériti hexidecimal, and a twoletter code for how the register is to be used: ee ee RW= Read/Write WO= Write Only "3, RO = Read Of. 

Note: Those registers shown in BOLDFACE should never be modified byyour-programs. Theyare set up for you by the machine at boot-time. They are included:here for informatiésal:purposes only: 

|System|Setup Registers|Setup Registers|||
|---|---|---|---|
|HENCoRZ<br>hac|[Memory Control<br>Register2_____———~—=—=—S<br>== rooooz<br>Rw<br>| HorizontalCount<br>a,<br>SSCSCSC~CS|||<br>OR|
|py||TighePenverical<br>«duo|||
|one||ObjectstPomter<br>SSCS «dw||
|||[Horizontal BtankingEnd<br>=<br>SSSSSC«*||
|haDB]||| Horizont OisplayBegn2 = —SSSCSCSC~FOG||
|||‘egramimnableInterruptTimer<br>fFoo0s0-52[wo|||



5 June, 1995 

Confidential Information “FO® Property of Atari Corporation 

©1995 Atari Corp. 

Technical Reference 

Page 3 

@ GPU Registers ; Peace TGPUFIagsRegstertzid || Vepaxeeeamxa [Maire[MatricControtegistor———SSSSSCSCSCSCSCSC~*~———SSSCSCSCSCSC~C~SC~SC~Adcress Register OTOL HO eENDSpe}[DataRegister|Organisation OTBzt TWOHO LecaRE GPU Program Counter epee PESREDATA [GPU[HighSST Data ControvStatusRegister Register ORE TRA | Lemar [Divideremainderunt oa LP 

Blitter Registers * Must be refreshed after a BLIT EES _ a Must be refreshed if used to store dynamic data (i.e. arinner loop réad Geeurs or GOURD or GOURZ is set). aankttivns, OE st* Older versions of the Jaguar Software:ReferenceManital (v2.2 & earlier) reversed the order of these descriptions. The equates have not chafiged, so your Sdlif6e.code should be unaffected. | TRICBASE—ABaseRegsier Sf ozzO0 — EatSrracs [Flagsopr Register fo =y RincitsAl_PIXEL [AiAi PixelCippng PointerSze2) eh. OEE zzee F0220C CePATg Pom feveeee | oe csr a **r** sepvee oe | - FeSester—[arstepFrecionvene«oz wo A2_PIXEL A2 Pixel Pointer 2225... F02230 parvaar SHEE [aeraaa epvene a SSC*deaaoeee toe Sant SiimmendStatus Regater = —SSSSSCSCSCSCSCSC~ come ——|=~Courts Register esncs |Regster———SSSCSSSSSSS~*dSource Beta Pagzc WO | rs pSTo [Destination DataRegsier SSS oz | SS "DSTZ [Destination zZ Register SON PSSRCZ —""T SoyrceZRegstert «ORS WO BS SRCID | SouisezReaiter2SCSCCSCSCSCSCS* GN S-pard |_———=SCSC~C~CS~CS~SCSCSCS*~«~ PattonBeta Register SSFNC [tenetizinetement OG [WOWo S13 intensty™ SSCCSCSC~‘“‘SC‘C~S~S~*i ORS SeST «azintensityintensty SSCS mez Ee © 1995 Atari Corp. Confidential Information “JER Property ofAtari Corporation 5 June, 1995 

; 

Page 4 

Technical Reference 

: 

| 

| 

Oe 

| 

' 

| 

Og 

| 

| 

5 

] 

| 

: 

| 

## Jerry Registers 

**==> picture [502 x 142] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||
|---|---|---|---|---|
|wo|
|PSPITI_‘| TimertPrescaler|ec|CL|TOON|EMO|
|PapIT2|| TimertDwider|20002|[wo|
|SHODE|||Sealode|SSC|SCS|
|wo}|

**----- End of picture text -----**<br>


## Joystick Registers 

DSP Registers 

5 June, 1995 

Confidential Information “FER Property of Atari Corporation 

© 1995 Atari Corp. 

| | | | | | 

Page 5 Technical Reference Giaguar Video & System Clocks In the Jaguar console, the video clock is chosen to allow an inexpensive RF modulator system. This requires slightly different clock speeds for NTSC and PAL systems (but the difference is only about 0.01%). To be cost-effective, the GPU/DSP processor clock speed is the'Saitie:as the video clock speed, and the 68000 is 50% of this clock rate: i Video Clock 26.5638900.MHz “PEE GPU/DSP Clock Rate PEE ees | 68000 Clock Rate 73.295453 MHz | 13.29695 MEE: 4... Eee The video system of Jaguar is programmable within the precision of the supplied video clock. From the video clock, the system produces the pixel (or dot) clock. The ratio betweén.video and pixel clock is determined by high order bits of the VMODE register. The possible values Gr.the ratio are shown in the table below, along with the number of pixels that will,fit on screen overscanied:or non-overscanned. For both PAL.and NTSC the “safé” video area is about The numbers are the same for NTSC and PAL.” 40us wide. The area required to guarantee overscan is about SO448..,The table gives the number of pixels that can be displayed within these times for allavailable pixel claék dividers. Note that these numbers | @[ be] are[ used] not "nice"[ in][ deciding] computer[ your artwork] numbers like[and.abject] 320 or 256,[ sizes;] ‘Also,[these.] note[ numbers] that should these‘arenot simply be used rough in calculating guidelines to | values for the video hardware registers. ‘To properly inisfalize your program, including video, you must use the standardizedJaguar Startup Code described in the Jaguar Libraries section. 

Pixel Divisor vaiue Gf of pixels # of pixels for VMODE register Non-Overscanned Overscanned ae a NOR ea ee eee — eis ae se We recammend that ALLsoftware for the Jaguar console overscan both vertically and horizontally so we will restrict ourselves to the OVERSCAN column. for the restof this discussion The first row tdiviser of 1) requires that the object processor be started twice each line and produces a ridiculously highresslitionfor aT, so it will be ignored. Adivisor of 3 gives a non overscanned resolution off about 355. This is a good match for many _ ww computer systems and programs designed around 320 pixel wide screens. A divisor of four gives pixels that are about square. Square pixels are a great advantage for art creation and we recommend their use. 

© 1995 Atari Corp. 

Confidential Information “JPR Property of Atari Corporation 

26 April, 1995 

. 

Technical Reference pixel divisor of 4. of 4. 4. gm 1 y pixel wide wide wf q 266 being visible being visible visible : : each side that side that that overscanned for PAL. PAL. This and! restricted 18 200 200 Significant, | ees change these these 4 9 ne | ( y S-Video, and and | Peritel/Scart modulator. | the same timings same timings timings f | to change these change these these | (MHz) ] A | ©1995 Atari Corp. | 

2 | 

i ' | _ 

> PageLet's look6 at the specific case of an overscanned game using square pixels. This uses a pixel divisor of 4. of 4. 4. In both NTSC and PAL this allows for about 332 pixels to be displayed. Choosing a 320 pixel wide wide bitmap gives us a <4% error. Of these 320 pixels we should only count on the middle 266 being visible being visible visible on most monitors and/or TV sets. This means that there is a border of about 27 pixels on each side that side that that may be visible, but which should not contain essential game information. _ The other pixel clock divisor that is of likely interest are is 5. In this ease the numberof overscanned pixels is usably close to a blittable width: 256. 8 EEE To overscan vertically we suggest a screen height of 240 lines for NTSE dad 288 lines for PAL. PAL. This will allow for both PAL and NTSC users to see a fully overscanned image bath. vertically and! _ horizontally. The guaranteed visible region within which facial game informationis. restricted 18 200 200 lines for NTSC and 240 lines for PAL. Using 200 lines of critical. video for both systemsis:# Significant, and acceptable, simplification. Pee ees | Ce | The information in this section is for informational purposes-only. Do not attempt to change these these timings or unpredictable results will occurl:: Te There are four versions of the Jaguar Console! io ~~ Where used[-] Video Standard j PSC USA} Canasta” __ [esUnited Kingdom ‘ PRACT [FAB | Germany tether European countries | PerteySeart = : The Jaguar console hasan external video connector which supports Composite video, S-Video, and and RGB. In addition, there 3$'an, RF Modulator oritput.on:all versions except the French Peritel/Scart : version. The Peritel/Seart version is identical to’ PAE=B, except that there is no RF modulator. | Composite video, S-Video, and:RGB. are all available on the Peritel version, and have the same timings same timings timings and characteristics of PAL-B. OPE | The various specification timings are shown below: { neAcomposte ee | The information in this section is for informational purposes only. Do not attempt to change these change these these i timings or unpredictable results will occur! —_ ~~ Chroma clock Subcarrier (MHz) Sound subcarrier (MHz) 

> {i PAL! |S 448861875 Pe s01.250 | MHZ ; PAL-B qasaei875 [591.250«| SSM 

4 

5 June, 1995 

ConfidentialInformation JER Property ofAtari Corporation 

Page 7 

F Technical Reference 

4 

j 

| The information in this section is for informational purposes only. Do not attempt to change these } timings or unpredictable results will occur! 

Parameter PAL NTS& : ee ae a eyewith us 4 ira syne wit | sus 4 48 | : ee Oe widh | aru 0260s 

**==> picture [3 x 7] intentionally omitted <==**

**----- Start of picture text -----**<br>
{<br>**----- End of picture text -----**<br>


© 1995 Atari Corp. 

Confidential Information ‘PPR Property ofAtari Corporation 

26 April, 1995 

: : 

Page 8 

Technical Reference 

. 

| 

| 

| : | : | | 

| 

; | 

q 1, 

i : 

; | 

q 

' j 

| | A detailed mechanical drawing is available on request. j | SRR aa q The external DSP connector is a custom 12-pin, two row edge connector. The top row isrow A, the j i bottom row is row B. Pin 1 is on the left, pin 6 on the right when looking at the console from the rear: , 26 April, 1995 Confidential Information “AO® Property of Atari Corporation ©1995 Atari Corp. (- 

JaguarConsoleHardwarePorts VideeConnector The external video connector is a custom 24 pin, two row edge connecigf.: “Thig:top row is row A, the bottom row is row B. Pin 1 is on the left, pin 12 on the right when lockingat thy ¢insole from the rear: 

**==> picture [430 x 315] intentionally omitted <==**

**----- Start of picture text -----**<br>
—_ — —<br>Pin Number Name Description<br>Audio Left EIAd Line level, ieft, audio 25... a<br>Audio_Gnd Audio Return (growfidy bie<br>Video_Gnd Video Return (ground) io<br>[5A [Bue  _| Blue-vid8o;'78Ohm, 0.7V peak-to-peak<br>Hofigental Syné,'75 ‘Ohm, 3V peak-to-peak22:“*<br>Audio Right | EIAj.Line level, right'audio<br>[3B| Audio” Gd. __| Audis: Relliny fground)<br>|__7B_ Video. Gnd Video Return {gteund)<br>S-Video 'ttima;'75 Ohm, 1V peak-to-peak<br>|10B)'f Video_Gnd: Video Return (ground)<br>118 | Composite "| Gomposte video, 75 Ohm, 1V peak-to-peak<br>**----- End of picture text -----**<br>


The Reserved signals should:be left unconnected. They may be used in future versions of the Jaguar console,aad therefore shouldbe.passed through on video adaptors. It is important to terminate the active signals'correctly. Do not load the 75 Ohm outputs with more than 75 Ohms. 

Page 9 

Technical Reference 

| | | 

**==> picture [406 x 133] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||
|---|---|---|---|---|---|
|Pin|Number|Name|Description|
|roa|
|Synchronous|serial word strobe|
|a|«4|SCK____||Synchronous serial clock|
|4A|CT|TxD|Synchronous|serial transmit: date|(data out)|
|SA|RXD__| Synchronous serial|receiv|data (dati).|
|iB|«eV|SOmA maximum load cS|oo|
|r3B.|SSCL UARRT_RXD|Asynchronous receive dat: 5s.|“BEES|

**----- End of picture text -----**<br>


All the active signals have 5 volt TTL levels. The SCK, WS, TXD asd’RXD signals are also connected to the cartridge expansion connector. They are used on the'CD-ROM peripheral, therefore care must be taken to avoid contention (see the audio sub-system-section below). EB 

**==> picture [2 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
.<br>**----- End of picture text -----**<br>


| 

Technical Reference 

4 

Page 10 

| q 

1 

‘G@artridge/ExpansionPott a4 j Information on the Cartridge/Expansion Port of the Jaguar is available to hardware/accessory licensees. Hardware licensees should contact Atari regarding the connection of devices to this port. 

q 

26 April, 1995 

Confidential Information “JER Property ofAtari Corporation 

©1995 AtariCorp. 2 

Page 11 

Technical Reference 

AS There are two types of Multi-Console games. The first type uses a special Local-Area-Network of multiple Jaguar consoles connected together via the console's asynchronous serial port. The second type uses the Jaguar modem to connect two Jaguar consoles via the telephone dHIES Zi... 

| 

| 

Ce ee, The low-level drivers required for networking multiple Jaguar consoles aré currently in developinient. Contact Jaguar Developer Support for further information... “EEEEEEB Eee ———— aT ee i¢ descritted in the section titled"Fhe:Jaguar Voice The specification for using the Jaguar modem. 

| 

© 1995 Atari Corp. 

Confidential Information JER Property ofAtari Corporation 

21 June, 1995 

Technical Reference ‘ 

| | i 

a Al | : : 

. 7 ‘ 

| 4 ; a 2 | i | o i ® | 

1 1 J3 J4 Bi-directional signal: signal: OEE Used asoutput to specify to controlia#sivhich asoutput to specify to controlia#sivhichoutput to specify to controlia#sivhich to specify to controlia#sivhich specify to controlia#sivhich data to to return | Usédas output to'specity to controllers whief' data output to'specity to controllers whief' data to'specity to controllers whief' data to controllers whief' data whief' data data to return : J6 Bidaectional signal.!22:%.. signal.!22:%.. Usedas output to specifyte: to specifyte: specifyte:te: controllers which data to return which data to return data to return to return[[to][ controllers][ which]][[ controllers][ which]][[ which]][[data][ to]][[ to]][[return]] j Used a§ a§[[Gitput][ to][ specify]][[ to][ specify]][[ specify]] ‘ 6 BOP [82|| Bitton input tight gun gun on Port¥ Port¥¥ j +5V DC_| DC_| a8 DC_| Maximum 50mA Maximum 50mA 50mA Toad se}_nle_nle | ple | Pulled upto 4V DC on 4V DC on DC on on 4 player adaptor player adaptor adaptor P72|| 0 [| J14 [Input only signal only signal signal | pia 8 sta [ Inpatoniy signal Inpatoniy signal signal | Signals J0-J15, and BO-B3 are all TTL level digital inputs or outputs. : Controlier Port 1 also has.a light gun input in addition to the signals listed above. A 71L rising edgeon ' the LP signal (pin 6 of port1;,shared with BO) causes the light pen registers (LPH and LPV) to be 

1 | ay 2 ‘ 

| | 

Page 12 Jaguar Controllers and Controller Ports There are two controller ports on the Jaguar console: Controller Port 1 and Controller Port 2. Each has the following functions: 

**==> picture [496 x 88] intentionally omitted <==**

**----- Start of picture text -----**<br>
© _ Four bi-directional digital pins _. -<br>e Six input only digital pins (split into 4 + 2 buttons) ce ee<br>Note: Early versions ofthe Jaguar console included an8 bitADC! onthe motherboard; ‘This has<br>been deleted - analog controllers now require their own ADC chip. 2225, an<br>**----- End of picture text -----**<br>


## SignaisandPincits 

**==> picture [429 x 252] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||
|---|---|---|---|---|---|---|
|Pin#|Port|1|Port 2|Description|
|1|J3|J4|Bi-directional signal: signal:|OEE|
|Used asoutput to specify to controlia#sivhich asoutput to specify to controlia#sivhichoutput to specify to controlia#sivhich to specify to controlia#sivhich specify to controlia#sivhich|data to to|return|
|Usédas output to'specity to controllers whief' data output to'specity to controllers whief' data to'specity to controllers whief' data to controllers whief' data whief' data data|to return|
|J6|Bidaectional signal.!22:%.. signal.!22:%..|
|Usedas|output to specifyte: to specifyte: specifyte:te:|controllers which data to return which data to return data to return to return|
|[[to][ controllers][ which]][[ controllers][ which]][[ which]]|[[data][ to]][[ to]]|[[return]]|
|Used a§ a§|[[Gitput][ to][ specify]][[ to][ specify]][[ specify]]|
|6|BOP|[82|||Bitton|input|tight gun gun|on Port¥ Port¥¥|
|+5V DC_| DC_||a8|DC_| Maximum 50mA Maximum 50mA 50mA|Toad|
|se}_nle_nle|||ple|||Pulled|upto 4V DC on 4V DC on DC on on|4 player adaptor player adaptor adaptor|
|P72|||0|[||J14|[Input only signal only signal signal|
|pia|8|sta|[ Inpatoniy signal Inpatoniy signal signal|

**----- End of picture text -----**<br>


1 Analog to Digital Converter — @ device that converts analog signals such as a variable voltage level into a digital format suitable for processing by a computer. 21 June, 1995 Confidential Information JPR Property ofAtari Corporation © 1995 Atari Corp. 

**==> picture [28 x 63] intentionally omitted <==**

**----- Start of picture text -----**<br>
"a<br>f<br>q<br>**----- End of picture text -----**<br>


Page 13 

Technical Reference 

(QFosistor Adaressing Digitalinputs The table below shows the purpose of the individual bits of the JOYSTICK and JOYBUTS registers. Please note that some bits are used for non-controller related purposes. 

| 

**==> picture [574 x 574] intentionally omitted <==**

**----- Start of picture text -----**<br>
_<br>JOYSTICK  $F14000 Read/Write<br>Read fedcba98 7654321q | f-1 Signals J1§:to J 1: “SEES<br>Pe Prermre ees |e cetrige ec cence<br>Write exxxxxxm 76543210 |e i = enable “d#+J0 outputs TEE<br>0 = disable J7230:outputs foe<br>dott, care Oe ae |<br>™ Audio: mute oa<br>0 = Addig:muted (reset state)<br>a<br>13-0  33-J0 outputs (Beet. 1)<br>oat =<br>JOYBUTS $F 14002 Rend Only<br>Read XXXXEXEX  rrdav3210 Lex don Hi Gare |<br>‘[ae, --Reserved. |<br>"gis Reserved8,<br>yoni.<br>Fi<br>: | ee “Hl Qs. PAL Video hardware<br>wo 1 = NTSC video hardware<br>ee o P-O. Button inputs Bl & BO (port 1)<br>[[Each][ controller]][[ controller]]<br>Allportcontroller has 4 bi-directionaldevicesportcontroller has 4 bi-directionaldevicescontroller has 4 bi-directionaldevices has 4 bi-directionaldevices 4 bi-directionaldevices bi-directionaldevicesdevices aféaddressedpins‘and'6,input throughpins. théaddressedpins‘and'6,input throughpins. thépins‘and'6,input throughpins. thé‘and'6,input throughpins. thé throughpins. thépins. thé thé [[digital:fines]] Wealways usealways use use [[ on]]  the [[ the]]  bi-directional [[ controller][ ports.]][[ ports.]]  pins as outputs. as outputs. outputs. By |:<br>writing a 4-bit code 4-bit code code to! these outpats,16 rows containing 6 bits of data each can be addressed. these outpats,16 rows containing 6 bits of data each can be addressed. outpats,16 rows containing 6 bits of data each can be addressed.16 rows containing 6 bits of data each can be addressed. rows containing 6 bits of data each can be addressed. containing 6 bits of data each can be addressed. 6 bits of data each can be addressed. bits of data each can be addressed. of data each can be addressed. data each can be addressed. each can be addressed. can be addressed. be addressed. addressed. Each |<br>controller is allocated 4 rows of data, 'S6:tip.to allocated 4 rows of data, 'S6:tip.to 4 rows of data, 'S6:tip.to rows of data, 'S6:tip.to of data, 'S6:tip.to data, 'S6:tip.to 'S6:tip.to 4 controllers may be connected to each port (via a 4- controllers may be connected to each port (via a 4- may be connected to each port (via a 4- be connected to each port (via a 4- connected to each port (via a 4- to each port (via a 4- each port (via a 4- port (via a 4- (via a 4- a 4- 4-<br>player adapter)fF:a.maximum adapter)fF:a.maximumfF:a.maximummaximum of 8 contréliés.total. contréliés.total. Controllers may be connected to the Jaguar in two may be connected to the Jaguar in two be connected to the Jaguar in two connected to the Jaguar in two to the Jaguar in two the Jaguar in two Jaguar in two in two two<br>1)  Bizectly to the controsier:port. to the controsier:port. the controsier:port. controsier:port.<br>2) Via amulticplayer adapto#multicplayer adapto# adapto# {usually a 4 player adaptor, or a pass-through connector on an 4 player adaptor, or a pass-through connector on an player adaptor, or a pass-through connector on an adaptor, or a pass-through connector on an or a pass-through connector on an a pass-through connector on an on an an<br>Advanced controllers controllers typically provide a “pass-through” “pass-through” connector to allow a standard Jaguar controller to allow a standard Jaguar controller allow a standard Jaguar controller a standard Jaguar controller standard Jaguar controller controller<br>wWWFWWF tosince be connected the advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity,since be connected the advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, be connected the advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, connected the advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, the advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, not have controller. as many buttonsOften this as the is standard Jaguar a necessity, have controller. as many buttonsOften this as the is standard Jaguar a necessity, controller. as many buttonsOften this as the is standard Jaguar a necessity, as many buttonsOften this as the is standard Jaguar a necessity, many buttonsOften this as the is standard Jaguar a necessity, buttonsOften this as the is standard Jaguar a necessity,Often this as the is standard Jaguar a necessity, this as the is standard Jaguar a necessity, as the is standard Jaguar a necessity, is standard Jaguar a necessity, standard Jaguar a necessity, a necessity, necessity, not a luxury, controller a luxury, controller controller |<br>**----- End of picture text -----**<br>


> [[Each][ controller]][[ controller]] Allportcontroller has 4 bi-directionaldevicesportcontroller has 4 bi-directionaldevicescontroller has 4 bi-directionaldevices has 4 bi-directionaldevices 4 bi-directionaldevices bi-directionaldevicesdevices aféaddressedpins‘and'6,input throughpins. théaddressedpins‘and'6,input throughpins. thépins‘and'6,input throughpins. thé‘and'6,input throughpins. thé throughpins. thépins. thé thé[[digital:fines]] Wealways usealways use use[[ on]] the[[ the]] bi-directional[[ controller][ ports.]][[ ports.]] pins as outputs. as outputs. outputs. By writing a 4-bit code 4-bit code code to! these outpats,16 rows containing 6 bits of data each can be addressed. these outpats,16 rows containing 6 bits of data each can be addressed. outpats,16 rows containing 6 bits of data each can be addressed.16 rows containing 6 bits of data each can be addressed. rows containing 6 bits of data each can be addressed. containing 6 bits of data each can be addressed. 6 bits of data each can be addressed. bits of data each can be addressed. of data each can be addressed. data each can be addressed. each can be addressed. can be addressed. be addressed. addressed. Each controller is allocated 4 rows of data, 'S6:tip.to allocated 4 rows of data, 'S6:tip.to 4 rows of data, 'S6:tip.to rows of data, 'S6:tip.to of data, 'S6:tip.to data, 'S6:tip.to 'S6:tip.to 4 controllers may be connected to each port (via a 4- controllers may be connected to each port (via a 4- may be connected to each port (via a 4- be connected to each port (via a 4- connected to each port (via a 4- to each port (via a 4- each port (via a 4- port (via a 4- (via a 4- a 4- 4- 

> player adapter)fF:a.maximum adapter)fF:a.maximumfF:a.maximummaximum of 8 contréliés.total. contréliés.total. Controllers may be connected to the Jaguar in two may be connected to the Jaguar in two be connected to the Jaguar in two connected to the Jaguar in two to the Jaguar in two the Jaguar in two Jaguar in two in two two 1) Bizectly to the controsier:port. to the controsier:port. the controsier:port. controsier:port. 2) Via amulticplayer adapto#multicplayer adapto# adapto# {usually a 4 player adaptor, or a pass-through connector on an 4 player adaptor, or a pass-through connector on an player adaptor, or a pass-through connector on an adaptor, or a pass-through connector on an or a pass-through connector on an a pass-through connector on an on an an Advanced controllers controllers typically provide a “pass-through” “pass-through” connector to allow a standard Jaguar controller to allow a standard Jaguar controller allow a standard Jaguar controller a standard Jaguar controller standard Jaguar controller controller wWWFWWF tosince be connected the advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity,since be connected the advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, be connected the advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, connected the advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, the advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, advanced at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, at the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, the controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, controllers same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, same time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, time usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, usually as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, as the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, the do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, do advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, advanced not have controller. as many buttonsOften this as the is standard Jaguar a necessity, not have controller. as many buttonsOften this as the is standard Jaguar a necessity, have controller. as many buttonsOften this as the is standard Jaguar a necessity, controller. as many buttonsOften this as the is standard Jaguar a necessity, as many buttonsOften this as the is standard Jaguar a necessity, many buttonsOften this as the is standard Jaguar a necessity, buttonsOften this as the is standard Jaguar a necessity,Often this as the is standard Jaguar a necessity, this as the is standard Jaguar a necessity, as the is standard Jaguar a necessity, is standard Jaguar a necessity, standard Jaguar a necessity, a necessity, necessity, not a luxury, controller a luxury, controller controller (and may be missing such critical buttons as Pause). 

**==> picture [2 x 27] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


© 1995 Atari Corp. 

Confidential Information “PPR Property ofAtari Corporation 

21 June, 1995 

% = 

j 

i Page 14 Technical Reference | ! Reading A Jaguar Controller #§# =... i iN ‘ Reading a controller is done in two steps: | | 1) Write a 4 bit code to the port’s output bits which specifies which row of controller data you want : : to read. Bits 0-3 of the JOYSTICK register contain the outputbits for'Part 1. Bits 4-7 specify : : the output bits for Port 2. Note that the codes used for port 2:afe’a mirtoriffage of the codes for ' ji port 1. (The bit order is reversed.) Bit 15 of JOYSTICK must also be set to:eablethe outputs. | j Bitaccidentally 8 is also usedor you to will controldisable audio your program’s muting, so yousound have generation... to beearful not to clear thisen bit ' 2) Read back the values contained in the JOYBUTS aid JOYSTICK registersi:.These will contain 4 the 6 data bits returned by each port. HEE EEE EB? ' ' For example, writing a value of $817E to JOYSTICK woyld allowyou'te,read row 0 of the first 7 controller connected to Port 1 and the first controller connected to Port 2::This value breaks down as: 7 $0100 = Enable Audio (bit 8 of JQ¥STICK coftzsls audio mute) © q $0070 = Setup read of row 0 (code: $01 11) of controller 0, port 2 5 $000E = Setup read of row 0 (code"$4410) of contr@tzer 0, port 1 q j $817E = value to write to JOYSTICK register ee ve : Below is a table that shows how ilie 6 bits of data for each row aré'returned by the first controller 1 z | connected on port 1 and the first Controller retaufied Of-port 2. The meaning of the bits depends on : q1 which row is being read and what type of controlleris:catinected (as defined later in the descriptions of , each controller type). ae “ TEE | i Retrei“( LULU | ; Output Pin # Input Pin # a 1 1 2 3 4 6 10 14 13 12 1 i POL 1,1 | 1 GR Cougs data | data | data | data | data | pt | On tI C20 Peedata | data | data__—| data, =| data’ S| ‘ Outjiut Pin # Input Pin # 2 @ ; 1 2 3 «4 6 10 14 13 12 1 b \ (J7) (J6) (JE) (Ue) (B2} (B3) (J12) (J13) (J14) (J15) 4 ’ | Pit iti Ose 6 6Ce | data | data | data | data | data || PotiPitoti]itt 1 EeveeBeem datasci |[datadata || data,data || datadata || data,data || datadata |e]] ] * Bit BO on Port 1 and bit B2 on Port 2 are used as a special “Bank 0” flag by bank switching controllers. ] ’ See Reading Bank Switching Controllers for more information. PI 

2 @ b 4 ’ |e]] ] ’ PI 

4 : 

q 

26 April, 1995 

Confidential Information “FOR Property ofAtari Corporation 

© 1995 Atari Corp. 

Technical Reference 

Page 15 

| 

| | | 

| 

| 

4 

| 

## o identifying Controller Types 

The basic type of controller is specified by the C2, & C3 bits returned when you read the controller, as shown in the table. The currently defined controller type identifiers are: } 

MoTC2 C30 |ResenedController Type 0] 1 _| Bank switching controller. (analog joystick, head-meiifted tracker, tC): i | [1 ]_0 | Tempest" rotary controler Software should scan all possible controller positions, including those on a 4-playst:adapter, ee determine which types of controllers are currently connected.Fhe. game can then Gffer the:viser the choice of which controller(s) to use. Ee OEEEEEES Some advanced controllers use a special bank-switching technique to rettiff tore information than the 24 bits of data available from a standard controllet::Fhis makes a wide variety:G£:controller types possible, so the specific controller type is idesitefied'by certain bits in the last barik'gf data returned by each controller. ZEEE TEE Data Returned from Last Bank Row 3 Row 2 Row 1 Row 0 Bank Switching Controller Type Ss ot ee reserved To |..1t | 1 | 0 [reseweg RTE LO TF, [Keyboard/Mouse SCS a Analog Joystick or Driving Controller See the desétiptions of the individial controller types and the section Reading Bank Switching Controllersfor additional information. 

1 Please note that the specification for identifying controllers was changed on March 31, 1995. The differences are important, but fairly minor from an implementation view, and do not affect any existing hardware on the market as of that date. 

1 

© 1995 Atari Corp. 

Confidential Information “JER Property ofAtari Corporation 

26 April, 1995 

' Page 16 } P Below Jaguar ' : 

Technical Reference 

f ' 

‘ 4 

4 : 1 ' i | q 

| 

i 

i 

] ] ’ | E ' { 1 

© 1995 Atari Corp. ] 

## Standard Jaguar ControllerMatrix 

Below is a table showing the matrix for the standard joypad controller which is packed out with every Jaguar console. When plugged directly into the console, the matrix for this controller is as follows: 

**==> picture [449 x 261] intentionally omitted <==**

**----- Start of picture text -----**<br>
J4 J5 J6 JZ Port2 B2 B3 J12 J13 J14 J15<br>J3 J2 J1 JO Portt Bo Bt J8 J9 J10 J<br>Row 3<br>pi foto yo -_<br>LL ee<br>Row 1<br>Row 0 own |bef Right<br> a zero means zero means means the appropriate Bitton is depressed... depressed... sae<br>**----- End of picture text -----**<br>


Reading a zero means zero means means the appropriate Bitton is depressed... depressed... 

**==> picture [26 x 17] intentionally omitted <==**

**----- Start of picture text -----**<br>
Hg<br>**----- End of picture text -----**<br>


4PlayerAdaptor= isi‘ (‘CO **;** ™ ***** Cs*=é—*i The fact that 16 rowsof data can be addressed allows a 4 controller adaptor to be connected to each s console controller port {for a total of 8 controllers using:two adaptors). The 4-player adapter is a device ; which expands either ofthe'console controller:perts:tdallow up to 4 controllers to be connected. It has 3 4 controller sockets (D845 ‘females, the same as on'the console) for controllers to be connected, anda short cable with a DB15 male cénneetor which plugs into the console. . ; The contralier: sockets on the adaptor have the.6 inputs wire OR'd together. The four output lines are an 3 active low;'4 to 16detiultiplexed version ofthe 4 console outputs. & Each sé¢ket recognizes 4 unique row codes which are used to specify requests for data from that 4 controller!:'The table below shows,the row codes which must be output from the Jaguar to request data q from controllérs ‘connected to specific sockets of the adapter. Note that socket 0 uses the same row { codes as a singlé:controller connegted directly to one of the console controller ports. ; 

26 April, 1995 

Confidential Information AR Property ofAtari Corporation 

] ' 

| | | | | 

**==> picture [533 x 407] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 17<br>Technical Reference ,<br>@ RowFrom Code Jaguar: Output Specifiescontroller whichconnected row of theto:<br>Portt2Portt1 J4J3 Jd5J2 JeJi J?JO SocketO Socket? Socket2 Socket 3<br>nS ee<br>Except for socket 0, the row codes shown in‘the.table are not the row, codes seen by the controllers<br>themselves. In order to make itself as transparenitias possible to the-¢ontrollers themselves, the adapter<br>_, converts the row codes for sockets 1-3 so that thosé ¢ontrollers will séé.Only socket 0 row codes. In<br>the code GGiGEthat says it wants to read Row1 of the<br>wo other words, when your program ontpuis the code to %1101 and then pass it to<br>controller connected to socket 2,dhe'4-player adapter wiliconvert.<br>socket 2. The controller connected to socket 2willshen see cede 94101, the same code you would use<br>to the Jaguar, and return the appropriate information.<br>to access a single controller connected directly<br>**----- End of picture text -----**<br>


for socket 1 instead of the codes for socket 0 Advanced controllers normally respond to row S0ides. because they have a pas§-through:connector for astanidatd joypad controller, which sees socket 0 codes andplayer responds adapter,as advancedthoughit controllerswere conrieetedwilkneverdirectly tosee codes the Jaguar. for socketHowever, 1 because when the connected adapter will to convert a 4- them to socket:@:eades and then output themonly to the controller connected to socket 1. Advanced controllers need todetect the presence of a 4-player adapter and change their behaviour when one is present. Therefore,the 4-Player adapter provides a +5v DC signal on pin 8 of each socket, which is normallynot connected when controllers are plugged directly into the console. Advanced controllers are expected'to detect this signal when present, disable their pass-through connector, and then respond as socket 0 instead of:socket 1. Be To summarize these ideas: the table below shows the various socket and controller positions with and w without a 4-player adapter. (Ports 1 & 2 are identical in these respects.) 

**==> picture [1 x 29] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


© 1995 Atari Corp. 

Confidential Information JER Property ofAtari Corporation 

26 April, 1995 

i ] : ’ i ’ i : : j q 4 7 a { : 1 ' 4 | | !1 : : ’ ] : ' | ] : q 

Page 18 

**==> picture [541 x 729] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 18 Technical Reference j ‘<br>Controller Port With 4-Player Adapter =~<br>Adapter converts row codes sent by Jaguar program and routes them to the appropriate socket. Socket 0 is 4<br>the same as a controller plugged directly into port. Standard and Advanced controllers respond only to socket '<br>0 row codes. Pass-through connectors of advanced controllers are disabled. -<br>Controller Port Without 4-Player Adapter . 3<br>Standard controller plugged directly into port is the same as socket 0 of a 4-player adapter. Advanced Ss<br>controllers plugged directly into port respond to Socket 1 row codes. Pass-through connectors of advanced a<br>controllers are enabled, and addressed as socket 0. ne SEE =<br>Because there are 4 row codes allocated to each socket, the.4-player adaptor there are 4 row codes allocated to each socket, the.4-player adaptor 4 row codes allocated to each socket, the.4-player adaptor row codes allocated to each socket, the.4-player adaptor codes allocated to each socket, the.4-player adaptor allocated to each socket, the.4-player adaptor to each socket, the.4-player adaptor each socket, the.4-player adaptor socket, the.4-player adaptor the.4-player adaptor adaptor wilkionly support support 4 tow gg<br>controller devices. Without additional logic, each input supportsup supportsupup to 24 24 bits of [[dita'{4]] rows of 6 bits). gs<br>Three bits are reserved bits are reserved are reserved reserved for the controller type identifier code; the controller type identifier code; controller type identifier code; type identifier code; identifier code; code; iéaving 21 21 bits for for data 22222" |<br>Intelligent controllers controllers (i.e. ones which use a microcontroller), ones which use a microcontroller), which use a microcontroller), use a microcontroller), a microcontroller), microcontroller), can multipiex-even more data onto the multipiex-even more data onto the more data onto the data onto the onto the the 7<br>same lines. lines. One way this can be done can be done be done done is for for themicrocontroller tomicrocontroller to to “Bank’switch” whenever it sees a sees a<br>transition from row 3 back to row 0. from row 3 back to row 0. row 3 back to row 0. 3 back to row 0. back to row 0. to row 0. row 0. 0. Different bits'6{ data are presented in presented in in each:bank.bank. See the section the section section i<br>Reading Bank Switching Controllers Bank Switching Controllers Switching Controllers Controllers later:j# this chapterfor, more information. this chapterfor, more information. chapterfor, more information.for, more information. more information. information. © ‘<br>Detecting the 4 Player Player Adapter & & Conticeted Controliets<br>To detect the presence of a 4-Player-adapter, a program:should inquire the status of Row 1 of controller fie<br>socket #3. If a 4-Player adapter.J§ present, the BO/B2bit:willbe cleat (0). Otherwise, it will be set (1). :<br>The pseudocode below demonsifates the basic technigite for detecting a 4 player adapter and the a<br>controllers connected to it, as wella any advanced controllers connected directly to the Jaguar: s<br>if PORT:SOCKEf3#C1 = 0 then { 4-player adapter found } g<br>PORT : SOCKET{CONTROLLERTYPE<br>if PORM:SOCKET£CONTROLLERTYPE“HORT= BANK-SWITCHING: SOCKET :C2/C3 then s=<br>“PORT: SOCKETS: BANKSWITCHTYPE = DETECT BANK_SWITCH_ TYPE |<br>eae Oot os :<br>i Best SOCKET EE S<br>else ee Oe &<br>228 PORT: SOCKETQ#CONTROLLERTYPE = STANDARD &<br>‘aaa. Uf PORT:SOCKEPTI::C2/C3 = ROTARY then a<br>“EUs. PORT: SOCKE@1::CONTROLLLERTYPE = ROTARY PS<br>“iglge if PORT:SOCKET1:C2/C3 = BANK-SWITCHING then 2<br>“EEE PORT: SOCKET: BANKSWITCHTYPE = DETECT _BANK_SWITCH_ TYPE : :<br>next endifPORT SeONEEEEEE EE . gEE<br>FUNCTION DETECBANK SWITCH_ T YPE i}Rr:<br>po<br>READ ROWS 0, 1, 2, 3<br>UNTIL ROW0:B0/B2 = 0 {bank 0} :<br>BANKCOUNT = 0 :<br>26 April, 1995 Confidential Information FER Property ofAtari Corporation © 1995 Atari Corp. ]<br>**----- End of picture text -----**<br>


Because there are 4 row codes allocated to each socket, the.4-player adaptor there are 4 row codes allocated to each socket, the.4-player adaptor 4 row codes allocated to each socket, the.4-player adaptor row codes allocated to each socket, the.4-player adaptor codes allocated to each socket, the.4-player adaptor allocated to each socket, the.4-player adaptor to each socket, the.4-player adaptor each socket, the.4-player adaptor socket, the.4-player adaptor the.4-player adaptor adaptor wilkionly support support 4 tow controller devices. Without additional logic, each input supportsup supportsupup to 24 24 bits of[[dita'{4]] rows of 6 bits). Three bits are reserved bits are reserved are reserved reserved for the controller type identifier code; the controller type identifier code; controller type identifier code; type identifier code; identifier code; code; iéaving 21 21 bits for for data 22222" 

Intelligent controllers controllers (i.e. ones which use a microcontroller), ones which use a microcontroller), which use a microcontroller), use a microcontroller), a microcontroller), microcontroller), can multipiex-even more data onto the multipiex-even more data onto the more data onto the data onto the onto the the same lines. lines. One way this can be done can be done be done done is for for themicrocontroller tomicrocontroller to to “Bank’switch” whenever it sees a sees a transition from row 3 back to row 0. from row 3 back to row 0. row 3 back to row 0. 3 back to row 0. back to row 0. to row 0. row 0. 0. Different bits'6{ data are presented in presented in in each:bank.bank. See the section the section section Reading Bank Switching Controllers Bank Switching Controllers Switching Controllers Controllers later:j# this chapterfor, more information. this chapterfor, more information. chapterfor, more information.for, more information. more information. information. © 

## Detecting the 4 Player Player Adapter & & Conticeted Controliets 

| | | | 

Page 19 

| : j| 

## Technical Reference 

**==> picture [7 x 14] intentionally omitted <==**

**----- Start of picture text -----**<br>
i<br>,<br>**----- End of picture text -----**<br>


50 READ ROWS 0, 1, 2, 3 SAVE ROWDATA( BANKCOUNT ) BANKCOUNT = BANKCOUNT + 1 UNTIL ROWO:B0/B2 = 0 {bank 0} return ROWDATA(BANKCOUNT — 1) sROWSO-3:B1/B3 

**==> picture [21 x 21] intentionally omitted <==**

**----- Start of picture text -----**<br>
oo.<br>**----- End of picture text -----**<br>


The JOYSTICK and JOYBUTS registers return the same data in the same bits:regardless of which socket is being read. However, be aware that without a 4-player adapter, reading sockets 1-3 of-4 port[ingorreet][data,] may return an ‘echo’ of the standard joypad controller at soeket:0...[To][ avoid][ reading] unless your program has detected that an advanced controller:oF'& 4eplayer adapter is conmiected, it should not try to read from sockets 1-3 (except for the detection. phasé:whenOEE the program is trying to detect what is connected). 

© 1995 Atari Corp. 

Confidential Information “JR Property ofAtari Corporation 

26 April, 1995 

‘ 

: | | : q 

j J Ji 

**==> picture [596 x 462] intentionally omitted <==**

**----- Start of picture text -----**<br>
‘ Page 20 Technical Reference 4<br>| AdvancedControllersg§.§ ###§.+=ssss—ii—i—i_i_ aR UU<br>eee|rrrss—twQQQ.CU__itC(ND.CUCi(‘(i‘iyN.COOCOSMC<br>These controllers support 6 degrees of freedom: Pitch, Yaw, Roll, X, Yabo Zi: We refer to Pitch as Z :<br>j Torque, Yaw as X Torque and Roll as Y Torque. Hence we have 6 values -"X):¥; Z:and TX, TY and 4<br>’ TZ. We also define 7 buttons, A-G. Bae OHEEEEEE 4<br>: Three banks of data are required, since we define 55 bits of information: 8-bit values for each Of 6 '<br>degrees of freedom (8*6=48 bits of information), plus 7 buttons: eee ee 4<br>| Bank B2 B3 2.~«A 14S eee &<br>| oO BO B1 J8 Jo J10 Ji; fd a<br>' Row3 ee MCE CO FC ee 4<br>: Row2 ee CH DO Ee i :<br>1 Roweee 0 (Cammcy) | RIT8 |Eeevo |eevi_| Yai.) Ys :4<br>| Bank B2 B30 I2—t*« J14 S15 '<br>j 1 Bo Bi J8 J3 J10 J11<br>Row 2 ~<br>| RowS G0 SS eC RC ee<br>Row 0 |<br>1 Bank B2 B3 J12 J13 J14 J15<br>q 2 BO Bi Jé J9 J10 J14<br>Row 2<br>: Row1 ND) E<br>‘ Row 0 a<br>**----- End of picture text -----**<br>


* Bit BO/B2 of row Gis used t8 synchronise the cycle of banks. It will always be zero in bank 0, while all other banks will return 1. Banks: Wwit:cycle in the order Bank 0, Bank 1, Bank 2, Bank 0, etc. See Reading.Bank Switching Controllers:for more information. 

**==> picture [500 x 136] intentionally omitted <==**

**----- Start of picture text -----**<br>
- The C3 and G2 its:identify the basic controller type. The B1/B3 bits of the last bank of the controller are<br>used to identify the: specific bank switching controller type.<br>. Value Meaning<br>oo X(730)<br>“LETS EMEF0) | X axis, anticlockwise rotation torque<br>TY (7:0) Y axis, anticlockwise rotation torque<br>TZ(7:0) Z axis, anticlockwise rotation torque<br>**----- End of picture text -----**<br>


=. 

q 

26 April, 1995 

Confidential Information “AO® Property ofAtari Corporation 

©1995 Atari Corp. | 

Page 21 

| 

Technical Reference 

|| 

| 

| 1 

\W@ 

| 

| 

4 

**==> picture [15 x 8] intentionally omitted <==**

**----- Start of picture text -----**<br>
wr<br>**----- End of picture text -----**<br>


+TY X is positive right to left He ee Z is positive coming BACK (towards the user) £22 OEE Torques are all positive in the COUNTER-CLOCEWISE direction, when facing the positive direction shown by the arrows above. i OEE When connected directly to a Jaguar controlleg port, the controle sill respond to socket 1 row codes (see 4-Player Adaptor). A pass-through connector allows a seconde controller to be connected (usually \W@ a standard Jaguarappear as if it was Controller, directly connected for compatibility9 the:Jaguar. reasons),“‘When-connectedwhich will régeive {Ga 4-player socket 0 adaptor, row codesthe pass- and through connector will not function, and the controller Will fespond:tsy socket 0 row codes. 

## mmm Ko oo Soe These devices provide thie angular values, according torthe orientation of the user's head. 

**==> picture [20 x 23] intentionally omitted <==**

**----- Start of picture text -----**<br>
w<br>**----- End of picture text -----**<br>


**==> picture [489 x 232] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||
|---|---|---|---|---|---|---|---|---|---|
|Bank|WE|B3tid2—<“‘«~‘|iA|J14|J15|
|O|8|BO|B1|J8|J9|J10|J11|
|Row 3|CoGGa|amALS|||td|
|[tam|B3|J12|J13|J14|J15|
|How 3|wreee|om)|tp|tt|
|Row 2|Pecos|i=|||Ae|||Aw|Azo|||AZ?|
|Row:|Geae|Cia7a|ava|[Avs|[Ave|TAY?|
|Row 0|i|TYAS|||ANB|AG|AKT|
|*|Bit BO/B2 of row|0|is used to synchronise the cycle of banks.|It will always be zero in bank 0, while|all|
|other banks will return|1.|Banks|will cycle|in the order Bank 0, Bank|1, Bank 2, Bank 0,|etc.|See|
|Reading Bank Switching|Controllers for more information.|

**----- End of picture text -----**<br>


; 

© 1995 Atari Corp. 

Confidential Information PPR Property ofAtari Corporation 

26 April, 1995 

4 Page 22 Technical Reference q -* The C3 and C2 bits identify the basic controller type. The B1/B3 bits of the last bank of the controller are ” ] used to identify the specific bank switching controller type. i) q Value Meaning 4 AX(7:0) _| Rotation angle around x (=roll=head tilted) axis ' AY (7:0) Rotation angle around y (=yaw=looking left/right) axis AZ(7:0) Rotation angle around z (=pitch=looking:apydownyaxis ' Zero is facing straight ahead. Positive values are tilt leftlook left/Idok up. Values are Hiigar angle | 1 values, where +180 degrees = $7F, -179 degrees = $80. on OEE ‘ When connected directly to a Jaguar controller port, the controller will responid:to socket 1 row godes . ' (see 4-Player Adaptor). A pass-through connector allows a:second controller te: be:-connected (usually 1| a standard Jaguar Controller, for compatibility reasons), whichsill receive socket O:nawigsdss'and q appear as if it was directly connected to the Jaguar. When connected:toa 4-player adaptor, the passthrough connector will not function, and the controller wilf ¥espond t¢:séicket 0 row codes. Rotary “Tempest’ Controller = OS | This device is similar to the original Tempest aécade controller.’ if tises a two phase optical switch, | which can be read by software to determine thedirection of rotations: S 4 B2 B3 J12 J13 J14 J15 , Row Bo Bi J8 J9 J10 J11 = Row 3 Ue ee a : 2 EC Ms a ee ee : | Row 0 I i i aa Te ee | The phase signals (Phas¢ 0:and Phase 1) specify:which'direction the rotary wheel is turning. They look | like this when the wheel #s'tuittiing anticlockwise!!!" [ : Phase O 8) 2. EE | Phase 1 “gy | — : Anticlockwise sequenicé| J10°(pin12) 0110011 | S11 (pind) 0011001... 1 Clockwise sequence J11J10 (pindl)(pinl2 0110011...0011001 | ;: 26 April, 1995 Confidential Information FUR Property ofAtari Corporation © 1995 Atari Corp. ; = 

: | ; 

| 7 

Page 23 

Technical Reference 

| | | | | | | | | 4 : | q 

; 

w 

1D src connected directly to a Jaguar controller port, the controller will respond to socket 1 row codes p (see 4-Player Adaptor). A pass-through connector allows a second controller to be connected (usually j a standard Jaguar Controller, for compatibility reasons), which will receive socket 0 row codes and j appear as if it was directly connected to the Jaguar. When connected to a 4-player adaptor, the pass: through connector will not function, and the controller will respond to sockét0:raw codes. 

| Analog Uoystick and “Driving” Controllers ee } These devices typically require 8 bits of analog resolution in 2 dimensions (X 46d _Y). Two 100Kohm 4 linear potentiometers are typically used, with a +5volt potefitial across the ends:::Fhe-center wiper will F then read a voltage between OV and +5V. HEB CEE ee To read this voltage requires an analog to digital converter ADC). A goud solution is to use the Motorola 68HCOSP9 microcontroller. This part has four 8 bit ADC chantils;:and 16 general purpose digital I/O lines. The four controller row outputs:would:.be used to select one'af:fgur 6 bit addresses. The two 8 bit ADC values use 16 addresses, leaving roam for.5 switches and 3 déviée identifier codes. 

In the example below, we have used bank switching to support €¥és:more switches. The bank is switched when the 68HCOS sees a transition from: Row 3 to Row 0:Bank identification is achieved by ___ 1 @ reading bits BO/B2 of Row 0. See Reading Bank'Switching Controllérs,for more information. aor _e Bs rr ar _ | 0 Bo B1 J8 Jg J10 J11 Mic) Te xm | xe | xe [xm 

**==> picture [506 x 180] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||
|---|---|---|---|---|---|---|
|Bank|MED|B30tit2s—=“‘<«é‘é«é|A|J14|J15|
|1|Je|B1|J8|J9|J10|J11|
|Row|
|3|ze|a|an|a|A|
|Row2|[ugar iebe Gs|
|*|Bit 80/B2 of row )is|tigséito|synchronise the cycle of banks.|It will always be zero in bank 0, while all|
|othigt:banks will return 12:Baaks will cycle in the order Bank 0, Bank|1, Bank 2, Bank 0,|etc.|See|
|Readifig: Bank Switching|Controllers for more information.|
|“*|The C3 and:62|bits, identity the|basic controller type.|The B1/B3 bits of the last bank of the controller are|
|used to identthe|spee|i|fie!fybank|switching|controller type.|

**----- End of picture text -----**<br>


“* 

: 

© 1995 Atari Corp. 

Confidential Information JPR Property ofAtari Corporation 

26 April, 1995 

j 1 q : : 1 1| ; : : q 

Page 24 Technical Reference : “Stick” Controller “Driving” Controller S f X(7:0) [Ee Steering. yi 4 Right = Positive delta values from centered Right = Positive delta values from centered 3 position. position. ; Left = Negative delta values from centered Left = Negative.de#a values from centered 5 position. position. 222sene EON Pitch. AcceleratatiBrake TE Fs. } Forward = Positive delta values from centered Acceleraté = Positive delta valuiss from | position. centered posifign... wee : Back = Negative delta values from centered Brake = Negative daltdvalues from cefitéred } j position. ‘pasition. DEES. une 5 Down . Right i |D :The range of possible X and Y values is 0-255; Buit-not all controllers ill use this entire range, and the 4 1| rangethe center,they harddo useright,is notand pre-defined. hard leftposition:Do not assufti¢'Analog devices.that certainare constadiffere **nt** valuesfrom control can a **l** erways beto controller, used for (ffs | and even from day to day as tenjperatureand humidity conditionseHange. For example, a driving j 1 controllerhard left). mayA different return values controllerdfthe of160 (steeringsame typewhee#¢entered), 245°(turnedfront tie.same company hard(or the right), same and controller 75 (turnedunder q different temperature and/or humidity conditions) may réttith values of 150 (center), 240 (hard right), | a 1 and 55 (hard left). The center position is different, and thé Value ranges are also different. Your | software needs to be ablgto account for this. . 9g : | It will be necessary to provide sine sort of calibration routine where your program will ask the user to ; 4j move the controller to:¢értain positions,inorder to read the values at those positions?. This should be ig ' an option on your controller configuratioriscreen. It would also be nice if the user could choose to 1 recalibrate.thestored thé current’Géittrolleréalibéation while pauvalue **s** edinto in **the** cattridge thiddle of EEPROM. a game. It wouldThat way, be anotherif the user niceis touchusing ifthe yousame ; : controllgg under the sam basic conditions most of the time, they won’t be forced to recalibrate each : ' Analog contrelieis. require a certain amount of processing time from the time the row code is written to | the JOYSTICK ‘register until the data read back from the JOYSTICK or JOYBUTS registers will be = ' valid.about 40With microseconds) a typical‘analés-controiler, when'going fromthis row delayto row is normally aboutwithin the same bank 25 microseconds(this delay (worseapplies caseto all is = \ Vi q 2 If you’ve ever played a game on a PC that uses an analog joystick, then you’ve probably seen examples of such . ; i calibration screens. i 1 26 April, 1995 Confidential Information 7 0 N Property ofAtari Corporation © 1995 Atari Corp. - 

> | Page 25 

| | | | | | ] ; { ! q { | . { | 4 

Technical Reference T) bank-switching controllers), and approximately 200 microseconds in between banks.4 There are two ; S" ways to handle this. You can do a small delay loop while waiting for the data to be available (do this in t a way that uses the bus as little as possible, i.e. avoid memory accesses). Or if your program has a timer interrupt of some kind, you could write out the row code on one interrupt, and then wait for another interrupt before reading the value back. You could also use GPU interrupts in a similar way. Whichever way you choose, try to avoid wasting CPU time and bus bandwidthjust waiting to read the controller(s) when there is other processing you could be doing. a ee | When connected directly to a Jaguar controller port, the controller will:tespond to socket Etow codes (see 4-Player Adaptor). A pass-through connector allows a second céigttaller to be connected fusually a standard Jaguar Controller, for compatibility reasons), which will receive'seeket 0 row codes:atid appear as if it was directly connected to the Jaguar. When.gennected to a 4-playeriadaptor, the. fassthrough connector will not function, and the controller will tespond to socket 0 rOW Codes ...385 and is subject to Note: The specification for this controller type is stilt in the preliminary stages change without notice. Contact Jaguar Developer Support for further information ifyour project 

One subject that has been discussed a number of times throughout this section is bank switching, a technique which allows.a controller to return more information that would otherwise be possible with a 

| Bank switching is done:aistomatically when the contraller sees a transition from row 3 to row 0 (of the , same controller socket):It is not‘possible to read only a particular bank or set of banks and ignore the other ones; you must always read all banks:even if you don’t really need all of theinformation. Programs must always read an entire bank'fromn-a controller at once. However, it is not required that you read all banks from a:single controller in @’single pass. It is acceptable to read a bank from one controller, followed by ‘4batik.or multiple banks from other controller, and then come back to read the next bank fom the first coritraller. Controllers are expected to ignore any requests for rows on other controllers:::Stich requests must not.cause the controller to lose synchonization or perform any bank The rows of each bank of @ eoptrotler must be read in sequence: Row 0, Row 1, Row 2, Row 3. The controller relies on the rows being read in sequence so that it can start processing the data for the next wo row in advance. The results of reading rows out of sequence are undefined; the data returned by the ee 4 These numbers were arrived at using a sample prototype analog driving controller using the Motorola 68HC05 © 1995microcontroller.Atari Corp. Confidential Information “JPR Property of Atari Corporation 21 June, June, 1995 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


21 June, June, 1995 

| Page 26 q ‘ | Bank 0: 1 Bank | It is not necessary L banks of a controller ' Bank00 1 if you you were reading a driving controller, : 

Technical Reference 

} . 4 i | ' | . & | : S q ? Hi &a ; ij 2 a s2 =: ; =. : 3 

j 

controller may be invalid. For example, your program would read data from an analog joystick controller like this: 

| Bank 0: Row 0, Row 1, Row 2, Row 3, 1 (controller will automatically bank switch here) Bank 1: Row 0, Row 1, Row 2, Row 3. . oe | It is not necessary to know in advance which bank is active when you start reading.” If You read all L banks of a controller into a table, you can Jook at the data afterwards‘ figure out wheré:thé:data for ' Bank00 is, and from there you can figure out where the data for the otfer:banks must be. Féféxample, 1 if you you were reading a driving controller, the data you read would end up if:4 table that looks lik¢:this: : BankO ) _ 1 Bank 1 The bottom row of the table would be an array of WORD values read from the JOYSTICK and JOYBUTS registers. You could store these values 'ittte. separate arrays #fyou prefer, and it is not : necessaryexample assumesto read both you theare always'teading JOYSTICK registerbesh registers and the JO¥BUTSaridStoring registerall tlke forresults each row,into a single but thistable for In this example, Bank 0 came first: but that won't always beithe case. You need to examine the data in ] the table to determine the location of each bank of data. Bark switching controllers always indicate 1 thetheBank JOYBUTS0 by setting register bit-{B0from ofRow controller0. Theportbit willbe.0:for1) or bit 2 (B2:ofBank controller0 and 1 portfor all 2) other of the banks. value readBecause from ; to findbanksthe aredat **a** lwaysfor all read'jn's¢quie fe otherba **n** ce,ks:once you fitid'Bank 0 in the table, then you know where where | In the examplé:above, because bit 0 of word J:was clear (assuming controller port 0), then you would : knowBank that thedata:forBank 0 was in words 6:7: Since we only have two banks, that means the datafor 1fist be in words:B#15. 4 Suppose you'had a6D Controtiér,:which has 3 different banks of information, connected to port 0. q After reading3 banks’ worth of information from this controller, you might end up with a buffer that 

**==> picture [24 x 17] intentionally omitted <==**

**----- Start of picture text -----**<br>
Jn<br>**----- End of picture text -----**<br>


21 June, 1995 

Confidential Information “FPR Property of Atari Corporation 

©1995 Atari Corp. 

Page 27 

: | 

Technical Reference 

| | | | | | 

Bank 2 Bank 0 ; Bank 1 || Thewordfirst 9. thingIn this you example, need to word do is 9 find the would have data bit for 0 Bank clear 0. to indicateFirst yeu wouldBank 0. lookTherefore, at bit 0@E-wordwords 8-151, then : contain the data for Bank 0. Once you know that, then youalso know that.Bank 1 is contained in “ oo q words 16-23 and Bank 2 must be in words 0-7. time reguited when switching freig-one row to the ‘ Note that there is a certain amount of processifig 4 next, because the microcontroller inside the gonitroller has i6pula.different set of data on the outputs. 1 This is normally approximately 25 microsecésids (worse case is‘about[40][ microseconds)][ when][ going] from row to row within the same bank. Analog:¢éntrollers typically:also require an additional 200 {so that the analdg:inputs may be digitized). See WW@ microsecondsine Analog Joystick when going And Driving-Controllers from one bank to the sectiénnexé ft-ideas about baw to deal with this. 

a © 1995 Atari Corp. 

Confidential Information FER Property ofAtari Corporation 

21 June, 1995 

| 

Page 28 

Technical Reference 

| . | ‘ . a : |g 2 3 | 3 2 E 4 | & 4a | b = 

| j | 

| 

1 q . | | j 

© 1995 Atari Corp. | 

**==> picture [415 x 197] intentionally omitted <==**

**----- Start of picture text -----**<br>
Video RF<br>Mute Control Modulator<br>Clocks Stereo |gtaoe<br>Jerry PL Mute |_| audio | Be “Se<br>TX Data pac fe Fagre<br>RX Data -— = oo. | DSP Part<br>Expansion/Cartridge Port HEE<br>**----- End of picture text -----**<br>


The Jaguar console includes a stereo 16 bit aiidio subsystetii:. Digital audio data Cai only be sourced from the Jerry DSP. This data can also be mi@nitored at the"éxpansion or DSP ports, on the TXD seriai data line. Jerry can also read serial digital audia.data on its RX pin.. The bit clock and word strobe signals can be sourced by Jerry, the expansion ‘pért:or the DSP port::'Hfthe clock source is not Jerry, then software must force the Jerry clock lines tristate;[by][clearing][bit][ 0:0f][ SMODE.] The Audio mute function has bees added to: allow non-audid:daia:te'be transmitted by Jerry, without making a horrible noise on the audio outputs.:: Whes'serial peripherals are connected to the DSP port, and are in use, the audio shouldbé muted bywritingzero to bit 8 of the JOY1 joystick register ($F14000). Take great care not toéause the'J4-J7 outputs#6. all go low (by writingaltobit15andOto bits 4-7 in the same register). This will inadvertantly cause multi-player adaptors to go into extended 

| 

21 June, 1995 

Confidential Information “FO®% Property of Atari Corporation 

Page 29 

| | | : | \ | : | | | ' | | 1 | g ' 

| ’ Technical Reference |Gms” | The Jaguar console cartridge port supports up to 6 Megabytes of address space. Cartridges can be 8, 16 ' or 32 bits wide. Special support is also included for serial EEPROMS. Reading and writing the EEPROM must be done through the Atari supplied routines. (See the sampke:program for accessing NVRAM.) This is the only way to ensure reliable operation. Ee ee Bit 0 of the JOYSTICK register, when read, represents the data output Bit of the EEPROM. ‘and not the JO input from the joystick. Since JO has always been used as an output only. so far, this should hot cause atid fot equal to the JG output | problems. But bear in mind that this data bit is now random when read, It should be noted that the EEPROM uses addresses in the GPIO0 and GPIO1 range (SFE4800" $F15FFF). Any inadvertent acccss (reads or writes) to these address tanges will cause subsequent EEPROM reads and writes to fail. So dont do it ... mee Oe = When you build your own 32-bit test cartridges Hsing Alaris 4-clip EPROM carindge blanks, the ordering of data in the chips is as follows: Be OEE Chip Bytes Bits in 32-bit long[$800007,][ ‘8800008,][ ete.] —@Y F-Ui[|][ $800003,] Us YU4|| **$8** G900 **00** 70 **,** $S **80000** 54 **, $800008,** eteSiC **.** gisee 4d24-d31) In a non-encrypted test cartridge, Jogations $800000 to $801 FFF should have values of $FF. Your[cartridges.] program code should always start at$802000:[in][ both][ enctypied][ and][ non-encrypted] Burning Your Own Cartridge EPROMS | For those wanting to usé an EPROM biirner to create their own non-encrypted test cartridges, any EPROM burner capable of handling 4megabit EPROM chips should be acceptable. If you would like a specific recommendation for a particular EPROM burner, Atari has had good success with the Pilot EPROM, Burner, manufactured by Advin. This burner is relatively fast, and can handle ait-¢fitire set of EPROM chips at once. The table below shows the mode] numbers, a description, and the price f:the base unit andiaceessories: a Price Model Description Pilot 882D | Base unit plas ‘Gang Faceplate 832D for up to DIL-32 Pin | $1 510.00 EPROM / 4 megabit (includes base unit and software) . w Pilot 844D Replacement Gang Faceplate for up to DIL-44 Pin $1095.00 Ss EPROM / 16 megabit (upgrades Pilot 832D to Pilot 64D) ae 5 At this time, the Stubulator ROM used in development machines currently only supports the use of 32-bit wide cartridges. © 1995 Atari Corp. Confidential Information PPR Property of Atari Corporation 21 June, 1995 

| 1 ‘ ' i ' 4 | : 

® a " dy: : ‘ . 

| : | ' 2 & i 3 | & 

| : F 7 . : ‘ Cd g. & : ' - bg = Po LY) 

fo ; 1 

| ' : | : ' j 

1 : 

Page 30 Technical Reference Pilot 844D Base unit plus Gang Faceplate 844D for up to DIL-44 Pin | $1795.00 complete EPROM / 16 megabit _ | (including base unit and software) package (Note: this unit does not include the 832D faceplace, and CANNOT handle 32 Pin EPROMs !!) 

Technical Reference 

This burner can burn a 4 megabit EPROM in approximately 3:08 minutes, or a 16 megabit EPROM in under 15 minutes. ee 

Please note that all prices shown are based on the latest information Gbtained by Atari; andiare subject to change without notice. These EPROM burners are not available directly'from Atari. Pleasé Sentact Advin to inquire about purchasing these products. To contact Advin from: North America: EEE, 

1050-L East Duane Ave. Technical questions: asxfde-Edwin “Ee Sunnyvale CA 94086 Sales information: ask forSvsan —_—s 

Advin’s USA office can handie out of countey: delivery if nétessary, but they may fave a local distributor. The distributor in England is (16Sbtain information about distributors for other countries in Europe, please contact Advin): ecm WEEE Quarndon Electronics Ltd. tiie, EEE TE Derby DE3 3ED se “Ese 

-«[EPROMs'ForMgkingTestCarttidges] The following EPROM[iypesfiave-been][successfully] used in Atari’s test department: For a 4x4 EPROM cartridge with 128 byi¢-EEPROM, a cartridge uses (4) 512kBit x 8 (4 megabit) chips. Be. EEE o Manufacturer Chip Code . | HE TC574000AD-120 or TC574000AD-150 “lee: AMD <2] AM27C040-150DC 

For 2 16x2 EPROMcartfidgewith 128 Byte EEPROM, a cartridge uses a single 1024kBit x 16 (16 megabit) chip: ceed . 

Manufacturer Chip Code 705716200 (Atari is currently looking for compatible parts) 

**==> picture [3 x 21] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


21 June, 1995 

Confidential Information FER Property ofAtari Corporation 

© 1995 Atari Corp. 

Page 31 

| || 

] 

F ‘Technical Reference ‘a Chips with access speeds slower those shown above are not recommended. Similar chips from other Py manufacturers may work, but have not been tested by Atari. Try them at your own risk. However, if fF — you do find other chips that work, please contact Atari’s Developer Support department and let them | know so that they can be added to the list. : 

© 1995 Atari Corp. 

Confidential Information JER Property ofAtari Corporation 

21 June, 1995 

