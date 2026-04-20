Pagel 

7 Jaguar Voice Modem 

| 

| . : | | i 

@ Please note that the Atari Jaguar Voice Modem section of the documentation is still undergoing We significant revisions to properly outline the various requirements for tasks such as making a call or { answering an incoming call. If this section ofyour documentation is more than two months old, #Bsoplease contact Jaguar Developer Supportfor an updated revision. 7 ci 2 | The Jaguar voice modem is a high performance (v32terbo) DSP based modem, with many additional @ features and modes which make it particularly suitable for an interactive and consumer friendly game , environment. : In the rest of this section, we discuss: | The Modem Interface ‘ Data Communications and Bandwidth g Flow Control s. Data Parsing ; AP Call Waiting @ We then conclude with a summary of the commands and unsolicited responses used in voice plus data mode. A full reference manual of all commands is available but not complete yet. This manual is only | necessary for full featured fax and data communication systems (without simultaneous voice). 

nets & ‘The interface between the Jaguar and modem is via the built in Jaguar UART. Communications in both | directions, are in the form of 2 or 3 byte packets, at a baud rate of 57600 or 19200 (1 start bit, no parity, | 2 stop bits). After reset, all communications are initiated by the Jaguar. Typically, Jaguar will send a command to | the modem, and the modem will respond. In simultaneous voice plus data applications, we usually reduce the baud rate between the modem and Jaguar, in order to ease the interrupt response requirements. The Jaguar can also enable various types of "unsolicited" data packets from the modem. In this case the modem may send a data/eommand packet to the Jaguar unsolicited. These unsolicited packets are iG typically used for incoming data, call waiting detection, loss of the line, and other errors. Commands from the Jaguar to the Modem are always sent as a two byte packet, with the least | significant byte sent first. 

: 

. 

|. 

©1995 Atari Corp. 

Confidential Information “FOR Property of Atari Corporation 

26 April, 1995 

Page 2 

Jaguar Voice Modem 

j 1 ; = q | @ 7 | 

: Replies from the Modem to the Jaguar are sent as two byte packets, with the most significant byte (usually the command byte) first. The modem will also send a padding byte of OxFF prior to a packet if | there wasa significant gap since the previous packet. : The Parse data flow diagram shows how to handle received data. 

In voice plus data mode (known hereafter as SVD - simultaneous voice plus data), compressed voice j - : data is sent over the telephone line in packets which have a one byte header. Game data packets canbe B inserted into this data stream at any time with a one byte overhead. The game data packets actually : @ interrupt the voice data stream to keep transport latency to an absolute minimum (which is necessary for Bs good interactivity). | = Developers need to understand the data bandwidth which is available, and then decide which packet | sizes are most appropriate for their game. The following equations describe the available bandwidth: ; Be Total data bandwidth = Line Speed / 8 (in bytes per second) j 7 [Modem data is sent with an embedded clock, with no need for start or stop bits] ; o@ Voice data bandwidth = (Voice sampling frequency/4) + (Voice sampling frequency / (4*Voice packet size **)** . This gives you the voice data bandwidth, in bytes per second. This shows that each voice sample uses2 | # | data bits - or 4 samples per byte, and each voice packet has a one byte overhead. e Game data bandwidth = (number of game data packets per second) * . | (game data packet size + x) 7 (x = 1 in normal mode, 2 for error detection mode) { The following table shows the voice sampling rates that the modem will use by default (assuming 80 1 | byte voice packets, and the default adaptive voice sampling rates): : | SpeedLine BytesTotalPer SampleVoice VoiceRate Data PacketVoice HeadersVoice BytesVoicePer RemainingBytesPer : j Second Rate Size Second Second q | P68{| 210 **0** s ea000| i700 |802.25 1721.25 | 378.75 | | |74400}1800 [5600 | 140080-*+| 75 | tai7s |[3005] | | | 12000 1500 11700 |[8013.75] 1113.75 |[386.25] | | 9600 7200 [ 3200 [ 800 [so T0800890 The Remaining bytes/second are available for data packets. Game data packets have a one byte j j overhead each, plus an additional overhead byte for error detection. Note that you MUST use a form of 4 ; error detection, since errors do occur over the line. Error correction is usually achieved by requesting = j fi 126 April, 1995 Confidential Information “POR Property ofAtari Corporation © 1995 Atari Corp. 

| | | 4 " | 

Page 3 

4 

|||Page 3|Page 3|Page 3|
|---|---|---|---|---|
|Ef<br>@,) "<br>|<br>~||JaguarVoiceModem<br>hatthepacketberesent. So,assumingaworstcasedatarateof378bytesper second,thefollowing<br>datapacket options are possible:|||
||||TotalData Rate<br>DataPacket<br>(Bytes/Sec)<br>Size|Packet<br>Overhead|TotalPacket<br>PacketsPer<br>TotalData<br>Size<br>Second<br>(Bytes/Sec)|
||||se||so|
||||ose||eo<br>Ee|
||||Asyoucansee,thesmallerthedatapacket size,theless<br>bytespersecond). However,thesmalJerpacketsdoprovide||lessefficientthismethodis(intermsofthetotal<br>provideahigher packet-per-second rate,whichwill|
|||increaseuserinteractivity.|||



- | bytes per second). However, the smalJer packets do provide a higher packet-per-second rate, which will increase user interactivity. 

- ; Example code is provided for initialization and overall flow control, and we suggest everyone use it. ae Once the two modems have completed "handshaking", the users will be able to talk over their headsets, 

- &. : @ whilst the Jaguars send each other data packets. | The Jaguar game will need a “Modem” option selection screen. This will allow selection of any of the following items: 1. Call. This brings up an edit field to enter the number to dial. When entered and OK selected, the modem will go off hook and dial the number. The user will hear the dialing via her headset. If the line is answered, she will be able to talk to the answerer via the headset. If there is no answer, she can select “Hang up”. 

- 2. Hang up. This will doa graceful cleardown (i.e. cause both ends to hang up together) if the modem was communicating digitally with the other end. If the modem was still in analog mode, it will simply hang up the line. 

   3. Answer. This is the selection used by the answerer after the two parties have verbally agreed over the analog line to play the game. This selection will mute the headsets and commence 

4. Adjust voice volume An outline of the Modem commands used for each of the four options listed above is given below. , _,, by w@ Example code is also available, and a flow chart is included. Details of each command are given at the — ~ end of this section. 

## i. 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


7 | 4 i i | 1 i | | | | : | |[‘] | | oH : 

© 1995 Atari Corp. Confidential Information “JPR Property ofAtari Corporation 

26 April, 1995 

Page 4 

Jaguar Voice Modem 

| ‘ = _ 

| 

.....§.§.§|©§©@.§©6—6pllCl.6.AUUUDChUCClt 

q | a 1 : ; y ‘ 

‘4 2 4 ae = a . u te e 

q 

: 

**==> picture [27 x 22] intentionally omitted <==**

**----- Start of picture text -----**<br>
-<br>**----- End of picture text -----**<br>


**==> picture [433 x 658] intentionally omitted <==**

**----- Start of picture text -----**<br>
Prompt User for |<br>number todial ;<br>: Initialize Modem as caller<br>{ t<br>Go off hook ,<br>| Wait for dial tone PO Rial Tone<br>| Dial number cs<br>, 2 r- : Report "No diai tone" |<br>/ Offer"Hang up" —<br>\. option to User<br>| requested?Hangup >——yes— Go On Hook |! q<br>' No ‘ a<br>No :<br>~<br>ke—No Tone detected? 4<br>oN va Main Menu j<br>Yes<br>|3<br>'<br>~<br>| Magic DTIMF 1<br>sequence? E<br>Yes q<br>| Send DTMF reply | i,» )<br>sequence \ 4<br>Confidential Information “70® Property ofAtari Corporation © 1995 Atari Corp. |<br>**----- End of picture text -----**<br>


26 April, 1995 . 

‘ 1 Jaguar Voice 

Page 5 

| j = | | : q q i a 

Modem 

**==> picture [507 x 492] intentionally omitted <==**

**----- Start of picture text -----**<br>
Report “Handshake in|<br>i progress"<br>; |<br>:<br>Wait up to 15 seconds |<br>!  forhandshaking |<br>a ~ oO \ .<br>Z Timeout F 2 Yes —+/ a<br>on or rant ° ( Report "Line Error’: |<br>:<br>~~Less thanbps? 9600a Yes \ goodReport enough“Line not fer; : 4<br>— L Voice plus Data" / -<br>Report connection rate | GoOnHook =|<br>nn<br>i/ Start Game } /<br>\ } ‘ Main Menu ><br>KC’<br>**----- End of picture text -----**<br>


Command Response Description FFFF Reset modem and do a seff test. . Eg (.FFFE rnone __| Set baud rate to 19200 / W OO0F TOOOF ___| Enable echo back of commands Bo00 Enable Analog Line to Headset connection r2cso._—+i|2cso. Set this modem up as a Caller, and enable call waiting detection | Ee Ee Set miscellaneous configuration items A021 A021 _| Set target error rate to better than 1 in 10e6 bits (i.e. minimum) i. © 1995 Atari Corp. Confidential Information “JER Property ofAtari Corporation 

26 April, 1995 

Jaguar Voice Modem Qin be 

: 

q 3 4 a . , 8 e 

| 

, ; 

| 

q 

4 

i 4 

Page 6 

**==> picture [513 x 219] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||
|---|---|---|---|---|
|m=|Command|Response|Description|
|FFFE|(no tone detected).|No timeout here|- users are talking.|
|If no tone|is ever detected,|the Caller will|never see the “Handshake|in|
|progress” status,|but the users will|still be able to talk and discuss the problem|||
|over the analog|line.|
|When magic tone|is detected...|

**----- End of picture text -----**<br>


## eo i 

## =... 

## Command Response 

4 4 | : © 1995 1995 Atari Corp. Corp. | 

' nn 1 26 April, 1995 Confidential Information TER Property ofAtari Corporation © 1995 1995 Atari Corp. Corp. 

t ( ' 1 q : 

éI | Jaguar——————————————————ee— Voice Modem 

s 

i 

**==> picture [158 x 10] intentionally omitted <==**

**----- Start of picture text -----**<br>
, Page 7<br>**----- End of picture text -----**<br>


**==> picture [3 x 10] intentionally omitted <==**

**----- Start of picture text -----**<br>
4<br>**----- End of picture text -----**<br>


**==> picture [3 x 24] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


**==> picture [387 x 513] intentionally omitted <==**

**----- Start of picture text -----**<br>
\<br>Go off hook<br>‘‘i \ "HangPrompt up anylines" User other to \}|<br>| \. OK, or QUIT /<br>x<br>User selected Ves { Hang up NO<br>' "QUIT"? \ Goto Main Menu =’<br>"OK"?<br>“a User selected<br>Hl jf<br>|<br>i Yes\ \<br>No<br>Send Magic DTMF sequence |<br>// [Response] Report "No [ from] \ —_a<br>j i heck \ No DTMEF reply within<br>\ oT actions? s 4 seconds?<br>\ ok or Quit ¢ ~<br>— a<br>Yes<br>laaN<br>**----- End of picture text -----**<br>


Page 8 

Jaguar Voice Modem 

i 

q 

**==> picture [324 x 6] intentionally omitted <==**

**----- Start of picture text -----**<br>
4‘<br>**----- End of picture text -----**<br>


**==> picture [604 x 627] intentionally omitted <==**

**----- Start of picture text -----**<br>
;| Report "Handshake in | |<br>q | progress" | d :q<br>q | Wait up to 15 seconds ; —_<br>| |  forhandshaking | a<br>: " i wD 4 : eo<br>: Timeout or Fail? >———Yes——>\ Report "Line Errore 4 =<br>: NZ ——- | Pe<br>|<br>|No | i:<br>\<br>! ;<br>Less than 9600 ~ eport Line no 4<br>' bps? Yes———*__ good enough for —_ |<br>“ \._ voice plus data"? ; ; ee<br>|<br>|<br>Go On Hook j<br>| Report connection rate| | 1<br>1 4<br>|4 a ‘ Main Menu /: Ej<br>( Start Game ) Ne 4<br>Command Response Description 4<br>FFFF Reset modem and do a seff test.<br>' FFFE Fnone _—_| Set baud rate to 19200 '<br>OOOF Enable echo back of commands 4<br>1 Booo Enable Analog Line to Headset connection j<br>1 2480 Set this modem up as an answerer, and enable call waiting detection q<br>26 April, 1995 Confidential Information “AER Property ofAtari Corporation © 1995 Atari Corp. §<br>**----- End of picture text -----**<br>


m — ff Jaguar Voice Modem 

Page 9 

**==> picture [559 x 351] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||
|---|---|---|---|---|---|---|---|---|---|
|&'|a|_|Command|Response|Description|;|
|"{3952|||3952|Set|miscellaneous configuration|items|
|A021|A021|Set target error|rate to better than|1|in|10e6|bits|(i.e.|minimum)|
|:|F207|F207|Enable|unsolicited|error|detection codes|
|Bé02|B602|Enable|error detection mode|
|Set|data|packet|size|
|B405|Set voice|packet|size to 80|bytes|
|A37E|TAS7E___||Enable|loss|of|line|detection|
|A060|A060|Go|off|hook|
|Prompt user “Hang up any other hand sets”|||
|||;|
|Wait for|user to|acknowledge|other|lines|are|hung|up|
|Send|magic|DTMF|sequence|
|6800|Poll DTMF tone|detector for magic DTMF|reply sequence|
|FFFE|(no tone|detected).|Timeout|after 4 seconds|
|if timeout,|prompt|user “No response from|caller modem.|Check modem|
|connections”|
|Wait for acknowledgment,|then go|to “Send|magic DTMF sequence"|
|When|magic|reply|detected|...|
|» i|O|[zx|2460|Display “Handshake in progress” status message.|
|8000|8000|Start|Handshake|
|8100|Poll for handshake successful|(timeout|after|15 seconds)|

**----- End of picture text -----**<br>


& 

Once the modem has been initialized, and handshaking has occurred, data transmissions are possible. A flow chart for received data is given below: 

**==> picture [2 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
.<br>**----- End of picture text -----**<br>


© 1995 Atari Corp. 

Confidential Information “FER Property ofAtari Corporation 

26 April, 1995 

Page 10 

Jaguar Voice Modem 

J 

q 

| . |goe q a 1 a ] . q bg ] **a** :: 4 : 7 : ‘ 

=q ‘ : | |{ 

**==> picture [566 x 633] intentionally omitted <==**

**----- Start of picture text -----**<br>
Main data Parse loop<br>2 Bytes ready? >——No ——-» Exit ! |<br>=q NN | putPutbytebyte x cxii n packet)packet | |<br>$FOxx? wa Yes———> buffer :<br>— Discard Byte 8—a<br>ws | Mark Packet as “Good".<br>$FF? —————Yes——-»; Point to next packet. +}<br>“ i buffer. —____—__><br>No<br>x- SF3xx?NN> Yes $F301? S——No—+} TransmitDiscard “Resend* packet. |———_——_—_——_—_,:<br>> x command : ;<br>No<br>$B1FF? Pause game | Report "Call \<br>Yes——> | Waiting" Cc<br>| eport "Line Lost -\<br>| possibly call if ya)<br>SA4xx? Yes $A4x1?DO) Yes waiting at other \G0 to D in "Call :|<br>i<br>1iNo | | j 4<br>|<br>woe L___no | Discard Byte Pair bt ;<br>{ Modem Error \ | :<br>**----- End of picture text -----**<br>


**==> picture [2 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


26 April, 1995 

Confidential Information FER Property ofAtari Corporation 

© 1995 Atari Corp. 

] | 

Page 11 

: g 

1 4 pw | j : 4| q 

{ ; ' 

Jaguar Voice Modem [nee The line which gets a call waiting tone will receive the unsolicited data packets $BiFF then $A4??. The = other line will just get a $A4?? packet. Both ends will then immediately go into analog line mode, @ which will allow them to talk, and for the call waiting receiver to ask the other party to wait while she q picks up the call waiting. She then selects the “go to call waiting” box, which flashes the line for her, @ has the conversation, then selects “reconnect”, which will flash the line again (back to the first party), and send the magic DTMF tone sequence - starting handshake again. 

**==> picture [379 x 448] intentionally omitted <==**

**----- Start of picture text -----**<br>
» C i<br>NY<br>"Flash to other = \<br>( line"<br>: “Hangup" ]<br>\"Restart game" _/<br>Flash Line \e-Yes <Flash to other line? > :<br>No .<br>a . ves GoOn Hook<br>:<br>No<br>( Main Menu t<br>yes<br>{ Goto Ein "Answer" \<br>**----- End of picture text -----**<br>


© 1995 Atari Corp. 

Confidential Information FER Property ofAtari Corporation 

26 April, 1995 

, 2 

Page 12 

Jaguar Voice Modem a 

: , 

| | : | 

j 

initiate:Report SofiwareReset = OXFFER ' function: This command causes the Voice Modem to reset all parameters to the default conditions. 3 , After resetting, the Voice Modem will return the self-test result executed during the _.. previous POR\. This command may be issued at any time. CAUTION: care should be f 4 taken because the command will clear all operating parameters to the default | = values. | = The Modem will internally issue the following commands during reset: ° Command Name Command Code : “ Set Configuration Word 1 0x2480 | @ Set Configuration Word 2 [Ox8952 | | = Enable Unsolicited Error Detection Responses OxF207 Set Bit Error Rate Target 0xA021 Connect Headset to Analog Line [ OxB00O "Ff : a Since it is not always possible to determine whether the modem host baud rate is set to { 8 57600 or 19200, the following procedure is recommended for issuing the reset command: | . ¢ Send Reset command at 57600 f * Ifa sucessful response (0xB800) is received within 1 second, then exit reset ‘ : ¢ Ifa response is not received within 1 second, issue the reset command at 19200 and j ignore the response (if any) * Then issue a reset command again at 57600, and wait for the response. : response: The response is returned at a host baud rate of 57600, after the reset is completed and j - within about 1 second. It is in the form 0xB80x where x has the bit form: 4 [DSP] [AFE] [ROM] [SRAM] where 0 is a pass and 1 is a fail. Thus a successful self-test will give a response of 0xB800. 4 default: N/A ; 

| 

|| | | 4 

Command Reference For Voice PlusData Unless otherwise noted, all values are in hexadecimal. 

. 

26 April, 1995 

Confidential Information “PER Property ofAtari Corporation 

© 1995 Atari Corp. } 

Page 13 

| Jaguar Voice Modem 

| 

| | 

function: Set host baud rate to 19200 Only reset {OxFFFF} can change the baud rate back to 57600. 

- | response: none | default: N/A 

| connsconenasn Telanaiog ane 0 i EE | function: Allow the headset to be used as a telephone handset, as if it were directly connected to the analog line. (In reality, a digital connection is made between the line Codec and the headset Codec) 

## i EE oxBone” 

- This command will also cause the modem to switch to SVD mode immediately after handshaking is complete. 

; response: The command will be echoed back within 1.2ms | ca:_—_ ee ene function: This command writes 12 bits, specified by nnn, to the modem Configuration Word 1. Bits 0-5 specify the modem type, and bits 6-11 specify other modem configuration items. The meaning and function of these bits are described below. 

Meaning Bit: 11 10 9 8 r4 6 A TC -atcecimeectRemote toopRequet| | tor { {|_| easeveawxrak | | ft potrt t _ Gockel |, | fotet | Peek smedwADOK |_| | ti ftet i _ TE eisable calwating detecton || | 1 {|e} eened Modem Type Data Rate(bit eemavs: s) Modulation Bit 5 4 3° 20) o an Piss 420 yaawrow | tofo}ot oto) 0 Vzbe ea Ce aoe ___—1 i799 [orsk__f feo fot to) t peel 2i2Aog ___ ——— **——** T100—segg_—_[rskfroesk [ff **e** Peoo yet ttte a Tosco Trek eet eo fo Beles ____—iovenaioo trek | fefpoyr tote) 7 We ___———tse0g_——_foaw fo frye tot tt 28 ___——1fao0_—[orsk Jo fa peta te te Vs as00_—Torsk ro Peet bt |. ©1995 Atari Corp. Confidential Information ‘FPR Property of Atari Corporation 26 April, 1995 

Page 14 14 

2 a: 

| | | : | be | q | | 

vir SSsté—é—“—S AG PAM TCM TCM OT Pt Et Po} oO Plo | | Bit 11: | Answer/Call - selects the answer mode or answer mode or mode or or call] mode handshake sequence for the modem mode handshake sequence for the modem handshake sequence for the modem for the modem the modem modem type a selected. This should only be changed when only be changed when be changed when changed when when the modem modem is off-line. off-line. | @& Bit10: Accept/Reject Remote Loop Request Loop Request Request - this will will allow or disallow response or disallow response disallow response response to remote digital remote digital digital z be loopback when requested by the far-end modem. when requested by the far-end modem. requested by the far-end modem. by the far-end modem. the far-end modem. far-end modem. modem. This is valid for V.32terbo/V.32bis/ is valid for V.32terbo/V.32bis/ valid for V.32terbo/V.32bis/ for V.32terbo/V.32bis/ V.32terbo/V.32bis/ V.32, . V.22bis, V.22 and and Bell 212 modem modem types. This may be changed may be changed be changed changed at any any time. —. ; re Bits 9-8: Tx Clock Clock - this selects this selects selects the source of the transmit bit timing, source of the transmit bit timing, of the transmit bit timing, the transmit bit timing, transmit bit timing, bit timing, timing, either locked to locked to to the external external ; . clock XTCLK, XTCLK, internal on-board crystal or locked to the received clock RDCLK RDCLK derived — from the far-end modem modem signal. | Bit 7: Enable call call waiting detection _ Bit 6: Reserved - this bit is reserved for future use and should be set to 0. | of - Bits 5-0: Modem Type - these 6 bits select the modem type desired. When selecting a V.32terbo/V.32bis/V.32 configuration, the desired rates should be defined using the Set Rate} | Sequence Command 1NNN. The combinations of these two commands would have the j effect of either setting a single speed, negotiating within a restricted set of speeds or allowing | all possible speeds. When using a test command, the highest rate enabled is used. 9 response: The command is echoed back within 1.2 ms after it was written. 4 : default: 2480 hex _ | SetConfigurationWord2 tT function: This command writes 12 bits, specified by nnn, to the modem Configuration Word 2. ] | _ The meaning and function of these bits are described below. 4 Meaning Bit: 11 109 8 7 6 5 4 3 2°14 ~«~0 | Reserved st—“‘;STTTTTTUCUTLTTLC UTE Ur } 26 April, 1995 Confidential Information PER Property ofAtari Corporation © 1995 Atari Corp. + 

**==> picture [500 x 294] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|
|Page 14 14|Jaguar|Voice Modem|
|Modem|Type|Data|Rate(bit|s)|Modulation|Bit|5|4|3|2|1|0|
|vir|SSsté—é—“—S|AG|PAM TCM TCM|OT|Pt|Et|Po}|oO|Plo|||
|Bit|11:|||Answer/Call|-|selects|the answer mode or answer mode or mode or or|call] mode handshake sequence for the modem mode handshake sequence for the modem handshake sequence for the modem for the modem the modem modem|type|
|selected.|This should only be changed when only be changed when be changed when changed when when|the modem modem|is off-line. off-line.|
|Bit10:|Accept/Reject|Remote Loop Request Loop Request Request|-|this will will|allow or disallow response or disallow response disallow response response|to remote digital remote digital digital|
|loopback when requested by the far-end modem. when requested by the far-end modem. requested by the far-end modem. by the far-end modem. the far-end modem. far-end modem. modem.|This is valid for V.32terbo/V.32bis/ is valid for V.32terbo/V.32bis/ valid for V.32terbo/V.32bis/ for V.32terbo/V.32bis/ V.32terbo/V.32bis/|V.32,|
|V.22bis,|V.22 and and|Bell|212 modem modem|types.|This may be changed may be changed be changed changed|at any any|time.|
|Bits 9-8:|Tx Clock Clock|- this selects this selects selects|the source of the transmit bit timing, source of the transmit bit timing, of the transmit bit timing, the transmit bit timing, transmit bit timing, bit timing, timing,|either locked to locked to to|the external external|
|clock XTCLK, XTCLK,|internal|on-board|crystal|or|locked|to|the|received|clock RDCLK RDCLK|derived|
|from|the|far-end modem modem|signal.|
|Bit|7:|Enable call call|waiting|detection|

**----- End of picture text -----**<br>


Page 15 

| 

| 

| Jaguar Voice Modem Meaning 

**==> picture [184 x 32] intentionally omitted <==**

**----- Start of picture text -----**<br>
Bit: —eo 8 TT 8S 8<br>**----- End of picture text -----**<br>


- Se xT a as a ss 

- | Bitli: Enable/Disable Answer Tone - the function of this bit depends on the state of Bit 11 of Configuration Word 1. When an answer mode handshake is selected (Configuration Word 1, 

- j Bit 11 = 0), clearing this bit enables the transmission of 3600 ms of 2100 Hz tone prior to ; beginning the appropriate handshake sequence according to V.25 recommendation. Setting this bit to one causes no 2100 Hz tone to be transmitted prior to the handshake sequence. When an originate mode handshake is selected (Configuration Word 1, Bit 11 = 1) this bit 

- | has no effect. This bit is not used with Bell 103 or Bell 212A modem types and will have no ' effect if these modem types are selected. This bit may be changed at any time. | Bits 10-9: Tones Selection - these two bits allow the generation of 550 and 1800 Hz guard tones for | V.22bis and V.22 answer modes and echo protection tone for V.33, V.17, V.29 and V.27ter half-duplex modes. For other modem types, no tone (00) should be selected. These bits should only be changed when the modem is off line. 

- Bit 8: Enable/Disable Auto-mode - this feature supports Annex A of V.32terbo/V.32bis/V.32 CCITT recommendations and EIA PN-2330 (draft proposal) for automode handshake which allows the Voice Modem to automatically determine the mode of the far-end modem 

- | during handshake and to reconfigure itself appropriately. This feature works if the far-end modem is a V.32terbo/V 32bis/V.32, V.22bis, V.22, V.21, V.23, Bell 212A or Bell 103. 

- | Bit 7: Dial-up/Lease-Line - this bit modifies the handshake from normal dial-up to a specitied 1 leased-line sequence if applicable. | = Bit. 6: Enable/Disable Auto-retrain and Auto-rate Renegotiation - if this feature is enabled, the Voice Modem will initiate a retrain or a rate renegotiation if the actual mean square error (MSE), which represents signal quality, is higher or lower than a dynamically set threshold. 

- : For a more detailed explanation refer to Section 8.2. Bits 5-4: Async/Sync Select - these bits function in conjunction with Configuration Word 2, bit las follows: If Configuration Word 2, bit 1=0 (serial data), then async mode is selected with bit 5-0. Bit 4 allows the choice of normal operation in the +1.0% to -2.5% rate range Or 

- j extended operation in the +2.3% to -2.5% rate range according to V.14 recommendations. However, if bit 1=1 (i-e. parallel data), then bit 4=1 configures the data interface for HDLC 

- : operation and bit 4=0 for asynchronous (8,N,1) operation as described in the parallel data mode section. Synchronous operation, either in serial or parallel data modes, is selected by 

- ai setting bit 4=1, bit 5=1. 

|. 

© 1995 Atari Corp. 

Confidential Information JER Property ofAtari Corporation 

26 April, 1995 

' | 

Page 16 

Jaguar Voice Modem 

q , ; | @ 7 | ] 5 : | = : 2 i 2 j Po : a _. * 7 : | og . j 2 _ ° ; 8 

| | || ; | 

| : | | 

| 

: ‘ 4 j E ; 4 

. | | | | 

|Bit1<br>fo <br>fo.|Bits<br> =6©{o0<br> [0|Bit4<br>{0 __| <br>[1 ||Function<br> SerialAsyncNormal Rate<br> SerialAsync Extended Rate|
|---|---|---|---|
|t+0)<br>1 ||||Parallel Syncw/HDLC|
||||ParallelSyncBitStream|



- Bits 3-2: Character Length - These bits are used to select the correct character length for the Serial V.14 async/sync converter. They are only used when the modem is operating in asynchronous serial mode (Configuration Word 2, bit 5=0, bit 1=0). The character length includes one start bit and one stop bit. Thus, the commonly used 7 data bit even parity one stop bit character format would require a character length of 10 bits (10). In asynchronous paralle] mode (Configuration Word 2, bit 5=0, bit 4=0, bit 1=1), the character length is always 10 bits. 

- Bit 1: Serial/Paralle] Data Mode - This bit configures the Voice Modem to pass data serially through the V.24 Pins RXD, TXD or in bytes through the controller interface. It is used in conjunction with Word 2, bit 4 and bit 5. Note: Serial mode is not available in “V.32terbo” 

- | 19,200 bit/s mode. 

Bit 0: Enable/Disable Adaptive RLSD Detection - This bit enables or disables the adaptive determination of RLSD thresholds to enable fast and consistent RLSD\ loss detection. Fora more detailed explanation refer to Section 8.5. 

- response: The command is echoed back within 1.2 ms. default: 3000 hex 

function: This command sets the BER target for the auto-speed selection feature. This feature enables Voice Modem to automatically select the highest data rate allowable by the _[modems][ and][ supported][ by][the][line][conditions][such][that][ BER][ does][not][ exceed][the][target] value. The command variable “n” assumes the following values: 

n=0Q ; Disabled n=1 ; BER=10E-6 n=2 ; BER=10E-5 n=3 ; BER = 10E-4 n=4 ; BER = 10E-3 

response: The command is echoed back within 1.2 ms. default: A021 hex 

. 

26 April, 1995 Confidential Information “7@® Property ofAtari Corporation 

© 1995 Atari Corp. | 

Page 17 

| Jaguar Voice Modem 

] 

| 

| j 

j 

i Enabie Unsolicitéd Error Detection Responses OXF207 function: The command allows the modem to return the OxF3xx error check responses (if enabled) | at the end of data packets response: The command is echoed back within 1.2 ms. 

## | moon 

function: Selects data modes: 

ae [nonreattimedata 

response: The command is echoed back within 1.2 ms. 

function: Set real time data packet size to xx bytes. response: The command will be echoed back within 1.2ms. default: 0xB504 

| 

| mam | function: Enable the unsolicited responses OxA4xx (see unsolicited response section below) response: The command will be echoed back within 1.2ms. 

AE: 

| on ee 

|| function: This command is used to detect presence OF absence of dial tone within a very short i ") interval. response: A response of 8CO1 means that a dial tone has been detected. . If a dial tone was not detected, the response will be 8Cxx, where xx is not 01. q ‘ © 1995 Atari Corp. Confidential Information “FOR Property ofAtari Corporation 26 April, 1995 

Page 18 

. 

Jaguar Voice Modem 

] 

2 | } 

2 | @ q i 

. 

: : 

| q 

The response is returned within 1.2 ms after the command was issued. 

> SeiVeiceSamplingFrequency= i i OxBSOx 

function: Set the compressed voice sampling frequency, as shown below: 

**==> picture [475 x 57] intentionally omitted <==**

**----- Start of picture text -----**<br>
Sample Rate x The default adaptive sampling rates are as<br>Adaptive sampling (Default) | 0 | follows:<br>ps6e00Hz Cd Connection Speed Sampling rate<br>**----- End of picture text -----**<br>


**==> picture [122 x 404] intentionally omitted <==**

**----- Start of picture text -----**<br>
| /<br>Set Dial :<br>to be dialed. j :<br>Detector :<br>1 :<br>q<br>q<br> the detector with with {<br>the DTMF DTMF 4<br>;<br>4<br>© 1995 1995 Atari Corp. Corp. ]<br>:<br>**----- End of picture text -----**<br>


response: The command will be echoed back within 1.2ms 

## Dial Number/Transmit DTMF Tone = 

## OKRA 

function: This command is used to dial a digit based on the mode selected using the Set Dial Mode. The command is of the form 8A2x hex, where x denotes the digit to be dialed. The status of digit dialling can be known using the Report Call Progress Detector command. 

## x=0123456789ABCDEF 

Number=0 123456789*#ABCD 

response: The command will be echoed back within 1.2ms. 

## PollDTMF Detector = = Oxb800, 

function: This command starts the DTMF tone detector and returns the status of the detector with with a response of 000x hex. The least significant digit of the response reports the DTMF DTMF tone pair received as follows: x = 0123456789ABCDEF DTMF Tone Pair = 0123456789* #ABCD 26 April, 1995 April, 1995 1995 Confidential Information AR Property ofAtari Corporation © 1995 1995 Atari Corp. Corp. 

26 April, 1995 April, 1995 1995 , 

‘ Jaguar Voice Modem y 4 A If no digit S , it is 

Page 19 

| 

a response: A response is returned within 1.2 ms after it was written. g eportHandshakeStatus = OB 100. | function: This command causes the Voice Modem to return a 12-bit response indicating the | progress through the handshake, retrain or rate renegotiation. response: The response is returned in the form of 8xyz hex, where x, y and z are shown below. | j Example: V.32bis handshake completed handshake completed completed at 14.4k bit/s: 0x86B2 | V.32bis handshake before rate determination: 0x8002 

j 

j 

1 

- If no digit is detected, a response of FFFE hex is returned. The digit detected is held until it is read by the controller or another digit is detected. 

- Example: V.32bis handshake completed handshake completed completed at 14.4k bit/s: 0x86B2 V.32bis handshake before rate determination: 0x8002 Auto-moding, no mode or rate is determined: 0x8000 

**==> picture [579 x 195] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||
|---|---|---|---|---|---|---|---|---|
|j|Handshake/Retrain|State|y|Data|Rate|Response|z|
|||Undetermined|||0|||Undetermined|||0|||
|||1200/75|
|=|75/1200|V.32terbo/V.32bis|.|
|Bs|~~2400|__Bell|212|PS}|
|i|4800|é|:|
|||7200|Bell|103|||8|||
|'|9600|Non-trellis|||8|||V.23|;|9|
|9600|=|
|||
|12000|[ve|s—CS|CBT|
|14400|8|
|16800|A|
|.|19200|| D|||||

**----- End of picture text -----**<br>


State x Auto-mode Handshake in Progress | 0 | Non-Automode Handshake in Progress Abort/idie Retrain in Progress 3 Rate Renegotiation in Progress | 5 | Data Mode Za 

response: The response is returned within 1.2 ms after the command is written. 

## ==—=————————— BA 

a 

function: Adjust voice volume. The allowable values for x are: 

. 

© 1995 Atari Corp. 

Confidential Information “7OR Property ofAtari Corporation 

26 April, 1995 

Jaguar Voice Modem 

: j 

| 

q : 

| 

- 

L q = _ 

a 

1 

' 

## Page 20 

, 

. 

||Level|||x||
|---|---|---|---|---|---|
|Maximum|volume|(default)|||0|||



## response: 

The command will be echoed back within 1.2ms 

function: Send data byte xx in real-time (low latency) mode. 

The data byte xx will be sent once the controller has received a full packet of bytes (packet size is set by the BSxx command). The typical latency is around 18ms. response: The command will be echoed back within 1.2ms 

26 April, 1995 

Confidential Information “7O® Property of Atari Corporation 

© 1995 Atari Corp. 4 

Page 21 

| Jaguar Voice Modem @nsolicited Response Reference | This section summarises the various types of unsolicited data that can be expected from the modem. 

function: The byte xx was received from the remote modem. If error detection has been enabled, note that the packet error status will only be received at the end of the packet (after all packet bytes have been received). 

. 

| CME LLL LL function: If error detection has been enabled (with the $B602 command), this reponse will be received after all bytes in a packet have been received. The format iS: 

| F301 = No Errors in packet F311 = Error ocurred in packet oc ee | function: When call waiting detection has been enabled with the 2C80 or 2480 command, this | response indicates that a call waiting tone has been detected. indicates that a call waiting tone has been detected. that a call waiting tone has been detected. a call waiting tone has been detected. call waiting tone has been detected. waiting tone has been detected. tone has been detected. has been detected. been detected. detected. This response will be followed by a A4?? response, indicating that the line has been lost response will be followed by a A4?? response, indicating that the line has been lost will be followed by a A4?? response, indicating that the line has been lost be followed by a A4?? response, indicating that the line has been lost followed by a A4?? response, indicating that the line has been lost by a A4?? response, indicating that the line has been lost a A4?? response, indicating that the line has been lost A4?? response, indicating that the line has been lost response, indicating that the line has been lost indicating that the line has been lost that the line has been lost the line has been lost line has been lost has been lost been lost lost (see below). below). 

## | oc 

| | response indicates that a call waiting tone has been detected. indicates that a call waiting tone has been detected. that a call waiting tone has been detected. a call waiting tone has been detected. call waiting tone has been detected. waiting tone has been detected. tone has been detected. has been detected. been detected. detected. This response will be followed by a A4?? response, indicating that the line has been lost response will be followed by a A4?? response, indicating that the line has been lost will be followed by a A4?? response, indicating that the line has been lost be followed by a A4?? response, indicating that the line has been lost followed by a A4?? response, indicating that the line has been lost by a A4?? response, indicating that the line has been lost a A4?? response, indicating that the line has been lost A4?? response, indicating that the line has been lost response, indicating that the line has been lost indicating that the line has been lost that the line has been lost the line has been lost line has been lost has been lost been lost lost (see below). below). | function: This unsolicited response type is enabled with the command OxA3FE. When enabled, the modem will report line lost, and occasionally also report that the line is still good. As shown in the parse data flow chart, the line good response needs to be taken into account, and discarded. 

| : 

: 

The least significant bit of the response indicates the line status: 

Joxxxx xxx1 = Line Lost Joxxxx xxx0 = Line Good 

| 

Only the LSB is valid. All other bits must be ignored. 

© 1995 Atari Corp. 

## Confidential Information JPR Property ofAtari Corporation 

. 

26 April, 1995 

Jaguar Voice Modem Voice Modem 

q : | = 

| 

7 . : ; 4 4 

Page 22 Jaguar Voice Modem Voice Modem FOB er Immediately subsequent to losing the line, the modem will switch back to analog mode, where the headset and microphone are connected to the analog line. 

Whena call waiting tone is detected by the remote modem, the local modem will just get this lost line response on its own. Both ends will in fact switch to analog mode, allowing the users to talk, take care of the call waiting, and then restart communications and ; handshaking. 

| 

26 April, 1995 

Confidential Information FR Property ofAtari Corporation 

© 1995 Atari Corp. | 

