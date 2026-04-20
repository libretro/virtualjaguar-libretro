Aw 

| 

Confidential Information Property of : Atari Corporation 

Jaguar Software Reference Manual - Version 2.4 

Page i 

2 

j 

**==> picture [583 x 668] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|TableofContentsi|
|Introduction...|cece eeceeecseeseessensesseeasenseeecseeecsesescseseesseesesseeesesseecensageatessecesseeneed|
|What|is|Jaguar?|soscceccccccssscsssesseesceceeceeecsssunnsssesessssssnsssssusssesseeesseeceen|Se|e|eengiille D|
|How|is Jaguar used?|.......ccssssssssssssssscessssssseeesseeceesssesssssnssnsnniereeseregheibecef|e|cennennet|
|Jaguar|Video|and|Object|Processor ........cscecstscieeeeeeeeeeeeeeed|
|OVEIVICW|cossesesssscsssescecsssssseeeseccessssseeceessstseceessnnnmesseeesesseennmmeeieu|ee|ennanels|HEEHEEEBE|
|Object Processor|Performance|.......sssesscsseesscsssssetsccesssssneeessesneresenneeeentsdggibseesaeee|cices|
|Memory|comtroller|....ssesssscsssssssssecssssesecssnsccensnesseeceesnseeescensnncscesenneeesbonnsssl|liec|e|ses|ee|
|Microprocessor|Interface......ssssssssssessessneessteeeeseesseeeeadhdiliggeetteceesesensmenneeeee|e|er|ee|
|Memory Map|enecssssssesssssccscscssssssssnssescececcsonsvaeseseseesceseeseeeesigiitlMececcesssnnessceeeensessQe|Sobral|
|Peripheral|Memory|Map...sssessssscssscccssssssssssceeecceceeeeb|e|c|cccces|e|scecnseeeseeenssniis|15)HEEEHEE|
|Object|definitions|.........sccssessssssseecsssseeessescesseccsssseeeecsssnenfiilldicesnsSEEEEBES|ccsesseeess 16|
|Description|of Object Processor/Pixel path .....:ccccsessssciiivsseseseeeeeneseee|epeeenseees 21|
|Refresh|Mechanism|.........-cccsescsscesceeesscessssseesepanpgptensnssnsennsnsnnnnnmeneseseeceeeeersitiisiniiin,.«|24|
|||Colour Mapping...........seecceseeseeseep|BEES|aia|sec|cceeeces|e|eneseeeneesssesesee sft|o|e|e2D|
|/|Introduction|....cscessccsssesssseecssseececeececseseeedheb|bi|ccecccecennes|bi GbelDisepesecscsssevecseccnssnseeseesener|25|||
|The CRY Colour Scheme|......:sssscssssse|dbiieeccscsscssccceececeebbe|bite|sssesescseeesceecesesee s|e|s25|!|
|||Graphics|Processor|Subsystem ...........1:: 5g|ibneesssessecceceeseeesn|HEE|ibaecessseesesneseeneeeenene29|||
|7|?|Memory Map|sescnsnnansnnnnnnnnenensessssssssssenssansstsl|LLU|ape sscseseeeeeesesceeeeeeeesdligilpeecscseeeeseeeeee|30|1|
|YM|«Graphics|Processor........sscseccsszsaisihnegeeseesgigs|ecseseeesessearesbiibeessessese|ee|st|seens|e|enesGS|||
|||What|is|the Graphics|Procegs@r? 228g.|tlibyeeeeeseennaggbitetiescsseenenees3D|
|—|Programming|the Graphids:Processor(3228)...|EEE|BS|'|
|Design|Philosophy .........J28..cscssccssseGein|ya|eessesessesnosnsnmeescecssssssneeesssesees|34|'|
|Memory|Interface|...cceccccesce|ese cccseescen|EE seececcsssceesnnee|Gigi|lip cesseeecssecssneeesseseeccaneeceseeeBO|]|
|Load and|Store Operations|sovessssssecesncesteseesnseeseneneesscrssdiibbdesssseeessutsessetsessetseseeeeees|3S|q|
|Arithmetic|Furétians|sesseteeeesenseeeeeeseseensteescnnsinetesissessenafhiiiiecesusmcessnsneessesssnnneeeseersse|98|||
|[nterrupts|2.20.1|Ege|lteeseoeeseseneneeeeeeeeeeennenesifittltimanaitSbG|sccecccccsesseneecceensssssneeeseeenees|39|1|
|Program|Control|F4OW|2.0|ccccccceccseeeeeert|EEE|U|ceccecccssssnereeseesnsnnnnneesesseeees40|4|
|Multiply|and|Ag@dtnulaté|Tastructions|2... eccceesscssseeesccsseesessseessseeesseecessteseeneseeseee42|j|
|Systolic|Matrix’|Multiplies|2222.sescccseeecssssnesesseesssseteeresseecesseecesnnseseessneseaneee43|{|
|DivideReBisterUnit .....eccceecceeccsseeceeseeeeersignage|epeescseeesssneeeesseeeesensesenneessesssntessneetsneeseeeeeeeee|43|{|
|External|FUGCPUC ieeeSS|ceeccceeeee|e|eeeeMEELone enesnsesessnnnenncececceceeecccccesensnneeessessnn|e|ec|e|sn|es|sssmeeesensssne|s|ns|s|sassssneeneeee|ss|eesssssns4G4|jj|
|||Back‘HnternalandRegistersUnpack|02/05iis.|Gi|.-ccscseeeeccccssseecseesescecessessenssnsnienseeececssnnessscscansnnnaneesessees cecceeccseeesesssnneessaneessnesesineessineesneessnessaecssnesesneseneense|4|54|
|Blitter|2.2|Se .cceccseccecsececeeceetbibblgescesssecescesecesceesececascssesecatecenecateuseesesanecersssrerscesneneeene|49|]|
|What'isProgramrninethé:|Blitter?the|Blitter200...|222Gob.|[.cccscsccccccsccssscsssscsssseseesesenssesseenseccessceceeeseeeeeseceeeseeens]|ccccccccecscsnssensnssnnsssnsnsansusnenesstecesceesesesssansssnenseeseeee499|1||
|Address|Genetatinisiisncsifl|el|occ essscccssssssseeeesseceeessesesssnsnvteeceessssnnuetecsesssnaseeesseees50|||
|Data Pate.|eee ee|cc ccccueecateccsssecessecessuscessneeenssesraseesneecnneseseesseneesaeeesesD2|||
|@|Bus|Interface|...c.cccccccccsssecsssssesscsesnesesssesececeestenesesecasucsessessecaeevsssarseseceeseeneeeeraneeeseaeeee|[D4]|q|
|Register|Description|.....ccccecsessssssseessesesseesesneesesecesnesteseesecsassaseusssensnteavsnenessnsassseeeeses5D|
|.|Address|Registers|.......-:ssscsessecesseeessesessesecsesecsessnenteucssesecsussesuesssussesscaesussseneresssaneeeeesDD|4|
|Control|Registers|.......eccececessessssecceeseeeseesnscsesessecessesesacsescassesussseseesnenecseseasenseessesesensD9)|i|
|Data|Registers|.0........:cccccscesesccsseesececseccecsessessessnseeceesscessssassseesecsssesssecseeseeereesseeesseseesOD|d|
|Modes|of Operation|.........scsccceceesessessescseeseseesessetecsssnecsneueeueseeunseereastesersessesessnseteteseses O4|:|
|© 1992-95 Atari Corp.|Confidential Information|TR|Property ofAtari Corporation|June|7, 1995|:|

**----- End of picture text -----**<br>


ii 

f | , 

Jaguar Software Reference Manual - Version 2.4 

JONTy cossesscssssccsssescestscsssessssesenseeeennseesenee ge **e** eessenseesanseeeneseeesenste AOU SOIC AS08 69 hhh Frequency dividers .....escsssssscsecseeeesesstssssesssssssssntnesorsesnannnnnnannnananennnnnnnnensnennennnrnnsgeg 69 my Programmable Timers ....-.cscossssssesssessssssseesercensessecnnnannaanascceesnensseseeeee te ete ee 8 70 Trnterrupts ..eeccsseccssscsssseesvessssnssceceneeseneccssnencesnssennsensnssenensseeueeeeueseeeseeeeessees ee ees e808 800 71 Synchronous Serial Interface........-.s.-s-sceccssereeceeecsesesett ts **e** seettnnnnaassses 72 Asynchronous Serial Interface (ComLynx and Midi) .......ssssesceseeseeeseeeesmustiiitionnss 73 Joystick Tater FACE seccccececessssssecensnssuneesuesssveresseseeeeueetuenseuneereneeenli pine ES. General Purpose IO DeCOdES -escsesssssssseesinsnssssstenesesssecenanssceessnsssteeipionccesesnascees ARG Introduction —ccscsssssenuunnasesansanasassenenenenseesusssenenessesssnenununvevssssnansnnsssieipyrsscssscssee TT EEEEEES Programming the DSP sscccccccccseseetsssavasssnannnnvnvesnssscnssnncsneeecensesessceesnseesslolitlbegsecee 77 “HEE Design Philosophy ..-secvecesveeeevssccentneeetnteneneennnetsegetntneenesevneneren teenie] 7 Hees | Pipe-Lining..ssccccoscsncmensteeennenneunenennnedl iyecmecnenenrenest AEB Eee Memory Mapnn: nnn, Load and Store Operations _eeecessisusesesanaunesssasnuessesismneessesiGediiji biiaesssessesseeees 18 CUBE | Arithmetic FUMCtiOns -..scc.ssssssssssssessssssseesesteesessesceeeenennngistib **i** eesUin **es** ss eeeeeess **e** rtGii 78 Interrupts a eceseespusitssnusannnensnansenannisansassnnnnet iessssaseaseesannstbHibillpgs se 79 Program Control TOW secsccsssseusesssssseenneessssseegagunnnggngseeeeesvsensesceccesnuunnasnnseensilipaidie D9 Circular Buffer Management ecscscceceneea SEES Obtgcecccceeneentneeseeneneenn ig FE Extended Precision Multiply / ACCUMUIAEBS!...........:--1EEE elses oeeseeeceteeeeeeerrtttetecee 79 Divide Unit seccccccssccsssssssedl i ilsves sosssceseecsnneetlbbithttitnesso ec cescensse ceee **s** snenssssseve **ee** sss **e** s 8Q Register File ccccesssesuuisnsssasennnnnsseseresees!ifsefligeessscsesssnsnsseeeesnndtliaeilityseccessnsecceesees BO External CPU AccessIG:3c INE re Internal Registers ccccovsccscnecsengggpeeeeneeeeensFAE URpeccsecceeneecneeneeteeipenceeneen 80 f Appendices ccecccssssensssssssssssesesssetGlllELE Nie sccccescse IH Bigg eeeseesssengd i pbeeeeennenneesssseee 85 RISC Instructionee ee 85 Writing Fast GPU and DSF. Programs vasetitBSteageecssevennerti tcoeeessenenneceesernes 99 Data Organisation - Big and:Léttle Endiagh 2222 cs ecessenenenssesssneneesseeseeee 10] 

ee © 1992-95 Atari Corp. Confidential Information TRProperty ofAtari Corporation June 7, 1995 

- 7 Jaguar Software Reference Manual - Version 2.4 

Page 1 

| | | { | | | | j i j ; q | 1 : 4 \ ' 

| — 

7 

This document is the Jaguar Software Reference Manual - it is a definitive reference work for the programmer's view of the Jaguar ASICs. It is neither a hardware reference work 80t puide to a particular implementation of the Jaguar design. a { Jaguar is a custom chip set primarily intended to be the heart of.a very high-perforradtice games / leisure: j computer. It may also be used as a graphics accelerator in moré. c@raplex systems, andapplied and to workstation business uses. EEE Be EEE q As well as a general purpose CPU, Jaguar contains four processifig units: Fese are: _ j — Object Processor nF _ : The Object Processor is responsible for generasitig-the display. For each displaytine it processes a set of commands - the object list - and genegatesthe dispiay-for that line in an intern@Fline buffer. Objects may be bit maps in a range of display resolutions,:he¥:may be scaled, conditional actions ‘ may be performed within the object list,'#8d interrupts to theGtaphics Processor may be generated. a The Graphics Processor is a.¥Biy fas:micro-procéss6t which is optifiiised for performing graphics generation. It has its own local RAM} asidl.a powerful: AEC which énéfudes fast multiply and divide operations. Be Heee 

The Blitter is closely coupled'to the GPU, and is able to fapidly move and fill graphical objects in memory. It includes hardware support for Z-buffering aad shading at very high speed. — Digital Sound Processor 6 Bed The Digital Soutid Processor is similar to the Graphics Processor, but is intended primarily for synthesizing sonnd, and for: playing back sampled sound. It may also be used for general processing tasks. - OE Jaguar provitles these. blocks with a 64-bit ditd path to external memory devices, and is capable of a very high data transfer rate into: external dynamic RAM. “8° 

© 1992-95 Atari Corp. 

Confidential Information FOR Property ofAtari Corporation 

June 7, 1995 

Page 2 Jaguar Software Reference Manual - Version 2.4 eee Howis Jaguarused? =. a 

**==> picture [4 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
_<br>**----- End of picture text -----**<br>


## Jaguar contains two custom chips, code-named Tom and Jerry. 

For graphics, Tom contains the Object Processor, the Blitter and the Graphics Processor. For sound, Jerry holds the Digital Sound Processor. In addition to these, there is an external CPU, currently a 68000. When animating graphics there are therefore four processing elements, and they havé: ail Betspecific roles to play. The CPU is used as a manager. It deals with communications with the outside world, and tapiddies the system for the other processors. It is the highest level in the control flow of a Jaguaé program, and has eomplete control of the system. “EEE CHEER The Object Processor is at the other end of the chain for generating graphics. It réads'an object list, and gpithe basis of the commands there assembles each display line of the video picture. Objects aréasually areas Of! pixels, and these may overlap and may be easily moved from fraié {o.frame. The order ie WHigh theyare” processed in the object list determines how they overlap. Objects Gast-aisG:modify what is alreaayirn:the display line being assembled, and can scale bit-maps. They may ¢omain transparent pixels. The Object Processor performs all the functions of a traditional sprite engine, Whitéalso offering all the flexibility of a pixel-map based system. It is capable of.a.range of animation effects, andtis a powerful graphics tool in its own right. pee OEE 

The Graphics Processor and Blitter provide a tight#y-coupled pai¥ Gf jirocessors for performing a much wider range of animation effects. A design goal of this's¥$tem was to provid¢:a fast throughput when rendering 3D polygons. The Graphics Processor therefore has a'fastinstruction througkputy.and a powerful ALU with a paraliel multiplier, a barrel-shifter, and a divide unit;:ig: addition to the normal arithmetic functions. The Graphics Processor has four kilobsiés of fast internal RAM, which is used for local program and data space. This allows it to execute progra#in paraliét with the othetptdicessingunits. The Blitter is capable of performing: 4 range of blitting @iération 64 biis‘dt'a time, allowing fast block move and fill operations, and it can generafe:strips of pixels for'Gourind shaded Z-buffered polygons 64 bits at a time. It is also capable of rotating bit-raaps, linedtawing, charagtér-painting, and a range of other effects. The graphics processorand the Blitter will usually act together pitéparing bit-maps in memory, which are then displayed by the Object 'Prcessor. i, _gfEEE The Digital Signal Processor has eight kilobytes offastigternal RAM, which is used for local program and data space. It is tightly cdupled toJerry's internal timers, interrupts and audio output to allow fast, independent access. ORE 

f : 

**==> picture [11 x 12] intentionally omitted <==**

**----- Start of picture text -----**<br>
is<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. Confidential Information FRProperty ofAtari Corporation 

June 7, 1995 

| Jaguar Software Reference Manual - Version 2.4 Page 3 | Jaguar Video andObjectProcessor 

| | | : | ; | j ; 1 1 1 q 1 j ‘ 

Oveview The Jaguar video section has been designed to drive a PAL/NTSC TV. However by adoptitig 4 flexible approach to the design the chip can be used with a range of display standaids through VGA toWiiristation. | This will allow the chip to become the backbone of many (possibly unforesééia} products. “PEERS Two colour resolutions are supported, 24-bit and 16-bit. The 24-bit mode is useftid faeapplications requiring true colour. The 16-bit mode is designed for animation. It consiigiesless memory, fits:better.into 64 big: memory, and in the case of CRY (Cyan, Red, Intensity), is simples. 0'shade and is almost tdistitioniishable from 24-bit mode. ee HEHEHE? Jaguar decouples the pixel frequency from the system clock byatising a line hutfer, This means thai the system clock does not have to be related to the colour carrier frequency and may be unaffected by gen-locking. There are actually two line buffers one is displayed while,thedither.is prepared by the Object Pocessor. Each line buffer is a 360 x 32-bit RAM. The line buffer coatasns physi¢alipixels these may be eithér16- or 24-bit pixels. The line buffers may be swapped over atte start and itt[$#e:tiddle][of][ display][lines.] In CRY, pixels at the output of the line buffer até gonverted to 24-bit RGB-pixels using a combination of 1. look-up tables and small multipliers. WEEE OEE, /) @ The video timing is completely programmablein units Gf thie-video clock. tee Jaguar uses an Object Processor, this Combines the advantages f frame, sire and sprite based architectures. Jaguar's Object Processor is simple:yet sophisti¢aied. It has scaledatid:unsealed bit-map objects, branch objects for controlling its control fay, and interfupe Objeceselt can interrupt the graphics processor to perform more complex operations on its behalf: The graphics procesgpe will support perspective, rotation, branches, palette loads, etc. ae * eee 

The Object Processor casiwrite into the line buffer at up to iw pixels per clock cycie. The source data can be 1,2,4,8,16 or 24 bits per pixels. Except for 24 bits, obivets of.difterent colour resolutions can be mixed. The low resolution objects, ofé:40 eight bits, use a palettéte@btain[a][ 16-bit][physical][colour.] A sophistication in the Object Processdtiis that it can modify the existing contents of the line butfer with another image. This could be used to pradice shadows, mist or smoke, coloured glass or say the effect of a room illuminated:-by.flash lamp. EBs The Object Processor énif'also ignore data whichis stored alongside pixei data. If, for instance, a Z buffer is needed then this can beSititatédnext to the pixels. This helps because DRAM RAS pre-charges are needed 

**==> picture [20 x 24] intentionally omitted <==**

**----- Start of picture text -----**<br>
wo<br>**----- End of picture text -----**<br>


**==> picture [6 x 23] intentionally omitted <==**

**----- Start of picture text -----**<br>
44<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. 

Confidential Information TR Property ofAtari Corporation 

June 7, 1995 

Hi 

Each object is described by an object header which is two phrases for an unscaled object and three phrases for a scaled object. When an image has been processed the modified header is written back to memory. The Object Processor fetches one phrase (64 bits} of video data at a time. This phrase.is expanded into pixels (and written imo the line buffer) while the next phrase is fetched. eee 'mage data consists of a whole number of phrases. The image data may need to be padded With dansparent pixels (colour zero in 1.2,.4,8 & 16-bit modes). BEE OPE The Object Processor writes into the line buffer at one write per system clock iigkiln 24-bits-per-pixel mode and for scaled objects one pixel is written per cycle. For unscaled objects with 16:d#fewer bits-per-pixel:pvo —- pixels are written per cycle. Most objects will therefore be expanded at twice the proééssct:clock rate. 25 If the read-modify-write flag is set in the object header the object dita'is, added to the previous cOhiteni® of the line buffer. in this case the data rate into the line buffer is halved. 2222250854, HERE os This peak rate may be reduced if the memory bandwidth is not higti enough: However if 64-bit wide DRAM is installed then these data rates will be sustained for all modes. oe When accessing successive locations in 64-bit wide:RAM tie- memory cvcle time is tW6 ack ticks. These are page mode cycles. When the DRAM row addgess"must cha#ige'there is an overhead ofbetween three and seven clock cycles (depending on DRAM speed}::Fhese RAS cyclés:will.occur infrequently during object data fetches but will typically occur during the fif§idata read after reading:the object header (because the header and image data will not normally be near eatother in memory). RAS ‘eycles will also occur after refresh cycles or if a bus master with a higher priority ‘steais.some memory cyélés in an area of memory with a a different row address. Retresh cycles tidemaily be pasipéned until object processing has completed. mM 

Memory controller == Jaguar's memory controller is very fast and flexible. It hides thé sigmory width, speed and type from the other parts of the system. “tee nee Memory is grouped into ‘Hanksthat may be of different-widthszspéeds and types (although both ROM banks have the same width and sped): Bach bank is enabléé:byacbip select. In the case of DRAM there are two chip selects RAS & CAS.:Memory:widths can be 8,16,32 or 64 bits wide but the memory controller makes it all look 64 bits wide. 2: HERE |: ‘There are eight.write strobes - one for each eigbE-bits. There are three output enables corresponding to : d[0-15],d[46-34}: aid: d{32-63]. Three memory typéS:are supported: DRAM, SRAM and ROM. I, ROM or: EPROM iS used fa" Bootstrap and for cartridges. The ROM speed is programmabie. The memory : controllerallows the system ‘té:view. ROM as 64 bits wide. Pull-up and pull-down resistors determine the ROM width dising reset. s, DRAM is the pringipal memory type, 6 it is cheap and fast when used in fast page mode. In fast page mode the DRAM cycles'at twa-ticks per trafisfér. The row time access is programmable. The column access time is not programmable andtannly be. adjusted by changing the system clock (a page mode cycle takes two clock ticks). The memory controflér:decideson a cycle by cycle basis whether the next cycle can be a fast page mode cycle. Data and algorithms should be organised to minimise the number of page changes. The page size is 2 kbytes. 

There are four memory banks; two of ROM and two of DRAM. 

. 

© 1992-95 Atari Corp. 

Confidential Information TR Property ofAtari Corporation 

June 7, 1995 

i e = Jaguar Software Reference Manual - Version 2.4 

Page 5 

| 

|. JAGUAR has been designed to work with any 16 or 32-bit microprocessor with (up to) 24 address lines. The | interface is based on the 68000 but most microprocessors can be attached by using a PAL to synthesize those control signals which differ. All peripherals are memory mapped; there is no separate I/O space. } The width of the microprocessor is determined during reset by a pull-up / paifl-down £esigtor, Variations in the | address of the cold boot code/vector is accommodated by making the bootatrap ROM appeareverywhere until | the memory configuration is set up by the microprocessor. ooo OTHERS The microprocessor interface is generally asynchronous so the clock speeds df ike microprocessor sid 0- processors may be independent. ieeeicoem “HEE Jerry uses the same microprocessor interface. foe TEE ae The CPU normally has the lowest bus priority but under interrupé ifs pkiority iS increased. The following list gives the priorities ot all bus masters. -— s oe OE Highest priority 1. Higher priority daisy-chained bus master ssi... eee 4. GPU at DMA priority a Ee bee & —bject Processor _ oe 10. Blitter at normal priority 2) He HO ne 

| ‘ ' 

**==> picture [4 x 11] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


**==> picture [7 x 28] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. 

Confidential InformationTER Property ofAtari Corporation 

June 7, 1995 

Page 6 

Jaguar Software Reference Manual - Version 2.4 

| | 

## MonoyWep 

Jaguar's memory map depends on how it is being used. 

**==> picture [492 x 581] intentionally omitted <==**

**----- Start of picture text -----**<br>
Following reset the following 2 Mbyte window, corresponding to the ROMO area, is repeated throughout the<br>16 Mbyte address space until memory is configured by the microprocessor by writing [to][ MEMCON1.] [(This]<br>allows the system to boot whether the microprocessor is a 680X0, an 80X86,of'é Eragspirter.) After<br>configuration, this map corresponds to the area defined as ROMO on the mapsbelow. “!ff0n.<br>LFEFFE120000 ae "k_ 2Ee<br>H28008 Be oo<br>Eee oe. Oe<br>Taternal ee ne<br>Bootstrap FOM a _<br>When the memory configuration is setGne of twi:memory maps is:selected depending on bit ROMHI of the<br>TRPBEE | Romo TS EEESUEfy opamo<br>00000 | Bootstrap[and FSg7Ste=sROM ebibyces Hue"coccoo “HeeBynamicbes RAM 4 Mbytes<br>{ ROME dibs. :ADRAM.<br>CartridgéiROi:. | € Moytes iie.. aafiebynamic RAM 4 Mbytes<br>DRAM? gE ee, ROM?<br>Dynamic RAM CMBV Re s Cartridge ROM 6 Moytes<br>JE ORANG Ee ROMO<br>(Ege Dynami coRaMe: | 4 Mpytes ~ Bootstrap ROM 2 Mbytes<br>000000 4. el soocoo Lane seerster’<br>“OBOMHT=1000 ROMHI=0<br>ROMO is the boaisttap ROM but interaal (ASIC) memory and peripherals occupy 128 Kbytes of this space, as<br>shown above. ROM! ig:the. cartridge:ROM.DRAMO and DRAM are the two banks of DRAM.<br>A 68000 system will naturally operate with RAM at 0, so the ROMHI = 1 map is assumed throughout this<br>document. If the system is operated with ROMHI = 0 then the first digit of all internal addresses should be }<br>rather than F.<br>**----- End of picture text -----**<br>


eee © 1992-95 Atari Corp. Confidential Information TER Property ofAtari Corporation June 7, 1995 

Jaguar Software Reference Manual - Version 2.4 

Page7 

es ,r,rrt~S—sC.C.Ci‘SOSSCOCC;s;ds+dd#W 

! | : | 

|. 

a 

| 1 : | | J ' ' ; i |i q i q 

4 

Internal Memory is mostly 16 bits wide to allow operation with 16-bit microprocessors. 

32-bit write cycles are allowed to some areas of internal memory notably the line buffer and the graphics processor memory. The line buffer support 32-bit writes primarily in order to accelerate Blitter writes to the line buffer. The graphics processor supports 32-bit writes to accelerate program and data.loads. 

||.<br>a|es<br>,r,rrt~S—sC.C.Ci‘SOSSCOCC;s;ds+dd#W<br>Internal MemoryMemory is mostlymostly 16 bits wide to allow operation withbits wide to allow operation withwide to allow operation withto allow operation withallow operation withoperation withwith 16-bit microprocessors.microprocessors.<br>32-bit write cycles are allowedwrite cycles are allowedcycles are allowedare allowedallowed to somesome areas of internal memoryof internal memoryinternal memorymemory notably the line buffer andbuffer andand the graphicsgraphics<br>processor memory. The line buffer support 32-bit writes primarilymemory. The line buffer support 32-bit writes primarilyThe line buffer support 32-bit writes primarilyline buffer support 32-bit writes primarilybuffer support 32-bit writes primarilysupport 32-bit writes primarily32-bit writes primarilywrites primarily in order to accelerateorder to accelerateto accelerateaccelerate Blitter writes to thewrites to theto thethe<br>line buffer. The graphicsbuffer. The graphicsThe graphicsgraphics processor supports 32-bit writes to acceleratesupports 32-bit writes to accelerate32-bit writes to acceleratewrites to accelerateto accelerateaccelerate program and data.loads.|es<br>,r,rrt~S—sC.C.Ci‘SOSSCOCC;s;ds+dd#W<br>Internal MemoryMemory is mostlymostly 16 bits wide to allow operation withbits wide to allow operation withwide to allow operation withto allow operation withallow operation withoperation withwith 16-bit microprocessors.microprocessors.<br>32-bit write cycles are allowedwrite cycles are allowedcycles are allowedare allowedallowed to somesome areas of internal memoryof internal memoryinternal memorymemory notably the line buffer andbuffer andand the graphicsgraphics<br>processor memory. The line buffer support 32-bit writes primarilymemory. The line buffer support 32-bit writes primarilyThe line buffer support 32-bit writes primarilyline buffer support 32-bit writes primarilybuffer support 32-bit writes primarilysupport 32-bit writes primarily32-bit writes primarilywrites primarily in order to accelerateorder to accelerateto accelerateaccelerate Blitter writes to thewrites to theto thethe<br>line buffer. The graphicsbuffer. The graphicsThe graphicsgraphics processor supports 32-bit writes to acceleratesupports 32-bit writes to accelerate32-bit writes to acceleratewrites to accelerateto accelerateaccelerate program and data.loads.|es<br>,r,rrt~S—sC.C.Ci‘SOSSCOCC;s;ds+dd#W<br>Internal MemoryMemory is mostlymostly 16 bits wide to allow operation withbits wide to allow operation withwide to allow operation withto allow operation withallow operation withoperation withwith 16-bit microprocessors.microprocessors.<br>32-bit write cycles are allowedwrite cycles are allowedcycles are allowedare allowedallowed to somesome areas of internal memoryof internal memoryinternal memorymemory notably the line buffer andbuffer andand the graphicsgraphics<br>processor memory. The line buffer support 32-bit writes primarilymemory. The line buffer support 32-bit writes primarilyThe line buffer support 32-bit writes primarilyline buffer support 32-bit writes primarilybuffer support 32-bit writes primarilysupport 32-bit writes primarily32-bit writes primarilywrites primarily in order to accelerateorder to accelerateto accelerateaccelerate Blitter writes to thewrites to theto thethe<br>line buffer. The graphicsbuffer. The graphicsThe graphicsgraphics processor supports 32-bit writes to acceleratesupports 32-bit writes to accelerate32-bit writes to acceleratewrites to accelerateto accelerateaccelerate program and data.loads.|es<br>,r,rrt~S—sC.C.Ci‘SOSSCOCC;s;ds+dd#W<br>Internal MemoryMemory is mostlymostly 16 bits wide to allow operation withbits wide to allow operation withwide to allow operation withto allow operation withallow operation withoperation withwith 16-bit microprocessors.microprocessors.<br>32-bit write cycles are allowedwrite cycles are allowedcycles are allowedare allowedallowed to somesome areas of internal memoryof internal memoryinternal memorymemory notably the line buffer andbuffer andand the graphicsgraphics<br>processor memory. The line buffer support 32-bit writes primarilymemory. The line buffer support 32-bit writes primarilyThe line buffer support 32-bit writes primarilyline buffer support 32-bit writes primarilybuffer support 32-bit writes primarilysupport 32-bit writes primarily32-bit writes primarilywrites primarily in order to accelerateorder to accelerateto accelerateaccelerate Blitter writes to thewrites to theto thethe<br>line buffer. The graphicsbuffer. The graphicsThe graphicsgraphics processor supports 32-bit writes to acceleratesupports 32-bit writes to accelerate32-bit writes to acceleratewrites to accelerateto accelerateaccelerate program and data.loads.|||||
|---|---|---|---|---|---|---|---|---|
||<br>j|WEMCONT<br>Memory Configuration RegisterOne =—=§§-— FooGONRW<br>DoNOT Modify:Forinformationonly)||||||||
|f|||Bits<br>Name<br>0<br>ROMHI<br>1-2<br>ROMWIDTH|Description<br>WhensetthetwoROM:decodesaddressthé:tap<br>8M within the<br>16Mwindow. Whenéleas'<br>tie ROM decodesaddress<br>the tottom<br>8M.Thisdocumentassumes h¥oughoutthatROMHI<br>is setwhen<br>| discussing registera@tesses.72222,<br>Specifies thewidth ofROM:<br>COREE||||||
|||||<br>3-4<br>ROMSPEED|[3<br>64bits<br>SpecifisstheROM cycletiie!<br>=,||||||
|||||5-6<br>DRAMSPEED::2.<br>cree<br>“EE?”|Specifies'the IERAM Speed. Thepagemodecycletime isalways<br>two.dlack cycles: FhesebitsdetermineRASrelated timingas<br>| folldWs:<br>“EEE,<br>Precharge | RAS toCAS<br>Refresh||<br>|<br>|||||
|||[—_|——“Sgrmaaenokgees<br>7fettieFASTROM<br>Séts:the ROMcycletimetotwoclockcycles.This isfortest<br>oa<br>| purposesonly.||||||||
|||||1812<br>IOSPEED 225...<br>THEE<br>“tues. <br>THE,<br>“ee <br>_<br>uD|Specifiesthespeedofexternalperipherals.Thenumberofcycles<br> |hereisthe overallcycletime,the control strobes areactivefor<br> |twocycleslessthanthis.<br>|0 18clockcycles||||||
||||es|3<br>6clockcycles|||||
|||||||||||
||||CPU32|Indicates thatthemicroprocessor is32bits.||||;|
||||15<br>unused||Settozero.|||||



© 1992-95 Atari Corp. 

Confidential Information TER Property ofAtari Corporation 

June 7, 1995 

Page 8 

Jaguar Software Reference Manual - Version 2.4 

i 

} | : | 

q ‘ 

All the ROMSPEED bits are set to zero on reset. ROMHI, ROMWIDTH and CPU32 are determined by external pull-up / pull-down resistors. All the other bits are undefined. ROMO repeats every 2 Mbytes until this register is written to. 

## MEMCON2° Memory Configuration RegisterTwo = 

**==> picture [494 x 456] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||
|---|---|---|---|---|---|---|---|
|MEMCON2°|Memory|Configuration|RegisterTwo|=|Foooo2 RW|
|Bits|Name|Description|
|0-1|COLSO|||Specifies number of columns|in:|RAMO|OEE|
|2|1024|ie,|=|ee|
|||3_|2048|co|eo|
|||2-3|DWIDTHO|Specifies|the width|of DRAMQ._|eee eres|
|||32|bits|||_|||
|3|_ 64 bits.|EE|||
|4-5|||COLS!}|Specifies|suimber'of ¢olumns inDRAML|=H|||
|6-7|DWIDTH1|_aap Specifies|the|width:of|DRAMI|2|
|8-11|REFRATE|“EE|||Specifies|the|refresh'tate. DRAM rows|are refreshed ata|
|HEERe-||||frequencyrequire a refreshof CLK frequency of/ (64:x (REFRATE+1)). 64 KHz. RefreshMany cycles DRAM occurchips at the|||
|ice|||end of objéekiprocessing.|If REFRATE|is zero|refresh|is|disabled.|
|12|||BIGEND|5s.|||Specifies|thatbig-endian|addressing should be used. This|
|“|OEE 'dorbe|used comfortably|with Big-endian|(Motorola)|processors|or|
|cae|“eullloa| determines the address of a byte within a phrase and allows Jaguar|||
|_aaniigiies..|“With|:Ejttle-endian|(Intel) processors.|
|||13222|ED.|Specifiés:that image data should be displayed from high order bits|||

**----- End of picture text -----**<br>


All the above bits are undefinedGt téset except BIGEND which is determined by external pull-up / pull-down resistors. 222288. OE HC °°Hordentak@ount——<“<SCS*«srORw This register comprises of a ten bit counter which counts from zero up to the value in the horizontal period register twice per video line. An eleventh bit determines which half of the display is being generated. The counter is incremented by the pixel clock. The vertical counter is incremented every half line in order to support interlaced displays. This register is only for ASIC test purposes. 

© 1992-95 Atari Corp. 

Confidential Information FER Property ofAtari Corporation 

June 7, 1995 

Jaguar Software Reference Manual - Version 2.4 

Page 9 

| 

yen avaricarcount ee robes AW This register comprises of an eleven bit counter which counts from zero up to the value in the vertical period register once per field. A twelfth bit determines which field (odd/even) is being generated. The counter is incremented every half line. This register can be read io do beam synchronous operations. It is only written to for ASIC test purposes. ee, pH oo Horzontaluightpen FoggbR RON, o This read only eleven bit register gives the horizontal position in pixels ofthe ddght-pen. _ ever ooo owenicantigntpen” 79 FOOO0A RD The low eleven bits of this register gives the vertical position ofthe fightepen in half lina 

a , These four registers allow the graphics processogig read the Gliszent object. This allows thé graphics processor object to pass parameters to the GPUsitterrupt service faistine. 

## aes 

This 32-bit register points to the stargf the abject list. All objects must be ona phrase boundary so the bottom three bits are always zero. Whenone-object links to'ansihérbits 3:00:21 of this address are replaced by the LINK data in the object. The vafue stored indis register shouldbe ward-swapped. Because the Object Processor could interrupt the 68000 in the middle of a write to this register, the 68000 should never be used to change OLP. Use the GPUinstead. an 

eeee Bit zero of this register can be tested by the Object Peaeessar branch instruction. If set the branch is taken, if clear execution continues with the déxs object. This flag is intended as a mechanism for letting the graphics processor control the Object Processéf program flow. A write (of anything) to this register restarts the Object Processor afteraGraphics Processor inté#rieptabject. 

**==> picture [450 x 61] intentionally omitted <==**

**----- Start of picture text -----**<br>
Biis Name Description<br>0 “282, VIDEN “clas | When set enables time-base generator. This should never be set<br>cseet tee 222 | to zero in a Jaguar Console.<br>1-2 TMODE..._ £2) | Determines how the line buffer contents are translated into<br>**----- End of picture text -----**<br>


. 

© 1992-95 Atari Corp. Confidential Information PER Property ofAtari Corporation 

June 7, 1995 

i . 

: j : 

, . 4 % 

| { | j 

## Page 10 10 

**==> picture [500 x 716] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|
|Page 10 10|Jaguar Software Reference Manual - Version 2.4|
|CRY|16|(0)|1|16-bit CRY. Each|32-bit|entry|in the line buffer|is treated|as two|
|||16-bit CRY pixels|on successive clock|cycles.|Each|is converted|||
|into eight bits of red,|green, & blue using a combination|of lookup|||
|||
|t|
|||| tables and multipliers. CRY16 pixels are arranged as follows:|||
|||||Bais|oo.|Bio|
|||||||GOGBSTRABEBEoT0o|||
|||:|The least-signifigant bit is normally interpreted asthe|Séast-|||
|||||signifigant bit of intensity.|If VARMOD|is also|set,|this’bizwill be|||
|||cleared to indicate|a CRY16 pixel andaly|the top seven|bigs will|||
|||be|used|to|determine|intensity.|eee|||
|||RGB24 (1)||||phys24-b|i|tcal RGB.pixel Each with 32zbzi eigh|t|ditsentryof inred, the eight line bufferis bits|GE|Blutr|e|:eightated asGeBES||||
|||||of green and eight bits|uBissed|-RGB24|pixels|arearrangedeS|||
|||||| follows:|(a.|||
|||!|__—|6h|||
|||||||ESSEROOO|R|ASE|ER|||
|||| DIRECTIO()||I|T6-bitERRdirect. Each 32-bitEEPOOEOeeeoe etry th.the|line buffer|is divided|into||||
|||||||two 16-biE Words which are outpéif: directly onto the red and green|
|||ioutputs|on|algersiate phases|of theWideo clock. This mode|is|for|
|||||_/||applications requirise-adot clock|iiexcess of the video clock.|It|
|||||222See| ‘is out as|s|umedidé:the tc|h|atip. further wultiplexitse'andIn this|modé blanking|andcolour video lookup active are will occur|||
|||||Pees|output:onthe|two|least|significant|bits of blue.|
|||RGB16 (3)|"EEE“|16-bie16-bit RGBRGB. Each'32bitpixels. REB16 entrypixels in theare linearranged buffer isas treated asfollows:|two.|
|||ss|||RHBSHOOREEOEEEES|||
|Hee|“lllThe|least-signifigant|bit|is normally|interpreted|as the|least-|
|“||significa bit of green.|If VARMOD|is also set, this bit will be|||
|||eee|||sét igHiidicate|a RGB16 pixel and only the top five bits will be|
|ee,|used f0:determine the|level|of green.|
|Bae|||GENBOCE:.|When|set this bit enables digital genlocking. This means that|
|ee|||“eleue,|4|external syncs will reset the internal time-base generators. Onits|||
|TEER|||“ees.|||own this mechanism does not give satisfactory genlocking|
|Oe|“©|||because there|is jitter. However this mechanism|is used to quickly|||
|“HORE|==)|lock onto a new video source. An external Phase Locked Loopis|||
|ee|||required for true genlocking.|Not supported|in Jaguar Console.|||
|2|8 ge|[|Enables encrustation. When set, the least significant|bitofthel6|5|
|||4|T INCEN|
|i|j|—_—|!|bit data|is used|to switch between|local and external video sources|}|
|j|J|using an externa! video multiplexer.|This allows|the video source|||
|{|to be switched|on a pixel by prxef basis.|/|
|5|{|BINC|Selects|the|local border colour if encrustation|is enabled.|i|
|To|
|© 1992-95 Atari Corp.|Confidential Information ‘FER|Property ofAtari Corporation|June|7,|1995|

**----- End of picture text -----**<br>


Jaguar Software Reference Manual - Version 2.4 

Page 11 

**==> picture [502 x 380] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|
|{|7|BGEN|Clears|the|line|buffer|to|the colour in|the background register after|
|||displaying the contents. This only has effect in CRY and RGB16|
|||modes.|
|iS|VARMOD|Enables variable colour resolution mode. When this bit is set the|
|least|significant|bit of each word|in the|line buffer|is used|to|
|determine|the colour coding scheme:oftpt|h|ere|15|bits.|If the|bit|
|is|clear|the|bits the word|is treated|ase|ERY|pixel.|If the|bit|is|set|
|||then|bits|[1-5]|are|green,|bits [649]|are blue’aHi@Bits|[11-15]|are|||
|||red, This mechanism|allows JAGUAR to support'a#|RGB window|||
|||against|a CRY background|for isistance.|GEE|
|9-11|PWIDTH1-8|This field determines|the width|of|[#uxéts.in][ video][ clock][ cycles.]|
|The width|is one|more|than|the valuig: this.fi|e|ld.|||
|||The video time bas¢:generator|is programmed in.cycles|ofthe:|
|||video clock and not the|iixel|clock produced|‘oy|thus wivides:”|||
|The display width shaild:b¢:sét.to be an integer nuriiber|[of]|[pixels,]|||
|[Es|Use|Wei.|e|. an integzero|e|sr multiplé:of thepixel:width programmed here.|
|BORD2|-—«»-BorderGolour(@Biuey|FoR|WO|
|These registers determine the physical border coluii,|There are eight|BHS|per|primary colour. Red is the less|
|significant byte of|BORD1.|This colour is displayed: between|the active portions of the screen and blanking.|It|
|is not necessary 10 display|a border. The-horder|area isdefinedby|the video|amme-base|registers.|
|Hp|oO|Morizontal|Period =|OBOE|WOO|
|Do|NOT|Modify:|For informationonly|

**----- End of picture text -----**<br>


This ten bit register determines the period of halfa display line ig:video clock cycles. The period is one tick longer than the value written into this register. Eee 

Do NOT Modify: Fer[Information] only” i.===. This eleven bit-register determines the start position of horizontal blanking. The most significant bit is usually set becausé blanking Starts in the second half 6fthe!fine. 

## Do NOT Modify: Forinformationonly 

This eleven bit register, determines the end position of horizontal blanking. The most significant bit is usually clear because blanking ésids. in the-first half of the line. 

Do NOT Modify: Forinformationonly |=| This eleven bit register determines the width of the horizontal sync and equalization pulses. The pulses start when the horizontal count equals the value in the register. The pulses end when the horizontal count equals © 1992-95 Atari Corp. Confidential Information AR Property ofAtari Corporation June 7, 1995 1995 

June 7, 1995 1995 

vy 

the horizontal period. The most significant bit is usually set because horizontal sync happens at the end of the line. The most significant bit is ignored in the generation of equalization pulses which are the same width as horizontal sync but which appear twice per line (for 10 half lines during field blanking). 

} 

Do NOT Modity: For information only) This ten bit register determines the end position of the vertical sync pulses. Weitical Sync Gongisis.of long sync pulses for several half lines. These pulses are generated twice per line::Wértical sync starts'at4Hé:same time as the horizontal sync or equalization pulses but end when the least signifgéantten bits of the hatizénta! HDB2 _ Horizontal DisplayBegin2 - "0003A WO These eleven bit registers control where on the display line the Object Processér starts. When the horizontal count matches either of the above registers the Object Processor starts execution atthig:address in OLP, the line buffers swap over and pixels are shifted out of thie dine buffer. WHHEEEEn 

The Object Processor can run twice per line in oriet to support dispiiy. modes where the amount of data on a display line is greater than can be contained in o¢:line buffer. Theline:Bufférs are each 360 words x 32 bits. If the display mode was 720 x 24 bits per pixel thé#idine buffer A might'b¢ displayed at the start of the line while buffer B was being written. Then during the sééenid-half of the display: line buffer B would be displayed while line buffer A was prepared for the next.line. In this:case.HDB1 would comlain a value corresponding to the left hand edge of the display and HDB? would contain 4 Value:corresponding to the middle of the display. If the Object Processor needs to ruigaily once pés'line then either thefegisterstake the same value or one register is given a value greater thafthe line lengthy: ride. NFP 

**==> picture [463 x 197] intentionally omitted <==**

**----- Start of picture text -----**<br>
This eleven bit register specifies when the display ends. Either border colour or black (if HBB < HDE) is<br>displayed after the horizongal:cdunt matches this registenscesiiie”<br>The relative positions of séiné of the above signals and the registers which define them are shown on the<br>following diagram. OEE<br>ee lay line TT TTS<br>/ ce nS | [re ns | | hec¢ ns | | neg<br>holank 7 he ee noes |<br>vactive i: Ee l/nabt . nde |<br>**----- End of picture text -----**<br>


, 

: 

| 

a©1992-95 Atari Corp. Confidential Information TER Property ofAtari Corporation June 7, 1995 

| | fi 

] | | : ‘ ] { ‘ 

| . am 

1 

j |[i] 

w 

**==> picture [541 x 56] intentionally omitted <==**

**----- Start of picture text -----**<br>
Jaguar Software Reference Manual - Version 2.4 Page 13<br>sn @ VP _—sisisojzéNerticalPeriod =  FOOOSEECCWO—“ es<br>BoNOT Modify:Forinformationonly<br>**----- End of picture text -----**<br>


This eleven bit register determines the number of half lines per field. The number is one more than the value written into this register. If the number of half lines is odd then the display is interlaced. BoNOT Modify: Forinformationonly == This eleven bit register specifies the half line on which vertical blanking begins: 3 VBEDO _VerticalBlankingEnd== Foooaz WO NOT Modify: Forinformationonly $= = =. eee. This eleven bit register specifies the half line on which vertical -Hfanking ends. Bo NOT Modify: Forinformationonly Forinformationonly = This eleven bit register specifies the half line onWwhtich vertical sync begis&, Vertical sync pulses are Generated from this line to the line specified by the'vertical period. OEE 

## Bo NOT Modify: Forinformationonly Forinformationonly = 

VDB_—ssdsisé Vertical Displayegin == =. Foosss WO This eleven bit register specifies the half line on whic abjectprocessing begins. Object processing restarts on everythese line until the half line specifiedty the VDEfegistet:“Fhie:border colour (or black) is displayed outside active lines. WHEE OE WHEE VDE ss Veettigal DisplayEnd ==, = 00048 WO This eleven bit register specifies thé’balf line at which object processing ends. Due to a bug in the Jaguar Console, this register should be sét:#t $F FF to cause the Object Processor to process every line. 

VERB = = WerticalEqualizationSegin = FOOO4AA WO DONOI Modify; forinformationonly This eleven bit register specifies fhie.half line on which equalization pulses start. 

VEE __MerticalEqualizationEnd = Foo0ac, ss WO Do NOT Modify:Forinformationonly = This eleven bit register specifies the half line on which equalization pulses end. 

| 

{ 

© 1992-95 Atari Corp. 

Confidential Information PO® Property ofAtari Corporation 

June 7, 1995 

, 

Jaguar Software Reference Manual - Version 2.4 

1 

_[Page][14] 

| } : ‘ : | 1 4 - 

z : 

This eleven bit register specifies the half line on which the VI interrupt is generated. This must be odd if the display is non-interlaced. This interrupt will occur once per frame when interlaced, that is every other field. 

These two 16-bit registers control the frequency of interrupts to the CPU and t6 the GPU. PREEOES PIT(] operate as a pair controlling the interrupts. on “CHEE The system clock is divided by (one plus the value in the first register). If the fist tegister contains zé86 the timer is disabled. The resulting frequency is divided by (one plus the value in the'seeoiad register) and these, output of this divider generates the interrupt. ohn eee eee Ee Do NOTModity:Forinformationonly This ten bit register determines the end position of the.equalization pulses. Equalizatién Sonsists of short sync pulses for several half lines on either side of vertical syne: These: pulses are generated twice: ger line. 

**==> picture [546 x 336] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|
|This register specifies the CRY coiour to which|the line|buffer|is cleared.|7|,|3|
|Tt|©|ePUInterrupt|ContraiResister|FooEO «RW|tO|
|This register enables,|identifies|and|a¢knowledges|intezsupts|fd|the five different CPU|interrupt sources.|||7|
|The|interrupts sources|are|as follows!|Hee|OEE|—|
|Equate|Bit|Interrupt|Description|||a|
|C_VIDENA ||0|+ Mideo|This interrupt|
|Ee|is|generai¢d by the video time-base, on the line|||||=|
|_||selected|bythe|Vitggsster.|||
|C_GPUENA||1|GPU|EE|This interruptis|generated|by|the graphics processor writing|to an|]|7|
|C_OPENA|Object|“yPhsinterrupt|is generated by stop objects.|||_|
|C_PITENAS(32%e...||Timer|||This'gmterrupt|is generated by the PIT.|[|
|C_JERENA)|4° Ferry|This interrupt is generated by an input to Tom and is intended|for|||e|
|||ae|a cseeeeeem|use by Jerry. This|is an active high edge-triggered|interrupt-the|||||q|
|cee|“ue|||first interrupt|will occur on the|first rising edge after ithas been|||(RE|
|C_VIDCLR®:|When set,|this bit clears pending video time-base|interrupts.|if|S|
|C_GPUCLR |G28: GPU|22) When|set,|this bit clears pending GPU interrupts.|i;|4|‘4|
|C_OPCLR|[10|“2:2 Object gi:|When|set,|this|bit clears pending Object Processor stop object|:|
|C_PITCLR|When|set,|this|bit clears|pending PIT interrupts|||
|C_JERCLR|Jerry|When|set,|this bit clears pending Jerry|interrupts.|]|

**----- End of picture text -----**<br>


" 

© 1992-95 Atari Corp. 

Confidential Information JER Property ofAtari Corporation 

June7,1995 3 

| Jaguar Software Reference Manual - Version 2.4 Page 15 a M@ Bits 0 to 4 enable the individual interrupt sources, ie. if bit 1 is set the graphics processor interrupt is enabled. Se = When read bits 0 to 4 indicate which interrupts are pending, i.e. if bit 3 is set there is an timer interrupt ij pending. Bits 8 to 12 clear pending interrupts from the corresponding interrupt source. Note that INT2 must always be written to at the end of a CPU interrupt service routine. 

i | i i] 

1] : 4 i | ; ; ; q 1 | ] ‘ 4 : : | : 

When an interrupt is applied to the CPU the bus priorities of the graphics ‘pracessor and Blitier e-reduced so that the CPU can service real time interrupts promptly. The bus priorities a#@festored by writing aty:value to this register. This should therefore always be done at the end of an interrupt service routine. After the: sprite to this port the Blitter or GPU may then restart, and no further instructions will the: be:executed until eittir:the next interrupt occurs, or the GPU or Blitter operation completes... EE Gee 

The colour look-up table translates an eight bit colour index into[a][ 16-bit][ physiéal][éolour.][ The][ eight][ bit][ index] comes from the object data, which may be 1,2.4 orS:hits:dn order to achieve a high: thzoughput there are two tables allowing two pixels at a time to be writteg amto the: ling buffer. There are 256 16+bif'entries in each table. Locations in the range F00400-5FE read:fram table A.Becations in the range F00600-7FE read from table B. Writing to either range writes to both iables. Writes to this: region of memory may be unreliable when an object with the ‘Release’ bit is part ofthe current object Hist. 

rr—“‘COsiOCOOSCC:OC:C:is*i* CC | There are two line buffers each of‘which consis of a 360 « 32cbit RAM. Each 32-bit long-word can be ] read/written as two 16-bit words. In 16-bit CRY mode each wétiis a CRY pixel; the less significant byte Ss the intensity. The word:with the lowest address corresponds tq:th€ left-most pixel. In 24-bit RGB mode each 4 32-bit long-word is a pixel: The less significant byséiofthe word at the lower address is the red value. The : more significant byte is tere¢n;value and the less'sggnifigant byte of the word at the high address is the : blue value. The fourth byte'is unused... | The first address range addresses line bigtter. A. The second addresses line buffer B. The third addresses the : line buffer currently selected for writing. PRe:fisst two address ranges are for test purposes the third is for the graphics. processor to assist the Object Proces86f:ii:preparing the line buffer. By additig 8000h to thé above, address ranges 32-bit writes can be made to the line buffer. This is mainly to accelera **te** h Blitter. 7, Soe eee Jerry and external peripheralviocéupy the 64k above the internal memory. All Peripheral Memory is 16 bits wide although it is likely that many devices will have eight bit buses. 

| 

eee © 1992-95 Atari Corp. Confidential Information JER Property ofAtari Corporation June 7, 1995 

> ' / 

= SNNNOOS DOOOIOD AO TT 

. gE: 14 q a | a 4a =. | 4 " poy | 8 — | Po _ ] Po | 

} =: 

| 

## . Page 16 PEONOeddantionsG EOD 

Jaguar Software Reference Manual - Version 2.4 TENE LE SIE SSE SE EEL -_ 

There are five basic object types 

## re rrr, C—*=“#LN” This object displays an unscaled bit mapped object. The object must be on a E® byte boundérin 64 bit RAM. 

## C—*=“#LN” 

|||Bits|Field<br>Description<br>||||
|---|---|---|---|---|---|
|||3-13||YPOS<br>Thisfieldgivesthevalueinthe:yerticalcounter(ifhalfdines) forthefist<br>(top)lineoftheobject.Theverti¢al:counter islatched whe the. Object”<br>Processorstartsso ithasthesamg:value-across the whole line:Hftthe™<br>display isinterlacedthenumbeg isevelt For evenlinesandoddforodd<br>lines. Ifthedisplay isnon-intétlacedthenumberisalwayseven.The<br>objectwillbe active while theverticalcounter $#:¥POS andHEIGHT>||||
|||| <br>|<br>i<br>||14-23 <br>‘<br>24-42 <br> 43-63||HEIGHT<br>Thisfieldgivesthenumber@fdatalinesinthe object.As‘each lineis<br>displayed the:he¢ght isreduced:by:Gne<br>fornon-interlaced displaysorby<br>twoforinterlaced.displays. (Theheigbit’becomes zero ifthiswouldresult<br>inanegative vakue;)/ThenewvalueisWitten backtotheobject.Please<br>notethat<br>forscaled:bifitiap objects,HEIGHT should actuallybethe<br>— oa<br>ic<br> |LINK<br>This defines the addressof ihe nextobject,<br>Phese nineteen bitsreplace<br>Hits3to21 in'theregisterOLP®*Fiis:aflows anobjecttolinktoanother<br>‘@bjectwithin thesame<br>4 Mbytes.<br> |DATA<br>This defineswherethepixéEdatacanbefound.LikeLINKthis isaphrase<br>addréss. These twenty-one bits:define bits3to23ofthedataaddress.This<br>eon<br>allowsobjectdatatobepositionedanywhereinmemory.Afteraline iS<br>“Hunts. |displayedthenewdata addréssiiswrittenbacktotheobject.||}<br>|<br>|<br>—|
|||Bits<br>0-11|Field<br>~<br>‘Description<br>|<br>|XPOS<br>“<br>This:definestheXpositionofthefirstpixeltobeplotted.This 12bit field<br>nitive.<br>defines<br>sta#t positions intherange-2048to+2047.Address0referstothe|||<br>||
|||12-14|{DEPTH “ses. |Thisdefines the number ofbitsperpixelasfollows:||||
||||<br>|<br>||Fede<br>“celeeValue BitsperPixel Type<br>VideoModesAllowedIn<br>Sy<br>**|**<br>20<br>1bivpixel © CLUT<br>CRY16, RGB16,&DIRECT16<br>Ee<br>"| &<br>2bits/pixel<br>«=CLUT<br>"<br>"||{<br>|<br>—|
||||EES” 4<br>16bits/pixel<br>Direct<br>"<br>"<br>"<br>|<br>5<br>32bits/pixel<br>Direct<br>RGB24||:<br>]|



i © 1992-95 Atari Corp. 

Confidential Information “JER Property ofAtari Corporation 

June7,1995 

3 

**==> picture [575 x 729] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||
|---|---|---|---|---|---|---|---|---|
|Jaguar|Software Reference|Manual - Version 2.4|Page 17|;|
|||Jaguar|Software|Reference|Mani|
|15-17|||PITCH|This value defines how much data, embedded in the image data, must be|i|
|skipped. For instance two screens and their common Z buffer could be|||
|j|arranged|in memory in successive phrases (in order that access to the Z|i|
|;|buffer does|not cause|a page|fault). The value|8|* PITCH|is added|to the|
|used when|the pixel data|is contiguous|- a|vadiséef|zero|will|cause|the|
|||||data address when a|new phrase must be fetched. A|pitch value of|one is|;|
|7|same phrase to be repeated.|SEE|
|18-27||DWIDTH|This|is the data width|in phrases.|i.e. Daifor|the|next|lige 6£pixels can|
|be found|at DATA+8*DWIDTH|2225.|EEE|||
|1|28-37||IWIDTH|_..|This is the image width in phrases (must'b¢son zero). May be used:for|:|
|38-44|||INDEX|For images with|1 to 4 bits/pixel the top 7 to 4bits:of:the index provide|t|
|46|RMW|Flag to add object|to data|in|lineSuffer.|
|for intensity|and|the two coléux|vectors: 22:28.|
|i|The values are then signed offsets|
|GL|ERARS|Figo|make|logical colour zero|transparent”|||
|j|48|RELEASE|This|bit forces tke:@bject. Processor|to release thé:bus:between data|F|
|fetches.|This|shoutd|typicablj:be|set for low colour résglution objects|
|||(1 to 8 bits-pe#:pixel)|becailSé|there|is time for another bus master fo use|:|
||||||theshould bus be between.data held: by:the Objectfetches.|Processdf:Forditetcolour because resolutionthere|is very objectslittle the time bus|||H[|
|a|||between data fetekes:and other bus mastérs would|probably cause DRAM|||,||
|||||page:faialts.thereby|sigwing the system. This bit may be set, however, in|||
|||Eb bit'sealed:bitmap objéets:|External|bussnasters, the refresh|||1|
|P||jechanism,|pd the|graphics|processor DMA mechanism|all have higher|||||
|thé|‘Hestipixel|to be displayed. This can be used to clip|hi|
|||49-54|| FIRSTPIX||“Phisfieldan‘#mage. identifiesThié significancééfthe|bits depends on the colour resolution of|'|
|||.|the object and whether the object|is scaled. The least significant|bit|is only|||A|
|HEEB|| significant for scaled object: where|the pixels are written into the line|||a|
|:|“Ee.| buffer one|at a tind:|The'reimaining|bits define the first pair of pixels|to be|||
|t|[es|Edisplayed.|In|1|bit’ per pixel mode|all five bits are significant,|In 2bits per|||
|{|||||Eee“|“tspuxel.field:displays mode|onlythe the whole top fourphrase. bits are significant. Writing zeroes to this|||
|||
|SCBITOBJScaled'BitMappedObiect|
|This objeét|displays|a scaled|bit|sapped object. The object must be on a 32 byte boundary|in 64 bit RAM.|
|Scaled bitmaps:will|not display properly in 24-bit RGB mode. The first 128 bits are identical to the bit|
|||mapped object|#xsépt|that TYPE isong. An extra phrase|is appended|to the object.|
|Bits|Field|Description|;|
|||0-7|HSCALE|Te his eight bit field contains a three bit integer part and|a|five bit fractional|
|buffer for each source pixel.|||:|
|o,|||part. The number determines how many pixels|are written into the line|
||}|8-15|||VSCALE|This eight bit field contains a three bit integer part and|a|five bit fractional|||
|“|||||||part. The number determines how many display lines are drawn for each|||.|
|||aspect|ratio.|
|||||| source line. This value equals HSCALE for an object to maintain|its|*|
|© 1992-95 Atari Corp.|Confidential Information 7E® Property of|Atari Corporation|June|7, 1995|

**----- End of picture text -----**<br>


**==> picture [2 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
.<br>**----- End of picture text -----**<br>


**==> picture [554 x 357] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||
|---|---|---|---|---|---|---|---|
|Jaguar Software Reference Manual|-|Version|2.4|
|Page|18|This eight bit field contains a three bit integer part anda|five bit fractional|1|
|16-23|||[REMAINDER]|
|part. The number determines how many display|lines are left to be drawn|||
|from the current source line. After each display line is drawn this value is|7|
|decremented by one. If it becomes negative then VSCALE is added to the|||
|;|
|remainder until|it becomes positive. HEIGHT|is decremented every|time|
|VSCALE|is added to the remainder. The new. REMAINDER|is written|||
|back to the object. This value should be iniulized|t6the.same|value as|‘|
|| VSCALE to produce a perfectly scaled fist line.|ccc|
|aes||Unused, write zeroes.|He|EE|
|epuoss|@iephicsProvescoropect|=|8|,|
|This object interrupts the graphics processor, which may act on behalf the Object Processét.|Phe|Object )|
|Processor resumes when the graphics processor writes to the OBF|3bject|Processor Flag) registefe2|
|Bits|Field|Description|
|| memory mappéa.in the object|cade registers OBI0-3], Sathe GPU can use|||
|||3-63|||DATA|These bits|may beasedby-the|GPU interrupt serviee:routine. They are,|!|
|i|||| them as data oea5 a pointer{o'additional them as data oea5 a pointer{o'additional as data oea5 a pointer{o'additional oea5 a pointer{o'additional a pointer{o'additional pointer{o'additional{o'additional|parameters.|||
|Execution continues with the object in the next phrase: Fhe continues with the object in the next phrase: Fhe with the object in the next phrase: Fhe the object in the next phrase: Fhe object in the next phrase: Fhe in the next phrase: Fhe the next phrase: Fhe next phrase: Fhe phrase: Fhe Fhe|GPU may set may set set|or|léar the (memory mapped) the (memory mapped) (memory mapped) mapped)|
|Object Processor flag and this can be used to flag and this can be used to and this can be used to this can be used to can be used to be used to used to to|redirect|the|Object Processor using:the following object. Processor using:the following object. using:the following object. following object. object.|

**----- End of picture text -----**<br>


**==> picture [519 x 348] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||
|---|---|---|---|---|---|---|---|
|||| them as data oea5 a pointer{o'additional them as data oea5 a pointer{o'additional as data oea5 a pointer{o'additional oea5 a pointer{o'additional a pointer{o'additional pointer{o'additional{o'additional|parameters.|||
|Execution continues with the object in the next phrase: Fhe continues with the object in the next phrase: Fhe with the object in the next phrase: Fhe the object in the next phrase: Fhe object in the next phrase: Fhe in the next phrase: Fhe the next phrase: Fhe next phrase: Fhe phrase: Fhe Fhe|GPU may set may set set|or|léar the (memory mapped) the (memory mapped) (memory mapped) mapped)|
|Object Processor flag and this can be used to flag and this can be used to and this can be used to this can be used to can be used to be used to used to to|redirect|the|Object Processor using:the following object. Processor using:the following object. using:the following object. following object. object.|
|.|
|This object directs object processing either to the:LENK object directs object processing either to the:LENK directs object processing either to the:LENK object processing either to the:LENK processing either to the:LENK either to the:LENK the:LENK|addeess|or to the object in the following phrase. to the object in the following phrase. the object in the following phrase. object in the following phrase. in the following phrase. the following phrase. following phrase. phrase.|
|Bits|Field|Description|
|Branch object|is type|three|Hae|||
|3.13|WHst|goHdition|is used to determine where|to continue|||!|
|14-16|CC|eecea These bits specify’|
|||||OFprotessing:|a|
|||||||"2|Branch|to LINK if YPOS == VC or YPOS == 7FF|;|||
|eee||1|"Bratchto LINK if|YPOS > VC|po|
|saOE|3|Branchi#é|LINK|if Object Processor flag is set|
|te|CEH| 4|Branch to LINK if on second half of display line|;|
|17-23|||uatised|ieee|
|94-42|||LINK Gees.|Thig defines|the address of the next object if the branch|is taken. The|j|
|EE|address|is defined as described|for the bit mapped object.|;i|4||
|unused|BeLat|

**----- End of picture text -----**<br>


. This object directs object processing either to the:LENK object directs object processing either to the:LENK directs object processing either to the:LENK object processing either to the:LENK processing either to the:LENK either to the:LENK the:LENK addeess or to the object in the following phrase. to the object in the following phrase. the object in the following phrase. object in the following phrase. in the following phrase. the following phrase. following phrase. phrase. 

d © 1992-95 Atari Corp. Confidential Information JPR Property ofAtari Corporation 

June7,1995 

4 

Jaguar Software Reference Manual - Verston 24 

Page {9 

é : A : ! 

j 1 j 1 

' 

## STOPOBJ StopObiectt 

This object stops object processing and interrupts the host. 

Bits Field Description . TYPE Stop object is type four cesttitin. . 3 INT FLAG When set, CPU stop object interrupts areiénablediies. 4-63 | DATA These bits may be used by the CPU inté#yupt service'toutine.They are memory mapped so the CPU can use thé as data or as a'poutiier to additional parameters. cece epee 

© 1992-95 Atari Corp. 

Confidential Information TER Property ofAtari Corporation 

June 7, 1995 

‘ % . 4 4 ' E : | ' | 4 : 

Page 20 

| 4 " : : 

**==> picture [496 x 727] intentionally omitted <==**

**----- Start of picture text -----**<br>
Jaguar Software Reference Manual - Version 2.4<br>20<br>™<br>.<br>Object [Processor][ Quick] s [ Reference]<br>’ (inverted fields are modifed by the Object Processor)<br>~SS Bitmap Object<br>TYPE = 0 sgitiigies,<br>Pathe beth her beech bo oo<br>DATA Pointer (Bits 23-3) LINK Pointer (Bits 23-3) HESCHT ypos 28h. [TYPE<br>64 56 48 40 32 24 “PE. B Eo<br>Leer berber beer reebercbeer berber<br>Unused FIRSTPIX INDEX WIDTH SWIDTHE::. EEEEPOS<br>) "<br> RELEASE REFLECT Ee “pred DEPTH<br>TRANSPARENT RMW we OEE<br>Scaled Bititiap Object oo<br>(Third phrase only. Phrases.ohe/and two are ihe'Sarnéias a Bitmap Object)<br>Phere bo eo Soe<br>___ GPU Interrupt Object”<br>64 56 48 nn ne! 16 8 0<br>Lert eer berrbertrerberebrer berber berber berth<br>|, Branch Object<br>64 Shite, 48 a0 ee, 3 2 "¢ ‘ 4<br>Lert rebel eet errberrteer rerbreebeertrerbeerbrecbeeor<br>BEL Unused SEE Link Pointer (Bits 21-3) Unused | CC YPOS TYPE}<br>Es EE Stop Object<br>64 a ee 32 24 16 8 0<br>Pee eo hee Eo oo eee eee<br>DATA TYPE<br>Enable Stop Object Interrupts<br>© 1992-95 Atari Corp. Confidential Information PER Property of Atari Corporation June 7, 1995<br>**----- End of picture text -----**<br>


3 

June 7, 1995 

Page 21 

| | 

7 , \ a \ i i | i q ‘ 

a Jaguar Software Reference Manual - Version 2.4 je Description of Object ProcessorPixelpath The following two diagrams show where the object data path fits into the Tom Chip. All the diagrams that follow are drastically simplified for clarity. 

**==> picture [517 x 599] intentionally omitted <==**

**----- Start of picture text -----**<br>
| : Object Line Pixels, Videos| |<br>: Processor | > | Buffer Generator... Timing “250%<br>—| Interface SE | HERES Beetle<br>Control: Memory : ve Graphiegii3:... . tos<br>)<br>Jaguar Chip Block Diageain,._<br>The processor bus is a 64-bit data, 24-bit address #iujti-master bus. The bis, master can change on a cycle by<br>ig, CYC}e basis with no overhead. The external CPU caniréls this bus when it'ig:the bus master. The 10 bus is a 16<br>Hu = data 16 address bus used for reading and writing to internal: memory and registers. The bus interface logic and<br>memory controller allows transfers offany: WHE.(one to eight bytes) to be made to any width of external<br>memory. The bus interface accommodates 16'ang:32-bit microprocessors: The bus interface also generates a<br>, multiplexed address for dynamic RAMs. The miilfiplexed.address 18:4 function of memory width and number<br>ofcolumns. The memory controllérdaly performs RAS: cveles, when the row address changes. This allows<br>contiguous regions of memory to be 'degessed riiech faster. 8,<br>The line buffer is a bridge between two asynchronous parts of fixe chip. On one side are the processors and<br>[In][ fact][ there] [are][ two][ line][ buffers.][ While]<br>memory. On the other Sidé:are the video timing and [pixel][ genggators.]<br>one is written into by the €)bjéet. Processor, the othé£ is:zead BY the pixel logic. Each line buffer is a small<br>low words.<br>360x32 RAM with independentwrite strobes for thehighand<br>Each location in the liné buffer may cantain one 24-bit pixel or two 16-bit pixe's.<br>oo ; oo Object Data ; ‘<br>. Address “Object >| Write back Path ‘ Re<br>Data<br>Object Processor Biock Diagram<br>© 1992-95 Atari Corp. Confidential Information “JER Property ofAtari Corporation June 7, 7, 1995<br>**----- End of picture text -----**<br>


' 

June 7, 7, 1995 

' a = | ‘ 4 

1 j j j 1 : { ' 

The Object Processor reads object headers and image data and writes back modified headers. The write back logic normally increases the data address by the data width. If the object is scaled then the data address is increased by a multiple of the data width and the vertical remainder is modified. The object data contains either physical colours in the case of 16 and 24 bits-per-pixel objects or logical colours in the case of 1,2,4 and 8 bits-per-pixel objects. Logical colours are translated into physical colours by the colour look up table or CLUT. ee HERI SHEE Deeata ,|: Latch Multiplexers CLUT i Latch Line ERE, fa pBaffer The Object Processor fetches data one phrase at 4 tiie until the immape data, for that header, is exhausted or until the line buffer address (X co-ordinate) has béé@me invalid. The[befiaviour][ of][ the][object][data][ path] depends on the colour resolution of the object (bits=peespixel) and on whetheethe object is scaled. In 24 bits-per-pixel mode each phrase contains two pixels (16:bits unused per piiase). The multiplexers select each in turn and one 24-bit pixel is weittes anio: the, line buifer:pet:clock cycle; The CLUT is bypassed for 24 In 16 bits-per-pixel mode each phrase contains four pivele! The multiplexers select two pixels at a time and two pixels are written into the line buffereach clegk cycle. The GLUT is bypassed for 16 bits-per-pixel objects. TE whi OE In 1, 2,4 and 8 bits-per-pixel modes each phrase contains 64, 32, 16 and 8 pixels respectively. The multiplexerstop bits from select the top two bits pixelsiat of tbe: patettea time. offset In 1. 2 (a and field 4bit,Hritiemodes:obyet tae header). pixel is The made two up eight to eight bit values bits by are taking used the as addresses to a pair of identical CLUTs yielding two sixteen bit physical pixels which are written into the line buffer every cycle. 3" Oe If an object is, scaled the Object Processor deais.swith one pixel at a time not pairs. Scaling is achieved by incrementing the line: buffer address independeritty:af-the counter controlling the multiplexer. For instance if the line buffer address igincremented twice as ofteii'as the counter then the image will be twice as wide. There aré:tWo line buffers A'& BeWhile A is written by the Object Processor B is being read by the pixel logic. At the:start of the next display tine the buffers swap over So A is displayed and B is written. This swap[all][ the][ signals][ attached][to][ the][ line][ buffers.] is effectively ‘achieved by multiplexéts[On] 

**==> picture [3 x 34] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


i" © 1992-95 Atari Corp. 

Confidential Information JER Property ofAtari Corporation 

June7,1995 

| 

. 

4 

ee = Jaguar Software Reference Manual - Version 2.4 Page 23 Mi =The above description is complicated by the following: ° : oe If a pair of pixels must be written to an odd location in the line buffer they must be swapped and one a pixel delayed. 4 . The line buffer address decrements if the object is reflected. | j . The colour to be written into the line buffer can be added to the previgiis Valéinstead. : ° One colour may be used as transparent and is not written into the ike buffer. OEE ee | : . The line buffers also appear as memory to the rest of the system. es, OE ; The pixel data path is shown in the following diagram. All the logic in this bax Fins from a different ¢idck to s the previous logic, this is the video clock. . EEE He ‘ ne Latch | 2:1 muxa CRY to ol com ao RGB In 24 bits-per-pixel mode the line buffer is read.it the vided clock frequency. The line buffer data is simply latched and presented at the pins as réd: green aid blue data bits: In CRY mode the line buffer is read at half the video clock frequency. Each read yields two 16-bit CRY values. These are multipiéXedinto the CRY to RGB:conversign:logic during succeeding video clock cycles. In this logic the more sign#figaitt.cight bits specify‘the: <eloui avid the less significant bits specify the intensity or brightness. The colour yalué'is:uged as an index to threé’ROMs. These ROMs contain the relative amounts of red, green and blue faréach cofour/Fhe outputs of the ROMs are multiplied by the brightness to get a final eight bits of red, green and blue. “HEE In RGB1 G.dfidde thetine buffer is read at half the wideo clock frequency. Each read yields two 16-bit RGB values. Bits0-5 formaihe six most significant bits‘of green, bits 6-10 form the five most significant bits of blue andbits 11-15 formthe: five most significant bits of red. All other bits are set to zero. In all these jnodes a small amoitiit of additional logic sets the output colour to black during blanking and to the border eglgur. where appropriate... A fourth mode e381S'to allow the sysi¢in to support very high pixel rates using external multiplexers and | multiplexerDACs. This isis drivencalled: directby the mode.video'clockIyithisdirectly. mode the Thelineoutputbufferofis theread2:1atmux the videois connected clock frequencydirectly toandthethered2:1 - nn. andfrequency.green outputsThis providesof the chip.a videoThis bandwidthallows 16-bitof upvaluesto fourto times be output the videoat twiceclock.the These maximumvalues videoshouldclock be reha synchronised, de-multiplexed and converted to analogue outside the chip. In this mode the blanking and border signals are output on the blue pins. 

: 

**==> picture [1 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. Confidential Information “FER Property ofAtari Corporation 

June 7, 1995 

Page 24 

Jaguar Software Reference Manual - Version 2.4 

i — F j 

The above picture is slightly complicated by the following: 

j = } | | - | 4 1 4 = ! ] | 4 |g . | » aa : 

. . 

| 

- ° The least significant bit in CRY and RGB16 modes can be sacrificed (treated as zero) and used to control an external video switch through the incrust output pin. 

- . In CRY and RGB16 modes a background colour may be written into the line buffer after it has been read. HEHE: 

- . In CRY and RGB16 modes the least significant bit may be used to determine wheitier the mode is CRY or RGB16. This could be used to drop a decompressed RGB pitiure into a CRYBicture without having to do a RGB to CRY conversion. Hees ERE, 

Theare average refresh frequency is defined by the REFRATEbits iit thé:MEMCON2 register: Refiesh-<¥jcles grouped together in order to lessen the impact on system perforsiazice:"However they cannot'bé performed in very large numbers or they would create “dead spots” in whichis processitig. was possible. This could disrupt the display or sound production. TEE WEEE Jaguarrefresh uses a counter to accumulate a count of refresh-cycles.When this counter reachesieight then eight cycles are done and the counter is set to zefQ.7° 22808 i.. WEEE Refresh cycles are also invoked when the Object Processor reaches thésend of the object list. After the Object Processor executes a STOP object JAGUAR perfatns as many refreshi¢¥cles as are necessary to decrement the refresh counter to zero. an WEEDS, This mechanism guarantees that the minimum refresh rate i8:maintained withdul interrupting the Object Processor and without creating "dead:spots':of tore than afew tpicroseconds.::." 

**==> picture [3 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
q<br>**----- End of picture text -----**<br>


**==> picture [14 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
ae<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. Confidential Information FR Property ofAtari Corporation 

June 7, 1995 

Page 25 

| Jaguar Software Reference Manual - Version 2.4 

’ :: i. : 

if 

) aL ee Jaguar produces a video output using eight digital bits each for red, green and blue. This allows each output to have two hundred and fifty-six intensity levels, and is enough to allow smopth shading from ofie:éelour to another. This twenty-four bit scheme is known as frue-colour. THEE “SHEE Jaguar can produce a display based on true colour pixels stored in memory in long srbxds, with eight bis 2 unused, and this is known as true colour mode. However, these:thizty-two bit pixels ‘aredarge and so consume a lot of memory; and they also consumea iot of memory bandwidilite fetch from RAM ‘far displays True-colour mode is therefore unattractive for general use, as mast fniages do not need its range of colours, and it is desirable to avoid the detrimental effects it has on perfgrmiance. Trug:colour mode is therefore a special case, and when it is used only true-colour images may be displayed. “28855. In normal operation, the Jaguar display system is aged on Siateen-bit pixels. Images iit Riemory may be[four][ or][ eight][ bit][ logical][célours.][ These][ logical] stored either as sixteen bit pixels, or may be stored:[as][ one, twa;] colours are used as indices into a Palette or Colgut-Look-Up-Tabie (LUT). which contains their corresponding sixteen-bit physical colours. cea CHEER if Sixteen-bit pixels may be stored as Six bits of greets; and five bits each forsediand blue, but this no jonger[red][ and] allows smooth shading. There is therefore.an additionaé scheme, known as the[‘CRY][ scheme][ (cyan.] intensity, see below) which still alloys smecosls intensity shadinige-T his CRY¥:s¢heme is now discussed in qecavGuouScheme a | coiivaiud Snatiniy Mequirements’ “ya (2 — The CRY scheme was derived principally to meet the requirements of Gouraud Shading. This is a technique that models the appearance of a lit curved:surface from a set of polygons. The problem the technique helps to overcome is that if the intensity due to afight:squrce is calculated for each polygon and the polygon is painted in that colou#; them'the polygons that make up:{hat:surface are each clearly visible. The technique of Goutaud’shading helps avoid this by calculating the intensity at each vertex, and ther each polygon edge, and hence along each scan line that makes up the display. If linearlyonly whitéafiterpolating fight sources along are cénsidered, then the only variation is one of luminous intensity, and not one of colour. It is:tbesefore attractive to‘have a colour scheme that contains an intensity vector, as the Gouraud shading calcufatioais.have then only {o:be performed for one value, rather than the three values that would have to be calculated3a true colouf scheme. As there is general agreement tiuit eight bits is enough to give smooth intensity shading (and it is a round | 4, number), it was therefore necessary to come up with. a scheme that allowed the colour to be expressed in eight a its. 

© 1992-95 Atari Corp. Confidential Information JER Property ofAtari Corporation 

June 7, 1995 

Page 26 

Jaguar Software Reference Manual - Version 2.4 rrtsr~—~—~«s—C“‘CCSCOC;#COUOC;i«i(;(C«CCz2#z+z+#;C 

§ . 

LL 

| : | : | j 4 | a 4 4 | 4 | 4 | , , _ 1 3 a | a j ’ | 3 ' _ _ i 

i 

**==> picture [483 x 215] intentionally omitted <==**

**----- Start of picture text -----**<br>
The colour space to be modelled may be considered as the RGB WHITE<br>cube shown, where the lowest vertex represents black, and the<br>highest white. The three edges running out from black are the three<br>orthogonal vectors red, green and blue. The sum of these three ahs,<br>vectors can describe any point in the cube. The three lower vertices EE<br>therefore represent fully saturated red, green and blue, and the three Be Reece cree<br>higher ones yellow, cyan and magenta. ees PB,<br>BLUE Me. A GREEN OE gl BED<br>This colour space model is only one of many ways of considering PARQ f A<br>what the human brain ‘sees’, but it has the advantage of modelling:::. Ba, * "A Fee<br>the display system used by colour monitors, and of being WEEE TOR BEARIG ia?<br>mathematically simple. ee “SEU HEE?<br>Physical requirements .——rrt~tr—.._—=«iz ECiCCSC«sCi«sCséC(‘éséréel<br>**----- End of picture text -----**<br>


The intensity vector can be considered as that component cf thé:sum of the red, green ané blue vectors thai lies along the diagonal of the RGB cube from blak[to][white.] “FH#S s8:not the ‘true! intensity, which is 2 weighted sum of red, green, and blue; but it bearS:é linear relationShig:tesit when the colour is not changed. It is necessary to come up with a scheme to encodé'4hé.colour value in the Semaining eight bits of the pixel. The following requirements were made on this schemieiiis.. ate 1. All two hundred and fifty-sixs#auss sBould represent valid, and diffeest, colours. 2. The colours should be well: spread outaérégs the colour space 222 2" 3. Colours should be able to be snixed by lingatly averaging their colour values. 4. An intensity value of zero muistbe black!” Ee As the remaining colour.space without intensity 1s two-dimensional, two vectors are required to represent a point in it. Ans, theta schepie was discarded as it would not meetitequirement two, and so a scheme based on two x, y vectors was choses... + HEE HEEB To meet requirement one’ the two'¥esiors must describe a point on a square area. As no existing colour space model is square when viéWed along the:inlensity axis, it was necessary to come up with a new one. The approach:chasen, after considerable expetitientation, was to take the view along the intensity axis of the RGBcube; which issbexagon, and distort it inté#:Square. This does not quite meet requirement 3, but is 

**==> picture [4 x 27] intentionally omitted <==**

**----- Start of picture text -----**<br>
]<br>**----- End of picture text -----**<br>


i( 

© 1992-95 Atari Corp. 

Confidential Information “JER Property ofAtari Corporation 

June 7, 1995 

Jaguar Software Reference Manual - Version 2.4 

Page 27 . 

; | : : : ' i 4 : : i i | | | il ; 

The colour mapping scheme chosen is based on defining 256 points on the upper surface of the RGB cube. 

In the figure shown, the hexagon GREEN ee corresponds to a view looking down onto Eo evan en GREEN vELLOW the RGB cube. This hexagon is distorted eee Gace onto a square, whose X and Y co-ordinates Seow eee are four-bit values. This defines 256 colour TEE ee { warns | levels. The choice of green as the primary Te OEE[ed] colour that lies on the middle of one face . lees eae eee was made after observing the effects of the | gue HEEB. 4 | oan fue three possible mappings, and corresponds Henge BS a ee with the expected result, as the human eye AOR EES siue oa a AED is least able to distinguish shades of green. MAGENTA WHEE y Note that in each of the three areas defined en on the hexagon and square, one of red, eee EE green or blue is at full intensity, and the others vary At the gentte. (white) they are all at'£ul intensity. The intensity scale for any given colour lies along the:fine between biick:-and the point on the top surface of the cube defined in the colour table. HEED OED _, Colours may be averaged by taking the average of tiigiz.eight-bit intensity: value, and each of the four-bit X ee) and Y components of the colour value. This will not pitédiive exactly the saffe'colour as the point midway between them in the RGB cube, but.willbe Chose to it. “2 ae, Ene This is a summary of the pros andtons of theCRY scheme: OEE Boe Advantages of CRY cm Pees : ¢ Smooth intensity shading from ‘T6sbit pixels” — ¢ Better matched to the capabilities of the human eye than 51655 bit RGB schemes [ * Suitable for efficiefifiGouraud shading . Ge ' Disadvantages Ee Be ee j « Steps are visible in'gtooth charige€iof saturation or hue + Translation from RGB to CRYis teestéaightforward } RGBIOCRY Conversion = | | The best technique is to calculate the intensity value, which is the largest of red, green and blue; and from this the ideal ROM eatry for that colour;[By][ scaling][ the][ RGB][ values][ by][ 255][/][ intensity.][ This can][ then][ be][ matched] to the actual ROM tables to find the'i€arest match. A quick way of doing this is by a lookup table. It is not necessary for this tohavie..2* entries;if turns out that taking the top 5 bits of each of the red, green and blue values (rounding where:appropriate}‘and using a 32768 element lookup table is adequate. 

4 

© 1992-95 Atari Corp. 

Confidential Information JPR Property ofAtari Corporation 

June 7, 1995 

HHS : ' : : s a. g g 4 Pl = & ' | | _ | 4 | 4 3 4 , 4 fr 4 _ a ; ] | { ; 1jj | q q a June7,1995 § 

**==> picture [590 x 733] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|Jaguar Software Reference Manual -|Version 2.4|i|
|mamPage 28|eos|si: a|
|The eight-bit colour value|is used to index a look-up table of modifier values for each of red green and blue;|
|which is multiplied by the intensity value te give the output level for each drive to the display. The look-up|
|tables|are:|
|C0|ge|@e..|©|
|REE|34.34a|«34|«34|34|34|34|34|34.|34|34|34|« SREEBEPORG eee tA0|
|62|68|68|68|68|68|68|68|68|68|68|68|G#i°43|2. EEE,|
|230|Olen Is.|
|192|102|102|102|102|102|102|102|102|102|102|95|Te|[47]|
|535|235|135|135|135|135|235|2135|235|23°|130|104|7HES2|26|0 MERE.|
|169|169|169|169|169.169|169|169|169|170|141|113|858886|28|0°|eae|
|0|HEE|
|563|203|293|203|203|203|253|205|503|183|153|122|91 Bee.|[20]|
|537|237|237|237|237|237|237|237|530197|164|132|98|GHuEs2.|0|HE|
|555|255|255|255|255|255|255|255|247|214|162,148|115|62|Hig|7|HHS|:|
|555|255|255|255|255|255|255|255|225|235|2682273|143|112|STepsei.|fee|'|
|555|255|255|255|265|255|285|255|25°|255|227498270|142|113|“BB|aneee|:|
|171|145|T19|HEE|:|
|955|255|255|255|255|255|255|255|955|255|24982285087)|
|955|255|255|255|255|255|255|255|955|255|2556968|BeeEe00|177|153|s|
|955|255|255|255|255|255|255|255|255|255|298/255|257N2Se..208|187|a.|
|355|255|255|255|255|255|255|255|255|255|255|255|255|2553240|221|g|
|555|255|255|255|255|255|255|255|253.2859|255|255|255|255°|2552255|g|
|GREEN|0|«17)«34|«SE|EB|8S|102|115|P86 ES88RO|187|204|22)|2 382255.|
|6|19|38|5S?|77|96|215|13 GEES 4|1795492211|231|250|255|285|4|
|255|255|255|z55|Pl|
|9|21|43|64|86|107|129|1588472|193|2152286,|
|6|23|47|Pi|95|119|142|1662490|214|238|8859855|255|255|255|=|
|255|255|255|&|
|6|26|52|78|164|2130|156|1638288|234|255|25358285,|
|5|26|56|85|123|142|270|199|#3165255|255|255|PH5255|255|255|'|
|D|30)|BL|GL|122|253|183|214|248,855,255|255|2580855|255|255|
|0|32|65|98|132|164,G1REeSo|255|HSSE255|258|2558255|255|255|
|»|35|6S|G8|132|168|52|[PS,][ 255]|[255]|[2582][ 5%]|[2][ B5EBES]|[255]|[255]|
|||||
|D|390|61|91|122|£83"|283|BHB244|255|FRG|285:|25122 55|255|255|_|
|5|28|56|85|113|Be2|ive|19852 26..255|255°|eshERSS|255|255|255|
|G ORE|2582255|255|255|255|255|255|||4|
|55|26293|5247|7871|16495|Pig@ed42RG|256|182216G28S0|2EEH236|255|255|255|255|255|||4|
|23€|255|255|255|255|3|
|5|21|43|64|86|10%EtZ9|“862172|193 QRS.|
|6.19|«38|67|77|96|225|134|154|£73|V6RE211|231|250|255|255|4|
|0|i?|34|aSi|€8|€5|192|229|736|153|1965187|204|221|238|255|,|
|RISE|255|255|255 72§8:.255|255|252|255|255|255|285° 255|255|255|255|255|fr|
|955|255|255|285.865|255|255|255|Pesne55e29|255|255|255|240|221|_|
|||
|'|355|255|255|28beegRUeSS|255|255|PRBEBSS|TSS|255|252|220|208|18)|a|
|755|255|255|BHP 2558|258.255|259|555|255|255|248|224|200|177|153|
|255|255|255|285|255|2882885255|255|255|249|223|197|2171|145|119|;|
|255|255|255|255|255|2e5'ReRH255|255|255|227|198|170|141|113|65|
|255|235|204|173|143|112|81|Si|]|
|2552531 25H255.25525H.295255|255259|259355|2552582985BeSoR47|214|181|1468|115|82|49|17|||
|2898237|280231|237|537|237|237830|197|164|131|9|65|32|3|{|
|253|203|203°2G%:203|503|202|203|203|183|153|122|9!|62|30|9|;|
|£BS|169|169|166:469..169|169|169|169|170|141|113|35|56|26|0|
|Bahia35|135|135|138935|135|135|735|135|136|104|78|52|26|9|
|10202|102|102|1627282102|102|102|102|102|95|7i|47|23|0|1jj|
|||68|68.68|68|68|“BH€8|EF|6s|68:|«(068|«O68:«CO64|«C43:|21|||
|34|SGea4,|34|34|fae|[24]|34|34|34|34|34|34|34|19|G|
|GO|0600|HGH|OO|eo|8|oC|0|5|6|0|GC|6|&|q|
|q|
|a|
|i|
|||
|ii|©|1992.95 Atari Corp.|Confidential Information JPR Property ofAtari Corporation|June7,1995|§|

**----- End of picture text -----**<br>


: Jaguar Software Reference Manual - Version 2.4 ’ Graphics Processor Subsystem 

Page 29 

: 

| | i 

| 

| 

## Graphics Processor Subsystem 

**==> picture [1 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
,<br>**----- End of picture text -----**<br>


**==> picture [507 x 530] intentionally omitted <==**

**----- Start of picture text -----**<br>
The Graphics Subsystem of Jaguar is a self-contained processing unit, whose view of the external system<br>processor and memory are controlled by a separate memory controller, which.is:1i0# art, the graphics system.<br>| The graphics subsystem transfers data to or from external memory by becoming the masigy S£the co-<br>| processor bus. This bus has a 64-bit (phrase) data path, and a 24-bit address; with byte resofution:cThis bus<br>| has multiple masters, and ownership of it is gained by a bus request/acknowlédge system, which 'ls:prioritised,<br>| i.e. ownership can be lost during a request (but not during a memory cycle). FHegraphics subsysten¥clually<br>| contains two bus masters, the Graphics Processor and the Blitter. OPER “HE<br>‘ The graphics subsystem also acts as a slave on the IO bus. Thisbiig.normally has a 16-bit Gata path, and!<br>f allows external processors to access memory and registers within:the Braphics subsystem. As:the data path<br>| within the graphics subsystem is 32-bit, all reads and writes must be [pales,] sees<br>j The memory within the Graphics Subsystem appears to be part‘of the general séiehine address space, both to<br>j the GPU and Blitter, and to external processors. The advantage to the GPU of havinglocal memory is both<br>that it is faster, and that it does not require ownershipi'd? tHe:system bus to be accessédi%,..<br>This diagram shows the architecture and data paths of the graphics'gubsystem: Oe<br>16/32-bit data 10 Bus. [75 Pe<br>Bus Slave Transfers CPU aédess to GPU oo<br>ocd GPU Bus Controller  .<br>aaa _ | 32-bit-diita Local BUS :<br>Dual-port 32-bitier._| Paces eeeeececes Blitter |<br>Register File al; ice cece Registers<br>paca _ a . GPU Gateway<br>8 — to main bus<br>| Eo ' 64-bit data Coprocessor bus<br>ONEEE DG be nee Bus Master Transfers<br>**----- End of picture text -----**<br>


a ©1992-95 Atari Corp. Confidential Information FER Property ofAtari Corporation June 7, 1995 

Jaguar Software Reference Manual - Version 2.4 

Page 30 

j 

| 

| ' 2 & ; = § , = fog 

: | | i: | 

bo 

: 

a -_ June 7, 1995 1 

| | 

si 

|TheGraphics sub-systemaddressspacecontains thefollowinglocations:<br>-FonIO GRLAGS___——[RW<br>TGPUflags<br>SN<br>ee ee||||||
|---|---|---|---|---|---|
|rFO2I0c[GEND. |WGPUbig/ littleendian:<ontrol HEE<br>Pee rR<br>PRW__[ GPO operation contol ites a —<br>FO211C |G_DIVCTRL<br>|W<br>|GPUdivisionmethod<br>CHEE<br>ea||||||
|Ai_CLIP<br>Ww<br>BlitterAlchippingsize...<br>rrO220C[ALPIXEL. RW BlitterAlpixelpointer “228...<br>'F02210 _|Al_STEP<br>|W<br>Blitter.Al step<br>io||||||||
||F0221C<br>FALING.<br>LW<br>BitterAlpixel'peisiterincrement<br>Fro220 [ALFING<br>«LW<br>liver Adpixel pointer incrementfraction|||||||
|F02234 |A2_STEP<br>"CTW<br>BIB<br>AQstep<br>|FO223C |BLCOUNT<br>“Ww<br>| Blitterloopieaunters<br>£02240<br>Blitter source data|||||<br>|||
|F02258<br>| B.SRCZ1 22:7228e.|W<br>Blitter sourceZdata 1||||||
|02270 ztBING:<br>iW<br>ce|:Blitterintensityincrement|||||||
|roe [BsTOP gCTW<br>Blittercollisionstopcontro}<br>Blitterintensity register3||||||
|F02284<br>Blitterintensity register<br>|<br>rro2ss jBI<br>EW<br>Blitterintensity register0||||||
|B_ZO<br>W<br>BlitterZregister0<br>=03000[GRAM<br>RW___[LocalRAMbase||||||



© 1992-95 Atari Corp. Confidential Information “JER Property ofAtari Corporation 

Jaguar Software Reference Manual - Version 2.4 

Page 31 

| 

| 

he i These locations may be accessed by all processors except the GPU for read or write as appropriate at the | i above addresses, where they appear to the system as 16-bit memory. As they are all actually 32-bits, transfers 7 should always be performed in pairs, in the order low address then high address. 

In addition, for high-speed write operations by 32-bit or 64-bit bus masters (especially for blit transfers), they may be written to as 32-bit locations at an offset of plus 8000 hex from the addresses above. They are not readable at these addresses. eee 

The GPU addresses them all directly as 32-bit locations in 32-bit internal faemory, and they are not accessibie to the GPU at the plus 8000 hex offset. ee OHEEEEn 

a ©1992-95 Atari Corp. Confidential Information oR Property ofAtari Corporation June 7, 1995 

Page 33 

| 

| 

, 

. 

. : : : i 

**==> picture [206 x 21] intentionally omitted <==**

**----- Start of picture text -----**<br>
Jaguar Software Reference Manual - Version 2.4<br>**----- End of picture text -----**<br>


**==> picture [529 x 52] intentionally omitted <==**

**----- Start of picture text -----**<br>
| GraphicsProcessom##§<br>This section describes the Jaguar Graphics Processor (GPU).<br>**----- End of picture text -----**<br>


**==> picture [475 x 337] intentionally omitted <==**

**----- Start of picture text -----**<br>
WalieeGphesProcesso?<br>The Graphics Processor (called here the GPU - Graphics Processor Unit) is 4 simpie, very fast, mieeds, :<br>processor. It is intended for performing the functions associated with generating Sraphics, such as thse.<br>dimensional modelling, shading, fast animation, and unpacking compressed images =... Hee<br>The graphics processor corresponds to the accepted notion of ‘& RISC Processor (Reduced tiistraction Set<br>Computer). This means that: Ee SEES<br>° most instructions execute in one tick fe OEE<br>° all computational instructions involve registers OEP COHERERE<br>° memory transfers are performed by load/store. instructions OPEEEE<br>. snstructions are of a simple fixed format,.withfew addressing modes “HERE<br>. there is a wealth of registers, and local.fiigh-speed tnenioty... WHE<br>It has several features to give high computational pawers, including: &s,<br>° ‘Highly pipe-tined architecture _ a<br>° one instruction per tick peak.tHroughput OE EES<br>- internal program and dataRAM' oa |<br>. register score-boarding #27 SHEE WHEE EEE<br>° ALU includes barrel shifter:and parallel stiultiplier!:: 5.<br>. systolic matrix multiplication” - ees<br>. fast hardware divide unit eae<br>. high-speed intégrupt response, including video object #iterrupts<br>**----- End of picture text -----**<br>


oe Co j The GPU.is progtammed in the same way‘a8 abyeather micro-processor. It has a full instruction set with a broad rangeofarithmetic:instructions, including add, subtract, multiply and divide; Boolean instructions, and | bit-wis€ 3nstructions. Ithas:@:range of instructions for loading and storing values in memory, with either 7 register:indirect, register indirect plus register offset, or register indirect plus immediate offset addressing modes. It148:jump relative and'absolute instructions, both of which may be made dependent on combinations of the zero, carry:and negative flags.'There are also some more specialist instructions suited to computing matrix multipliés;‘atid.some useful aids to floating-point calculations. The GPU is a full 32-bitpideessotin that all internal data paths are 32-bits wide, and all arithmetic instructions (except multipty}:perform 32-bit computations. The instructions are 16-bits wide. {&@ TheIt also GPU has has 1K sixty-four of local high-speed internal 32-bit 32-bit general RAM, purpose which is registers, where its of instruwhi **c** tionsh thirty-t and **wo** are visiblerking data **a** tre o **n** eormally time. stored. It also has access to external memory via the 64-bit co-processor bus, and can perform byte, word, long-word and phrase data transfers on this bus. It can also execute its instructions from external RAM. © 1992-95 Atari Corp. Confidential InformationTER Property ofAtari Corporation June 7, 1995 

**==> picture [2 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


June 7, 1995 

. | CG 

as ' : | : S j i | ' 4 ] 4 | @ ; 4 : 

| a | b ' 2 | & 

| 

Page 34 Jaguar Software Reference Manual - Version 2.4 Desgnphiosopty— cr The GPU is a RISC processor, normally executing one instruction per tick, and therefore capable of very high instruction throughput. The RISC versus CISC debate is a complex one, and will not be discussed here. The RISC approach was chosen for the GPU principally because it occupies less silicon.[—] The RISC approach leads to a processor design without micro-code, effectively the instrixition set is the micro-code, and most instructions execute in one tick. The advantage is thatinstructions‘a @xecuted quicker, but the disadvantage is that some operations require more instructions to execute. eee The GPU is also intended to perform rapid floating-point arithmetic. It has nd fisating-point instructigas.as such, but has some specific simple instructions that allow a limited precision floating-point library to be: capable of in excess of 1 MegaFlop. “eee “BEBE Eg HES The GPU is intended to be programmed in assembly language, ait HOt in a compiled languageias the'tisks it is intended to perform are simple repetitive operations, best writteHin assembly language. OEE 

The GPU design makes extensive use of pipe-liniig:i0 improve its.throughput. This meaits that although the GPU can achieve a peak rate of one instruction per tick, each instructionis actually executed over several ticks, but only spends one tick at each pipe-line Stage. It is important'to: understand this as it does have some significant consequences on GPU behaviour. HEE erecta For a typical instruction, such as ADD, the pipe-line stages:are: a 

**==> picture [475 x 94] intentionally omitted <==**

**----- Start of picture text -----**<br>
2 read operands frou segisters OES “eee, OAC<br>4 write result back to register ee ee<br>In addition to these stages;.apre-fetch unit attempts to maintain’ small queue of unexecuted instructions, to<br>keep the instruction executiog-unit busy. i hte<br>**----- End of picture text -----**<br>


i 

| 

© 1992-95 AtariCorp. 

Confidential Information “PO® Property of Atari Corporation 

June7,1995 

Jaguar Software Reference Manual - Version 2.4 ¢. w Register Score-Boarding =«—«— 

Page 35 

| | q { 

| q1 

{ & | 

j 

— an instruction would read a register that is still in the process of being computed by the ALU. 7 an instruction would perform a conditional jump, or add or subtract with carry, before the flags have WN been set as the result of some arithmetic operation. i — an instruction would read a register that is being read from internal memory. 

The main side effect of the pipe-lined nature of GPU operation is the interaction of instructions at different stages of the pipe-line. They may affect the same operand, or the same piece of the hardware, and so a conflict can potentially arise. 

**==> picture [6 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
-<br>**----- End of picture text -----**<br>


**==> picture [556 x 305] intentionally omitted <==**

**----- Start of picture text -----**<br>
1 - Read Operands RAM a ae |<br>For instance, if the instruction after an ADD was'a second ADD of andthekvalue to the same register; then if<br>L.aa w oldthe two value ins( t heructions value were from just to before follow the first eachADD). other Fortunately,through the pipe-line,theGPU hardWate tén:the second detects this ADD erroneous would use the<br>condition and suspends execution untill the correct value is #éady..Clock cycles that occur during these hold-<br>The fiseve shows the alate Slow assacintasvenir dhe gpvemBeus au auitiuenc iusiruciion. THe wick Ones<br>correspond to a pipe-line stage, so thaf:when an:instructionis:atthe Read Operands stage, the previous<br>;<br>4 instruction is at the Compute Result stage, and the one beforé'that at the Write Back Result stage.<br>**----- End of picture text -----**<br>


4 

1. The RAM used within ‘the GPU for its registers has‘only two data ports, so if the instruction at stage three has to write:back to adifféient register from the two registers being read by the instruction at stage one, then a clash occurs. “HEE Es. 

2. The instruction at stage one of the pipedling:may need to read a value being computed by the ‘Stageinstructionthree. attagé-two,OEE but this value will'not be available until the instruction at stage two reaches 

The GPU: operates what is knowH aéa score-board to help the programmer avoid a whole class of these problems. This fags registers that wilf/alter once some operation has been completed, and will force program flow to wait if'aninstruction reads atagged register. This mechanism also applies to the flags, and will wait 

, 

j 

© 1992-95 Atari Corp. 

Confidential Information “7@® Property of Atari Corporation 

June 7, 1995 

, 

i | n 

Page 36 Jaguar Software Reference Manual - Version 2.4 — anrelatively instructionslow, wouldthis can read cause a register thata significant is thedelay. target of a divide operation - as the divide unit is ’1.. 1 q2 —_ an instruction would read froma register that is waiting to be ioaded from slow external memory 5 (which takes a variable amount of time). q ee |,r,rmrtrtrt~CSCOCiCO;COCOCOCCitiCéiéC(C(itéiétCiés . The score-board unit also controls the writing back of computed values. The tegisters are a bakk Gf:dual-port : RAM, so it is not possible to read two register values simultaneously while Waiting to a third. OEE 4 If the register to be written back to is being read by the instruction currently at stage. of the pipe-line; GF if ’ one of the operands of that instruction does not involve a register,read, then the writé-backwill be concealed. | Otherwise, the instruction will be held up one cycle while the caitipisted value is written backi::.... fe 4 The score-board unit controls all operations that involve writing td fegisions,, and will also genefate await : Be state if the instruction that would have executed reads two registezs, neither: Of which is the target of the write. = Write-back data sources are: wee OEE - _ the result of an ALU computation _ seine... EEE 7 —_ the result of a divide operation (this occuig in parallel witty the ALU) HE . the data from an internal load operation’ OEE i y — the data from an external load operation “fos. OH e If two of these are to be written back simultaneously, execufion is always heid:ap for a tick. One technique that can be used to help avoid ait states from the’ score-board unit is to interleave two sets of calculations, i.e. ensure that conseciztive instructiags do not use the Sasiie:stegisters, but that instructions two BS cc Lmhm”rm™mr™mrm™~—~™”.CrC;sCO;C;OCO®#CNCCO(tét(iwizs | Pipe-lining also affects the éxecution of jump instru¢tions. The'tiinsfer of control does not occur until the instruction after the jump dustruction has been execiited:‘Phas ¢an be confusing, but helps to increase the ; overall instruction throughput.The safest technique is tofollow all jump instructions with a NOP (null 4 operation), but it is quite reasonable'te place almost any other instruction here - but see the notes below on ; program control flow. OEE Memoryinetinet The Graphi¢s Graphi¢s Processor is intended'to operate in parallel with the other processing elements in the Jaguar is intended'to operate in parallel with the other processing elements in the Jaguar intended'to operate in parallel with the other processing elements in the Jaguar operate in parallel with the other processing elements in the Jaguar in parallel with the other processing elements in the Jaguar with the other processing elements in the Jaguar the other processing elements in the Jaguar other processing elements in the Jaguar processing elements in the Jaguar in the Jaguar the Jaguar Jaguar system. In Grdet:to do this, In Grdet:to do this, Grdet:to do this, do this, this, a well-behaved GPU program should only make occasional use of the main well-behaved GPU program should only make occasional use of the main GPU program should only make occasional use of the main program should only make occasional use of the main should only make occasional use of the main only make occasional use of the main make occasional use of the main occasional use of the main use of the main of the main the main main ( memory bus. TiGPU therefore hasfour Kilobytes of local memory, organised as 1K locations of thirty-twoGPU therefore hasfour Kilobytes of local memory, organised as 1K locations of thirty-two therefore hasfour Kilobytes of local memory, organised as 1K locations of thirty-two hasfour Kilobytes of local memory, organised as 1K locations of thirty-two Kilobytes of local memory, organised as 1K locations of thirty-two local memory, organised as 1K locations of thirty-two memory, organised as 1K locations of thirty-two organised as 1K locations of thirty-two as 1K locations of thirty-two 1K locations of thirty-two locations of thirty-two of thirty-two thirty-two ; This memory memory is intended intended to be Sed for both program and data. both program and data. program and data. and data. data. It can be cycled at the graphics processor can be cycled at the graphics processor be cycled at the graphics processor cycled at the graphics processor at the graphics processor the graphics processor graphics processor processor j 

Memoryinetinet The Graphi¢s Graphi¢s Processor is intended'to operate in parallel with the other processing elements in the Jaguar is intended'to operate in parallel with the other processing elements in the Jaguar intended'to operate in parallel with the other processing elements in the Jaguar operate in parallel with the other processing elements in the Jaguar in parallel with the other processing elements in the Jaguar with the other processing elements in the Jaguar the other processing elements in the Jaguar other processing elements in the Jaguar processing elements in the Jaguar in the Jaguar the Jaguar Jaguar system. In Grdet:to do this, In Grdet:to do this, Grdet:to do this, do this, this, a well-behaved GPU program should only make occasional use of the main well-behaved GPU program should only make occasional use of the main GPU program should only make occasional use of the main program should only make occasional use of the main should only make occasional use of the main only make occasional use of the main make occasional use of the main occasional use of the main use of the main of the main the main main memory bus. TiGPU therefore hasfour Kilobytes of local memory, organised as 1K locations of thirty-twoGPU therefore hasfour Kilobytes of local memory, organised as 1K locations of thirty-two therefore hasfour Kilobytes of local memory, organised as 1K locations of thirty-two hasfour Kilobytes of local memory, organised as 1K locations of thirty-two Kilobytes of local memory, organised as 1K locations of thirty-two local memory, organised as 1K locations of thirty-two memory, organised as 1K locations of thirty-two organised as 1K locations of thirty-two as 1K locations of thirty-two 1K locations of thirty-two locations of thirty-two of thirty-two thirty-two This memory memory is intended intended to be Sed for both program and data. both program and data. program and data. and data. data. It can be cycled at the graphics processor can be cycled at the graphics processor be cycled at the graphics processor cycled at the graphics processor at the graphics processor the graphics processor graphics processor processor clock rate, and so is extremely fast. It may be viewed as a simple cache RAM, with software cache control - this technique is known as visible caching. When the graphics processor is executing code out of internal RAM, program fetch cycles will occupy less than half the RAM bandwidth. To load up a program into the RAM within the GPU, the best technique is to use the blitter. Set it to blit phrases, and use the 32-bit GPU address range (see below). 

© 1992-95 Atari Corp. Confidential Information “JPR Property ofAtari Corporation 

June 7, 1995 

Page 37 

| 

yy 

| ) 

7 j : 

## Jaguar Software Reference Manual - Version 2.4 

**==> picture [513 x 304] intentionally omitted <==**

**----- Start of picture text -----**<br>
wv To the GPU programmer the local RAM, local hardware registers, and external memory all appear in the<br>same address space. The GPU memory controller determines whether a transfer is local or external, and<br>generates the appropriate cycle. The only programming difference is that only 32-bit transfers are possible<br>within the GPU local address space, whereas 8, 16, 32 or 64-bit transfers are permitted externally.<br>The local RAM sits on an internal GPU 32-bit bus. Also present on this bus are. various GPU control registers,<br>and the Blitter control registers. When a GPU transfer occurs outside the logit address Space, a gateway<br>connects the local busto the main bus. If a sixty-four bit transfer is requested, a special:register is used for the<br>other half of the data. ees OEE<br>The address space is organised as follows: A Ss<br>F02000 - FO21FF Graphics processor control registers OE ce<br>F02200 - F022FF Blitter registers fs, THEE EES<br>This local address space is also available to external devices via the yo mechisiisdin.,<br>The GPU local bus can therefore perform transfers :{6#three.quite separate mechatifsitis:These are, in<br>— Instruction fetch oo OCEEEEE<br>**----- End of picture text -----**<br>


## BxiemialView ofGPUSpase 

The GPU internal address space is accessible by anytither Jaguarbus imaster, i.e. the CPU, the Blitter and the 4 DSP car al! aanus GPLLintamnal Sate This is nant of the Jaguar I/O space within Tom. This is normally g viewed as 16-bit read/write memory:but by adding 8000 hex'i¢:the addresses it is also available as 32-bit a write only memory, which is faster to access for a bus master ‘hich can perform 32-bit transfers. Specifically, i | this allows the blitter t@:¢epy data into the GPU space more rapidly than it would using the 16-bit space — for 4 maximum transfer speed:1sse:the blitter in phrase mode, writitig to the 32-bit address range. Please note that g the 68000 in the Jaguar @érisoie taay not address this'$2:bit'wide memory. $F Transfers to/from addrésses within the'Yange SFO2000-SFO7FFF and $F1A000-SF1FO00 are executed 32 bits | at a time using a latch mechanism and must ibe handled carefully by external processors. When a 16-bit word : is read fromthe:GPUat a longword-alignéd address, a 32-bit read is performed. The high word is transferred j and the ow  word-3§ Jatehed. Any 16-bit read operation at a GPU longword-aligned address + $2 simply | transfersthe latched data... When a 16-bit word is written (6'a longword-aligned address, the data is latched. When a 16-bit word is written to: Jéngword-aligned address + $2, 32-bits (the written word and latch) are transferred. The GPWane Data Ordering Conventions The GPU can operate in both a big-endian and little-endian environment, and as long as the memory interface ’ ie is programmed to the correct endian mode, and the transfer requested is the width of the operand required, y then this operation is largely invisible to the programmer. The GPU is itself either-endian - this means that the first instruction of the pair in a long-word is programmable. This is controlled by the BIG_INST bit. - 

] 

## © 1992-95 Atari Corp. 

**==> picture [2 x 21] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


Confidential Information TER Property ofAtari Corporation 

June 7, 1995 

ions 

| | 7 | | : 1 j | 

, 

The GPU has a set of load and store instructions, each of which take two register operands. One register is used to provide the address, the other is either read to supply data to be stored or is written with load data. Load and stores may be performed at byte, word, long-word and phrase width. Bytes.and words are aligned with bit 0, and when loaded the rest of the register is set to zero. When phrasés ars read Of:written, a register within the GPU local address space should already contain the other long-waitd for store Operations, or is loaded with the other long-word for load operations. Performing phrase load$iand stores is the:fastestway of transferring blocks. com WEEE Load and store operations may also be performed using one of two simple indexed addressing schemes: “these are both based on using either R14 or R15 as a base register, with either a five bit ‘unsigned offset (in long: words) encoded into one of the register fields or another registeE:¢Ontaining the offset: THEI s.a two tek: overhead involved in using these instructions, as the address has t@ cofputed. OE In local memory, only long-word reads and writes are permitted. 9 Load and store operations will normally complete in one tick, ortwo ticks for indeed, addresses. The transfer may not be complete at this point, and if another load.or.store operation occurs befté'tlie previous one has unit;“ Which is described completed it will be held up. Load data is written under the control of the score-board elsewhere. ee ce The gateway between the GPU local bus and the:external co-processof biis contains a control block for generating external memory transfers. When this bidtk.is idle, load and stgz¢:operations complete as quickly as they would in local memory. For load operations, #&:data is not loaded inta:the target register, however, until the external transfer has taken place:"The score-board taechanism prevetizs:use of this data before it has been loaded, but other computationmaytake place. If there is andther load gestore instruction in the program before the gateway has completed its:transfer, then[it][ will][ be][ held'tip][until][ the"gateway][is][ idle.] 

Due to a bug in the Jaguar Console, DMA transfers are tot permitted. 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


The GPU:gontains a powerful ALU section, which'as well as the normal arithmetic and Boolean functions, all with 32-bit'word size, coniains:a perform their respective functionsin16 by one 16 tick. fast parallel multiplier, and a 32-bit barrel shifter, both of which The GPU alsa Gontains a divide unit: ‘This performs serial division at the rate of two bits per tick, on 32-bit unsigned operands;;producing a 32-bit quotient. The operation of this runs in parallel with normal GPU operation. Es Le 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
J<br>**----- End of picture text -----**<br>


| | | 

i © 1992-95 Atari Corp. 

Confidential Information FPR Property ofAtari Corporation 

June 7, 1995 

. 

Jaguar Software Reference Manual - Version 2.4 

Page 39 

| : | | | | | | | | | | | | 

**==> picture [551 x 352] intentionally omitted <==**

**----- Start of picture text -----**<br>
@,. @ The ALU has the following set of flags:<br>Z zerTo set appropriately by all arithmetic operations, normally being set if the result of<br>| the operation was zero.<br>N negative set appropriately by all arithmetic operations, normally being set if the result of<br>the operation was negative (bit 31 is a one). cuttin.<br>C carry set according to carry or borrow out of all add andsubtragtoperations; set with the<br>| bit that is shifted out of shift and rotate operatigng'for shift by:aneydeft undefined<br>by other arithmetic operations. i HEGRE |<br>interrupts, ccc lc<br>The GPU can be interrupted by five sources. Interrupts force a call to'an address in local RAM aven by<br>sixteen times the interrupt number (in bytes), from the base of RAM: Etig'the responsibility ofthe”<br>programmer to preserve the registers and flags of the underlying:¢ode. Primary.register 31 is the interrupt<br>stack pointer. Primary register 30 is corrupted when instructifl o wn is transferied:tothe interrupt service<br>routine. Neither register should be used for any other.purpose when interrupts aré‘enabiled.<br>Interrupts are allocated as follows: Se WEEE<br># Interrupt<br>Object Processor: “HEE<br>& lw<br>° [1 (iseryinterpt<br>| 0 = €PU intertape: fa<br>**----- End of picture text -----**<br>


The flags register contains individual jiiterruptienables for cath of these sources, as well as a master interrupt mask for all interrupts. When the master interrupt mask is set,te:primary register bank is selected (see When an interrupt occurs; thé’master interrupt mask Bit-is set: The individual enables are not affected, but no other interrupts will be serviced itil the mask bit iscleared:The interrupt service routine should normally clear the master interrupt tHask, aid the.appropriate interrupt latch, and enable higher priority interrupts The value pushes onto the R31 stack is the addiéss of the last instruction to be executed before the interrupt occurred;‘The 'interrupt'service routine should thegéfore add two to this value before using it to return from the The interrupt latches may be readin the status port, and are cleared by writing a one to their clear bits, writing ° The cause ofthe Interrupt may be determined by the location jumped to, but not from the flags register, as more than one interriipf Jatch bit may:be set. There is a certain degree of interruptprioritization, in that if two interrupts arrive within a few ticks of each other, the higher numbered will be serviced first. Beyond this, interrupt prioritization is under software 5X wi control, as described above. The only operations that are atomic are single instructions, or certain instruction combinations (see below). Interrupts may be disabled by clearing all the enable bits. It is therefore not practical for the interrupt stack to be shared with the underlying code, unless all interrupts are masked across stack operations. 

© 1992-95 Atari Corp. Confidential Information FER Property of[Atari][ Corporation] 

June 7, 1995 

i 

Jaguar Software Reference Manual - Version 2.4 

_ PageAn example 40 interrupt service routine, which does no more than clear the interrupt, is shown below. The 

i < 

- |4 j | 4 _ 7 | ‘ q ; _ =. . | 3 y ' 1 : j | | : j : | | 1 41 4 

interrupt source was interrupt 2. int_serv: movei #G_ FLAGS, 130 ; point R30 at flags register load (r30),r29 ; get fiags belr #3,r29 ; clear IMASK etc bset #11,2729 ; and interrupt 2 latehgseiiin. load (r31),r28 3; get last instruction addease: ss... addq ‘#2,r28 ; point at next to:be' executeg@iign, _ addq #4,r31 ; updating the stagkpointer eset store 129, (r30) ; restore flags co OHH Similar interrupt service routines can handle all the interrupts. Note the followins points about this code _ Registers R28 and R29 may not be used by the underlyinig:code as they are corrupied. (you may choose to use any two registers in bank #0), in addition ta[R30-and][ R31][ which][ aré’always:sGrnipted] by the interrupt process itself. Note: R30 is automatically: sorupied. when an interrupt occurs not just - py the interrupt service code as shown. Pca EEE — Interrupts are re-enabled on the instruction after the jump. If they were enabled any sooner then no other interrupt service routine would be able:te ise: R.28 and R29, as they could:potentially corrupt If the interrupt source was the Object Processoi; thenthe interrupt gervice routine should read the Object Code registers, if required, and then re-start the Object Processor by wifizig[to][ the][ Object][ Processor][ Flag] 

**==> picture [1 x 30] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


- meee eee It is necessary for certain operations to be atomi¢, #.¢;3iteerupts may iiot occur during these operations. Three GPU instruction types temporarily #eek.out intertupts ‘while they complete their operation. These are: — Immediate data moves, using the MOVE! instruction. ‘Iiiterrupts are locked out while the two words of immediate data are fetched. Feey 

- — Matrix multiply @perations, using the MMUES.instniction. Interrupts are locked out until the operation has completed:=. EEE 

- —_ Multiply and accumulate operations, using the IMULTN and IMACN instructions. The result register is not preserved by interrupts, #ad'therefore any multiply/accumulate operation must consist of a sequenve-of IMULTN and IMACN instructions followed by a RESMAC instruction, with no intervening iastructions. The IMULTN'aad IMACN instructions are always atomic with the 

- Jgueceeding instruction. See the section below on multiply/accumulate instructions. 

- —_ “Juimp instructions arealways atomic with the instruction which succeeds them. 

- | mS La Program control normally euaeupwards through memory executing instructions sequentially. The GPU can also transfer program flow by performing jump instructions. Two types of jump are supported, relative and absolute. Jump relative takes a signed five-bit offset, which is treated as an offset in words, and added to the program counter. Jump absolute transfers the contents of a register into the program counter. 

- ' © 1992-95 Atari Corp. ConfidentialInformation “JPR Property ofAtari Corporation June7,1995 

June7,1995 

, oe | j 

| . 

1 

[ Jaguar Software Reference Manual - Version 24 Page 41 if i Both types of jump may be conditional on the contents of the ALU flags. If the appropriate condition is not © met, then the jump instruction is ignored and program flow continues with the next instruction after the jump. The instruction after a jump is always executed. This is a side-effect of the pre-fetch queue. Programmers ; may choose either to place a NOP after every jump instruction, or may take advantage of this to place a useful ? instruction after the jump which will be executed whichever branch is followed... | The program counter may also be copied into a register. oP ee 7 The GPU can cease operation by clearing the GPUGO bit in the GPU contol register (desepbed: below). It j may-iuen only be restarted by an external write to this register, or by a resgh.. EEE | ‘SiigleStep Operation ] As an aid to the debugging of GPU programs, the GPU can be sét td'single step through pragilins;:Bausing : between instructions until restarted. This operation is controlled by:and:external CPU as follows?!" ; 1. Set up the program counter, then set the GPUGO and SINGLE_STEP xontrol bits in the control ‘ register. OE f -2,._-—-Poll for the SINGLE_STOP flag in the staus register.- at this point the first iustiaction has been 3. Set the SINGLE_GO bit in the control tegister (keeping GPUGO and SINGLE_STEP set). 4. Poll for the SINGLE. STOP flag being sé#(his is the read versionOf the SINGLE_STEP flag), which oe indicates that the next instruction has been executed. “HEE | If the GPU register file is to be réad from or written to, then singlé-steppine will have to be suspended and an appropriate transfer routine run, Wikich will require:that the:GPUGO bit must be cleared first and the program j counter modified. Unfortunately, cleating theGPUGObit has the effect of altering the value in the program counter, as the pre-fetch queue is disearded. Therefore, after'st¢p4 above, the following operations should be performed: “se ee — read the program gounter value fie oP | — clear the GPUGO contol bit “EEE — read or write t6:thie register filé‘as required | —_ add two.tothe program counter Valié’read | It is necessary to add tW6'té the program counter, as the value read reflects the last instruction executed (or last word ‘Gfimmediate data ifjt'was MOVE]. illegal Inctrudtion Gombingfions ° Do not place a MOVELiistriction after a jump, as the jump will take effect before the data is fetched, and so will change where the immediate data is fetched from. é ° Do not place two jump instructions sequentially, the results are not predictable, and may not be relied 

: 

- ° Do not place a MOVE PC to register instruction immediately after a jump, the value read can not be relied upon. . 

- ° Do not follow an IMULTN instruction by anything other than another than an IMACN instruction. 

ve © 1992-95 Atari Corp. Confidential Information FRProperty ofAtari Corporation June 7, 1995 

| 

| 1<q i, . Y ' | 1 , 7 4 4 } % | a og , 8 Po 1 ] | 4 ] ; | 

| 

| | 

| 

Page 42 Jaguar Software Reference Manual - Version 2.4 ° Do not follow an IMACN instruction by anything other than another than another IMACN instruction or a RESMAC instruction (see below). . Do not precede an MMULT instruction by a LOAD or STORE instruction. a rt—t—C‘(‘(CiCO##W#N#COWC#C(‘t«é«C«dd Conditional jumps encode from a five bit flag field. This is: ee —— Bit Condition | 0 _| Zero tlag must be clear for jump to occur. HEE WHERE Zero flag must be set for jump to occur. TEE CHEER Flag selected by bit 4 must be clear for jump to occur. EE TEE ; This gives useful jumps as follows (other codes are either jump always Or jistip never, and are reserved for future modifications) “EE OEE 

**==> picture [374 x 252] intentionally omitted <==**

**----- Start of picture text -----**<br>
)<br>Code # Condition Description<br>Sy<br>00100 Jump if carry fiag is,clear EE<br>00101 NC NZ Jump if carry flag's§:¢¥ear and zero flag is clear<br>g1000 | 8 |C__| Jump'iFcatsy Magis set<br>01001 | 9 {CNZ | Fiump if carry ffag is set and zero: tap is clear<br>01010 Jutap if carry flag ib Set dd zero flag is set<br>10101 NN NZ Junipif negative flag is cleataiid zero flag is clear<br>10110 NN Z::.. Jump if negative flag is clear:and zero flag is set<br>11001 Jump if negativeflag $s'set and zero flag is clear<br>11010 ‘Jump if negative flag isset and zero flag is set<br>Tae eae<br>**----- End of picture text -----**<br>


## Multiply and Aceufucceinstuctons 

The GPU supports multiply and aceiimulate (MAC) operations. These involve multiplying two values together, and:ddding their product té thesum of the products of some previous multiply operations. These are typically used formatrix multiply and digital filtering type applications. Due to the pipe-lined natuié-of the design, the multiply and its associated add do not take place in the same cycle. MAC instructionsaré not: therefore like other instructions, in that a special instruction is needed to write back their result. 

I 

© 1992-95 Atari Corp. 

Confidential Information 7, 0 WN Property ofAtari Corporation 

June 7, 1995 

w s 

: Jaguar Software Reference Manual-Version24 ge ' wv Take as an example multiplying R8 times R9, R10 times R11, R12 time R13, and placing the sum of their pS products in R2. All values are signed. The instructions are as follows: ' imultn r8,xr9 ; compute the first product, into the result z imacn r10,ril ; second product, added to first 1 imacn r12,r13 ; third product, accumulated in result ; resmac x2 ; sum of products is writtenshO..r2 MAC instructions may only be followed by further MAC instructions or by the RESMAC: instruction. No ' other cumbinations are permitted. eee eee ee Systolic Matrix Multiplies : The GPU contains a mechanism GPU contains a mechanism contains a mechanism a mechanism mechanism for performing integer performing integer integer matrix miultiplies at a burstate a burstateate O£the maximul 

: The GPU contains a mechanism GPU contains a mechanism contains a mechanism a mechanism mechanism for performing integer performing integer integer matrix miultiplies at a burstate a burstateate O£the maximul obtainable from the hardware multiplier, which is one multiply per:fick. This is generally sigefuls-but has been designed in particular for the matrix multiplies required by the Diserete Cosine Transform algorithm. One technique for this involves performing two 8x8 integer matrix rpultiplies'in Sixecession on a matrix, using the ; same fixed coefficients, but rotated for the second multiply.“ Meee The GPU therefore has a MMULT instruction, which:initiatesasequence of betwee fiiree and fifteen multiply/accumulate instructions, as described abigve, Corréspanding to one product ter##:of the result matrix. One of the source matrices is held in the secondaey register bank,the. other in local RAM. The matrix held in registers is packed, i.e. two elements per registet:This allows all Of an Sight-by-eight matrix to be stored in i the secondary register bank, and is the raison d‘élte-of the second bariki2%:, WFwo = Awhich matrixis always multiplyin is the initiated secondary by the regisiet MMULTbank, instrustiGit:-Thiscontainingthe-first takes as two eleniénts its $G1srce of parameter the matrix the row. register,Its destination parameter is the register,in the currently selected fegister. bank, i which to write the result. The matrix held in RAM may be accessed in either increasing row or itcreasing column order, in other words the data for each successive multiply:operation,aré eithierone!location or the matrix width apart. Like interrupts, the systolic operation is perfornied by forcing internally generated instructions into the instruction stream. The. first instruction is IMULTN, the middi¢:anes IMACN, and the last RESMAC. These have their operands médifiedin the manner described above!" The MMULT instruction shouid:aot be preceded bya LOAD or STORE instruction. 

## Mmm 

The divide iinit perforttis unsigned division, taking'as operands 32-bit divisor and dividend, giving a 32-bit quotientand a 32-bit remainder. The quotient is the result of the divide instruction, and replaces the dividend in the destination register. Divides are performed at the rate of two bits per tick, so that the complete divide operation:completes in sixteen t¢kS:,The divide instruction has no effect on the flags. If another instruction attempts to read the quotient or start another divide operation while the divide unit is active, then wait states.will be inserted:until the divide unit has completed. The remainder register may beiéad after the divide has completed, this value in this register may either be positive, in which case it coiitaitisthe actual remainder, or negative, in which case it contains the remainder minus the divisor. Divides may also be performed on unsigned 16.16 bit values, by setting the offset control flag in the divide control register. The quotient is then also an unsigned 16.16 bit value. 

rn © 1992-95 Atari Corp. Confidential Information TR Property ofAtari Corporation June 7, 1995 

Saar Senenieenena 

_ os 

{[—] 

‘Page 44 en 

Jaguar Software Reference Manual - Version 2.4 

aq 1 a 4 & a . ] 2. ; = 1 { : , 4 | OF a a s _ ; ) Po 

‘ ] , : j 

The GPU contains a register file of sixty-four thirty-two bit registers. All of them may be used as general purpose registers, although some are also assigned special functions. All instructions contain two five-bit register operand fields, although they are not always used as such. Where an instruction referencesa register, this five-bit field is turned into the registeriaddress: There are two banks of these 32-bit registers,.primary and secondary. The primary register bank, bank 0, isdiWavSiused for interrupt service. This is forced by the IMASK bit, when it is set selection of:bank 0 is forced:HE IMASK is clear REGPAGE is obeyed. THEE ce Bank select bits are provided in the flags register, and special MOVE instructions low data to be moved, 

Roma The GPU internal address space is accessible to an external bus taster at any'timié’s.external access having data into the local the highest priority on the GPU local bus. This means that the Blitter may be used'td:ddad The local address space is accessible for read orwwrite at the addresses given elsewhere in this document, and these locations are presented as sixteen bit mem@ry;.which must always:be accessed as long words in the order low address then high address. HE WHEE To allow faster transfers into the GPU space, all the repistérs are also available as thirty-two bit memory, at an offset of 8000 hex from their normakadditsses. At this:addtess, the internal:‘taemory is write only. The 68000 may not access this memory as if transters data 16-bitsatatime, gee If the Blitter is being used to writeinto the GPU space,:then phrase wide transfers may be performed, as the bus control mechanism will automatically divide Bese Up'4¢ suit the width of the memory being addressed. 

ne Ls ae The pack and unpack instyHictidis provide a means far avsfaging up to 32 CRY pixels. The unpack operation leaves the intensity value: uachasged;:shifts the lower colournibble up 5 bits, and the higher colour nibble up 10 bits. The pack operatiée reverses hiss. 

**==> picture [421 x 77] intentionally omitted <==**

**----- Start of picture text -----**<br>
oo UE, pack<br>Colour fisid 4 ee! Colour field 2 intensity field<br>**----- End of picture text -----**<br>


Register containing unpacked pixel There are five unused bits above each field in an unpacked pixel, allowing up to 32 unpacked pixels to be added together. If a power of two unpacked pixel values are added, then a shift can be used to re-align them prior to packing the average value. 

© 1992-95 Atari Corp. Confidential Information “JER Property ofAtari Corporation 

June 7, 1995 

. : | 

| 

: 

b: r iy JaguarThe bits. Software Referencethat do not contain Manual. packed - or Version 2.4 unpacked pixel. data are always set to zero. This is useful for anti-aliasing and scaling effects. 

## Page 45 

This section describes the internal registers of the Graphics processor. Nofe that soitie:Gf these are read or write only. ‘ HEE EEE , All GPU registers are 32-bit, and will require all 32 bits to be written. — 

**==> picture [553 x 484] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||
|---|---|---|---|---|---|---|---|---|
|This register provides status and control bit for several important|GPU-functions. Control|bits|aig|
|Bits|Equate(s)|Description|_|
|ZERO_FLAG|The ALU zero flag, set if thé:tesult of thé'd#st:arithmetic|operation was|
|1|zero.|Certain|arithmetic instructions do not affectthe|flags,|see above.|
|CARRY_FLAG|The ALU carry: flag,|S8F|Or.cleared by carry/borroW|Gtit-of the|
|definedadder/subtraet,and|reflects|ca#ry|out of|some shift operations, but it is not|
|2|after:|other|arithmetic|'apésations.|
|NEGA_FLAG|The ALU negative flag, set if the'Fésizlt.of the last arithmetic operation|
|was|negative.|ih.|Es|
|||wv|3|IMASK|Interrupt|mask,|set|b¥:the|interrupt contrdl:logic at the start of the service|
|a|ToHtHG, aiid. is cleared:by: the interrupt service routine writing a 0. Writing|||
|4-8|42to|this ‘Iocition has noéff6edi..|
||GCPUENA|‘einterrupt|enable|bits.for|interrupts:0:4:|The status of these|bits is|
|G_PITENAG_JERENA|{overridden|byIMASK:Themeaning of these bits are:|
|G_OPENA|‘8.€PU Inti,|
|1|Jerry|Interrupt|7,|
|G_BLITENA:.|2|Timing Generator|2?|
|9-13|G_CPUCLRffeP"|UE Interrupt latch clear bits. These bits are used to clear the interrupt latches,|
|G_JERCLR#"|“which-may be read from the status register.|Writing a zero to any of these|
|G_PITCLR|bits|}eaves.it|unchanged,|and|the read value is always zero.|
|JL|GBLIFCER|We|
|14|28 EREGPAGE|2s,|[|Switches from register bank 0 to register bank|1. This function|is|
|ae|“eleeesd|overridden by the IMASK flag, which forces register bank 0 to be used.|
|This|bit must not be set due to a bug in|the Jaguar Console.|||

**----- End of picture text -----**<br>


**==> picture [2 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
.<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. 

Confidential Information 7O® Property ofAtari Corporation 

June 7, 1995 

Page 46 

Jaguar Software Reference Manual - Version 2.4 

i 

| 

j j [ 1 { ' ] ] 

| 

> WARNING- writing a value to the flag bits and making use of those flag bits in the following instruction - will not work properly due to pipe-lining effects. If it is necessary to use flags set by a STORE instruction, then ensure that at least two other instructions lie between the STORE and the flags dependent instruction. If . it is necessary to use flags set by an indexed STORE instruction, then ensure that at least four other instructions lie between the STORE and the flags dependent instruction. 

| 

**==> picture [495 x 381] intentionally omitted <==**

**----- Start of picture text -----**<br>
Gone” oo nauconor Register Foz Mieonly<br>This register controls the function of the MMULLT instruction. Control bits:36;, _ -<br>Bits Equate(s) Description<br>4 |MATCOL When set, this control bit maké:the matrix held in'tHenibry. [be][ accessed:]<br>ema Adare Register FOze | Wrteonly<br>This register determines where, in local RAM, the.giiatrix teléin| memory is. WHEE)<br>Bits Equate(s) Description<br>eePMatixadcresy<br>GiEND YateOraanigaueniRebisted /Fa2I0G Iwate only<br>This register controls the physical jayout of pixel data and GPU 1G registers. Tf its current contents are<br>unknown, the same data should be#Eitten to boththe‘low:dad high 16-bits.<br>Bit Equate(s) Description<br>BIG_IO When this bit is set, 32-bit registers in the CPU I/O space are big-endian,<br>oon. i.e. the more significant 16-bits:appear at the lower address.<br>1 | BIG_PIX “222228. | When this bit is sefthe pixel Organisation is big-endian. See the discussion<br>EEEEEEEES elsewhere in this document:<br>BIG INST <7  “fe¥Bea this bit is set the order of word program fetches is big-endian.<br>**----- End of picture text -----**<br>


Gipe gi/i@PU ProgramCounigi 7 Foatio” Read/Write The GPU program counter inigy-be written whenever the GPU is idle (GPUGO is clear). This is normally used by the CPU:to govern where progzam execution will start when the GPUGO bit is set. The GPU program counter may be read at any time, and will give the address of the instruction currently being executed:If the.GPU reads it, this. must be performed by the MOVE PC,Rn instruction, and not by performing a load from? tz... Gee The GPU program counter takisk always be written to before setting the GPUGO control bit. When the GPUGO bit is cleared, the program counter value will be corrupted, as at this point the pre-fetch queue is discarded. 

© 1992-95 Atari Corp. Confidential Information “7O® Property of Atari Corporation 

1 

June7,1995 

| 

| | 

. | 

| 

## Jaguar Software Reference Manual - Version 2.4 y ic. crau = CPU ContorStatus Register "> Fo2tT4 

## Readiris 

## Page 47 

This register governs the interface between the CPU and the GPU. 

**==> picture [564 x 653] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||
|---|---|---|---|---|---|---|---|---|
|Bits|Equate(s)|Description|
|GPUGO|This bit stops and starts the GPU. The|CPU or.GPU|may write to this|
|register at any time. The status of this bitditer|a|system,|reset may be|
|externally|configured.|Pecee|
|1|CPUINT|Writing a 1 to this bit causes the GPU iginterrupt the CPU.|There|is no|
|need for any acknowledge,|and no need'té'¢lear the bit to zerd,|Writing|a|
|zero has no effect.|A value of zero is always tead.|LE|
|2|FORCEINTO|Writing a 1 to this bit causes a GPUinterrupt|fype:0,|There|is no néed-for|
|any acknowledge, and no n¢éd.to clear the bit tozero:Writing a|zetg|has|
|no effect.|A value of zero is|always|read.|Baraat|
|[This][means]|that|
|3.|||SINGLE_STEP|When this bit is set GPU singke-stepping|[is][ enabled.]|
|[until]|[a][ SINGLE_GO]|
|program execution will pauséafter|each|[instruction,]|
|command is issued.|TEE|CEE|
|The read status ofthis|flag, SINGLE_STOP,|‘itidi¢ates whether the GPU|
|has actually stepped,|and’should|be polled before #siing|a further single|
|step commasid.'A one‘néans|the GPU is awaiting a|SENGLE_GO|
|4|SINGLE_GO|Writing a one:t6:this bit advances|propram|execution by one instruction|
|when executio#'is|paused|in single-step|tiode.|Neither writing to this bit|
|;|
|HOE|writing a Zero, will|have|any effect. Zero is always|
|7|w|at anyother|time,|
|eebils|indicate which interrupt request|
|‘The|status ofthese|
|6-10||G_CPULAT|‘| faterrupt latches.|
|and|the appropriate|bit should be cleared by the|
|G_JERLAT|‘:fatch|is currentivactive;|
|G_PITLAT|‘ioletrupt seewice routine;|sing the INT_CLR bits in the flags register.|
|G_OPLAT|Writing to these bits has naeffect. The meaning of these bits|are:|||
|GBLITLAT;,||0|CPU|Interrupt.|ES|
|||"ey|[1|Semy Interrupt.|,|
|Ee|OTB, Object Processor|
|eee|[ae|Bitter|
|ii||BUS_HOG|
|ao|'Ehis bit should not be set in the Jaguar Console.|
|12-15||VERSION22000|These bits allow the GPU version code to be read. Current version codes|
|EO|are:|
|“SEEET|Pre-production|test silicon|
|.|
|Ly|w|2FutureFirstva p|r|iantsoductionof the release GPU may_|contain|additional|features|or|
|enhancements,|and this value allows software to remain compatible with|
|all versions.|It is intended that future versions will be a superset of this|;|
|GPU.|
|||
|© 1992-95|Atari Corp.|Confidential Information|“JER Property ofAtari Corporation|June 7, 1995|

**----- End of picture text -----**<br>


eee 

~ 

eee oe 4 

~ ee 

f aa 

| : 

_ Page 48 

Jaguar Software Reference Manual - Version 2.4 

: . 4 % 

/ 

Po { = 

This 32-bit register provides the high part of GPU phrase reads and writes. It is physically a single register, and therefore a phrase read followed by a phrase write will write back the same high data unless this register 

GOREMAINE DIide Unitremainder: > Foatie Readeny This 32-bit register contains a value from which the remainder after a division maybe calculated. Referin the 

> GuveTREDieeunCoRIRIC Wma Bit Equate(s) Description DIV_OFFSET If this bit is set, thenthe divide unit performs division of unsigned 16.16 bit numbers, othegWasé 32-hit unsigned integer divisiar:is performed. 

i 

© 1992-95 Atari Corp. 

Confidential Information “JER Property of Atari Corporation 

June7,1995 | 

Jaguar Software Reference Manual - Version 2.4 

Page 49 

**==> picture [159 x 23] intentionally omitted <==**

**----- Start of picture text -----**<br>
r een<br>**----- End of picture text -----**<br>


This section describes the Jaguar Blitter. | io Blitter is an abbreviation for bit block processor. It purpose is to process,‘by filling or copying, biscks of bits or pixels. These blocks may be one contiguous piece, or they may be sub-blocks(such as rectangles}:within a The Blitter may also be seen as a hardware engine designed for painting and moving pixelsias quickly a8 possible - it performs a variety of graphics operations at a rate ligited:largely by the memory. access speed. It is used as an aid to the GPU, allowing a GPU program to process: high-Jevel graphics operations, whilst the Blitter, in parallel, performs the low-level repetitive pixel-by-pixel operatiGAgs 2: andgradients associated witk:e.polygon, while the For example, the GPU might calculate the co-ordinates Blitter draws the strips of pixels. Alternatively, the GPU:[might][be][processing][ text][ with][attributes,][ and] computing font addresses and window positions;:while the’Blitter:paints the characters. The Blitter can perform a variety of operations i blocks of memibey; including: + simple memory copies _ _— iy ° = Copies and fills of rectangles within windows OSE HG *_ Tine-drawing a Ee coal EP ~ | imageraionandsang | li ¢ single-scans of polygons fills’ &, a “ “ a + Gouraud shading + Z-buffering ee The Blitter can operate on 1; 24, 8 16 or 32 bit packed'pixels, with considerable flexibility with regard to the The tour de force of the Blitter is its ability. to generate Gouraud shaded polygons, using Z-buffering, in sixteen bit pixel mode. A lot of the logi¢'i#i:thie Blitter is devoted to its ability to create these pixels four at a time, and:fa: intensity write tem at a rate limited only'by the. bus bandwidth, using the GPU to calculate the Z and generate[realistic] gradients animatéd and start and[312.] eraphics. stop pixels on atine-by-line basis. This will give the system the ability to ee ee ee The Blitter is programmed by settitig up a description of the required operation in its registers. These are accessible in the systemtaémorymap, and so may be set by the GPU or by an external processor. The registers control the three functional blocks that make up the Blitter, the address generator, data path, and . w control logic. Each of these is described in the sections that follow. The descriptions that follow give a fairly dry account of how the Blitter works. These are useful for reference, but for an introduction to how to use the Blitter use the examples further on. 

: . 

© 1992-95 Atari Corp. Confidential Information JER Property of Atari Corporation 

June 7, 1995 

| ' : | 

i | , 

7 

| | 

' j a | 4 P 4 4 = q ] q j 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


**==> picture [506 x 684] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 50 Jaguar Software Reference Manual - Version 2.4<br>The Blitter architecture is summarised in the Figure below:<br>Graphics Processor Data Bus ComparatorAddress<br>Address _jeakefe Address<br>Registers pra s:Génerator<br>State Machines i eee WHEE<br>feria. _<br>: “EEtband<br>Data PGEEEEE eae Co-processor<br>Co-processor Data In . SHEE Outpat<br>Feo Intensity or Z ae<br>oe oa<br>The address generator generates an address withita window of pixels. A window is a packed array of pixels<br>_ in memory,and may weil béthe data associated with an Object Processor object. A window is described by<br>its base address and width. A:pointer into this window is set up for the Blitter start position, and is<br>programmiéd:interms of its X aid: ¥address. The ability to program the address generator in pixel address<br>terms considerably,simplifies the task [of][ preparing][ Blitter]  commands.<br>In addition to these registers, various other registers contain specific values to allow considerable flexibility in<br>how the pointers are moditied during Blitter operations.<br>The Blitter has two address‘generation units, used for the source and destination addresses of copy operations,<br>etc. The two address generators are called Al and A2. A1 is normally the destination address register and A2<br>the source, although these roles may be reversed. Al is more sophisticated in its address generation<br>capabilities than A2.<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. 

; 

**==> picture [23 x 296] intentionally omitted <==**

**----- Start of picture text -----**<br>
'<br>|<br>a<br>4<br>q<br>'<br>**----- End of picture text -----**<br>


Confidential Information FER Property ofAtari Corporation 

June 7, 1995 

w " 

**==> picture [579 x 448] intentionally omitted <==**

**----- Start of picture text -----**<br>
Jaguar Software Reference Manual - Version 24 Page 51<br>" M® The address register block looks like this:<br>"9 ALBASE F02200 Al base address<br>Al_FLAGS F02204 Al control flags<br>Al_CLIP F02208 Al clipping size cents.<br>AlPIXEL | F0220C Al pixel pointer ee |<br>Al_STEP F02210 Ai step integer part ce os<br>| Al FSTEP | F02214 Al step fractional part 7 7 :<br>Al_FPIXEL | F02218 AY pixel pointer fraction TE 3<br>Al_INC F0221C Al increment integer parties... TE Be 7 ae<br>Ai_FINC F02220 Al increment fractional part, —<br>A2 BASE | F02224. | A2 base address i<br>- OE<br>A2_FLAGS | F02228 A2 control flags<br>AdPIXEL | F02230 ADpixelpoiter "<br>AD STEP | F02234 A2 step integer,part ee<br>All notions of address within the Blitter correspond with the concept SEs window. A window is a rectangle of<br>pixels, stored in memory as a lineaf'array of packed phrases. A window is described by a base register, and<br>has a width and height, both in pixéis-A set of flagsdescripethe size of those pixels, their physical layout in<br>memory, and various aspects of how'the pointet'is updated. “2:8,<br>The address itself is generated from a pixel pointer. This has an X and Y value, and again is in pixels. The<br>pointer may point to areas:outside the window, and:Al supports ‘hardware clipping of addresses outside the<br>**----- End of picture text -----**<br>


The X and'® paintéts are sixteen bit values. Hawever, the address generation mechanism will only generate valid addresses for¥: values in the range 0-4095' ‘i.e. it treats Y values as 12-bit unsigned values. The higher order bitsof Y are ignored,Kis treated as an unsigned 16-bit value, but only values from 0-32767 are valid in The address generator derives the window width from a very simple six-bit floating-point format. The width value has a fourbitunsigned exponéat, and a three bit mantissa, whose top bit is implicit, and which has the point after the impiicittop bit. This:is similar to a cut down version of the IEEE single precision format without the sign bit. It‘mustgive whole number of phrases in the current pixel size. Valid exponent values areintherangeO-11. 0 For example, a window width of 640 is 1010000000 binary, i.e. 1.01 x 2“9. Therefore the mantissa takes the value 01 (implicit top bit), and the exponent 1001. The width is therefore 1001 01 in binary. Note that there is a window bounds clipping mechanism for the A1 pointer, which treats the X and Y as signed sixteen bit values. This is described elsewhere. 

: 

I ©1992-95 Atari Corp. Confidential Information PER Property ofAtari Corporation June 7, 1995 

Jaguar Software Reference Manual - Version 2.4 nl 

: . & 4 4 i : : 4 q , | 4 : f 4 , 4 q | 4 | 4 q ’ : ; : q : 4 | 

, ; 

——Page 52 

; Both Blitter address generators can update their pointers so that they describe a raster scan over a rectangle. Along a scan line, the pointer may be updated either by one pixel or to the next phrase boundary, depending on how the Blitter is currently operating. Refer to the Data Path section for further details. At the end of a scan line, the pointer is updated by a step value, which is the distance tn:X and Y to the start of by the Blitter's the next scan line. This action of scan across the block, then step to the next start, ‘isconolied snner and outer control loops, the inner loop traversing a scan line, and the'duiter loop adding the:step value. Thus the inner loop length is the block width, and the outer loop length the!bieck height. PEE, In addition to these modes, both address registers have certain special modes:? Ss. TE tHe geinter, so that the A2 may have a Boolean mask applied to its pointer. This is logically ANDedwith pointers may not exceed the bounds of a rectangle, whose sides atta power of two pix Joag. This is:ee? intended to repeat a source texture or pattern over a larger destinaiion azea, €.8- filling a wail with @sepeated Al supports address updates based on a Digital Differential Andilyzer. This techivique produces successive address by adding an increment to the pointers, both of which have integer andfrastiGnal parts, and is used in particular for line-drawing and rotating images. ee cee The pointer and increment of Al, in both X and.¥, have sixtees bitinteger parts and sixteen bit fractional parts. The step value used on the outer loop addgess update also hasisteger and fractional parts. a ___[—] Z The Blitter has a sixty-four bit datapath, with 4 variety ofregisteriedt-can be used to process entire phrases at : once, or one pixel at a time. Pixelsimay the one, two, four, eight, sixteen OF thirty-two bits wide, and are always stored in a packed manner! 25. Ee Data registers are: cE ae Oe B_SRCD F02240 Source data, or computed intensity fractional parts PBSRCZ1 | F02258" ‘Sense Z1, or computed Z integer parts B_SRCZ2 [02260 Source22, Gr. computed Z fractional parts BPAID ° FOR26B:.. Pattern data,or computed intensity integer parts BING| F02274 | increment When writing or copying pixels, arbitrary alignment of the source and destination data is allowed, and the Blitter aligns the source to mateh fhe destination data when required. When transferring phrases the source and destination address pointers do not need to be aligned to the same point in a phrase, the Blitter will automatically align the source to the destination, but only for pixels of eight . bits or larger. If two source phrases must be read before a destination phrase can be written, then the ‘ SRCENX flag must be set to ensure that enough source data is fetched for the blit to operate correctly. © 1992-95 Atari Corp. Confidential Information JER Property ofAtari Corporation June7,1995 

| a e “ i ] 

| 

| 

| 

| 

Jaguar Software Reference Manual - Version 2.4 Page 53 There are therefore two source data registers, to provide current source and previous source for alignment. There is also a destination data register, which can be logically combined with the source, and is also used to restore the destination data area when only parts of it are updated. There is a parallel mechanism for Z data, used for Z-buffering. This allows the depth of the data about to be written to be compared with the depth of the data already present on the Screen, and the write of the new data inhibited if the data already present has a higher priority. This applies to Sixtesia bit fixe] mode only. There are therefore two source Z registers and a destination Z register. pee _— 

- ¢ the logic function unit _ s “HEE ue * computed Gouraud shaded data He _ The default is the LFU output. The ADDDSEL flag selects adder output, PATDSEL Selects the pattern register, and GOURD selects computed data. EE Ee “HEE Write Z may come from Le _ 7 

- Se The GOURZ flag selects computed Z:data. OEEEEE be (EREE Overriding both these selections i§ a mechanism to write back‘uBGhigtiged destination data. If a mode is enabled where data may be inhibited, e.g. bit-to-byte¢xpansion, or Z buffering, then a pre-read of the 

- . destination data should be performed:This also applies to pixel sizes of less than eight bits. 

- | Data Comparators © oes 

- | There are three data comparators available withinthe Bhittér, These are: . The bit comparator. This 1s used for bit to pixel expansion, and selects a bit or group of bits from the source data register, using a counter which is cleared every time the inner loop is entered. The bit is then used to control whether apixelis written at the current location. 

- ° The 2 comparator. This is used in 16-bit pixel mode to compare the 16-bit un-signed integer Z {attribute of apixelion the screen, the destination Z, with that about to be written, the source Z, and to “prevent the write operation if the pixel on the screen has a higher priority. 

- ° The data comparator. This is used to provide a means to make block copies with transparent colours, and #0Help with flood fill byperforming searches. It compares pixel values in either 8 or 16-bit pixel comparemodes. ft normally comparesthe source data register with the pattern data register, but it may also destination data with the pattern data. 

- The comparators may be used £6 achieve three effects: 

   - ° When painting pixels one at a time a Comparator output can be used to inhibit the write of a pixel, leaving the previous value unchanged. 

**==> picture [56 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
June 7, 1995<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. 

Confidential Information FPR Property ofAtari Corporation 

Page 54 

Jaguar Software Reference Manual - Version 2.4 

qq a | 3 q | | Z | } 4 | 4 7 q ; | 4 4 . 4 

. 

| 

| 

° When painting pixels a phrase at a time, the comparator outputs can force destination data to be written back. If this has been previously read then the data will be left unchanged, if not then a background colour can be used, stored in the destination data register ° The action of the Blitter can be stopped altogether. This may be used for collision detection, searching, etc. Note that the bit comparator can only produce a mask to operate over an entire phrase 1n:8-bit pixel mode. 

Businterface The Blitter accesses memory through the 64-bit co-processor bus, and takes full advantage of the width aud high-speed of this bus. The Blitter will normally cycle this bus at a rate limited onty::bythe speed of the #288 external memory, although there is a one-tick overhead when tutziing round from a read4'4 write transfer All external memory is viewed by the Blitter as being phrase wide if the: physical layout is nareawer then the memory controller expands the transfer into the appropriate numberof transfers. The Blitter requests the bus at the start of an operation, and will not stop requesting it, until the entire[granted][the][ bus] operation is complete. As described elsewhere, higher priority bus masters can requést'énd[be] during a Blitter operation, and this will suspend Blitfer operation until the higher priority:epéeration has released the bus. Bae oe “ 

! 

© 1992-95 Atari Corp. 

Confidential Information “JER Property of Atari Corporation 

June7,1995 

} | Jaguar Software Reference Manual - Version 2.4 Page 55 7 ST | ‘ The following is a list of all the externally accessible locations within the Blitter. The data registers may only | be written to while the Blitter is idle. 

Page 55 

' AiBNSE SR Rase Restater! Restater! yr orozz00 || wiitetoniy| , 32-bit register containing a pointer to the base of the window painted to by Al. containing a pointer to the base of the window painted to by Al. a pointer to the base of the window painted to by Al. to the base of the window painted to by Al. the base of the window painted to by Al. base of the window painted to by Al. of the window painted to by Al. the window painted to by Al. window painted to by Al. painted to by Al. to by Al. by Al. Al. This addeess'inust, be be | AcorLagS AT raseResiser ecm RaaaA Wits enly | A set of flags controlling various aspects of the Ad window dnd how addresses are updated: Bits Equate(s) Name Description : 0-1 |PITCH1~4PITCH1~4 | Pitch The distance between sticgessive phrases of pixel data in between sticgessive phrases of pixel data in sticgessive phrases of pixel data in phrases of pixel data in pixel data in data in in the . window data structure. Gaps Gaps igy.be used to provide to provide provide alternate Bee pixel maps maps f6r.double-buffering, for Z data, and for other Z data, and for other data, and for other and for other for other other control a ele information. "The information. "The "The distance betwegii'two successive betwegii'two successive successive phrases of . 2° V/pikeleis given by fwo'o.the given by fwo'o.the by fwo'o.the fwo'o.the power of this value, with of this value, with this value, with value, with with one special | eee casé}'1.¢. apitch of O'trigasis apitch of O'trigasispitch of O'trigasis of O'trigasis O'trigasis pixel data phrases are data phrases are phrases are are contiguous, Be means:1:phrasegaps,gaps, 2 means 3 phrase gaps; but 3 means 3 phrase gaps; but 3 3 phrase gaps; but 3 gaps; but 3 but 3 3 means 2 . ee eeeee phrase: gaps, gaps, Whigh may be especially useful for may be especially useful for be especially useful for especially useful for useful for for double-buffered 

All address registers are 32-bits unless otherwise indicated. a ee AiBNSE SR Rase Restater! Restater! yr orozz00 || wiitetoniy| 32-bit register containing a pointer to the base of the window painted to by Al. containing a pointer to the base of the window painted to by Al. a pointer to the base of the window painted to by Al. to the base of the window painted to by Al. the base of the window painted to by Al. base of the window painted to by Al. of the window painted to by Al. the window painted to by Al. window painted to by Al. painted to by Al. to by Al. by Al. Al. This addeess'inust, be be phrase 

**==> picture [480 x 272] intentionally omitted <==**

**----- Start of picture text -----**<br>
Bits Equate(s) Name Description<br>0-1 |PITCH1~4PITCH1~4 | Pitch The distance between sticgessive phrases of pixel data in between sticgessive phrases of pixel data in sticgessive phrases of pixel data in phrases of pixel data in pixel data in data in in the<br>window data structure. Gaps Gaps igy.be used to provide to provide provide alternate<br>pixel maps maps f6r.double-buffering, for Z data, and for other Z data, and for other data, and for other and for other for other other control<br>ele information. "The information. "The "The distance betwegii'two successive betwegii'two successive successive phrases of<br>2° V/pikeleis given by fwo'o.the given by fwo'o.the by fwo'o.the fwo'o.the power of this value, with of this value, with this value, with value, with with one special<br>eee casé}'1.¢. apitch of O'trigasis apitch of O'trigasispitch of O'trigasis of O'trigasis O'trigasis pixel data phrases are data phrases are phrases are are contiguous, 1<br>Be means:1:phrasegaps,gaps, 2 means 3 phrase gaps; but 3 means 3 phrase gaps; but 3 3 phrase gaps; but 3 gaps; but 3 but 3 3 means 2<br>ee eeeee phrase: gaps, gaps, Whigh may be especially useful for may be especially useful for be especially useful for especially useful for useful for for double-buffered<br>| "=" | 7buffer displays, 48it allows two phrases of pixels to each phrase<br>of Z-buffer data - thére is no need to double buffer the Z data..<br>“i.<br>3-5 | PIXEL1 “A Pixel size The pixel size; Where the actual pixel size is 2“n, n is the value<br>PIXEL2 f° "sie, | stored here: Values 0-5 are allowed.<br>PIXELS oo<br>6-8: |ZOFFS1-6": |Zoffset | This value gives the offset from a phrase of pixel data of its<br>oe oe tte corresponding Z data in phrases. Values of 0 and 7 are not used.<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. 

Confidential Information “FER Property ofAtari Corporation 

June 7, 1995 

**==> picture [610 x 689] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||
|---|---|---|---|---|---|---|---|---|---|
|||Page|56|$$. $$$.|Jaguar|SoftwareReferenceo_OManual|-|Version 2.4|oO4:|
|BFt|9-14|||See Desc.|Width|This width is distinct from the width in pixels stored in the|
|[|window register, and is the width used for address generation.|:|
|The width|is a six-bit|floating point value|in pixels, with a four bit|‘|
|\|unsigned exponent,|and|a|three bit mantissa,|whose|top|bit|is|4|
|'|implicit, and which has the point after the implicit top bit. This is|S|
|similar to the IEEE single|precision|format|without the sign bit.|It|4|
|the|ilerent pixel|size. The|g|
|||must give a whole number ofphrases:|
|||;|following is a list of valid widthigguates:|WHEE|4|
|||/|WID2|WID28|‘3WiD160|WID89G2::.|||4]|
|||WID4|WID32|WiDL92|WID1024::.|||Z|
|||WID6|WID40|WID234%:,|WID1280|22:|=|
|WID8|WID48|WID256:2:|,WID1536 2|=|
|WID12|WIBG4:|8.|WID384|W208"|4|
|WID14|WIRBO|8|WD 448|WID2560|||:|=|
|WID16|WID96|—"‘WHH51.2|WID3072|i|4|
|WID20.-|WID112.—|WID64Q..|WID3584|-|
|WID342|eWID128|——_|WID768".|=|
|16-17|| See Desc.|X add ctrl.|These:Gontrol the update:ofthe X pointer on each pass round the|||4|
|||inner lodp. Values are:|Oe|||@-|
|XADDPHR (00)|-|Add|phrase width and truncate to|q|
|||ee|phrase|boundary|(sets phrase mode)|
|||fk|28XADDPIR(OD)..-|Add pixel size, effectively add one.|||[ae|
|ce|‘SEADDINC (11) “=|Add the|increment|—_|2|
|||@|
|;|18|||See Desc.|Y add cit,|| This bit:¢gntotshow|the Y pointer is updated within the inner|
|"=||Gncrement mode.|2222.|/|
|||“122.1|loopéftis overridden|by the X control bits if they are in add|s|
|||19|TXSIGNSUB|[Xsiga.,|||This birtiay|
|fe|be set in conjunction with the|X add pixel size mode|POG|
|age|“Hea, other modes.| to make theopération subtract pixel size. It should not be set|with|Poe,|8|
|"Makes|the Y add one mode into Y subtract one.|7|
|Ace|A¥enppiny’Size”|9|Fozz08|Wiiteonly|
|This register register|contains the size in the size in size in in|pixels, and is optionally used for clipping writes, so that if the pointer leaves and is optionally used for clipping writes, so that if the pointer leaves is optionally used for clipping writes, so that if the pointer leaves optionally used for clipping writes, so that if the pointer leaves used for clipping writes, so that if the pointer leaves for clipping writes, so that if the pointer leaves clipping writes, so that if the pointer leaves writes, so that if the pointer leaves so that if the pointer leaves that if the pointer leaves if the pointer leaves the pointer leaves pointer leaves leaves|1|
|the|window:|bounds|no write isperftmed. The width is an unsigned fifteen bit value in the low word, the write isperftmed. The width is an unsigned fifteen bit value in the low word, the isperftmed. The width is an unsigned fifteen bit value in the low word, theperftmed. The width is an unsigned fifteen bit value in the low word, the The width is an unsigned fifteen bit value in the low word, the width is an unsigned fifteen bit value in the low word, the is an unsigned fifteen bit value in the low word, the an unsigned fifteen bit value in the low word, the unsigned fifteen bit value in the low word, the fifteen bit value in the low word, the bit value in the low word, the value in the low word, the in the low word, the the low word, the low word, the word, the the|
|height an urisignéd an urisignéd urisignéd|fifteen|bit value value|it|the high word. The top bit of each word high word. The top bit of each word word. The top bit of each word The top bit of each word top bit of each word bit of each word of each word each word word|is ignored. ignored.|
|The window origia{0,9).is origia{0,9).is|always|at|the|top left hand corner of the window, and so clipping is performed left hand corner of the window, and so clipping is performed hand corner of the window, and so clipping is performed corner of the window, and so clipping is performed the window, and so clipping is performed window, and so clipping is performed and so clipping is performed so clipping is performed clipping is performed is performed performed|
|when the pointer values the pointer values pointer values values|aré:negative,|or when the pointer values are greater than or equal to these values. when the pointer values are greater than or equal to these values. the pointer values are greater than or equal to these values. pointer values are greater than or equal to these values. values are greater than or equal to these values. are greater than or equal to these values. than or equal to these values. or equal to these values. equal to these values. to these values. these values. values.|If|
|the desired desired|clip rectangledoes:net rectangledoes:netdoes:netnet|have|its top left corner at the window origin, then the window base register top left corner at the window origin, then the window base register left corner at the window origin, then the window base register corner at the window origin, then the window base register at the window origin, then the window base register the window origin, then the window base register window origin, then the window base register origin, then the window base register then the window base register the window base register window base register base register register|
|should be modified to make be modified to make modified to make to make make|it the top left corner of the clip rectangle. the top left corner of the clip rectangle. top left corner of the clip rectangle. left corner of the clip rectangle. corner of the clip rectangle. the clip rectangle. clip rectangle. rectangle.|q|

**----- End of picture text -----**<br>


This register register contains the size in the size in size in in pixels, and is optionally used for clipping writes, so that if the pointer leaves and is optionally used for clipping writes, so that if the pointer leaves is optionally used for clipping writes, so that if the pointer leaves optionally used for clipping writes, so that if the pointer leaves used for clipping writes, so that if the pointer leaves for clipping writes, so that if the pointer leaves clipping writes, so that if the pointer leaves writes, so that if the pointer leaves so that if the pointer leaves that if the pointer leaves if the pointer leaves the pointer leaves pointer leaves leaves the window: bounds no write isperftmed. The width is an unsigned fifteen bit value in the low word, the write isperftmed. The width is an unsigned fifteen bit value in the low word, the isperftmed. The width is an unsigned fifteen bit value in the low word, theperftmed. The width is an unsigned fifteen bit value in the low word, the The width is an unsigned fifteen bit value in the low word, the width is an unsigned fifteen bit value in the low word, the is an unsigned fifteen bit value in the low word, the an unsigned fifteen bit value in the low word, the unsigned fifteen bit value in the low word, the fifteen bit value in the low word, the bit value in the low word, the value in the low word, the in the low word, the the low word, the low word, the word, the the height an urisignéd an urisignéd urisignéd fifteen bit value value it the high word. The top bit of each word high word. The top bit of each word word. The top bit of each word The top bit of each word top bit of each word bit of each word of each word each word word is ignored. ignored. The window origia{0,9).is origia{0,9).is always at the top left hand corner of the window, and so clipping is performed left hand corner of the window, and so clipping is performed hand corner of the window, and so clipping is performed corner of the window, and so clipping is performed the window, and so clipping is performed window, and so clipping is performed and so clipping is performed so clipping is performed clipping is performed is performed performed when the pointer values the pointer values pointer values values aré:negative, or when the pointer values are greater than or equal to these values. when the pointer values are greater than or equal to these values. the pointer values are greater than or equal to these values. pointer values are greater than or equal to these values. values are greater than or equal to these values. are greater than or equal to these values. than or equal to these values. or equal to these values. equal to these values. to these values. these values. values. If the desired desired clip rectangledoes:net rectangledoes:netdoes:netnet have its top left corner at the window origin, then the window base register top left corner at the window origin, then the window base register left corner at the window origin, then the window base register corner at the window origin, then the window base register at the window origin, then the window base register the window origin, then the window base register window origin, then the window base register origin, then the window base register then the window base register the window base register window base register base register register should be modified to make be modified to make modified to make to make make it the top left corner of the clip rectangle. the top left corner of the clip rectangle. top left corner of the clip rectangle. left corner of the clip rectangle. corner of the clip rectangle. the clip rectangle. clip rectangle. rectangle. } © 1992-95 Atari Corp. Confidential Information IER Property ofAtari Corporation June7,1995 

June7,1995 

| 

| =. 

| | | 

| 

| AAcRING’? AN Inéreinient Bfmetion/ 97/9 F02220°» Write only This is the fractional parts of the increment described above. 

## 1 Jaguar Software Reference Manual - Version 24 Page 57 | A= et mmm OOS Raat 

| | This register contains the X (low word) and Y (high word) pointers onto the window, and are the location } where the next pixel will be written. They are sixteen-bit signed values. If X and Y values go out of range = positively then they will advance through memory (X will wrap onto the next line, Y will go off the end of the @ ~~ window). Only X values in the range 0-32767 and Y values in the range 0-4095:idl:produce valid addresses | from the address generator, values outside this range are for clipping purposes Only. 282. ALsten oa sep vas mn rome wares The step register contains two signed sixteen bit values, which are the X step (iéw Word) and Y step (high | word). These may be added to the X and Y pointer on each passround the outer loop, between passes through the inner loop. OE Sa | When calculating the step value for phrase-mode blits, note that the X pointer will be left pointing at‘the start of the first phrase not written by the blit.an Ad oFSTER TAN Step Fraction Value 1 F02214 “aie only i The step fraction register may be added to the fractional parts Of He'Al pointer in the same manner as the step value. This is used when Al is being used'fG'scan over the source Gf a scaled or rotated image. me AAoRPIKEL “AN PINel Pointer Fraction. FozaIB Readiite 4 This register contains the fractional parts of the pointer when At isbeing bed to implement a DDA. based and the Y part in the high word. address generator, for line-drawing,etc.The X part is.in the lowWord. Arne nnn een Or eriaIC wien The increment is added to.the pointer value within the inner loap'when the address update is in add increment mode. This register contaias'the two 16 bit signed integer parts of the increment, the X part is in the low word, the Y part in the high word... EEE 

| 

poo BASe CAD Baebnauister et )Foazas Tete only 32-bit register cdptaining a pointer to the base of the window pointed to by A2. This address must be phrase 

© 1992-95 Atari Corp. 

Confidential Information JPR Property ofAtari Corporation 

June 7, 1995 

; Page 58 

Jaguar SoftwareReference Manual - Version 2.4 

' E | ' | :' | a 4 ; | 4 | q a q a. 3 4 

| 1 - Add one Add one one ee Ce with theX add pixel size mode to make theX add pixel size mode to make the add pixel size mode to make the pixel size mode to make the size mode to make the mode to make the to make the make the the 19 | Xsign Xsign This bit may be set ingonjunction bit may be set ingonjunction may be set ingonjunction be set ingonjunction set ingonjunctiongonjunction operation subtract pixel subtract pixel size. It should'not be.set with other modes. with other modes. other modes. modes. | 20. |Ysign | Makes the Y add one Makes the Y add one the Y add one Y add one add one one ‘siide into Y subtract Gi6... subtract Gi6... Gi6... | This register is used as the window aie only if thé sense that it Hasebe used 10 AND mask the pointer . register when the Mask flag is set. “This causes the address.to wrap withisi'4 Tectangular area and may be used | This register contains the register contains the contains the ¥ (low word) and Y (high Y (high (high ord) posaters onto the window, and are the location onto the window, and are the location the window, and are the location window, and are the location and are the location are the location bit sgned values. If X and Y values go out of range and Y values go out of range Y values go out of range values go out of range go out of range out of range of range range ; where the next pixel will the next pixel will next pixel will pixel will will be: written. written. They are sixteeii sixteeii 

| { : 

| 

Aset of flags controlling various aspects of the A2 window and how addresses are updated. 

**==> picture [496 x 250] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||
|---|---|---|---|---|---|---|---|
|Bits|Name|Description|
|Por|[rich|||
|[3-5|
|[68|| Pixelsize||As Al.|ek|
|[9-14||Zoffset|[|AsAl.|Be|PE|
|[iS|[Mask[Width|__|| As Enab es A|l|.|Boolean AND masking of the A2|pointeroo by:its.window|register.cs 22245.|
|the inner loop.|#22:|
|16-17|| X add ctrl.|These control the update of the X pointer on each passitgiund|
|||QO - Add phrase width (truncate to phrase boundary)|EEE bean|
|01|- Add pixel size (effectively add oné¥|2|OSE|
|10|- Add zero|EEE|Se|
|)|
|18|| Y add ctrl.|This0 - Add bit controls zero|how the Ycntte, pointer isupdated withia:the-inner loop.OPER|||
|1|- Add one Add one one|ee|Ce|
|with|theX add pixel size mode to make theX add pixel size mode to make the add pixel size mode to make the pixel size mode to make the size mode to make the mode to make the to make the make the the|
|19|| Xsign Xsign|This bit may be set ingonjunction bit may be set ingonjunction may be set ingonjunction be set ingonjunction set ingonjunctiongonjunction|
|operation subtract pixel subtract pixel|size.|It|should'not|be.set with other modes. with other modes. other modes. modes.|
|||20. |Ysign|| Makes the Y add one Makes the Y add one the Y add one Y add one add one one|‘siide|into|Y subtract Gi6... subtract Gi6... Gi6...|

**----- End of picture text -----**<br>


This register contains the register contains the contains the ¥ (low word) and Y (high Y (high (high ord) posaters onto the window, and are the location onto the window, and are the location the window, and are the location window, and are the location and are the location are the location bit sgned values. If X and Y values go out of range and Y values go out of range Y values go out of range values go out of range go out of range out of range of range range where the next pixel will the next pixel will next pixel will pixel will will be: written. written. They are sixteeii sixteeii positively then they will advance through memory (X will wrap onto the next line, Y will go off the end of the window). Only X values’in the range 0:32767 and Y values in the range 0-4095 will produce valid addresses from the addressgenerator, values outside'thas range are for clipping purposes only. ea ot n= The step‘register contains two signed. sixteen bit values, which are the X step (low word) and Y step (high word). Thesé:iHay,be added to the cand Y pointer on each pass round the outer loop, between passes through When calculating the step value for pirase-mode blits, note that the X pointer will be left pointing at the start of the first phrase not writerby tbe biit. 

© 1992.95 AtariCorp. 

| 

1 

ConfidentialInformation “AOR Property of Atari Corporation 

June 7, 1995 

| | 

| | 

| 

**==> picture [560 x 723] intentionally omitted <==**

**----- Start of picture text -----**<br>
i 1 Jaguar Software Reference Manual - Version 2.4 Page 59<br>i Gonrolnegisies<br>Si BOCMD “Command Register = iii F022 Write only<br>@ This register describes the operation of the Blitter. A write to this register initiates: Hitter. operation, so it<br>j should be written to last when setting up a Blitter command. Control bits ae<br>' Bits 0-5 enable corresponding memory cycles within the inner loop. Destinatign.write cycles are tijways<br>performed (subject to comparator control), but all other cycle types are optiongh::. eeceen<br>De SRCEN ~~ | Enables a souce data read as part of he inner loop operas<br>1 | SRCENZ Enables a source Z read as part of thé isner loop operation-"Eisbit is ignored<br>2 |SRCENX Enables an "extra" source data read af the sta¢t af.an inner loop operation. This is<br>bit-to-pixel expansion. If SRCENZ is set an extra ‘Ligadis also performed.<br>| Co Seeeaeee<br>3 DSTEN Enables a destination data:tead:p a rts of inner loop operaiige;.Thismust always<br>be performed for pixelssitialiertHani®bits,where part of the'déStination data<br>write will need to restére the data that 'Was.previously there.<br>y ~ the effect ofintibiting destitiatiatwrites within the:inner loop, but Blitter<br>| operation wiltcontinte,<br>| 7eeeSet to #ef0. ee<br>. Bits 8-10 enable address updates wiikiin the outer loop. Thes¢'should only be enabled when required as there<br>is a one-tick overheadper update. OEP ee OEE<br>|e UPDAIFa __..| Ade d  thee fractional part inner loop operations of the Al in step thé outervalue lo p.t o  the fractional part of the Al pointer |<br>[GRA10 Aner he SRL a eer ee<br>[loop<br>hee te the 2step value to the A2 pointer between inner loop operations in the outer |<br>Reverses the notinal toles of the address registers from A] as destination and A2<br>fe geeeos.| as source to A2 as déstization and Al as source.<br>12 GOURD “| Bnable Gouraud shaded data updates within inner loop, i.e. the intensity gradient<br>es }¥gactional part, repeated four times, is added to the computed intensity fraction<br>cio register (a.k.a. destination data), then the intensity gradient integer part is added<br>. . oh“lee | with @ka. thé:¢arry paltem from data). theprevious add to the computed intensity value register<br>13. |ZBUFF |Enable polygon Z data updates within the inner loop, i.e. add Z fractions to the Z<br>8 ‘integerstea(source (source Z 1).  Z 2), then add with carry the Z integer part to the Z<br>i w {44 Enable carry into the top byte of the intensity integers in Gouraud data updates<br>\ (leave clear for CRY mode).<br>sR15 TOPNEN ooEnable carryeeinto the top nibble of the intensity integers in Gouraud data updates<br>I<br>; © 1992-95 Atari Corp. Confidential Information AR Property ofAtari Corporation June 7, 1995<br>**----- End of picture text -----**<br>


Jaguar Software Reference Manual - Version 2.4 

: 

| Bits 16-17 select alternative write data - the default source is the 16-17 select alternative write data - the default source is the select alternative write data - the default source is the alternative write data - the default source is the write data - the default source is the data - the default source is the - the default source is the the default source is the default source is the source is the is the the Logic Function Unit, whose output is Function Unit, whose output is Unit, whose output is whose output is output is is | controlled by the LFUFUNC bits. || 17 |ADDDSEL | Selectssource data the sum is a signed of source offset. and Leave destination TOPBEN data as and theTOPNEN write data. clear Note and that the the source | data gives three signed offsets for each of the CRY fields,.and the intensity value 5 i will saturate. Set TOPBEN and TOPNEN and sixtben bit saturating adds are | | : . | performed. This can be used to lighten and darkén:images. THs works only is 164 | . 18-20 |ZMODE These bits give the conditions under which the Z éatmparator generatesae thhibit. Setting them all to zero disables the Z comparator. fhis:can only operate in EOsDit } per pixel mode. eae Tee | | bit 0 - source Jess than destination 25.. cece GEE | | bit 2 - source greater than destination pecrer eee OEE | 21-24 | - The bits control the data produced by the: logic function unit. The output is the @ [ Boolean OR of the following minterms> eee } 4 I bit 0 - NOT source AND NOT[destination] CHEE P| bit 2 - source AND N@Fdestinatioa:::5... OE | a bit 3 - source AND destination WHEE | 4 | | The following are assignéd equates for combinations of the above: q |: | LFU_CLEAR —€f05. LFU_LSAD: S&D LFUNOTS =1S... LFULNSORD — !S|D 4 | LFUNOFD 2 'D |&LFUSORND — S/!D | 4 f LFU_N'SXORD '(S*D) “2.-FU_SORD S|D | ff | «4 LFU_LNSORND = !S|!D = LFU_ONE ones | _ the pixel value comparator compare destination data with pattern data rather § 4 | | 25 ‘Make | “than source data with pattern data. i a | 26 |BCOMPEN “EEnable write inbibit on the output from the bit comparator. This works pixel by =| = } 4 pixel in any Size, Wut over whole phrases only on 8-bit pixels. When operating in | <a eles... | pixel mode then thi: witedoes not occur unless BEGWREN is set, but in phrase fo is always written when the comparator determines that the f eye mode destination data ee “4 -gigel should not be written. 1 27 “{:;BCOMPEN Enable:write inhibit on the output from the data comparator. This only appliesto |; 4 atid 16-bit per pixel modes. When operating in pixel mode then the write | | HE 8-bit SER does notcur unless BKGWREN is set, but in phrase mode destination data is § “Henan, | always writfen when the comparator determines that the pixel should not be 1 buito write back destination data. This only applies to pixel mode, in phrase mode | destination data is always written. ’ | 28 pro ee inhibit occurs, this flag enables the Blitter to still perform the write, | 

## Page 60 Jaguar Bits 16-17 select alternative write data - the default source is the 16-17 select alternative write data - the default source is the select alternative write data - the default source is the alternative write data - the default source is the write data - the default source is the data - the default source is the - the default source is the the default source is the default source is the source is the is the the Logic Function Unit, whose output is Function Unit, whose output is Unit, whose output is whose output is output is is 

t 

© 1992-95 Atari Corp. 

Confidential Information JPR Property ofAtari Corporation 

June 7, 1995 

q 

|| | 

1 : 

” 

. 

**==> picture [213 x 26] intentionally omitted <==**

**----- Start of picture text -----**<br>
L Jaguar Software Reference Manual - Version 2.4<br>**----- End of picture text -----**<br>


**==> picture [34 x 26] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 61<br>**----- End of picture text -----**<br>


**==> picture [517 x 584] intentionally omitted <==**

**----- Start of picture text -----**<br>
29 BUSHI<br>j<br>Setting BUSH cerosslong-blits- may disturb the sereen<br>This bit should not be used due to a bug in the Jagwat:Gonsole.<br>:<br>30 |SRCSHADE | This bit uses the IINC register to modify the intensity of data:tead from the source<br>_ | address, and may be used to lighten or darken itdages. It may be:nsédin<br>conjunction with GOURZ, but not GOURD. The:data read from the:satixce is<br>modified, so source data should be selected using the.LFU as the write Gath: This<br>|<br>j is particularly intended for performing flat shading ontexture mapped SUrEagES.<br>ei- a ae<br>Bit State Description<br>IDLE When set, the blitter is completely idle and its last bus transaction is |<br>completed. ao<br>1 STOPPED When set, the blister 48'stopped in its collision détéétion mode - see the<br>collision confrgi register Below. “eee<br>inner SREADX<br>” 4 inner SLREADX Diagnostic only... WHEE<br>inner SREAD Diagnostic only. 22:85. eee<br>inner DREAD “Psagnosti¢ obly. Tieatl<br>[8| inner DEREAD [Diagnostic OB. rs CERES<br>5Tinner DWRITE | EBagnostic ony,<br>inner DZWRITE<br>12 | outer INNER::.. Diagnostic only. HEE<br>13 | outer AIFUPBATE | Diagnostic onlyfies..<br>outer ALUPDATE:=: |.Diagnostic only 22 eeeee<br>Bcountilmieounters neater ear yFezesc “ Witeonly<br>The low word is the numibey 6f iterations of the inner loop operation. This is a sixteen bit value which reloads<br>the inner }gop counter on each entry to the inner loop.<br>The high ward isthe number ofiterations of the outer loop. This is a sixteen bit value which is loaded directly<br>into the outerloap counter. Eee<br>The counters both accept values in'the range 1 to 65536 (encoded as 0).<br>**----- End of picture text -----**<br>


: 

© 1992-95 Atari Corp. Confidential Information PO Property of Atari Corporation 

June 7, 1995 

|[Page][ 62] 

Jaguar Software Reference Manual - Version 2.4 

1 = q ' ‘ 4 Y : = | = | | || 

| 

i z 

buns All data registers are sixty-four bits, unless otherwise noted. 

The source data may be pre-loaded with data for bit-to-byte expansion. The'spiirce data tegiiter also serves to hold the four sixteen bit fractional parts of intensity when computing Gouri shaded intensity... je “peTore=r -peetnation Data Register! FOzRAS! | Write only") This 64-bit register holds the destination data - which may be cidféy read in the innertogp tallow ae Or.jtmay be used to Bwve background or unmodified pixels to be written back correctly when in phrase-mode, paper colours, if it is not read. Ee OEE pousTz we bectnationz nasser” = POzebO Reon This 64-bit register holds the destination Z value, ind may be used.as the data register. = pisnezmconerzneaaets | nn Hiss niteony The source Z register 1 is also used to hold the four intéget:parts of computed Z. eisncze’Source’z Heuister2 Roane Wetponiy The source Z register 2 is also used:ta:hold the folst fraction patts of computed Z. ecparo smeeanern Daanegicter Ul) ees awateony The pattern data register alsa sérves to hold the comipuiigaiatensity integer parts and their associated colours. ment oo Romero Witte only BfiNC Intensity incremen’ This thirty te bi register holds the integer‘aiid fractional parts of the intensity increment used for Gouraud thé colour value, and should therefore normally be left set to shading.Note that the top eight bits will! modify ene eer nee ETA ion This thirty-two bit register holds the integer and fractional parts of the Z increment used for computed Z 

: 

| 

© 1992-95 AtariCorp. 

Confidential Information “JPR Property of Atari Corporation 

June 7,1995 

Page 63 

| This registers allows the Blitter to be stopped when an inner loop write inhibit occurs. Blitter stop will occur | in painting in pixel-by-pixel mode (X add control is 1), BKGWREN is clear, and one of BCOMPEN, 7 DCOMPEN or ZMODEO-2 is set, along with the matching condition. @ The Blitter operation may at that point be resumed or aborted. Peete 

| | ' 

| 

## Ss Jaguar Software Reference Manual - Version 2.4 a BSTOR = hollision'contfol——— ORR R Wiiteconly 

**==> picture [2 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
_<br>**----- End of picture text -----**<br>


**==> picture [480 x 98] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||
|---|---|---|---|---|---|---|---|---|
|°|
|Bit|Name|Description|
|0|||RESUME|Writing a one to this bit when the|Blitter has skapped under the|ali6ve|conditions|
|will cause|the Blitter to resume operations.|Writizig:a zero has no effects:|
|1|ABORT|Writing a one to this bit when the Blitter has stopped|tinder|the above conditions|
|||will cause the Blitter|to terminate|the current|operation:|and.revert|to|its|idle:sfate.|
|Writing a zero has no effect.|et|TENGE|ese|
|STOPEN|Set this bit to enable Blitter collision $t6ps::|Clear|it to disable thers; /22222"|

**----- End of picture text -----**<br>


**==> picture [521 x 150] intentionally omitted <==**

**----- Start of picture text -----**<br>
pero ntentyse rene wiite only<br>: Bie _imensity2 =. Foeeso mneonly<br>| BH  intensityi =», Foezea §=Wilteonly<br>| Bio _—sintensityo i“ sR Ozeee Wille only<br>4 These four registers provide an alternate view of the:computed intensity integer parts (pattern data) and<br>£., computed intensity fractional parts (source data) régastérs, They are a convéitient way of updating the<br>2 intensity values for Gouraud shading. .Rash:register is @:24:bit value (8.16 bifiumber), with the top eight bits<br>" —_—iunused, that modifies the corresponding fieHis of the computed: iatensity integer and fractional part registers.<br>' Note that the colour fields in the pattern data registers are unafféétedby Writes to these registers.<br>**----- End of picture text -----**<br>


**==> picture [497 x 54] intentionally omitted <==**

**----- Start of picture text -----**<br>
B27 2m + +j.- === Foeeso $ Witeonly<br>Bz mt i —“‘C*lLCC*é COG OCWiitccnly<br>B20 2 = 4) Ro2zes  Wateonly<br>**----- End of picture text -----**<br>


These registers are analagous to‘the ittensity registers, and are for Z buffer operation. They affect the corresponding parts ofthe computed'Z imteger (source Z1) and computed Z fraction (source Z2) registers. They are 32 bit values (16.16 bit numbers}. 

| 

EN © 1992-95 Atari Corp. Confidential Information PPR Property ofAtari Corporation June 7, 1995 

- Page 64 

64 Jaguar Software Reference Manual - Version 24 1 - Moccsuropemtion section discusses some of the typical modes of operation of the Blitter. discusses some of the typical modes of operation of the Blitter. some of the typical modes of operation of the Blitter. of the typical modes of operation of the Blitter. the typical modes of operation of the Blitter. typical modes of operation of the Blitter. modes of operation of the Blitter. of operation of the Blitter. operation of the Blitter. of the Blitter. the Blitter. Blitter. It is by no means a by no means a means a a complete |g to all possible modes, but will show how to do certain common operations. This is the best way to learn all possible modes, but will show how to do certain common operations. This is the best way to learn possible modes, but will show how to do certain common operations. This is the best way to learn modes, but will show how to do certain common operations. This is the best way to learn but will show how to do certain common operations. This is the best way to learn will show how to do certain common operations. This is the best way to learn show how to do certain common operations. This is the best way to learn how to do certain common operations. This is the best way to learn to do certain common operations. This is the best way to learn do certain common operations. This is the best way to learn certain common operations. This is the best way to learn common operations. This is the best way to learn operations. This is the best way to learn This is the best way to learn is the best way to learn the best way to learn best way to learn way to learn to learn learn = to use use the Blitter. Throughout this section, section, flags in flags registers that are not mentioned should:always:Deset in flags registers that are not mentioned should:always:Deset flags registers that are not mentioned should:always:Deset that are not mentioned should:always:Deset are not mentioned should:always:Deset not mentioned should:always:Deset mentioned should:always:Deset should:always:Desetset to Zero. Registers , 4 are not mentioned need not be set up. not mentioned need not be set up. mentioned need not be set up. not be set up. be set up. set up. up. HP OTUREEEE 4 pickMeves & simplest of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter another. The Blsiter The Blsiter Blsiter The Blsiter Blsiter Blsiter | 4 very rapid way rapid way way rapid way way way of transferring data? data? data? _ perform this operation one phrase at a time, and operation one phrase at a time, and one phrase at a time, and phrase at a time, and at a time, and a time, and time, and and operation one phrase at a time, and one phrase at a time, and phrase at a time, and at a time, and a time, and time, and and one phrase at a time, and phrase at a time, and at a time, and a time, and time, and and phrase at a time, and at a time, and a time, and time, and and at a time, and a time, and time, and and a time, and time, and and time, and and and it is therefaré:a is therefaré:a therefaré:a is therefaré:a therefaré:a therefaré:a source address of the data should be stored in the A2 base register, and the destination address of the data should be stored in the A2 base register, and the destination of the data should be stored in the A2 base register, and the destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination address of the data should be stored in the A2 base register, and the destination of the data should be stored in the A2 base register, and the destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination of the data should be stored in the A2 base register, and the destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination base register, and the destination register, and the destination and the destination the destination destination register, and the destination and the destination the destination destination and the destination the destination destination the destination destination destination address 4 4 4 the Al Al Al base register. If these are not phrase aligned addresses then they register. If these are not phrase aligned addresses then they If these are not phrase aligned addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they register. If these are not phrase aligned addresses then they If these are not phrase aligned addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they If these are not phrase aligned addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they phrase aligned addresses then they aligned addresses then they addresses then they aligned addresses then they addresses then they addresses then they shioild't¢e rounded down toa phrase toa phrase phrase toa phrase phrase phrase | @ boundary, and the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The and the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The and the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The into the X pointer. The the X pointer. The X pointer. The pointer. The The the X pointer. The X pointer. The pointer. The The X pointer. The pointer. The The pointer. The The The Y = pointer should be set to zero. should be set to zero. be set to zero. set to zero. to zero. zero. should be set to zero. be set to zero. set to zero. to zero. zero. be set to zero. set to zero. to zero. zero. set to zero. to zero. zero. to zero. zero. zero. OE , 4 The length of the block should be stored in the innel length of the block should be stored in the innel of the block should be stored in the innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel length of the block should be stored in the innel of the block should be stored in the innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel of the block should be stored in the innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel be stored in the innel stored in the innel in the innel the innel innel stored in the innel in the innel the innel innel in the innel the innel innel the innel innel innel Sounder =the =the =the number represents‘thé ‘hizmber of pixels, so represents‘thé ‘hizmber of pixels, so‘thé ‘hizmber of pixels, so ‘hizmber of pixels, so of pixels, so pixels, so represents‘thé ‘hizmber of pixels, so‘thé ‘hizmber of pixels, so ‘hizmber of pixels, so of pixels, so pixels, so‘thé ‘hizmber of pixels, so ‘hizmber of pixels, so of pixels, so pixels, so ‘hizmber of pixels, so of pixels, so pixels, so of pixels, so pixels, so pixels, so so 1 q largest block that can be copied block that can be copied that can be copied can be copied be copied copied block that can be copied that can be copied can be copied be copied copied that can be copied can be copied be copied copied can be copied be copied copied be copied copied copied is 32767 32767 32767 pixéis;wherewherewhere 32+bit pixels are set this is 128K: For smaller set this is 128K: For smaller this is 128K: For smaller is 128K: For smaller 128K: For smaller For smaller smaller set this is 128K: For smaller this is 128K: For smaller is 128K: For smaller 128K: For smaller For smaller smaller this is 128K: For smaller is 128K: For smaller 128K: For smaller For smaller smaller is 128K: For smaller 128K: For smaller For smaller smaller 128K: For smaller For smaller smaller For smaller smaller smaller , 4 blocks it is usually easier to it is usually easier to is usually easier to usually easier to easier to it is usually easier to is usually easier to usually easier to easier to is usually easier to usually easier to easier to usually easier to easier to easier to work in bytes. The in bytes. The bytes. The The in bytes. The bytes. The The bytes. The The The Outer counter shotild bé:set to one. shotild bé:set to one. one. shotild bé:set to one. one. one. FY The Blitter needs to be told how to update the pointeis Blitter needs to be told how to update the pointeis needs to be told how to update the pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis Blitter needs to be told how to update the pointeis needs to be told how to update the pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis needs to be told how to update the pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis to update the pointeis update the pointeis the pointeis pointeis update the pointeis the pointeis pointeis the pointeis pointeis pointeis after each read each read read each read read read aiid Write cycle, so the add control bits Write cycle, so the add control bits cycle, so the add control bits so the add control bits the add control bits add control bits control bits bits Write cycle, so the add control bits cycle, so the add control bits so the add control bits the add control bits add control bits control bits bits cycle, so the add control bits so the add control bits the add control bits add control bits control bits bits so the add control bits the add control bits add control bits control bits bits the add control bits add control bits control bits bits add control bits control bits bits control bits bits bits ] ‘ ; are set to zero to indicate phrase mode in both addréss flags set to zero to indicate phrase mode in both addréss flags to zero to indicate phrase mode in both addréss flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags set to zero to indicate phrase mode in both addréss flags to zero to indicate phrase mode in both addréss flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags to zero to indicate phrase mode in both addréss flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags in both addréss flags both addréss flags addréss flags flags both addréss flags addréss flags flags addréss flags flags flags registers. HEE f 4 Having set these, set these, these, set these, these, these, a command command command is stored stored stored ti thé command register,.with the SRGEN bit set to enable source register,.with the SRGEN bit set to enable source the SRGEN bit set to enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source register,.with the SRGEN bit set to enable source the SRGEN bit set to enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source the SRGEN bit set to enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source bit set to enable source set to enable source to enable source enable source set to enable source to enable source enable source to enable source enable source enable source reads, and the LFUFUNC bits set to and the LFUFUNC bits set to the LFUFUNC bits set to LFUFUNC bits set to bits set to set to to and the LFUFUNC bits set to the LFUFUNC bits set to LFUFUNC bits set to bits set to set to to the LFUFUNC bits set to LFUFUNC bits set to bits set to set to to LFUFUNC bits set to bits set to set to to bits set to set to to set to to to 1100 to'select. source data: data: data: Efthe.source4@'not phrase aligned,the.source4@'not phrase aligned,4@'not phrase aligned, phrase aligned, aligned,the.source4@'not phrase aligned,4@'not phrase aligned, phrase aligned, aligned,4@'not phrase aligned, phrase aligned, aligned, phrase aligned, aligned, aligned, then the the the ; 4 SRCENX bit must be set. bit must be set. must be set. be set. set. bit must be set. must be set. be set. set. must be set. be set. set. be set. set. set. ae Hee . Rectangle Moves Moves a. Rectangle moves are vety:like block moves, but use a two-dimensional moves are vety:like block moves, but use a two-dimensional are vety:like block moves, but use a two-dimensional block moves, but use a two-dimensional moves, but use a two-dimensional but use a two-dimensional a two-dimensional two-dimensional data set rather than the one-dimension set rather than the one-dimension rather than the one-dimension than the one-dimension the one-dimension one-dimension | 4 of a block a block block operation. This:bringsin various new congepts. This:bringsin various new congepts.in various new congepts. new congepts. congepts. 8 , 7 A two-dimensional two-dimensional array Gf pixels is.stored in memory Gf pixels is.stored in memory pixels is.stored in memory in memory memory #84 linear array of phrases. This will usually be the linear array of phrases. This will usually be the array of phrases. This will usually be the of phrases. This will usually be the phrases. This will usually be the This will usually be the will usually be the usually be the be the the { 7 data field of a a bit-mappedobject.object. Fhe Blitter has to know the width of this window of pixels. As an address in Blitter has to know the width of this window of pixels. As an address in has to know the width of this window of pixels. As an address in to know the width of this window of pixels. As an address in know the width of this window of pixels. As an address in the width of this window of pixels. As an address in width of this window of pixels. As an address in of this window of pixels. As an address in this window of pixels. As an address in window of pixels. As an address in of pixels. As an address in pixels. As an address in As an address in an address in address in i. the window, window, in pixel terms, is given pixel terms, is given terms, is given is given given by#hé:X-pointer plus the width times the#hé:X-pointer plus the width times the plus the width times the the width times the width times the times the the Y pointer; a multiply operation a multiply operation operation , is necessary to:compute the address. To avoid address. To avoid To avoid avoid the.need for a hardware multiplier in the Blitter address a hardware multiplier in the Blitter address hardware multiplier in the Blitter address multiplier in the Blitter address in the Blitter address the Blitter address Blitter address address q generator,the Widththe Width Width iS‘rather strangely encoded encoded j * Blitter window width is‘expressed as a floating-point number. The actual value has a four-bit exponent and a window width is‘expressed as a floating-point number. The actual value has a four-bit exponent and a width is‘expressed as a floating-point number. The actual value has a four-bit exponent and a is‘expressed as a floating-point number. The actual value has a four-bit exponent and a‘expressed as a floating-point number. The actual value has a four-bit exponent and a as a floating-point number. The actual value has a four-bit exponent and a a floating-point number. The actual value has a four-bit exponent and a floating-point number. The actual value has a four-bit exponent and a number. The actual value has a four-bit exponent and a The actual value has a four-bit exponent and a actual value has a four-bit exponent and a value has a four-bit exponent and a has a four-bit exponent and a a four-bit exponent and a four-bit exponent and a exponent and a and a " three-bit mantissa, whose top bitis.implicit. This allows Blitter window widths to be any value whose binary whose top bitis.implicit. This allows Blitter window widths to be any value whose binary top bitis.implicit. This allows Blitter window widths to be any value whose binary bitis.implicit. This allows Blitter window widths to be any value whose binaryis.implicit. This allows Blitter window widths to be any value whose binary This allows Blitter window widths to be any value whose binary allows Blitter window widths to be any value whose binary Blitter window widths to be any value whose binary window widths to be any value whose binary widths to be any value whose binary to be any value whose binary be any value whose binary any value whose binary value whose binary whose binary binary ] 4 form has has #6:#hore than three significant digits followed by some number of zeroes. three significant digits followed by some number of zeroes. significant digits followed by some number of zeroes. digits followed by some number of zeroes. followed by some number of zeroes. by some number of zeroes. some number of zeroes. number of zeroes. of zeroes. zeroes. 4 As an example, an example, hefe. are how various svindow widths encode: are how various svindow widths encode: how various svindow widths encode: various svindow widths encode: svindow widths encode: widths encode: encode: i : Value Binary Floating-point Encoded : : =25:00G0000 10100 10100 1.01 x 2%4 x 2%4 0100 01 01 | —s0|| b00001010000- | —_101x2%6 —_101x2%6 [LT 900010000000. |[[_-1.00x2°7_]] | 011100 1 640] oori9000000.—fOx2"9 T0011 ] Ti1900000000 | iix2i {10 : a a____ © 1992-95 Atari Corp. Confidential Information FRProperty ofAtari Corporation June7,1995 4 

| Moccsuropemtion { This section discusses some of the typical modes of operation of the Blitter. discusses some of the typical modes of operation of the Blitter. some of the typical modes of operation of the Blitter. of the typical modes of operation of the Blitter. the typical modes of operation of the Blitter. typical modes of operation of the Blitter. modes of operation of the Blitter. of operation of the Blitter. operation of the Blitter. of the Blitter. the Blitter. Blitter. It is by no means a by no means a means a a complete | guide to all possible modes, but will show how to do certain common operations. This is the best way to learn all possible modes, but will show how to do certain common operations. This is the best way to learn possible modes, but will show how to do certain common operations. This is the best way to learn modes, but will show how to do certain common operations. This is the best way to learn but will show how to do certain common operations. This is the best way to learn will show how to do certain common operations. This is the best way to learn show how to do certain common operations. This is the best way to learn how to do certain common operations. This is the best way to learn to do certain common operations. This is the best way to learn do certain common operations. This is the best way to learn certain common operations. This is the best way to learn common operations. This is the best way to learn operations. This is the best way to learn This is the best way to learn is the best way to learn the best way to learn best way to learn way to learn to learn learn E how to use use the Blitter. u Throughout this section, section, flags in flags registers that are not mentioned should:always:Deset in flags registers that are not mentioned should:always:Deset flags registers that are not mentioned should:always:Deset that are not mentioned should:always:Deset are not mentioned should:always:Deset not mentioned should:always:Deset mentioned should:always:Deset should:always:Desetset to Zero. Registers i that are not mentioned need not be set up. not mentioned need not be set up. mentioned need not be set up. not be set up. be set up. set up. up. HP OTUREEEE i pickMeves | The simplest of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter simplest of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter another. The Blsiter The Blsiter Blsiter The Blsiter Blsiter Blsiter very rapid way rapid way way rapid way way way of transferring data? data? data? i will perform perform this operation one phrase at a time, and operation one phrase at a time, and one phrase at a time, and phrase at a time, and at a time, and a time, and time, and and operation one phrase at a time, and one phrase at a time, and phrase at a time, and at a time, and a time, and time, and and one phrase at a time, and phrase at a time, and at a time, and a time, and time, and and phrase at a time, and at a time, and a time, and time, and and at a time, and a time, and time, and and a time, and time, and and time, and and and it is therefaré:a is therefaré:a therefaré:a is therefaré:a therefaré:a therefaré:a The source address of the data should be stored in the A2 base register, and the destination address of the data should be stored in the A2 base register, and the destination of the data should be stored in the A2 base register, and the destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination source address of the data should be stored in the A2 base register, and the destination address of the data should be stored in the A2 base register, and the destination of the data should be stored in the A2 base register, and the destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination address of the data should be stored in the A2 base register, and the destination of the data should be stored in the A2 base register, and the destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination of the data should be stored in the A2 base register, and the destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination base register, and the destination register, and the destination and the destination the destination destination register, and the destination and the destination the destination destination and the destination the destination destination the destination destination destination address 4 4 4 the Al Al Al EF base register. If these are not phrase aligned addresses then they register. If these are not phrase aligned addresses then they If these are not phrase aligned addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they register. If these are not phrase aligned addresses then they If these are not phrase aligned addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they If these are not phrase aligned addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they phrase aligned addresses then they aligned addresses then they addresses then they aligned addresses then they addresses then they addresses then they shioild't¢e rounded down toa phrase toa phrase phrase toa phrase phrase phrase | boundary, and the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The and the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The and the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The into the X pointer. The the X pointer. The X pointer. The pointer. The The the X pointer. The X pointer. The pointer. The The X pointer. The pointer. The The pointer. The The The Y pointer should be set to zero. should be set to zero. be set to zero. set to zero. to zero. zero. should be set to zero. be set to zero. set to zero. to zero. zero. be set to zero. set to zero. to zero. zero. set to zero. to zero. zero. to zero. zero. zero. OE The length of the block should be stored in the innel length of the block should be stored in the innel of the block should be stored in the innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel length of the block should be stored in the innel of the block should be stored in the innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel of the block should be stored in the innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel be stored in the innel stored in the innel in the innel the innel innel stored in the innel in the innel the innel innel in the innel the innel innel the innel innel innel Sounder =the =the =the number represents‘thé ‘hizmber of pixels, so represents‘thé ‘hizmber of pixels, so‘thé ‘hizmber of pixels, so ‘hizmber of pixels, so of pixels, so pixels, so represents‘thé ‘hizmber of pixels, so‘thé ‘hizmber of pixels, so ‘hizmber of pixels, so of pixels, so pixels, so‘thé ‘hizmber of pixels, so ‘hizmber of pixels, so of pixels, so pixels, so ‘hizmber of pixels, so of pixels, so pixels, so of pixels, so pixels, so pixels, so so the largest block that can be copied block that can be copied that can be copied can be copied be copied copied largest block that can be copied block that can be copied that can be copied can be copied be copied copied block that can be copied that can be copied can be copied be copied copied that can be copied can be copied be copied copied can be copied be copied copied be copied copied copied is 32767 32767 32767 pixéis;wherewherewhere 32+bit pixels are set this is 128K: For smaller set this is 128K: For smaller this is 128K: For smaller is 128K: For smaller 128K: For smaller For smaller smaller set this is 128K: For smaller this is 128K: For smaller is 128K: For smaller 128K: For smaller For smaller smaller this is 128K: For smaller is 128K: For smaller 128K: For smaller For smaller smaller is 128K: For smaller 128K: For smaller For smaller smaller 128K: For smaller For smaller smaller For smaller smaller smaller | blocks it is usually easier to it is usually easier to is usually easier to usually easier to easier to it is usually easier to is usually easier to usually easier to easier to is usually easier to usually easier to easier to usually easier to easier to easier to work in bytes. The in bytes. The bytes. The The in bytes. The bytes. The The bytes. The The The Outer counter shotild bé:set to one. shotild bé:set to one. one. shotild bé:set to one. one. one. | | The Blitter needs to be told how to update the pointeis Blitter needs to be told how to update the pointeis needs to be told how to update the pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis Blitter needs to be told how to update the pointeis needs to be told how to update the pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis needs to be told how to update the pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis to update the pointeis update the pointeis the pointeis pointeis update the pointeis the pointeis pointeis the pointeis pointeis pointeis after each read each read read each read read read aiid Write cycle, so the add control bits Write cycle, so the add control bits cycle, so the add control bits so the add control bits the add control bits add control bits control bits bits Write cycle, so the add control bits cycle, so the add control bits so the add control bits the add control bits add control bits control bits bits cycle, so the add control bits so the add control bits the add control bits add control bits control bits bits so the add control bits the add control bits add control bits control bits bits the add control bits add control bits control bits bits add control bits control bits bits control bits bits bits i are set to zero to indicate phrase mode in both addréss flags set to zero to indicate phrase mode in both addréss flags to zero to indicate phrase mode in both addréss flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags set to zero to indicate phrase mode in both addréss flags to zero to indicate phrase mode in both addréss flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags to zero to indicate phrase mode in both addréss flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags in both addréss flags both addréss flags addréss flags flags both addréss flags addréss flags flags addréss flags flags flags registers. HEE | Having set these, set these, these, set these, these, these, a command command command is stored stored stored ti thé command register,.with the SRGEN bit set to enable source register,.with the SRGEN bit set to enable source the SRGEN bit set to enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source register,.with the SRGEN bit set to enable source the SRGEN bit set to enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source the SRGEN bit set to enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source bit set to enable source set to enable source to enable source enable source set to enable source to enable source enable source to enable source enable source enable source reads, and the LFUFUNC bits set to and the LFUFUNC bits set to the LFUFUNC bits set to LFUFUNC bits set to bits set to set to to and the LFUFUNC bits set to the LFUFUNC bits set to LFUFUNC bits set to bits set to set to to the LFUFUNC bits set to LFUFUNC bits set to bits set to set to to LFUFUNC bits set to bits set to set to to bits set to set to to set to to to 1100 to'select. source data: data: data: Efthe.source4@'not phrase aligned,the.source4@'not phrase aligned,4@'not phrase aligned, phrase aligned, aligned,the.source4@'not phrase aligned,4@'not phrase aligned, phrase aligned, aligned,4@'not phrase aligned, phrase aligned, aligned, phrase aligned, aligned, aligned, then the the the SRCENX bit must be set. bit must be set. must be set. be set. set. bit must be set. must be set. be set. set. must be set. be set. set. be set. set. set. ae Hee | Rectangle Moves Moves i Rectangle moves are vety:like block moves, but use a two-dimensional moves are vety:like block moves, but use a two-dimensional are vety:like block moves, but use a two-dimensional block moves, but use a two-dimensional moves, but use a two-dimensional but use a two-dimensional a two-dimensional two-dimensional data set rather than the one-dimension set rather than the one-dimension rather than the one-dimension than the one-dimension the one-dimension one-dimension of a block a block block operation. This:bringsin various new congepts. This:bringsin various new congepts.in various new congepts. new congepts. congepts. 8 i A two-dimensional two-dimensional array Gf pixels is.stored in memory Gf pixels is.stored in memory pixels is.stored in memory in memory memory #84 linear array of phrases. This will usually be the linear array of phrases. This will usually be the array of phrases. This will usually be the of phrases. This will usually be the phrases. This will usually be the This will usually be the will usually be the usually be the be the the data field of a a bit-mappedobject.object. Fhe Blitter has to know the width of this window of pixels. As an address in Blitter has to know the width of this window of pixels. As an address in has to know the width of this window of pixels. As an address in to know the width of this window of pixels. As an address in know the width of this window of pixels. As an address in the width of this window of pixels. As an address in width of this window of pixels. As an address in of this window of pixels. As an address in this window of pixels. As an address in window of pixels. As an address in of pixels. As an address in pixels. As an address in As an address in an address in address in Hl the window, window, in pixel terms, is given pixel terms, is given terms, is given is given given by#hé:X-pointer plus the width times the#hé:X-pointer plus the width times the plus the width times the the width times the width times the times the the Y pointer; a multiply operation a multiply operation operation is necessary to:compute the address. To avoid address. To avoid To avoid avoid the.need for a hardware multiplier in the Blitter address a hardware multiplier in the Blitter address hardware multiplier in the Blitter address multiplier in the Blitter address in the Blitter address the Blitter address Blitter address address generator,the Widththe Width Width iS‘rather strangely encoded encoded | Blitter window width is‘expressed as a floating-point number. The actual value has a four-bit exponent and a window width is‘expressed as a floating-point number. The actual value has a four-bit exponent and a width is‘expressed as a floating-point number. The actual value has a four-bit exponent and a is‘expressed as a floating-point number. The actual value has a four-bit exponent and a‘expressed as a floating-point number. The actual value has a four-bit exponent and a as a floating-point number. The actual value has a four-bit exponent and a a floating-point number. The actual value has a four-bit exponent and a floating-point number. The actual value has a four-bit exponent and a number. The actual value has a four-bit exponent and a The actual value has a four-bit exponent and a actual value has a four-bit exponent and a value has a four-bit exponent and a has a four-bit exponent and a a four-bit exponent and a four-bit exponent and a exponent and a and a | three-bit mantissa, whose top bitis.implicit. This allows Blitter window widths to be any value whose binary whose top bitis.implicit. This allows Blitter window widths to be any value whose binary top bitis.implicit. This allows Blitter window widths to be any value whose binary bitis.implicit. This allows Blitter window widths to be any value whose binaryis.implicit. This allows Blitter window widths to be any value whose binary This allows Blitter window widths to be any value whose binary allows Blitter window widths to be any value whose binary Blitter window widths to be any value whose binary window widths to be any value whose binary widths to be any value whose binary to be any value whose binary be any value whose binary any value whose binary value whose binary whose binary binary form has has #6:#hore than three significant digits followed by some number of zeroes. three significant digits followed by some number of zeroes. significant digits followed by some number of zeroes. digits followed by some number of zeroes. followed by some number of zeroes. by some number of zeroes. some number of zeroes. number of zeroes. of zeroes. zeroes. As an example, an example, hefe. are how various svindow widths encode: are how various svindow widths encode: how various svindow widths encode: various svindow widths encode: svindow widths encode: widths encode: encode: Value Binary Floating-point Encoded =25:00G0000 10100 10100 1.01 x 2%4 x 2%4 0100 01 01 | —s0|| b00001010000- | —_101x2%6 —_101x2%6 [LT : 900010000000. |[[_-1.00x2°7_]] | 011100 | 640] oori9000000.—fOx2"9 T0011 Ti1900000000 | iix2i {10 a a____ | 

The simplest of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter simplest of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter of all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter all Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter Blitter operations is a block move, copying one area of memory:oxto another. The Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter operations is a block move, copying one area of memory:oxto another. The Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter is a block move, copying one area of memory:oxto another. The Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter a block move, copying one area of memory:oxto another. The Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter block move, copying one area of memory:oxto another. The Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter move, copying one area of memory:oxto another. The Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter copying one area of memory:oxto another. The Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter one area of memory:oxto another. The Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter area of memory:oxto another. The Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter of memory:oxto another. The Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter memory:oxto another. The Blsiter another. The Blsiter The Blsiter Blsiter another. The Blsiter The Blsiter Blsiter The Blsiter Blsiter Blsiter very rapid way rapid way way rapid way way way of transferring data? data? data? will perform perform this operation one phrase at a time, and operation one phrase at a time, and one phrase at a time, and phrase at a time, and at a time, and a time, and time, and and operation one phrase at a time, and one phrase at a time, and phrase at a time, and at a time, and a time, and time, and and one phrase at a time, and phrase at a time, and at a time, and a time, and time, and and phrase at a time, and at a time, and a time, and time, and and at a time, and a time, and time, and and a time, and time, and and time, and and and it is therefaré:a is therefaré:a therefaré:a is therefaré:a therefaré:a therefaré:a The source address of the data should be stored in the A2 base register, and the destination address of the data should be stored in the A2 base register, and the destination of the data should be stored in the A2 base register, and the destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination source address of the data should be stored in the A2 base register, and the destination address of the data should be stored in the A2 base register, and the destination of the data should be stored in the A2 base register, and the destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination address of the data should be stored in the A2 base register, and the destination of the data should be stored in the A2 base register, and the destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination of the data should be stored in the A2 base register, and the destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination the data should be stored in the A2 base register, and the destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination data should be stored in the A2 base register, and the destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination should be stored in the A2 base register, and the destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination be stored in the A2 base register, and the destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination stored in the A2 base register, and the destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination in the A2 base register, and the destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination the A2 base register, and the destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination A2 base register, and the destination base register, and the destination register, and the destination and the destination the destination destination base register, and the destination register, and the destination and the destination the destination destination register, and the destination and the destination the destination destination and the destination the destination destination the destination destination destination address 4 4 4 the Al Al Al base register. If these are not phrase aligned addresses then they register. If these are not phrase aligned addresses then they If these are not phrase aligned addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they register. If these are not phrase aligned addresses then they If these are not phrase aligned addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they If these are not phrase aligned addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they these are not phrase aligned addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they not phrase aligned addresses then they phrase aligned addresses then they aligned addresses then they addresses then they phrase aligned addresses then they aligned addresses then they addresses then they aligned addresses then they addresses then they addresses then they shioild't¢e rounded down toa phrase toa phrase phrase toa phrase phrase phrase boundary, and the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The and the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The and the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The the offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The offset (in the pixel size set) from the phrase bogindary writtes into the X pointer. The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The (in the pixel size set) from the phrase bogindary writtes into the X pointer. The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The the pixel size set) from the phrase bogindary writtes into the X pointer. The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The pixel size set) from the phrase bogindary writtes into the X pointer. The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The set) from the phrase bogindary writtes into the X pointer. The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The from the phrase bogindary writtes into the X pointer. The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The the phrase bogindary writtes into the X pointer. The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The phrase bogindary writtes into the X pointer. The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The writtes into the X pointer. The into the X pointer. The the X pointer. The X pointer. The pointer. The The into the X pointer. The the X pointer. The X pointer. The pointer. The The the X pointer. The X pointer. The pointer. The The X pointer. The pointer. The The pointer. The The The Y pointer should be set to zero. should be set to zero. be set to zero. set to zero. to zero. zero. should be set to zero. be set to zero. set to zero. to zero. zero. be set to zero. set to zero. to zero. zero. set to zero. to zero. zero. to zero. zero. zero. OE The length of the block should be stored in the innel length of the block should be stored in the innel of the block should be stored in the innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel length of the block should be stored in the innel of the block should be stored in the innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel of the block should be stored in the innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel the block should be stored in the innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel block should be stored in the innel be stored in the innel stored in the innel in the innel the innel innel be stored in the innel stored in the innel in the innel the innel innel stored in the innel in the innel the innel innel in the innel the innel innel the innel innel innel Sounder =the =the =the number represents‘thé ‘hizmber of pixels, so represents‘thé ‘hizmber of pixels, so‘thé ‘hizmber of pixels, so ‘hizmber of pixels, so of pixels, so pixels, so represents‘thé ‘hizmber of pixels, so‘thé ‘hizmber of pixels, so ‘hizmber of pixels, so of pixels, so pixels, so‘thé ‘hizmber of pixels, so ‘hizmber of pixels, so of pixels, so pixels, so ‘hizmber of pixels, so of pixels, so pixels, so of pixels, so pixels, so pixels, so so the largest block that can be copied block that can be copied that can be copied can be copied be copied copied largest block that can be copied block that can be copied that can be copied can be copied be copied copied block that can be copied that can be copied can be copied be copied copied that can be copied can be copied be copied copied can be copied be copied copied be copied copied copied is 32767 32767 32767 pixéis;wherewherewhere 32+bit pixels are set this is 128K: For smaller set this is 128K: For smaller this is 128K: For smaller is 128K: For smaller 128K: For smaller For smaller smaller set this is 128K: For smaller this is 128K: For smaller is 128K: For smaller 128K: For smaller For smaller smaller this is 128K: For smaller is 128K: For smaller 128K: For smaller For smaller smaller is 128K: For smaller 128K: For smaller For smaller smaller 128K: For smaller For smaller smaller For smaller smaller smaller blocks it is usually easier to it is usually easier to is usually easier to usually easier to easier to it is usually easier to is usually easier to usually easier to easier to is usually easier to usually easier to easier to usually easier to easier to easier to work in bytes. The in bytes. The bytes. The The in bytes. The bytes. The The bytes. The The The Outer counter shotild bé:set to one. shotild bé:set to one. one. shotild bé:set to one. one. one. The Blitter needs to be told how to update the pointeis Blitter needs to be told how to update the pointeis needs to be told how to update the pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis Blitter needs to be told how to update the pointeis needs to be told how to update the pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis needs to be told how to update the pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis to be told how to update the pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis be told how to update the pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis told how to update the pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis how to update the pointeis to update the pointeis update the pointeis the pointeis pointeis to update the pointeis update the pointeis the pointeis pointeis update the pointeis the pointeis pointeis the pointeis pointeis pointeis after each read each read read each read read read aiid Write cycle, so the add control bits Write cycle, so the add control bits cycle, so the add control bits so the add control bits the add control bits add control bits control bits bits Write cycle, so the add control bits cycle, so the add control bits so the add control bits the add control bits add control bits control bits bits cycle, so the add control bits so the add control bits the add control bits add control bits control bits bits so the add control bits the add control bits add control bits control bits bits the add control bits add control bits control bits bits add control bits control bits bits control bits bits bits are set to zero to indicate phrase mode in both addréss flags set to zero to indicate phrase mode in both addréss flags to zero to indicate phrase mode in both addréss flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags set to zero to indicate phrase mode in both addréss flags to zero to indicate phrase mode in both addréss flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags to zero to indicate phrase mode in both addréss flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags zero to indicate phrase mode in both addréss flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags to indicate phrase mode in both addréss flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags indicate phrase mode in both addréss flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags phrase mode in both addréss flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags mode in both addréss flags in both addréss flags both addréss flags addréss flags flags in both addréss flags both addréss flags addréss flags flags both addréss flags addréss flags flags addréss flags flags flags registers. HEE Having set these, set these, these, set these, these, these, a command command command is stored stored stored ti thé command register,.with the SRGEN bit set to enable source register,.with the SRGEN bit set to enable source the SRGEN bit set to enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source register,.with the SRGEN bit set to enable source the SRGEN bit set to enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source the SRGEN bit set to enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source SRGEN bit set to enable source bit set to enable source set to enable source to enable source enable source bit set to enable source set to enable source to enable source enable source set to enable source to enable source enable source to enable source enable source enable source reads, and the LFUFUNC bits set to and the LFUFUNC bits set to the LFUFUNC bits set to LFUFUNC bits set to bits set to set to to and the LFUFUNC bits set to the LFUFUNC bits set to LFUFUNC bits set to bits set to set to to the LFUFUNC bits set to LFUFUNC bits set to bits set to set to to LFUFUNC bits set to bits set to set to to bits set to set to to set to to to 1100 to'select. source data: data: data: Efthe.source4@'not phrase aligned,the.source4@'not phrase aligned,4@'not phrase aligned, phrase aligned, aligned,the.source4@'not phrase aligned,4@'not phrase aligned, phrase aligned, aligned,4@'not phrase aligned, phrase aligned, aligned, phrase aligned, aligned, aligned, then the the the SRCENX bit must be set. bit must be set. must be set. be set. set. bit must be set. must be set. be set. set. must be set. be set. set. be set. set. set. ae Hee 

| 

1 7 Jaguar Software Reference Manual - Version 2.4 Page 65 4 : The largest width value allowed is the last value one in this table - the smallest width is one phrase in the @ «current pixel size. The width must always be a whole number of phrases in the current pixel size. : Rectangles are blitted like a raster scan, i.e. a line of pixels is transferred, then the pointer advances one line a and transfers the next scan line of the rectangle. This jump from the end of one line to the start of the next is = given by the step value. If pixels are being transferred one at a time, then the step. value for X is the window | width minus the rectangle width. If pixels are being transferred one phrase,at4 timié, ‘Bien the X pointer is left @ pointing at the start of the next phrase after the end of the block, and so the'step valué'shoaitdbe reduced 1 Clipping may be performed by the Al address generator, and simply prevents writes occurring ‘at addresses Z outside the window boundaries, i.e. X or Y either negative or grater than the widow size. The windowisize is & programmed in the Al window size registers. This is not much faster than writitig {hé-clipped pixels, soif a § _large number of pixels are to be clipped then it is worth performingthe clipping at ‘higher-level. AEE Character painting is a particular example of a class of operations requiring bit #8 pixel expansion. As well as 1 character painting, this may include such things as:ba¢kground patterns, simple texture fills, etc. When bit to pixel expansion is being performed, hie sourcé data 18.used as a bit mask. Bits are extracted from the source data and if they are set then the corresponding pixel is:paitited in the currently selected output data form, if the bit is clear then either the pixel is leftianchanged, or a background colour is written. "7 This allows character painting to paint the charactéts Gily, leaving the batkgtound unchanged (if the destination data is read), or with another:ealour writ **t** he. ‘paper’et6 areas (pré-loaded into the destination | Character painting can be performed one pixel ‘at’ time.in all sctéen modes, and can also be performed one phrase at a time in eight and sixteen:bit per pixel: odes: The bit selection counter is reset every time ihe dnner loop is left, so bit packed data patterns may be up to eight pixels wide. cee 

- The Blitter can rotate and Scale intageéias a single operation. Consider takinga rectangular image and okiting it into a window. ° The bounding:rectangle of the rotated image is calculated in the destination window. . This rectangle is fi¢n transformed into the source image co-ordinate system. . “ADs used as the destination address register and performsa raster scan over the bounding rectangle, pixel-by-pixel. The width arid height of the blit are given by the size of this bounding rectangle. 

- ° Al perforzis.a scan over thé: Source image, with the increment integer and fraction set up to describe a scan over thefirst.line ofthe:translated bounding rectangle. The step and fraction parts then translate it to the start of thenext'scan. 

- iJ . onlyClipping be enables is generated when when A1 lies A1 withinis outsidethe bounds the boundsof the ofsource the sourceimage, image, soclipping thatthe writesrotated atform A2 will . correctly. 

Consider as an example, a 12 pixel square image starting at (10,10) in a window. We would like to rotate this image clockwise by 30 degrees, make it larger by a factor of 1.3, and move it across by 30 pixels. 

**==> picture [1 x 17] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. Confidential Information“7O® Property of Atari Corporation June 7, 1995 

~ ae _ a a ij : Ve i Page 66 Jaguar Software Reference Manual - Version 2.4 | i 1| programFirst it is below necessary shows to transpose how to do the square'sthis: co-ordinates into the target co-ordinate system. The basic :» im | 100 deg30 = .523598775 7 110 PRINT “Co-ordinates? " ] J 120 INPUT xi, yi ' 130 x = xi - 16 : 140 y = yi - 16 of hed Ellin. | : 150 xs = (x * COS(deg30)) - (y * SIN(deg30)) eae CC ] t 160 ys = (x* SIN(deg30)) + (y * COS(deg30)) eee OTHER | i 170 x = xs * 1.3 ee cece ; 210 PRINT "Translated: ", INT(x + .5), INT(y + -5) 0 “SHess.| ce Er This translates the vertices of the square as follows: oe Ee oe | : (10,10) -> (43,5) Eee, OEE” | i (21,10) -> (56,12) SEE = | | (21,21) -> (48,25) oan | The bounding box is therefore from X = 36 to 56, and-¥:%.9.to25. The vertices of titig ate.then translated ij back to the source co-ordinate system, as shown by:anethexbasic. program: CHEE g 100 degm30 = ~.523598775 ees OEE : i 110 PRINT "Co-ordinates? " “HAE aceeterem 4 } 130 x = xi - 46 oo a I 140 y = yi = 16 “Ee "8 | 150 x= x / 1.3 hein. WEES bat wo yey /13 0 ge ee 7. 170180 xsys == (x(x ** COSSIN(dégm30))(degim30)) —"Mtybr+ (¥EF COS(degm30}}iigne”SIN (deQEgQ}Jissasiiiy =| i **2** 1000 y=ys+16PRINT "Reverse tramslatedt”,geINT(x"#255), INT(y + .5) a i This translates the vertices of the bounding box as follows: Hee : j (36,25) -> (49726) 4] | We then set up Al as the source address register, making its window base the top left hand corner of the ] source image,:and-its window size the image'$izé;The A1 pointer will traverse the translated bounding box. rr 4 | Gourdud Shadingand 2 Buffering OU | Gouraud shading is a simple techitiqiie for modelling lit curved surfaces, which are represented bya series of ; ’ polygons. To'make.the surface appear curved, the intensity must vary smoothly, rather than being uniform = over each polygon: {36uraud shading #pproximates to the appearance of the curved surface by computing the PF intensity at each vertex; using a veriéx normal, and some suitable illumination model. The vertex intensity is , | ‘, then linearly interpolated'across'thepolygon edges, and the edge intensities are linearly interpolated across rf ; the polygon scan lines. -_ j 7 Gouraud shading is only an approximation to the appearance of the curved surface, and may appear unnatural F where there are large intensity changes across single polygons. However, it is much more attractive thannot «4 q graduating the shading at all. Better shading can be achieved with Phong shading, where the normals are 4 q 7 interpolated, but this is much more computationally intensive, and is not feasible within the Blitter. 4 1 | © 1992-95 Atari Corp. Confidential Information “JER Property ofAtari Corporation June 7,1995 3 ’ 

| 

= 

| Jaguar Software Reference Manual - Version 2.4 Page 67 ® 7-buffering involves attaching a Z value attribute to each pixel, which corresponds to how far away it is from - the observer. When pixels are drawn on the screen, their Z values can be compared with the Z of the pixels already there, and the existing data preserved if closer to the observer. Z-buffering therefore provides a simple | means of achieving hidden surface removal. The Blitter can perform Gouraud shading and Z-buffering in sixteen bit pixel modeonly. Each blit creates one | scan line of a polygon, with the graphics processor responsible for re-calculating t¢ Start, length and gradient | parameters for each scan line. Four pixels and their associated Z values caii:be calculated! as:fast as the memory interface can write them out, so the bus rate is always the limiting:£actor. HEE | To calculate the Z and intensity values, the Blitter contains registers which represent the Z and intensity with a sixteen bit integer and sixteen bit fractional part. The intensity integer also €dittains the colour valtié;:80 | intensity is prevented from overflowing into the colour information. The TOPBEN ad TOPNEN bits:enable | There are four of these thirty-two bit values for intensity, and four for'Z, so that four pixels tnay be eatculated in parallel. There are also thirty-two bit Z and intensity incrementtepisters;:which give the amount added to each pixel for each write. ae OSE At each pass round the inner loop; the sixteen-bit fractional part of the intensity increment is added to the fractional parts of the intensity values, held in the source:data.register. Then the eight-bit integer part of the intensity is added with carry out of the fractionaiadd to the #Meger pixel values in the pattern data register. : BothCarry the is prevented intensity and from the propagating Z values saturate. from intensity This:ttieans to colour.that if A:siilar they reachmechanismtheir lowestgoverns Z. or highest values they jg ate clipped there, rather than wrapping round. For‘exainple, adding one toa'#, value of FFFF hex will give : FFFF, not the overflow result 0000. ages. CHEER HEE To take an example, consider blittifig an 18 pixel-strip of Goutatid shaded. 2-buffered pixels. The Blitter command registers would be programmed as follows (all other registers need not be written). Address registers are set up as follaws: = Al_BASE 0x01600008° Tne window basé atidress Al PITCH 1 Pixel data and Zkdata alternate Al PSIZE Hed 16-bit pixels 22° Al _ZOFFS “En, 2 data is one pk¥ase up from pixel data Al WIDTH “Goes 20-pixélwindéwi' 1.01 x 2°4 = 0100 01 A1_ADDC GEHEHE ES Add one pHraSé”to address Ai_WIN_X 20° lunees. Window width Al WIN_Y ES “aeeewWindow height Al PTR_X 1 ““omvpst pixel at address 0,1 Al_PTR Yiguisiie,, 0 Receee Data registers aré'sét up’assuming the first pixel fias an intensity of C7.2833, and a colour of 00. The intensity gradient:is minus 15.9265:The values for the first four pixels have to be set up (the left-most is actually off the edgeOf the strip, so theintensity gradient is subtracted from it). Similarly, the Z of the first pixel is E7E7.E000)and the Z gradient'is Minus 1818.1FFF. Pattern “2 Bepc00C700B1 069: Intensity integer parts and colour data Source “EBRDCRACT7D6B1C23E:, Intensity fractions Source 21 FREFETEICFCFBIB? Z integer parts Source 22 FFFFEOS96OO2A002 Z fractional parts I Inc FFAQB66C@ 22tntensity increment (four times minus 15.9265) w Z Inc SFOFBO04 Z increment (four times minus 1818.FFFF) Control information is set up as follows: Inner count 18 Strip width Outer count 1 Single pixel high strip DSTEN 1 Read destination data, to restore if necessary DSTENZ 1. Read destination Z, to compare with computed Z © 1992-95 Atari Corp. Confidential Information “PO® Property of Atari Corporation June 7, 1995 

June 7, 1995 

**|** i || a rei 1 

- Version 2.4 & ; 

ok}q : _ 4 . 4 & 

Page 68 Jaguar Software Reference Manual DSTWRZ 1 Write destination 2, restoring or replacing CLIP_AlGOURD 11 ClipGouraudwithindatawindowcomputation enabled GOURZ 1 Z buffer data computation enabled PATDSELZMODE 13 WriteOverwritepatternexistingdata data if the new Z value is greater than or equal to the existing Z value The numbers here are pretty arbitrary, but they show the general idea. es 

j 

© 1992-95 Atari Corp. 

Confidential Information “JER Property ofAtari Corporation 

June7,1995 

b 

Page 69 

, 

| | | | 

- | Jaguar Software Reference Manual - Version 2.4 4: =ri‘<Ci<*™iCOtOiOCOCOCOCXCOVCSCCYCi;itza.:CWitiw.:«CUCCizCNCU;i‘iC(i‘CSCO(O(Oiti(‘aHri‘<Ci<*™iCOtOiOCOCOCOCXCOVCSCCYCi;itza.:CWitiw.:«CUCCizCNCU;i‘iC(i‘CSCO(O(Oiti(‘aH Jerry is the companion chip to Tom in the Jaguar games console. Jerry provides the following functions: * Asecond RISC processor (DSP) principally intended for sound oe ee synthesis. a 

- | © Frequency dividers for clock synthesis. ce WEEE * Two programmable timers. TEE EERE 

- | © Stereo PWM DAC (requires few external components). THEE THEE ¢ Synchronous serial interface and baud rate generator (12S). OEE eee * Asynchronous serial interface and baud rate generator (ComLynx). OTEEEES EES 

- | © Six general purpose IO decodes ee CREE * Two DMA channels (by way of DSP interrupts). EP OE 

- | Jerry occupies a 64K byte slot in Jaguar's address space. It appears as a 16 Bil part.(as does all 10). The DSP however is a 32 bit processor so all transfers to theDSP.are done in pairs. OBER 

=ri‘<Ci<*™iCOtOiOCOCOCOCXCOVCSCCYCi;itza.:CWitiw.:«CUCCizCNCU;i‘iC(i‘CSCO(O(Oiti(‘aHri‘<Ci<*™iCOtOiOCOCOCOCXCOVCSCCYCi;itza.:CWitiw.:«CUCCizCNCU;i‘iC(i‘CSCO(O(Oiti(‘aH 

## Frequency dividers g§ = «== cc 

|_||Jerryisresponsibleforthesynthesisofthreeimportiatclocks. i|
|---|---|---|
|y||Chromaclock.<br>Thisis 4.43 MHz forPAL<br>and 3.58MHzforNYSEandshouldhavea50%duty|
|||Videoclock.<br>Thisig a multipleof the pixel clock (which 1S typicallybetween6MHzand12<br>MHz)‘and must be tiététo theehroma clock in order toavoid the "wood grain|
|||<br>||Processorclock.<br>Thisdeterminesthespeedofthemesiory interface, thegraphicsprocessor, the<br>24objectprocessorandthedigital sound processor. Thisclockisdividedbytwoto<br>“HfBtovideaclockforanexternalprocessor.|
|||Threeregisters control the clock logi¢ tiJerry.Theratiobetween thevideoclockandthepixelclockis<br>determinedbyTOM.<br>WEEE|



## CLKY =~ Pipeessorciock divider = F010 ss WO Do NOW Modify: Forinformation only, 

This register only used if the progegsor clock is generated by PLL. This ten bit register determines the frequency ratia: between the processéf'clock oscillator input (PCLKOSC) and the processor clock divider output (PCLKDIV); §8:PLL clock synthesis PCLKDIV is typically locked to CHRDIV so the processor clock frequency willbe 9 “22222 eueete 

## (N+1)*CHRDIV 

y whereN is the value written to this register. This register is initialised to one on reset. The PCLKDIV output produces a pulse every N + 1 PCLKOSC cycles. 

a ©1992-95 Atari Corp. Confidential Information 7@® Property of Atari Corporation June 7, 1995 

Jaguar Software Reference Manual - Version 2.4 

| 

| | 

LY | g a | | ql g = f 4 | | j — SS : { | : : | 4 | 

2 ‘ i 

## Page 70 

DoNOTThis register Modif is onl **y** used: For if theinformation processor clockonly is generated by PLL. This ten bit register determines the frequency ratio between the video clock (VCLK) and the video clock divider output (VCLKDIV). As before in PLL clock synthesis VCLKDIV is typically locked to CHRDIV so the videoSlock.frequency will be whereN is the value written to this register. This register is initialised to zéieon reset. The VELRRIV output produces a pulse every N + 1 VCLK cycles. SHEE cen | Do NOT Modify: Forinformationonly This six bit register determines the frequency ratio between the chroma escillator (CHRIN, CHROUT) and she chromia:aséiilator frequency byN+1 | the chroma clock divider output (CHRDIV). The divider divides’ This register is 7 where N is the value written to the register. The CHRDIV output has a 50% dutyeyele. | initialised to 3Fh (divide by 64) on reset. ee. THEE The most significant bit of this register enables the chroma dscilbitoronto the VCLK pin. This bit is clear on Where PLL synthesis is used this register 1S typicablyleft as reset. This provides the lowest reference : frequency for generating PCLK and VCLK. EEE Be OEE , f For non-PLL synthesis the chroma crystiil 1s some smail'maliiple ofthe chroma carrier and this frequency is be used as the video clock. This register 3s written: with the apprepriate:-number to: generate the chroma frequency | on the CHRDIV pin and bit 15 is ¢et:to enable the erystal frequeney:onte He VCLK pin. Jerry contains two identical timers. Each consists oftwo sixteea bit dividers. The first stage (loosely called the pre-scaler) divides théprodessor clock by N + 1: The second stage divides this frequency by M+1, where It is therefore possible to achieve frequency 1 N and M are the values written #¢:their associated registers: division in the range four t¢ fourbuon... . The outputs of the second stages may be aset:to interrupt either of the digital sound processor or the external | It is intesided that tinter Gné-is used to generate the’Sample rate frequency for sound synthesis and that timer | two is used,to generate a‘twNgiG:tempo frequency. The timers may however be used for other purposes. It | should bé:soted that writing toadbe-associated registers presets the counters so they could be used to provide | programmable delays. Also the repisters are readable which can be used to measure time accurately. This might be used:in:deyvelopment to help: profile code or to help measure the time between joystick events. There are four registéts dssociated with the timers. The read addresses are different to the write addresses. 

ips ss Timer2Prescaler 10004 WO The pre-scalers divide the processor clock by N + 1 where N is the 16 bit value written to them. The prescalers are down counters which are loaded when the register is written and when they reach zero. They are © 1992-95 Atari Corp. Confidential Information JPR Property of Atari Corporation June7,1995 

Jaguar Software Reference Manual - Version 2.4 Page 71 readable, this is really for chip test purposes, but they might be used by the DSP to measure short events with 

Page 71 

| 

precision. 

pita. —sTimer2Divider = NOG WO These dividers divide the output from the corresponding pre-scalers by Ni: where ‘NS the.16 bit value written to them. The dividers, like the pre-scalers, are down counters whigh:are loaded wher tie.register is written and when they reach zero. cece ecco When they reach zero they may interrupt either of the DSP or the CPU. These isiterrupts are independently 

There are six interrupt sources which may interrupt the externiil microprocesssii: The interrupt sources are as 

## ) 

## ) 

- e External A rising edge on the EINT}O} input to Jerry may cause an intereapt. * DSP The DSP may generaté 4A interrupt by writing to a port. ia ¢ Timers Both timers may generate interrupts. “22%, ¢ Sync. The synchronous serial interface can generateingerrupts as described below. ° UART The asynchronous serial'interface can generate istezrupts as described below. It is likely that only one or two interflipt souldes would HotrBally be directed at the microprocessor. Some of the above are mainly of relevance:{a'the DSPin'sound synthesis, The Interrupt control register enables, identifies and acknowledges CPUinterrupts from the.six different interrupt sources. 

## siNTeTALornternipt{ControfRegister’ | F1og20" RW 

**==> picture [500 x 226] intentionally omitted <==**

**----- Start of picture text -----**<br>
Name Bit Description<br>P)EXTENA| _@.__| Enable external interupisis<br>Ty-TIMIENA| 22° | Endbig Timer One (sample rate) interrup's.<br>J TIM2ENA Enable TitHet:Two (tempo) interrupts.<br>J ASYNENA# 2: Enable Asyichraious Serial Interface interrupts.<br>J_SYNENA 8 Enable Synchronous Serial Interface interrupts.<br>_EXTELR PB | Clear pending external interrupts.<br>TDSPCLR,. | 9 | Cleat pending DSP interrupts.<br>TTIMICLR Cleat pending Timer One (sample rate) interrupts.<br>J_TIM2CLR ae7 Cleat pending Timer Two (Tempo) interrupts. |<br>J_ASYNCLR "Clear pending Asynchronous Serial Interface interrupts.<br>J SYNCLR Clear pending Synchronous Serial Interface interrupts.<br>**----- End of picture text -----**<br>


Bits 0 to 5 enable the individual interrupt sources. When read bits 0 to 5 indicate which interrupts are pending. Bits 8 to 13 clear pending interrupts from the corresponding interrupt source. © 1992-95 Atari Corp. Confidential Information “JPR Property ofAtari Corporation 

**==> picture [2 x 18] intentionally omitted <==**

**----- Start of picture text -----**<br>
;<br>**----- End of picture text -----**<br>


} 

June 7, 1995 

Jaguar Software Reference Manual - Version 2.4 

: 

_ Page 72 

) 

. 

= a | ; | a | | P| “ .ro” ‘ : : : ) ! : | | 

| || 

|| | | 

t 

| 

**==> picture [553 x 644] intentionally omitted <==**

**----- Start of picture text -----**<br>
The synchronous serial interface is controlled by seven registers. These are all within the local address space<br>of the DSP, and so may be accessed by the DSP without any external bus overhead. Other processors may<br>access them at these addresses. All transfers to them should be 32-bit, but the registers themselves are only<br>scuK:*oetwsenatciocerrequsneyi URIRRO WON<br>This eight bit register determines the frequency of the internally generated sé#ial:clock. The frequenay:is.<br>Serial Clock Frequency = System Clock Frequency / (2:%:(N+1)) EE Be<br>where N is the number written to this register. Es SEES UE<br>a, ae<br>-<br>Bit Name Description<br>FO) PINTERNAL When set this bff enables the serial clock and word strobe outputs.<br>RESERVED Seito zero. <<<br>2 |WSEN This bit enables the:generation of word siobe pulses. When set JERRY<br>producesa word ste6bé:qutput which is alfemnately high for 16 clock<br>farthercyekisaid [high] ‘tow [piiises.] for 16-eigckicycles. [ This] [bitis] [ignored.]  When'éieared [when] [INTERNAL]  Jerry will [ is]  not generate [ cleared.]<br>3 FRSnG iinetinterrupts Oi the rising edpe ofOtwert word strobe.seme<br>4 PFALLING | “Enables interapts on the falieng edge of word strobe.<br>5 EVERY WORD Enables interrupts on the MSE of every word<br>) Abbe transmitted or received. 5°<br>RIpAC™ Po Right transmitdata(to DACs) FAR<br>[pac _Lefitransmitdata (to DACs) FIAIC WOU<br>These two,sixtebit r gisters e n: hold data to be fraBsmitted. Note that these registers have right and left<br>swapped Si pUIpOSE: |. we<br>uno gy en vengigtaattor's) ENR WO<br>| HID |Rlghttransmitdata(oS) | FIA WOU<br>These two sixteeti bit segisters hold data to be transmitted.<br>/ RAXD light [recelvedata(from{’'s)] FIAM@C RO<br>| These two sixteen bit registers hold received data.<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. Confidential Information “JER Property ofAtari Corporation 

June 7, 1995 4 

Page 73 INO TRO 

| 

Jaguar Software Reference Manual - Version 2.4 estate Ses sms 

**==> picture [514 x 310] intentionally omitted <==**

**----- Start of picture text -----**<br>
Bit Name Description<br>Ws This bit reflects the state of the Word Strobe pin. Do not use this to check for data |<br>ready, use the Interrupt control register.<br>| Aeyachronous Serial Interface (ComLynxand Mig)<br>The asynchronous serial interface consists of two wires, UARTI, the receive dab input and UARTO the,<br>transmit data output. This interface is primarily designed to support ComLynx btidt'canalso be used ifr<br>A prescaler register is used to allow programmable baud rates. ee “EEE<br>The data transmitter is double buffered, allowing a character ibe‘written isité-the data register before the<br>transmission of a previously written character is complete. The data receiver #449. double buffered, a second<br>character can be received on the UARTI pin before.she:previous character has béé#:readfrom the data<br>Data is both transmitted and received in the fossnat shown below;<br>Start j------------ 8 Dake Biteih-----“REE eRarity SE6p<br>**----- End of picture text -----**<br>


The parity can be ODD, EVEN oe lone. The polarity GF both the output and the input can be programmed to be active high or low. The polarity:shown is active Ow. sees. Two classes of interrupt can be genetated by the asynchronotig serial interface, namely receiver or transmitter interrupts. Each of these classes can be individually enabled. The table below summarises the interrupts in each class. OEE be. ee Receiver Interrupts. ee OEE . Parity Error ee EEE . Framing Error _ . "Receive Buffer Fails. Transmitter Interrupts 3 - Transit Buffer Empty 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


| 

© 1992-95 Atari Corp. 

Confidential Information “JER Property ofAtari Corporation 

June 7, 1995 

' 

: - Page 74 Jaguar Software Reference Manual - Version 2.4 | ASIC”K °° ‘Asynchronous Serial interface Clock = = 10084 RAW " This sixteen bit register determines the baud rate at which the asynchronous serial interface works. The g\ frequency generated is given by: Clock Frequency = System Clock Frequency / (N+1) where N is the number written to this register. ee, | The frequency generated by this register is further divided by sixteen to give the baud rates Se, | 4 | ASICTRE << "Aeynchronciis Serial Control Fieode WO| - tsié@ Bits Name Description i | 0 [ODD Writing a 1 to this bit selects odd parity’: CHEB ont a 1 PAREN Parity enable. When parity is disabled:the: value of the EVENbit is:franszitted | | in the parity bit time. BEEP SUE g 2 |TXOPOL Transmitter output polarity. Setting'this bit to aGe:causes the UARTO output to Pf | be active low. HEE P| 3 | RXIPOL Receiver input polarity: Writing:a.one to this bit makes thé: LARTI into an = | 4 TINTEN Enables transmitter jaterrupts. Note that the asynchronous serial interface bit in | the Interrupt Controk:Register also needs'#) bé:set to enable interrupts. ; 4 | 5 | RINTEN Enables receiver intertiypts..As for TINTEN the:asynchronous serial interface bit 4 in the Interrupt Control Régister must also be set: CLRERR Clear Errat:: Writing a one to'thisbit clears any patity, framing or overrun error 1 conditigte FEES, eee 14 |TXBRK Transit break. Setting this bit causes @-bréak level to be transmitted on the , iz UARTG pin. It forcesthe UARTQ output active. This may be high or low 7 H dependitig'dn the state[of][ the][ TROPOL][ bit.] | @ | All unused bits are reserved and should be written 0 ES | 1 | ASISTAT “ Aeynchisnous SeriaiStats= = Fi0032 FO | Bitsa Nameeee | TheseDescriptionbits:réflect the state of the corresponding bits in the ASICTRL —, =4 | 7 =YRBF "258%, | Receive buffer full. When set this bit indicates that a character has been | 4 | ee “ells[|][ received][ and][ is][ available][ in][ the] ASIDATA[ register.] | ; 4 9 |PEs. [Parity Error. This bit indicates that a parity error occurred onareceived | § : SHEED character. 4 10 [FE eee Framing Error. A framing error is detected when a non zero character is ‘ ' “HEELEcEteelgeseived without a stop bit at the expected time. — | 11 | OE “=| Overrun Error. An overrun error is detected when a character is received 4 : { on the input before the last character was read from the ASIDATA q i register. ] ' 13. | SERIN Serial Input. This bit reflects the state of the UARTI pin. Its sense can be : i inverted by setting the RXIPOL bit in the ASICTRL register. 4 q | © 1992-95 Atari Corp. Confidential Information PU™® Property of Atari Corporation June 7, 1995 | 

| \ 

“ 

Page 75 q | Jaguar Software Reference Manual - Version 2.4 . . 14 Transmit Break. This bit reflects the state of the corresponding bit in the ~? ASICTRL register. a 5 ERROR Error. This bit is logical OR of the PE, FE and OE bits. This allows a g single test for error conditions. BH All unused bits are reserved and may return any value. ee. aa ae | When this register is read it returns the last character received in bits [0.7] aadzero in bits (8..15]. Tie act of reading this register clears the receive buffer ful! condition leaving the way cléay f5x,subsequent characters to When the ASIDATA register is written bits [0..7]} are transmitted fr6m,the UARTO pin Bits {Bed} are not j used and should be written as zero. ee WEEE | ec ee .LlLlFPFEn Jerry has four outputs which together control fgur external FELAICs to provide the joystick interface. There are two registers ae WEEEEEEE “ When read the joystick input buffers are:enabied and the data:reflects the staté of the sixteen joystick inputs. the read. EE ee Output JOYLO is asserted (activé:low) during When written the low eight data ‘its are latched ints the jaystick output latch. Output JOYL2 is asserted (active low) during the write. The tiost signifiéant bit (15345 tised to enable the joystick outputs. This bit is[15.] cleared (disabled) by reset. Output JGYL3 is the inverse of the[¥alue][ in][ bit] JOY’ut wo When read the button itiput buffer is:enabled and the data reflects the state of the four button inputs. Output JOYL! is asserted (active low) during the read. 

**==> picture [1 x 29] intentionally omitted <==**

**----- Start of picture text -----**<br>
;<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. 

Confidential Information “JPR Property ofAtari Corporation 

June 7, 1995 

| | py | Gaumibapessiobecsdes j Jerry has six general purpose 1O decode has six general purpose 1O decode six general purpose 1O decode general purpose 1O decode purpose 1O decode 1O decode decode outputs which are asserted (active low) in the following address 

. A | | | 

) 

| 

i ) | 

1 

June 7, 1995 | : 

Jerry has six general purpose 1O decode has six general purpose 1O decode six general purpose 1O decode general purpose 1O decode purpose 1O decode 1O decode decode outputs which are asserted (active low) in the following address 

> ranges.GPIO0 |F14800-Fi4FFFh = | RESERVED 

es 

GPI02 |F16000-FIOFFFh |RESERVED — <7 GPl04 | F17800-F17BFFh RESERVED THE “EEE The term “General Purpose” is a misnomer because most of the outputs afé'teserved. =_— 

i 

© 1992-95 Atari Corp. _ Confidential Information “7U® Property ofAtari Corporation 

| 

| 

q Jaguar Software Reference Manual - Version 2.4 Page 77 7 pp | mm. LL 4 Theinstruction DSP is partset and of programming the Jerry chip model, in Jaguar, but and there are is a variant certain of differences. the GPU within“fhe Tom.DSP has It uses full atéess'to avery similarthe system memory map as a bus master, and its internal memory may be accessgd:by the other bus Triasiers 1 The DSP performs two réles within Jaguar, its primary functigti:is sound synthesis aid it-may also be = available for additional graphics processing. Ee TEE ites cael i Sound synthesis may be the playback of sampled sound or algorifhitiie Sdund generation, or a mixture of the two. As the DSP is a fast general purpose processor it may be used for abroatt-range of synthesis techniques. ' It contains several optimisations for sound processing when compared to the GPU;.in particular higher precision multiply / accumulate operations, circular.buffer management, audio wave tables in local ROM, additional local fast RAM, and audio output hardware withist its internal address spaces!!! As many sound generation techniques will not sequire anything: ike'the full power of the DSP, it may also be used as an additional graphics processor. It has:fui access to the efitire:system address space, although its bus bandwidth is lower as it has a 16-bit interface to’éxtérnal memory. It miightwell be used with sound synthesis kg occurring under an interrupt at sample rate, with the[uaderlying][ code performing something][ like][ matrix] HA = multiplies for 3D object rotation. ..f:8fHibe.. WEEE Ee This section assumes an understafiding of the GPU, and outlines thie: differences between the GPU and the i=LL . Refer to the 'Programming:the Graphics Processor!:section inthe GPU description. 

re Refer to the: ‘Design Philosophy’ section on the:GPU description. | ce Co ee Refer tothe ‘Pipe-Lining’ section onthe GPU description. 

© 1992-95 Atari Corp. 

Confidential Information “JPR Property ofAtari Corporation 

June 7, 1995 

: | | ff | | | ] | | : ; q | 

Page 78 

Jaguar Software Reference Manual - Version 2.4 

: 

4 

i 

| | | | : : : ' ij 

. i‘iéié‘éQ j ; P| 

. 7 | : | | 

J 1=. 

## MemoryyMapRefer to the the 'Memory 

Refer to the the 'Memory Interface' section of the GPU description for a discussion of the basics of the DSP memory interface. Thewith DSP has 8K bytes of local fast RAM (twice as much as the GPU), and 2Kbytesof wave tables to help sound synthesis. These are laid out as follows: Ee. FIA000-FIAIFF DSP control registers oa 6h F1B000-FICFFF local RAM ae _— 

## WaveTableROM = 

The wave table ROM contains eight 128 entry wave tables. These ase Signed 16-bit values; and ai'Siga" extended to 32-bits, so that the ROM appears to occupy 1K 32-bit:locatigns:Only the bottom 16bits are significant. oe 

**==> picture [535 x 124] intentionally omitted <==**

**----- Start of picture text -----**<br>
The waves available are as follows: ee _ :<br>F1D000 ROM_TRI A triangle wave, Ee '<br>F1D400 ROM_AMSINE| An amplitiide modulated SINE wave ij<br>F1D600 ROM_12W A sine wavé:and its second order harmonic<br>F1D800 ROM_CHIRP16 | A chirp - this'i§'a'sine wave increasiiigin frequency<br>F1DA00 ROM_NTRI Astriangle wave with:figise superimposed .<br>es<br>FIDCW ROM DELIA Agi,<br>**----- End of picture text -----**<br>


Refer to the ‘Load and Store Operations' section ofthe GPU description. 

> ArthmeticFunctonsse rr Refer to the ‘ArithmeticFunctions’ section of the GPU description. The DSP réjilaves the unsigned saturation funetigas of the GPU with two signed operations. SAT16S takes a signed 32-bit operatid:and saturates it to a signed'16-bit value, i.e. if it is less than $FFFF8000 it becomes SFFFF8000 and if it isgréatei:than $00007FFF it becomes $00007FFF. SAT32S takes a signed 40-bit signed 32:bioperand {see thevalue sectionin a beléw:exititledsimilar maniter. 'Extended Precision Multiply / Accumulates') and saturates it to a 

**==> picture [1 x 6] intentionally omitted <==**

**----- Start of picture text -----**<br>
q<br>**----- End of picture text -----**<br>


q 

© 1992-95 AtariCorp. Confidential Information PER Property of Atari Corporation 

June 7,195 fi 

Jaguar Software Reference Manual - Version 2.4 

j 

| 

| 

**==> picture [34 x 26] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 79<br>**----- End of picture text -----**<br>


Refer to the Interrupts’ section of the GPU for a general discussion ofhow DSP interrupts behave. There are six interrupts sources within the DSP. These are allocated as follows: 

The external interrupts are inputs from additional Jaguar hardware ouside the Tom & Jerry system: The timer interrupts are from Jerry's local programmable timers, the PS interrupt is:from the local synchronous serial interface, and the CPU interrupt is generated by any processor Writing to thé DSP.control register. | Se ee Refer to the ‘Program Control Flow’ section of the. GPU description. 1@ Growler Butler Management So 6 As circular buffers are common ig DSP algorithins, for samiple-lodping, EIEOs, and so on; there is hardware and aligned to a 2" boundary, where n support for addressing circular bisffers. These have ta-he.2" words'loug: is any practical value. Tee [=F The support takes the form of two variants ofADDQ and SUBQ, namely ADDQMOD and SUBOMOD. These allow pointers to be updated with the value wrapping it: the form of counting modulo 2°. This is controlled by the modiila:zegister which is a mask on the result.of these instructions. Where a bit is 1 in this register,may modify the result it. Normally of theADDOMODthe high: bits of or SUBOMODiis this register are'setunaffectedto one, by and the the instruction, low bits set to where zero[it] is as appropriate. 0 the add Extended Precision Multiply /Accumulates 0 Refer td the ‘Multiply asd, Accumulate Instructions’ and the ‘Systolic Matrix Multiplies’ sections ofthe GPU description for an introduction to and explanation of these instructions. When muliiply and accumulate operations are performed, using the IMULTN, IMACN and RESMAC instructions, ‘ofthe MMULT instrisction, the accumulated result is actually calculated as a forty bit signed integer. Thejopeipht bits are effectively overflow bits, after a RESMAC, they are at F1A120. However, the SAT32S instruction takes as its forty:[bit][input][ the][ register][ operand][ as the][ low][ thirty-two][ bits][ and][ the][ eight] overflow bits of the accnmilator as tts top eight bits, and saturates the forty bit signed integer to thirty two bits; i.e. if it is less than FE80606000 it becomes FF80000000 and if it is more than OO7FFFFFFF it becomes .& OO7FFFFFFF. “ The SAT32S instruction should therefore only be applied to the result of a multiply / accumulate operation, and before any further multiply / accumulate operations are performed. The SAT16S instruction operates only on its thirty-two bit register operand and takes no account of the overflow bits. | © 1992-95 Atari Corp. Confidential Information JPR Property of Atari Corporation June 7, 7, 1995 

June 7, 7, 1995 

t Page 80 ' Refer to the ‘Divide the ‘Divide ‘Divide Unit' section of section of of the GPU description. GPU description. description. | oe | j Refer to the ‘Register File’ section of to the ‘Register File’ section of the ‘Register File’ section of ‘Register File’ section of File’ section of section of of the GPU description. GPU description. description. l Se i] Refer to the "External CPU Access’ section of to the "External CPU Access’ section of the "External CPU Access’ section of "External CPU Access’ section of CPU Access’ section of Access’ section of section of of the GPU GPUdescriptign. ii Addresses in DSP space are only available as 16-bit in DSP space are only available as 16-bit DSP space are only available as 16-bit space are only available as 16-bit are only available as 16-bit only available as 16-bit available as 16-bit 16-bit memory inté: Which 32-bit transfers 

a - 

Jaguar Software Reference Manual - Version 2.4 

| ) q ' Refer to the ‘Divide the ‘Divide ‘Divide Unit' section of section of of the GPU description. GPU description. description. | oe ee | | j Refer to the ‘Register File’ section of to the ‘Register File’ section of the ‘Register File’ section of ‘Register File’ section of File’ section of section of of the GPU description. GPU description. description. 6h 2 l Se eS Lr ] i] Refer to the "External CPU Access’ section of to the "External CPU Access’ section of the "External CPU Access’ section of "External CPU Access’ section of CPU Access’ section of Access’ section of section of of the GPU GPUdescriptign. ee ES ] ii Addresses in DSP space are only available as 16-bit in DSP space are only available as 16-bit DSP space are only available as 16-bit space are only available as 16-bit are only available as 16-bit only available as 16-bit available as 16-bit 16-bit memory inté: Which 32-bit transfers Hust Be-perfetmied in a the order low address then high address. ee OEE | # ij na. 4 piFLAcs* rsp riage Register! | (| BIAT00 “Readwrite & _ , _ ’ This register provides status and control bit for several important DSP functions. Control bits are: ] ' a Bits Equate(s) Description i ZERO_FLAG TRe:ALU zero flag, set £ theresult of thelast arithmetic operation was ed hot affect the flags, see above. 1 | 1 “geto. Certain:arithmetic instructoas:deby Carry/borrow out of the , 4 t 1 CARRY FLAG EThe ALU carty flag. Set-orcleared and reflects carry out of some shift operations, but it is not _ ie “gdder/subtragt, i defied after‘other arithmédi¢:gperations. t | i 2 NEGA_FLAG The ALU negative flag, set ithe result of the last arithmetic operation fo F Hb. was negative. _ Se Pi " 3. |IMASK ,, | Interrupt mask, S61 bythe interrupt control logic at the start of the service | ec soutine, and is cleared y'the interrupt service routine writing a 0.Writng | : G28 “leg Eto this location has no effect. i 4-8 |D_CPUENA~ Interrept-enable bits for interrupts 0-4. The status of these bits is | i by. IMASK. These bits correspond to: _ D,J2SENA overridden i EDETIMIENS 0 CPU EES SPD_TIMZENA"| DLEXTOENA™)=, **[** 12 PS”Timer] i7 on "| 4EINTIO] : 9-13 |D?€FUELR Interrupt latch clear bits for interrupts 0-4. These bits are used to clear the i D_I2SEER Es. interrupt latches, which may be read from the status register. Writing a : D_TIMICER: 2:.1..4%4er0 to any of these bits leaves it unchanged, and the read value is always | j |[zero.] i | D_TIM2CLR : D_EXTOCLR 7 14. |REGPAGE Switches from register bank 0 to register bank 1. This function is q overridden by the IMASK flag, which forces register bank 0 to be used. 7 © 1992-95 Atari Corp. Confidential Information ‘JER Property ofAtari Corporation June 7, 1995 : 

~ 

| 

| 

wW 

Jaguar Software Reference Manual - Version 2.4 

Page 81 

» |. 15 | DMAEN This bit must not be set due to a bugin the Jaguar Cagsote.. 16 |D_EXTIENA Interrupt enable bit for interrupt 5. Fuitefion[as][ bits][ 4-8.] “828%, D_EXT1CLR Interrupt latch clear bit for interrupt 5. Functiow'as.bits 9-13. “s WARNING- writing a value to the flag bits and making use of thasé'flag bits in the following Sisteuction will not work properly due to pipe-lining effects. If it is necessary USé:flags set by a STORE instruction, then ensure that at least two other instructions lie between the:5 FORE anid:{hé flags dependent instruction. If it is necessary to use flags set by an indexed STORE instruction, then ensure'that:atJeast four other instructions lie between the STORE and the flags dejséident instruction. eee BMTIXC — DSP Matrix Control Register F1A104—s Writeonly This register controls the function of the MMULT idstruction. Control biis'are: 

**==> picture [482 x 310] intentionally omitted <==**

**----- Start of picture text -----**<br>
MATRIX3-15 [oMatrbewidth, in the rangé3to15 228°<br>4 MATCOL 2g When set, this: control bit make:{he matrix held in memory be accessed<br>“4 down one colusti®;:4s opposed to along one row.<br>DMIXA” DSP Matrix Address Register, = FIA108 = Writeonly<br>This register determines swhere::in local RAM, ths saab held in memory is.<br>Bits Equate(s} - Description<br>P21 [— sd Matiicaddress,<br>BEND — “DSP Gate Orgahisation Register FIAIOC Writeonly<br>This register controls the physi¢aldayout of DSP I/O registers. If its current contents are unknown, the same<br>data shoukt bé-written to both thelow and high 16-bits. ,<br>Bit Equate(s) ‘Description<br>BIG IO 22228 e nat! "When this bit is set, 32-bit registers in the CPU I/O space are big-endian,<br>“SEE27 e. the more. significant 16-bits appear at the lower address.<br>2 BIG_INST processor.When this bit is set the DSP does word program fetches like a big-endian<br>**----- End of picture text -----**<br>


©1992-95 Atari Corp. Confidential Information “7% Property of Atari Corporation June 7, 1995 

**==> picture [591 x 733] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||
|---|---|---|---|---|---|---|---|---|---|
|Page 82|Jaguar|Software Reference Manual - Version 2.4|||
|BPO|DSP|Program|Counter’|FIATIO.|Read/Write|||
|The DSP program counter may be written whenever the DSP is idle (DSPGO is clear). This is normally used|mm|
|||by the CPU to govern where program execution will start when the DSPGO bit is set.|a|
|The DSP program counter may be read at any time, and will give the address of the instruction|currently|
|being executed. If the DSP reads|it, this must be performed by the MOVEPC,Ra|instruction,|and not by|Ss|
|The DSP program counter must always be written to before setting the DSPGO control|bit: When the DSPGO|,|
|bit is cleared, the program counter value will be corrupted,|as at this pointfhe|pre-fetch quewiéig:discarded.|2|
|pocTRL|=e|DSP|Controrstatis Register!”|FIAITA|||Readwrite!|Z|
|This register governs the interface between the CPU and the DSP...|Sa|fee|
|I|Bits|Equate(s)|Description|:|
|DSP may write to this|F|
|'|DSPGO|This bit stops and starts|the|DSP.|TheCPL:or|}|
|L|register at any time. The status of this bitafter#:system|reset may be|
|'|externally configured...|EEE|
|the|GPU.|There isno|||:|
|.|1|CPUINT|Writingneed for a any1|to:thisa¢knowledgé;Biticauses atid:no the DSPneed to to clear interrupt the bit|[to][ zero.][ Writing]|[a]|4|
|1|zero has noéffect. A value of|zereis.always read.|
|[type][ 0.][ There][ is][ no][ need][ for]|1|
|'|2|||FORCEINTO|Writing a|1 tathis|[bit][causes][ a][ DSPisterrupt]|
|a|any acknowledge,and|no need to cleat|thé: bit to zero. Writing a zero has|
|ibis|bis|is set DSP. sisgle-stepping:i8|enabled. This means that|7|
|'|3|||SINGLE_STEP|‘Wihen|
|i|f|program|exégution|will paiise|#ti@readts|mnstruction,|until|a SINGLE_GO|
|||| command|isissued.|
|i|‘EDhe|read staius|of this|fag,|SINGLE|STOP,|indicates whether the DSP|4|
|\|hag|dictually Sfpped, and'shiduld be polled before issuing a further single|||1|
|the|DSP is awaiting|a SINGLE_GO|||
|iy|step command.|A|one meaiig|
|fh|oe|command|Ee|
|||||
|\|4|||SINGLE _GO##:s:.|||Writing a one|to|this|bit.|[advances][ program][ execution][ by][ one][ instruction]|
|‘|Alec] when execution'is|paused in single-step mode. Neither writing to this bit|||
|i|||Eee|“t- atany other time, nor writing a zero, Will have any effect. Zero is always|4|
|6-10|.:DECPULAT|Interrupt|létches|for interrupts 0-4. The status of these bits indicate which|||
|!|-aED|SEAT:|
|ce _D_TIMILAT#::,,2%...||clearedinterrupt by requ th|e|stinterrupt latch is service currently routine, active, usi a|n|dg|the|appropriateINT_CLR bits in bit should the|be|]|
|||=D TIM2LAT|“EUELCOL|flags register. Writing to these bits has no effect. These bits correspond to:|||
|i|||OE|ct 3|Timer 2|
|||© 1992-95 Atari Corp.|Confidential Information|“JER|Property|of|Atari Corporation|June 7,195|§|

**----- End of picture text -----**<br>


! 

**==> picture [532 x 644] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 83<br>Jaguar Software Reference Manual - Version 2.4<br>" 11 BUS_HOG | Ginna the DUP is excouting code out of external RAM itwill normally |<br>This bit must not be set in the Jaguar Console,<br>12-15 |VERSION These bits allow the DSP version code:tg:be read. Cuggent.version codes<br>12 First production release HEB. ERE<br>|<br>: Future variants of the DSP may contain additional features or WEEE<br>| enhancements, and this value allows softwarét0'xemain compatibié: with<br>all versions. It is intended that future versions Wil: bé:a,superset of:fhis<br>Interrupt latch for interrupt 5: Has she.same function fcrimteereptS as bits<br>6-10 have for interrupts 0-45." OE<br>This 32-bit register holds the value which govertis which bits até [middified][ by][ the][ ADDQMOD][ and]<br>a: :theans that it may be changed.<br>SUBOMOD instructions. A 1 means that the bit will be unaffected,<br>Normally, the higher bits are set to 1 and the lowée Sits to 0. This allows:addresses to be readily generated for<br>a") circular buffers of size 2" bytes, where n is betwee#t 0: and 31. tee<br>: poneManesepnnasluniktonainlegiii” “igggpmiemeneatony<br>This 32-bit register contains a valug from which tie remeinder after a division may be calculated. Refer to the<br>section on the Divide Unit. “He HEE OE,<br>pcpverniscniaeannecantorggeag- “caetanierciwatdony*<br>~~ - Description<br>Bit Equate(s)<br>0 | DIV_OFFSEF’ “ETE flais,<br>bitauibers,bit is set, otherwise then the divide 32-bit unsigned unit performs integer division division of isunsigned performed. 16.16<br>D-WAGHI’“lananiply &/Accumulate High Bits FIAT20° Radon<br>This 32-bit register allows the high bits of the accumulated result to be read. After a RESMAC instruction the<br>result reguster of the RESMAE ¢aatains the bottom 32 bits of the accumulated value, and this register<br>contains thet6p.cight bits, which are:sign-extended to 32 bits.<br>In the DSP, certain peripheral 10 functions are mapped into the internal DSP space for higher efficiency when<br>the DSP is controlling them. These are effectively 32-bit locations. These are the PWM DACs and the<br>Synchronous Serial Interfaces 22°" ,<br>**----- End of picture text -----**<br>


© 1992-95 Atari Corp. 

Confidential Information FER Property ofAtari Corporation 

June 7, 1995 

. 

Jaguar Software Reference Manual - Version 2.4 

Page 85 

: | | | | 

: | 

| 

| Mmmummst GPU and DSP instructions are all sixteen bits, made up as follows: ae Oe * op code defines the instruction to be executed Ee OEE oe ° reg2 is the destination operand, or the only operand of singi¢:cpérand instructions “EEE * reg! is the source operand EE a The reg2 and reg] fields usually hold a register number, but have other meaningéwith some instructions. The instruction set is as follows, where the syntax'i8) 4. ee <Op code name> <source>,<destinatiga> — - CHE Note: To remain compatible with future versions of the Jaguar chipsetalways clear the reg! field of single | i operand instructions and leave both fields of NGP:éleared. “EEE 

The description of each instruction’indicates bow it affects the fags. The flags are valid when the result is written. This is discussed further:under “Writing Fast GPU arid DSH Programs”. Register Usage oe [2 The description of register usage shows whereit uses a register port. Cycle 1 is the clock cycle at which the instruction is considered to be “executing”, and is generally the:pipe-line stage at which its register operands are read. It is the only:pipe-line stage occupied byNOP. Wherg:an instruction affects the flags, these are valid at the clock cycle when!{he tesult is written. This#s discussed further under “Writing Fast GPU and DSP Programs”. Ey EEE EEE 

**==> picture [8 x 34] intentionally omitted <==**

**----- Start of picture text -----**<br>
bl<br>**----- End of picture text -----**<br>


**==> picture [497 x 161] intentionally omitted <==**

**----- Start of picture text -----**<br>
No. Syntax Description<br>22 |ABS<br>RE Absolute Value<br>ead eee 32-bit integer absolute value. Has the same effect as NEG if the<br>al OEEEEEE operand is negative, otherwise does nothing. Note that this<br>ce on WEEE instruction does not work for value 8000000b, which is left<br>ieee OEE unchanged, and with the negative flag set.<br>OEE “ae | Z- set if the result is zero<br>OEE Booed C - set if the operand was negative<br>| Cycle 1: Destination register read<br>| OBE Register Usage i<br>Cycle 3: Destination register write<br>**----- End of picture text -----**<br>


: 

© 1992-95 Atari Corp. 

Confidential Information JER Property ofAtari Corporation 

June 7, 1995 

| i ' : 4q | | | i f ' | 4 i. | 4 I a . : : q ' | q , | f | j i ‘ 4 | 

Page 86 86 

**==> picture [554 x 730] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|
|Page 86 86|;|Jaguar|Software Reference Manual - Version 2.4|g|
|0|||ADD|Rn,Rn|Add|a|
|32-bit two's complement integer add, result is destination register|.|a|
|contents added to the source|register contents, and is written to the|4»|
|destination|register.|||
|Z. - set if|the result is zero|
|N|- set if the result is negative|ete|B|||
|Cycle|1: Source register read|& Pestination regisiersead|i|
|Cycle 3: Destination register wre:|EEE|a|
|T||ADDC|RaRn|Add with Carry|Te|"||
|32-bit two's complement integer add'witlicarry in accordin#és.|—|||a|
|||the previous state of:the carry flag, otherwisellike ADD.|22|||a|
|||C - represents carry oil|of the adder||||=|
|Cycle 1: Source register read & Destinatidia register read|a|
|2|||ADDQ|n,Rn|Add|with|Quick|Data:|ag|||a|
|||32-bit fvo's complement iateger add, where the source field is|||gg|
|immediate data in the range|132, otherwise like|ADD.|g|
|PP Regier Usage 8|Be|el|||
|63.|||[ADDQMOD]|[n,Rn]|2s.|Add svithQuickData using Modulo Arithmetic|,|
|(DSP|only)|OE|| 32-bit|two's complement integer add like|ADDQ, except that the|||Ff|
|“ee|||result bits may be uBmodified data if the corresponding modulo|||=|
|HEE|register bits are set: Ehis allows circular buffer management (for|||rf|4|
|ee|||2n size Hubiers),;where|the high bits of the modulo register are set,|=|
|Eeece|os.|| and the low bits'left clear.|.|gg|
|4|
|ge|“Sls.|[|][ Z-][ set][ if][ the][ result][ is][ zero]|
|“EELUEN|- set|if the result is negative|q|
|"|G iepresents carry out of the adder|=|
|elie...|
|1|
|ae|EEE|Cycle|[T:]|[Destination][ register][ read]|
|one|OEE|Cycle 3: Destination register write|;|4|
|3.|EADDOT|n,Rn|WEEE|Add with Quick Data, Transparent|;|4|
|om|“||32-bit two's complement integer add, like|ADDQ except that itis|||||#|
|OE|“ce|| transparent to the flags, which retain their previous values.|.|
|“teati||Register Usage|P|
|SURE EEE|Cycle‘1: Destination register read|||||@|
|Cycle 3: Destination register write|}|4|
|© 1992-95 Atari Corp.|Confidential Information “FO® Property of Atari Corporation|June 7, 1995|i|

**----- End of picture text -----**<br>


q 

, | | 

**==> picture [538 x 767] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||
|---|---|---|---|---|---|---|---|
|—=—E|eee|
|Page 87|
|Jaguar Software Reference Manual - Version 2.4|
|32-bit logical AND, the result|is the Boolean AND of the source|
|)|9|||AND|Rn,Rn|Logical AND|
|register contents and the destination register contents,|and is|
|written back to the destination|register.|
|Z|-|set|if the|result|is zero|
|N|-|set|if the|result|is negative|git.|
|Cycle|1: Source register read &|Destination register|tead|
|Cycle 3: Destination register wrte::..|ecccom|
|15|||BCLR|n,Rn|Bit Clear|cece|secre|
|Clear the bit in the destination register'selected by the immediate|
|||||| dataof the destination in the source registet fied,|which|is in the rage Q.31.|The other|bits|
|7, - set if destination registerare unaffectedis:now|all zero oF“He|||
|N - set from bit 31 gfthe resus,|||
|C-notdefined|OEE|
|\|Cycle:4:|Destination|register read|CEs|||
|||Register Usage|oe|||
|i|| Cycles:|Destinatiox:|register write|Ee|
|Set the bit in the destinatian|gépister selected by the immediate|—||
|||i4|||BSET|o,Rn|Bit Set|oo|||
|data in|the:source|field, whicl3s|[in][ the][ range][ 0-31.][ The][ other][ bits]|
|»|||ep|of the destination|register are unaffected.|
|HEELS|||Cyele1: Destinatioi-register read|||
|"||Cycle 3:|Destination|tegister write|||
|)|
|EEE|Test the’bit|[in][ the'destination][ register][ selected][ by][ the][ immediate]|
|||13|||BYST an 2.|Bit|Test|ae|
|on|data in|the|source: field, which|is in the range 0-31.|
||||||HEE?eee“i,“ris.| Z-N|- setif not defined the|selected bit is zero|
|“ope.not defined|{|
|£a|Cycle: Destination register read|
|map|OEE|Cycle 3: (flags are valid)|
|30|2|3|-CMP|Rn,Rn|WEEE|Compare|||
|||coo_|EEE“ene|||[|comparison.]|stored,32-bit compare, but the flags this reflect is the same the result as|SUB of the without comparison, the result whichbeing||||
|||EGE|[EBs][ ge]|2|| may therefore be used for equality testing and|magnitude|
|HS|Z - set if the result is zero (operands equal)|
|=_—|N|- set if the result is negative (source greater than destination|
|||
|y|operand)C|- represents borrow|out of the subtract|||
|Register Usage|||
|Cycle|1: Source register read & Destination register read|;|
|Cycle|3:|(flags|are valid)|
|©|1992-95 Atari Corp.|ConfidentialInformation|JPR|Property ofAtari Corporation|June 7, 1995|

**----- End of picture text -----**<br>


**==> picture [595 x 735] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|i .|_|~Page 88|Jaguar Software Reference Manual - Version 2.4|j|
|i|31||CMPQ|n,Rn|Compare with Quick Data|-|7|
|ifi|| Z32-bit - set compare if the result with is immediate zero (operands data equal) in the range -16 to +15.|||ik.|
|y/|| N - set if the result is negative (immediate data greater than|||
|i|y|||destinationC - represents operand) borrow out of the subtract...||||P|;|
|i|||Register Usage|OEE|a|
|||Cycle|1: Destination register read|OE|a|
|21|||DIV|Ra,Rn|Unsigned Divide|Eee|CHEE|||
|The 32-bit unsigned integer dividetid:tn, the destination|register|is|||7|
|i|||divided by the 32-bit unsigned integer:divésor|[in]|[the][ source][ 1:]|&|
|register,|yielding a 32:bit unsigned integér:qiGtient|as the|result,|ad|
|| like normal microproeessar|division.|The remainders|available,|||
|||and division may|alsoS¢performed on|16.16 bit|unsigned’|a|
|:|||integers. Refer to|the|seetion’as atithmetic|functions.|||Ff|
|{.|| ZNC - unaffected|"3°|EE|Be.|||
|1|Cycle 1:Sautceregister read & Destination :#épister read|||,|
|'|CycleiiB:|Destinafidsi register write|WEEE|||aS|
|I|20||IMACN|Ra,Ra|Signed Integer Multiply/Accumulate, no Write-Back|&|
|1|||16-bit Signed integer multiply aad accumulate, like IMULT,|||-|
|"|| except thatthe 32-bit product'is:dded|to the result of|the previous|||gS|
|t|arithmetié|6pération,|and the reset|[is][ not][ written][ back][ to][ the]|
|1|agit Bestination régistef:|Intended|to bétised after IMULTN to give a|
|i|||BO|| *|rele to the section|6xMultiplyand Accumulate instructions|||q|
|i|"ib, ||Regitter Usage|3,|;|4|
|||==?|| Cycle'l: Source register read & Destination register read|
|i|17||IMULT|RaRa|Signed Integer Multiply|4|
|i|||HEE|16-bit signed integé¢|multiply, the 32-bit result is the signed|||j|
|i|cen|||integer pradictof the|bottom 16-bits of|each of the source and|||
|a|Eee|destination tégisters, and is written back to the destination|||1|
|4|
|bo|nite.|.|wae“Nieset if|[if]|the|[ the]|result|[ result]|is|[ is]|zero|[ negative]|||:1|
|q|||Ape|OPE|Register Usage|||
|i|am|EERE|Cycle|1: Source register read & Destination|register read|j|
|q|ic eee|“EE|Cycle|3: Destination|register write|
|/|[18||EMULTN|Rn,Ro|"8|| Signed Integer Multiply, no Write-Back|
|:|OEE|“a,|| Like IMULT, but result is not written back to destination register.|4|
|q|acces|EE|Intended to be used as the first of a multiply/accumulate group, as|1|
|L|“HEEB|Ein eee|EE|there are potential speed advantages in not writing back the result.|3|
|q|OEE|Z, - set if the result is zero|
|q|||N|- set|if the result is negative|
|7|;|C|- not defined|
|q|Register Usage|||
|q|Cycle|1: Source register read & Destination|register read|||
|||© 1992-95 Atari Corp.|Confidential|Information “JER|Property ofAtari Corporation|June 7, 1995|‘|

**----- End of picture text -----**<br>


| | | 

| | ] 

**==> picture [571 x 692] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||
|---|---|---|---|---|---|---|---|
|||Jaguar|Software Reference Manual - Version 2.4|Page 89|
|4|53|| JR|ce,n|Jump Relative|
|||Relative jump to the location given by the sum of the address of|
|||the next instruction and the immediate data in the source field,|
|which|is signed|and therefore|in the|range +15|or -16 words. The|
|||||condition codes encode|in the same way as JUMP.|
|||||RegisterZNC|- unaffectedUsage|Pee“aitiiivtne,.,|
|||Cycle|1:|(flags must be valid).!:2°|eee|
|52|| JUMP|cc,(Rn)|Jump Absolute|en|eect|
|| Jump to location pointed to by'thé'source register,|destizidition|.|
|||field|is the condition code,|where|thé:bits encode|as|follows::,|
|||Bit - Condition|7|ee|||
|||1|- zero flag must be|Sét'for jump|to occur|nee|||
|||||3|- flag selected bybit'4|must:be|Set|for|jump to occur|
|||| 4 - if set select negative flag, if cleat:select carry.|
|||jump.taIf more|than,onedesur|(the.conditions condition is set,are theti‘they:must ANDed)|22285: all be true for the|||
|i|Cycle: 45.(flags must bevalid):|
|41||LOAD|(Rn),Rn|Load|Long.|Ee|||
|p|||ii)_..{ 32-bit Vgadress, memory:read.which|must Thebe|long-word source:|registeraligned. contains The destination a 32-bit byte|||
|||£|°°) register will have|the|data loaded|into|it.|
|||cam|Register|Uisage|
|||“ae"||||Cygie'l:Cycle|n: Source:gegisterDestination|tegisterread write (internal memory|at cycle 3 or|
|Hon.|4,|external memory:subject|to bus latency)|
|43|||LOAD|(Ri4#K¢Rn|Load Long,|with: Indexed Address|
|44|| LOAD|(R1S#RE|RR.|32-bit|meriaty|read, as LOAD, except that the address|is given by|
|EP|ee.|| the sum of either R14 or R15 and the immediate data|in the source|
|EE|“cel. register|field,|in the range|1-32. The offset|is in long words, not in|
|||-|we Bytes, therefore a divide by four should be used on any label|||
|||jee|eee|“asithinetic to give the offset. This is slower than normal LOAD|
|eee|cee|operations due to the two-tick overhead of computing the address.|||
|||Oe|ZNC|- unaffected|;|
|cee|WE|Register Usage|||
|eer|eee|Cycle|1: R14 or R15|register read|
|eee|“HEE|||Cycle n: Destination register write (internal memory|at cycle 5 or|
|OEE|“=|||6, external memory|subject|to bus latency)|
|58|||LOAD (Ri4#Rn),Rn <=|| Load Long, from Register with Base Offset Address|
|59|||LOAD(R15+Ra)Ra|32-bit memory load from the byte address given by thesumof|=||
|_|R14 and the source|register|(the address|should be on|a long-word|||
|)|boundary).Cycle|1: R14Otherwise or R15|register like instructionsread & Source 43 andregister 44.|read|
|6,|external memory subject to bus latency)|||
|||Cycle n: Destination register write (internal memory at cycle 5 or|

**----- End of picture text -----**<br>


© 1992-95 Atari Corp. Confidential Information ‘JER Property ofAtari Corporation 

June 7, 1995 

**==> picture [589 x 730] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||
|---|---|---|---|---|---|---|---|---|---|
|j|Page 90|Jaguar Software Reference Manual - Version 2.4|
|4|
|i|39||LOADB|(Rn),Rn|Load8-bit memory Byte|read. The source register contains a 32-bit byte|||}|
|||address. The destination register will have the byte loaded into|||a|
|;|bits 0-7, the remainder of the register is set to zero. This applies to|4|
|[|external memory only, internal memory will perform a 32-bit|7|
|/|read.|||=|
||| CycleCycle n:1: Source Destinationregister register read write (externalHee|OREmemory subject to|||g1|
|||bus latency)|2|S||||
|1|16-bit memory read.The source register:contains a 32-bit byte:|||:|
|:|{|||address, which|mustbe|word|aligned. The|destinationsegistet|Will|||
|\|||| have the word loaded istic: bits, 0-15, the remainder: of the reaister|
|7|i|||is set to zero. This applies:|toexternal memory only, internal|||}|
|||ZNC- unaffected|~|EEE|:|
|i|| memory will perfornga:32-bit read.|
|||| Register Usage.|_|||
|||||| Cycle se|Destination|Fegister write (external memory subject to|||
|i|42|||LOADP|(Rn),Rn|Load|Prase|OE|
||i|||||(GPU only)|_aahsaddress,64-bit memsoity.read. whidittiust The be phrase source:tegister aligned.|The contains a destination 32-bit register byte|||
|r|||oui|have|the low fengword loadéd/:into it, the high long-word is|7|
|i:|||Ae|available in the high*half register. ‘This applies to external|:|
|f|oe|||memory:|onlsi:internal merry|will perform a 32-bit read.|
|1|
|4|||OE|ZNC# unaffected|2:5,|
|i|||||.|Register Usage|2:|
|b|||Ha|||Cycle|1: Source register read|
|i|i|THERE|||Cycle n;-Destinatian|register write (external memory subject to|
|q|||bus|latency|||
|a|48|||MIRROR|Rove?|ee...||Mirror Operand|4|
|,|(DSP only)|“*|“5|Eephe register is mirrored,|i.e. bit 0 goes to bit 31, bit 1 to bit 30, bit|j|
|||||nites|“42ag:bit 29 and so on. This is helpful for address generation in Fast|||;|
|}|re|| Z - set'#f the result is zero|;|||
|it|ee|ceeeccamy|| N - set if the result is negative|a|
|||Aes|OEE|| C - not defined|||
|'|OH|ey|||Cycle 1: Destination register read|||]|
|||||“HHH|ise)|| Cycle 3: Destination register|write|fg|
|’|© 1992-95|Atari Corp.|Confidential Information “JPR Property ofAtari Corporation|June7,1995|||

**----- End of picture text -----**<br>


| 

j 2 1 1 

**==> picture [555 x 723] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||
|---|---|---|---|---|---|---|---|---|
|Jaguar Software Reference Manual - Version 2.4|Page 91|
|il|54||MMULT|Rn,Rn|Matrix Multiply|
|location of the|register source matrix,|the product|is written|into|
|=|||| Start systolic matrix element multiply, the source register is the|
|||the destinationThe flags|reflect register. the|final Refer multiply/accumulate to the section on matrixoperation: multiplies.|
|Z|-|set|if the|result|is|zero|sani.|
|N|-|set|if the|result|is negative|22:2|Ens ee,|
|||
|||C- represents carry out of the!adder|OPED|||
|Register Usage|oo|_|||
|Refer to the discussion|of mult#pl§/accumulate|Macca|
|1|34.|||MOVE|Rn,Rn|Move Register to Register|255...|ees|
|ZNC|- unaffected!|
|.,.|CHEE|gee|
|Cycle|1: Source register:tead®:....|SEEESEEEE|
|Cycle|2:|Destinatiog|fepister|wete:..|
|51|||MOVE|PC,Rn|Move Program Count to Registet:|
|||Load the.destination|register with thé‘addiess of the current|||
|||
|.|||||instryson:|The setual value read from the P€is modified to take|;|
|||intg-aecount|theéffeets.of pipe-lining and préfetch, to give the|||
|||||cofteet address.|Thisis|the:only way for the GPU/DSP to read|its|||
|iS|22||Oyele|2:|Destination|register write:|
|37|| MOVEFA|RnRn|2|||Move|from|Alternate|Register”|
|ee|32-bit|alternate|register|to register transfer, the source register|
|||con|||lying|in|the|ofher-bank of 32 registers.|
|“ae|||ZING unaffected!|
||||||Register Usage|72):|
|ccm|||Cycle|1: Source register read|
|||38||MOVED nRS|Move|iminiediate|
|GoP|NGie.|| 32-bit register load with next 32-bits of instruction|stream. The|
|fee|clea|first word in the instruction stream|is the low word, the second the|||
|es|||ices|Cycle 3: Destination register write|
|352ci BMOVEQ|n,Ro|8s,OH|Move32-bit Quick register Data load with immediate value in the range 0-31.|||
|oe|“=|||ZNC|- unaffected|
|||||Suge.|fl|1|Cycle 2: Destination|register write|
|_|
|||||36.|| MOVETA Ran fe||| Move32-bit to register Alternate Register to alternate register transfer, the destination register|
|/|
|| at")|||lying in the other bank of 32 registers.|||
|“|| ZNC-|unaffected|||
|||Register|Usage|
|| Cycle 1: Source register read|| 7|
|Cycle|2: Destination register|write|
|© 1992-95 Atari Corp.|Confidential|Information “JER Property ofAtari Corporation|June 7, 1995|

**----- End of picture text -----**<br>


| _ Page 92 Jaguar Software Reference Manual - Version 2.4 = | 55 | MTOI Rn,Rn Mantissa to Integer q Extract the mantissa and sign from the IEEE 32-bit floating-point . af | | number in the source register, and create a signed integer in the _ | | destination. The most significant bit is bit 23, but it is sign g q extended. \ Z 4 | Z, - set if the result is zero | a : N - set if the result is negative fF fen. | : ‘ t Cycle 1: Source register read EEE OPER | : rr Cycle 3: Destination register writ@: OEE | i| || integer16-bit unsigned product ofthé:bpttom integer multiply, the 16-bits 32sbHt:tesult of each'bf theis source anid the unsighied | fis if | | destination registers, and:isawritten back to the destinarion: 2° | I | q: | || NZ-set - set ifthe if bit 31 resultisizéro of the result is“82sone#222. | 4g : | Cyclé:#: Source régistertiread & Destination register read | | if Cyclé:3; Destination registef:write ' | 32-bit two's complement negate; the result is the destination ; : qe contents:subtracted from:Zéfo, and is written back to the } i £222 destination register: Note that 804300000h cannot be negated. | — i | tees, | C- repeesents borrow out of the subtract | a : Cycle 1: Source register read | | i ' Eee Cycle 3: Destination segister write zz a 56 |NORMI Rn,Rn “=F Normalisation Integer | 4 : fs, Gives the ‘normalisation integer’ for the value in the source | @ : Paar register, which should be an unsigned integer. The normalisation | | | { | aoe eee integer is the amount by which the source should be shifted right | | 4 { om EEE to normalise it (the value can be negative), and is also the amount | , | : See “lees | to be added to the exponent to account for the normalisation. | q | SHE seek | Z- set if the result is zero **—** 4@ {: | escanaaOESPea | NRegi - **s** etter if Usagthe r **e** sult is negative | ,| |@| a | Cycle 1: Source register read ; & q | Cycle 3: Destination register write 4 © 1992.95 Atari Corp. Confidential Information 70% Property of AtariCorporation June 7, 1995 J 

! 

| ; | 

| 

' q 

**==> picture [542 x 677] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||
|---|---|---|---|---|---|---|---|---|---|
|Page|93|
|Jaguar Software Reference Manual|- Version 2.4|
|)|12|| NOT|Rn|Logical NOT|
|||[32-bit][ logical][ invert,]|[the][ result][ is][ the][ Boolean][ XOR][ of][ FFFFFFFF]|
|,|
|hex and the destination|register contents, and|is written back to|||
|;|||the d7,|-|s|e|tstinationif the|result register.is|zero|!|
|N|-set|if the|result|is|negative|ain...|
|Register Usage|Eee|OEE|
|||
|||Cycle 1: Destination register read|cccccem|
|Cycle 3: Destination register wails:|acces|
|10|||OR|Rn,Rn|Logical OR|"reig|thié-Boolean OREE of hE|||
|[or][ operation,]|[the][ result]|
|||[32-bit][ logical]|
|||source|register contelitsand the destination tegister content§;and|||
|i|7, - set if the result|is 26m! 22.|“CHEEEBEEBE"|||
|8...|||
|||| NC-notdefined - set if the result|igBegative!!—|||
|||Register|UsageSource|tegister read & DestinationOE|gegister read|||
|||Cycles:|
|||Cycle’: Destination|yegister write|OEP|||
|63||PACK(GPU only)Rn|| TakesPack|an,CRYunpackedPixel|pixel|vglué.and|packs it into a 16-bit CRY|||
|||pixel. $i48:22|to 25 are mapped dato|bits 12 to 15; bits 13 to 16|||
|Gita|bits 8 to 11; aid|bits 0 to 7 are mapped onto bits|||
|qr|csegeot® mapped|
|.|Ee|The régi field should be:Séf|to zero to differentiate this|
|||
|P| from|UNPACK.|See|this'section|on Pack and Unpack|||
|||Be|| Flags! esi,|
|||| Cycle 1: Destination:tegister read|\|
|Ss.|Cycle 3: Destinationtegister write|||
|19||RESMAC Rts.|Multiply/Accumulate|Result Write|
|EEE.|Takes the current Contents|of the result register and writes them to|
|a.|ee|||the register|indicated. Intended to be used as the final instruction|
|.|
|ae|“1 of a multiply/accumulate|group.|
|_|“Eee.)|ZNC:-referunaffectedto the section on Multiply and Accumulate instructions|||
|_|||Register Usage|
|Bene|eee|Cycle 3: Destination|register write|
|||TEESE|ONEHEEE|32-bit rotate right by the bottom 5 bits of the source register. Can|||
|EEE|“cues.|| be used for ROL functions by complementing the value.|
|TE|gee|1N-setif the result is negative|
|eeeeeeeeaaceal|| C - represents bit 31 of the un-shifted data||||
|||||_|| Register Usage|
|Cycle|3:|Destination|register write|
|W|||||Cycle 1: Source register read|& Destination register read|||

**----- End of picture text -----**<br>


© 1992-95 Atari Corp. 

Confidential Information “JER Property ofAtari Corporation 

June 7, 1995 

1 | 

Page 94 Jaguar Software Reference Manual - Version 2.4 g | 29 | RORQ n,Rn Rotate Right by Immediate Count a Immediate data version of ROR. Shift count may be in the range A Z - set if the result is zero | q " | N - set if the result is negative i | C - represents bit 31 of the un-shifted data j ; | Register Usage Pree ciecertee | = i Cycle 1: Destination register read Ese | if Cycle 3: Destination register white cee 3 i 32 | SAT8 Rn | Saturate To Eight Bits oo “HE | § unsigned integer. If it is negative it if:set:to zero, if it is gréatee. | a | (GPU only) | Saturate the 32-bit signed integer:operand value to an S3Bit. | a i | than 255 it is set to 255. This is useful fae: computed intensitigs: | ; } | | and so on, to counteragt:the effect of rounding exrors. | | 2 s.. mE | a i Z - set if the result is zéng@? A C - not defined “ EEE | i | | Cycle JiBlestinationregisterread | | i | Cyclé'3? Destination ségister write - a a 33 | SAT16 Rn Saturate To Sixteen Bits: 2. | | a (GPU only) Saturate:the 32-bit signed inte ger.operand value to a 16-bit 4 4 unsignéd:isiteger. If it is negative itis set to zero, if it is greater | = i _fthan 655359618 fesse wakes, and so Off;{d:eounteract thé:effect of rounding errors. | 4 I Set to 65535. Thi§:ig-useful for computed Z, audio , I | iP | Flags Ff [og 4 Te | N - cleared, | a “He? | C-nobdefined “He2, a . "| Register Usage TEE, 7 i atk. | Cycle 1: Destination register read (4 i oe | Cycle 3:Destinatias register write & : 33. |SATI6S Ro 2282088, Saturate to:Sixteen Bits | ¢@ [ (DSP only) HEP22" 7“Hd integer. Saturate the 32-bitIf it is negative signedit is integer less than operand 8000h valueit is to set a to 16-bit that, signedif it is | **4** y _ “"dgeéater than 7FFFh it is set to that. f | ae OE C - not defined , q He “euiec. | Cycle 1: Destination register read _ ' OEE “Hel | Cycle 3: Destination register write Zz 

| 

© 1992-95 AtariCorp. 

Confidential Information JER Property of Atari Corporation 

June 7, 1995 | 

" 

| 

**==> picture [545 x 732] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||
|---|---|---|---|---|---|---|
|Page|95|
|||Jaguar|Software Reference Manual - Version 2.4|
|.|62|||SAT24|Rn|Saturate To Twenty-Four Bits|
|(GPU only)|Saturate the 32-bit signed integer operand value to a 24-bit|
|than|16,777,215|it is set to 16,777,215. This|is particularly|useful|
|||||unsigned integer. If it is negative|it is set to zero, if it is greater|||
|:|||| for computed intensities, to counteract the effect of|rounding|||
|||errors.|_ettltines.|
|Flags|ee|ee|
|||
|7 -setifthe|result is zero.|||CEEHEEE|
|||||Cycle 1: Destination register read|cee|eet|||
|||||Cycle 3:|Destination|fegister write|Ce|ee|
|||
|||42|||SAT32S|Rn|Saturatesigned|integer. Multiply/Acewmulate This:ses the‘GverfiowResult bits fromEEE oe|
|||(DSP only)|Saturate the 40-bit sighed integer operand value to aif 32-bit|||
|multiply/accumulate“operations|ag the'top eight|bits of the source|||
|||value.|Ifthe.accumulated value is lessthad:80000000h|it saturates|
|||to|thasef'i238|gredter then7FFFFFFFh itSatusates to that.|||
|||Z|- setit the result|#6|2810|a|
|||| N ~setif the result is negative|||
|if|ent Cycle|1: ‘Destination register read.|
|||a||32-bitA|positive shift ‘valle left|or causes a right piven'by shift tothe the value right. in Values the source of|plus or register.|||
|“HEE|.||mitius set thirty-two if the resultGt3'2er0greater give zero. Zero is shifted in.|||
|ES|||Ny|- set if the result|
|4s negative|||
|||THEE|| C - représents big:|Oot the un-shifted data for right shift, or bit 31|
|||HEP|“culeced|Cycle 1: Source register read & Destination register read|
|“HEHE|@ycle|3: Destination register write|
|||
|Ae OEE|| As SH but right shift is arithmetic, i.e. sign shifted in.|
|1|7cei|CHEEEEE||| NZ|-- set set if if the the result result is is zero negative|||
|7|OEE|| C - represents bit 0 of|the un-shifted data for right shift, or bit 31|
|eee|“eee|||for left shift|
|||eee _|ee|Cycle 1: Source register read & Destination register read|
|BoE|Cycle 3: Destination|register|write|
|© 1992-95 Atari Corp.|Confidential Information “7O® Property|of Atari Corporation|June 7, 1995|

**----- End of picture text -----**<br>


**==> picture [2 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


**==> picture [1 x 34] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


**==> picture [601 x 725] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|
|1|Page 96|Jaguar|Software Reference Manual - Version 2.4|s|
|'|27|||SHARQ|n,Rn|Shift Arithmetic Right|o|
|]|As|SHRO but arithmetic shift right,|i.e. sign shifted in. Best|ad|q|
|i|Z - set if the result is zero|s|
|4|N|- sei|if the result is negative|4|
|||| C - represents bit 0 of the un-shifted|data|a|
|i|| Register Usage|ie|||
|||| Cycle 3: Destination register wie:|accom|||
|:|24|||SHLO|n,Rn|Shift Left with Immediate|Shift|Count|7|a|
|i|! 39-bit shift left by n positions, inthéxange|1-32.|OtherWige|dike|||Ss|
|||||||SH. (The shift value is|actually encoded-as 32-n, this ishavdied|9)|
|||| by the assembler)...|Oe,|Ee|a|
|q|| N-set if the result is|negative|OEE|||
|;|C-- represents|bit 31|of|thewn-shifted data|—|||
|||||| Register Usage|7|||
|i|||||Cycle 1: Destination register|read|#288:|||a|
|:|||+ Cycle 3:.Destination|register write|CHRHE in|=|
|4|||25||SHRO nn|Shift|ight|withLiimediate Shift|Count =|5|
|:|||As SHEQ but shift 'right,:zero shifted in.|/|Zz|
|7|lz - Sé€3fthe|result is ZEPG|=|
|[is][ negative.]|||@|
|a|||| N - se€|[ifthe][ result]|
|q|||||.|.|| C - represents.bit 0 of the un-shifted data|||;|2|
|||a7|[STORE|Rn(Rn)|«||StoreLiong|
|||=.|||=|
|iF|ccm|| 32-bitmemory: weite. The source register contains a 32-bit byte|||q|
|||||register contains|the|[data][ to][ be][ written.]|a|
|4|||||“HEE”|| addvess, which mustbe long-word aligned. The destination|||=|
|. qa||||||eeete|RegisterCycle 1:SautdéregisterUsage ai!|read & Destination|register read|||]p|4|
|:|49|||STORE|Rn(Rit+n)“22|%,,.|||Store Long, with Indexed Address|,|||
|:|50|| STORE|Rn(RiStn)|“|4:32-bit memory write, write as STORE, with address generation|in|||
|i|estes,|Ghercame manner as the equivalent LOAD instructions.|4|
|(|Eye|SEE|Register Usage|||:|
|;|a|OOH|||Cycle 1: R14 or R15 register read|=|
|io|eee|Cycle 2: Source register read|||@|
|i|;|60|[ORE Rn,(R14+Rn)'223,|||Store Long, to Register with Base Offset Address|a|
|:|||61|||STORBRn(R15+Rn)|“4|||32-bit memory store to the byte address given by the sum of R14|j|
|||Pf|
|L|WEE.|aati’|||boundary).|Otherwise like instructions 49 and 50.|
|’|||||WEEE|ae|: and the destination register (the|address should be|on|a|long-word|4|
|y|||SOE|| Register Usage|||.|4|
|i|||!|Cycle|1: R14 or R15 register read & Destination register read|,|
|q|||Cycle 2: Source register read|
|q|© 1992-95|Atari Corp.|Confidential Information ‘JER|Property ofAtari Corporation|June 7,|1995|4|q|

**----- End of picture text -----**<br>


} 

| 

4 : 

## Jaguar Software Reference Manual - Version 2.4 

Page 97 

**==> picture [546 x 699] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|f|45|| STOREB|Rn,(Rn)|Store Byte|
|8-bit memory write. The source register contains a 32-bit byte|
|||address. The destination register has the byte to be written|in bits|
|||0-7. This applies|to external memory only, internal memory will|
|ZNC|-|unaffected|eee.|
|||| pR|e|gisterrform a Usage 32-bit write.|ee|
|||
|Cycle|1: Source register read &|Destination|register|read|
|||46||STOREW|Rn,(Rn)|Store Word|aon|WEE|
|16-bit memory write. The|soureg|register contains a3gasitbyte|
|address, which must be word aligned: ‘The destination|register has|
|the word to be written|in bits 0-15.This|applies to external?|
|memory only, inteiiial.memory|will performs: #:32-bit write2!|
|Cycle|1: Source register|read|&|Destination register read|||
|48|||STOREP|Rn,(Rn)|Store Phrase|~*|WEEE|
|(GPU only)|||64-bit memory|write. The source register¢ontains a 32-bit byte|
|||address,confains thewhich low’ titst légigewordbe phraseof the aligned. ‘The data to bedestination written,|the register high|
|||||long-word is obtained froie.the high-half register. This applies to|||
|{|extefial.|memory only, inteimal-memory|will perform|a 32-bit|
|Bee|Cycle:1;|Source registerread &|Destination|register read|
|||register contents sititracted from the destination|register contents,|
|||||SEES|| 32ebit two's cotaplément integer subtract, result is the source|
|||HeHEHE|borrow:outand is written|[of]|to the|[ the][Subtract,]|destination|[ and][ the]|register.|[ zero][ flag]|The|[ is]|carry|[ set][ if]|flag|[the]|represents|[ result][ is]|||
|poe|||Z-setif the result is zero|||
|HEE|“clilds.||N|- set if the result is negative|
|||
|-|“TAL.|represents borrow out of the subtract|||
|| Register|Usage|
|AS|Bee|
|||Sep|Cyélé:l: Source register read & Destination register read|
|ey|OTHE|Cycle 3: Destination register write|
|||5|ESUBC|RnRn|Subtract with Borrow|
|con|EE|32-bit two's complement integer subtract with borrow in|
|THEE|“8|||according to the carry flag, otherwise like SUB.|
|WEEE|2|| Z- set|if the result is Zero|
|tae|i|| N-|set if the result is negative|
|oe|C|- represents borrow out of the subtract|
|—|Register Usage|||
|gv|Cycle|13|:|SourceDestination regist r|e|gisterr read writ & D|e|stination register read|}||
|ee© 1992-95 Atari Corp.|Confidential InformationFERProperty ofAtari Corporation7,June 7, 1995 1995|

**----- End of picture text -----**<br>


1 Page 98 Jaguar Software Reference Manual - Version 2.4 a | 6 SUBQ n,Rn Subtract with Immediate Data s i 32-bit two's complement integer subtract, where the source field is ays 4 il | immediate data in the range 1-32, otherwise like SUB. “ § | | Z - set if the result is zero '' |{ NC -- represents set if the result borrow is negative out of the subtract. i , ja : Cycle 1: Destination register rea UES ; 4 | | Register Usage Se | a | | Cycle 3: Destination register write: EE ' | 32. |SUBQMOD n,Rn Subtract with Immediate Data:#:::. Ee a \ (DSP only) | 32-bit two's complement integer subtract like SUBQ, excépk that | | ( | the result bits may be unmodified data if the corresponding’, | a q | modulo register bits are set. This allows ‘dipéuifar, buffer SEB { | | management (for 2" sizebuffers), where the High Bits.of thé” | : 4 | modulo register are set; and tlie, low bits left clear: 808s" f 7, - set if the result is Zero" "222288. is a | | N - set if the result isiiégative “28S. a | i | | C- represents borrow out of the subtract #irior to the modulo | a 3: Destination register read | ey |, q || CycleCyclé:3:Destination registee write a | 7 | SUBOT n,Rn | Subtract:with Immediate Data, Transparent j : | | | 32-bit two's Gomplement integer subtract, like SUBQ except that r § 4 | | att itis wranspareit tothe flags, which tétain their previous values. | oo Po | | Cycle1: Diéstitation register read | @ | , 63 |UNPACK Rn ==" Onpack CRY Pixel:[=] | i:’ (GPU only) ._SHEN **|** Takesinteger. an Bits packed CR¥:pixel 12 to 15 ate mapped value onto and unpacks bits 22 toit 25; into bits a 32-bit 8 to 11 4Pf a TEBE | are mapped onto bits 13 to 16; and bits 0 to 7 are mapped onto [| s ' EP | bits 0 to 7.Aflother bits are set to zero. The regi field should be P| |: | oniiivinn. ge“ "See“fs|[Pass set to and one Unpack to differentiate this from PACK. See the section on | **a** Pq i | fe ZNC: Ghaffected i ‘ Pp OPER Register Usage = eee “ult. 1 Cycle 3: Destination register write = { aye ecm Cycle 1: Destination register read L | 11 a |XGR:.Ro,RnWEEE TE"EE | 32-bit| Logical logical XOR exclusive or, the result is the Boolean XOR of the | 4I | i OEE Ee | source register contents and the destination register contents, and | , a “HECOEeec.. aati’? | is written back to the destination register. | : ' OS EDEEEEES | 7. - serif the result is zero yf 44 ‘ N - set if the result is negative F | | C - not defined . i Register Usage 7 ' | Cycle 1: Source register read & Destination register read — | 4 | | | Cycle 3: Destination register write | fr 4 q © 1992-95 Atari Corp. Confidential Information “7O® Property ofAtari Corporation June7,1995 (im 

Page 99 

Jaguar Software Reference Manual - Version 2.4 

## . Witing Fat GPU andDSP Progiams 

To get the most out of the Atari RISC processors, it is important to avoid wait states. Each processor can execute one instruction per tick in ideal circumstances, but it is very easy for code to be subject to so many wait states that it only achieves around half this figure. It will be worthwhile.far pfegrammers to tune the :anermost loops of their code for maximum performance, and the rules given here Shouid help do that. A well written program can usually achieve an instruction throughput of around two-thirds of the peak. figure. Wait states usually occur either because an instruction would otherwise use'some system resoures, such as a register or a flag, which is not valid; or it would use a piece of hardware that iscurrently still activé:fiom an earlier operation, such as the external memory interface. This is because the chipset:makes significantiuse of pipe-lining to improve performance. oe eects AES Wait states are incurred when: ee — « an instruction reads a register containing the result of the previous instfaction, one tick of wait is incurred until the previous operation completes. HEE reece +» an instruction uses the flags from the previous instruction, one tick of wait is iigutred until the previous operation completes. eee “EEE - a result has to be written back and neither.t6gister operand:6t Hix instruction about to be executed matches, one tick of wait is incurred to letthe.data be written es «two values are to be written back at once, oné tick.of wait is incurred: 2 2. » an instruction attempts to use the resuit.of a divide instruction before itis ready. Wait states are inserted until the divide unit completes:the divide, between oe: ad sixteen wait states can be incurred. + a divide instruction is about tobe executed:and the.previous one has aot completed, between one and sixteen wait states can be incurred. fee eae - an instruction reads a register which is awaiting data from an incomplete memory read, this wiil be no more than one tick from internal memory, but can be severab:ticks from external memory. * a load or store instfuction is about to be executed and the memory interface has not completed the transfer for the previous ones (one internal load/store’or tworexternal loads/stores can be pending without holding up instruction flow} de, ee” + after a store instruction with an indexed addressing mode (one tick). + after ajump:or jr (three ticks if executing out of internal memory). ° if the nextinstruction has not been read, this will only occur when executing out of external memory. . during a matrix multiply if:the CPU accesses the internal space of Tom or Jerry (whichever made the The most common cause of wait-states is using a register which was altered by the previous instruction. For example consider ‘this code fragment’ 4 ada sox, roe ; add. offset to X 2 shrq #1,79 : apply scaling factor , 3 add r0,x4 : add to base w 4 add r5,r1 >; add offset to ¥ 5 shrq #1,r1 : apply scaling factor 6 add ri,ré ; add to base 

## (iy - 

© 1992-95 Atari Corp. Confidential Information “JPR Property ofAtari Corporation 

June 7, 1995 

Page 100 

Jaguar Software Reference Manual - Version 2.4 

4 : : 4 ; 

° ‘ail 4« 

| 

4 

Wait states will be incurred after instructions 1, 2, 4 and 5. If the code were laid out like this: 1 add r3,x0 ; add offset to X Zz add r5,xr1 ; add offset to Y 3 shrq #12,r0 j; apply scaling factor 4 shrq #1,xr1 j; apply scaling factor 5 add r0,r4 ; adc to base 6 add rl,ré ; add to base OHSS. No wait states would occur. This is an example if interleaving, and this is apowerful techaique for speeding up code. It is well worth the performance enhancement - 6 ticks instead of[in][this][ example] +ig ensure that your code is laid out like this. Obviously there is a considerable overhead i#:thinking this out, byt for loops that are executed many times it is well worth doing. THERE EEE 

‘: 

© 1992-95 Atari Corp. 

Confidential Information “PER Property ofAtari Corporation 

June 7, 1995 

Page 101 

Jaguar Software Reference Manual - Version 2.4 

| 2 

## ee 

: moe The Jaguar system is intended to be usable in either a little-endian, e.g. Intel 80x86, or big-endian, e.g. 680x0, environment. The difference between these two systems is to do with the way in which bytes of a larger operand are stored in memory. There is potential for considerable confusion,nété; Be:this section attempts to explain the differences. i When storing a long-word in memory, 4 big-endian processor considers that the most signifieadit byte is stored at byte address 0, while a little-endian processor considers that the ifidst significant byte istered at ##i§ is.not an issue forthé:hemory byte address 3. When both 32-bit processors are fitted with 32-bit memory interface, as the concept of byte address has no meaning; where it does becomie'@'pireblem is when the:data path width is narrower than the operand width. fee acces Be mes This document adopts the big-endian convention andMotorola @perand ordering convention Euille-endian and Intel operand conventions could equally well have been applied... en ee The IO Bus Interface is a 16-bit interface. Thegefore, 32-bit daka-guch as addresses will be presented differently between the little-endian and big-edian systems. What kappens, in effect, is that the sense of Al is inverted between the two systems. Abig-endian, system will see'the tigh word of long-word at the low address, a little-endian system will see the high word.at the high addres$:! 

## Lb 

As the co-processor bus interfack is 64-bits wide, these-is.no problem regarding big and little endian systems, although graphics processor prograrimers should always tse: byte, word, or long-word transfers as appropriate the CPU is big or little endian. to the operand size to avoid having:[be][ awate][of][whether] —S—— nae One side effect of the big or fittleendian philosophies is with regard to the organisation of pixels within a phrase. oa In the little-endian system, the left-most pixelis always the least significant. In a phrase of data the left-most pixel includesbit }..In byte address terms, this.#s.in byte 0. In the big-endiai: system, the left: most pixel is always the most significant. The left-most pixel therefore always includesbit 63;..n byte address terms this is stored in byte 0. 63° 86755 48 7 0 left right 

Consider an eight-bit per pixel mode: - in pixel mode, the left-most pixel in both systems is at byte address 0. © 1992-95 Atari Corp. Confidential Information “JER Property ofAtari Corporation 

June 7, 1995 

Page 102 

Jaguar Software Reference Manual - Version 2.4 

1 4 a : 

i 

= 

- in phrase mode, the little-endian left hand pixel is on bits 0-7, the big-endian left hand pixel is on bits 56-63. 

(these modes refer to Blitter operation, which is described elsewhere) 

This difference therefore affects operations that involve addressing pixels within a phrase when transferring a whole phrase at once (Blitter phrase mode). a 

© 1992-95 Atari Corp. 

Confidential Information TER Property ofAtari Corporation 

June 7, 1995 

