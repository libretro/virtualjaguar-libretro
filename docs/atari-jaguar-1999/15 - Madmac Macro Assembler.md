Madmac Macro Assembler 

Page 1 

| | i | | j : 4 ' 

## MadmacMacroAssembler 

## ee ,,.,hrt””—™,.S™r——.._CicCOCC . 

This document describes MADMAC,a fast macro assembler that generates code for the Motorola | 68000, Atari Jaguar GPU/DSP, and 6502 processors. It was originally written at Atari Corporation by | programmers who needed a high performance assembler for their work. Madmac was originally , t distributed as part of the Atari ST Computer Developer’s Kit, and has been updated to support the | requirements of the development system for the Atari Jaguar console. Madmac is intended to be used by programmers who write mostly in assembly language. It was not | originally intended to be a back-end to a C compiler. Therefore it has creature comforts that are usually } neglected in such back-end assemblers. It supports include files, macros, local symbols, some limited F control structures, and other features. Madmac is also blindingly fast! , a feature often sadly and | obviously missing in today’s assemblers. 

## 'fheCommandLine = 

L The assembler is called MAC.EXE (for the PC/MSDOS version) or MAC.TTP (for the Atari/TOS version). The command line takes the form of: | mac [switches] [files ...] | Acommand line consists of any number of switches followed by the names of files to be assembled. A | switch is specified by a dash (“-”) followed immediately by a key character. Some switches accept or | require arguments to immediately follow the key character, with no spaces in between. Key characters | are not case-sensitive, so “-d” and “-D” produce the same effect. | Switch order can be important. Command lines are processed from left to right in one pass, and } switches usually take effect when they are encountered. It is best to specify all switches before listing | the names of the input files. 1 If the command line is empty, the Madmac prints a copyright message and enters an interactive mode, | prompting for successive command lines with an asterisk (“*”) character. Hitting {Enter} on an empty command line will cause Madmac to exit. After each assembly in interactive mode, Madmac will print / asummary of the memory usage, the number of lines processed, and the amount of time the assembly 1 took. | Input files are assumed to have the extension “.S” and Madmac will look for a file with this extension if none is specified. Different extensions may be used if they are specified on the command line. More than once source file can be specified. The files are assembled into one object file as if they were concatenated. pod The PC/MSDOS version of Madmac has been benchmarked at over 240,000 lines per minute on a DX2/66-based PC. Of course, your mileage may vary. © 1994 Atari Corp. Confidential Information ‘PER Property ofAtari Corporation 8 November, 1994 

Page 2 Madmac Macro Assembler i | Madmac normally produces object code files with the same filename as the input source file, except yy { with a “.O” extension. If multiple files are specified, the name of the first file is used. If the first input a filename is a device (like CON:), then the output filename will be NONAME.O. The “-o” switch can be | = used to change the output filename. 4 CommandlineSwitches= = . ... §- j A summary of the available command line switches is shown below. Please note that some switches - may not be applicable to Jaguar programming. They are listed for completeness. | = |1 -?—~—tsts—SSsSSwitch= PrintDescription Madmace usage information. =.. | | The -6 switch causes Madmac to act as a back end assembier for the Alcyon ; ' C compiler. However, this mode is not 100% compatible with the AS68 | = assembler (which is the normal Alcyon C back-end assembler). | = Symbols beginning with a capital “L” are not included in the object file. (These 1 7 are special symbols used by the Alcyon C compiler.) 4 a ; This is generally not applicable to Jaguar programming unless you're using a = the Alcyon C compiler on an Atari computer to generate 68000 code. u: -a[s] text, data, bss Output DRI-format absolute executable file (ABS). Using -as instead of -a adds symbols to the output file. ._— | text = Address for TEXT segment : é data. = Address for DATA segment a bss = Address for BSS segment i Zz. ' Values for text, data, and bss can be: 8 : a hexadecimal value to be used as the address. fo r: relocatable segment (not useful for Jaguar programs) - | x: contiguous segment (contiguous with previous segment) For example "-a 802000 x 4000" would put the TEXT segment at $802000, the q | DATA segment immediately after that, and the BSS section at $4000. j 

7 

I 8 November, 1994 

Confidential Information FR Property ofAtari Corporation 

© 1994 Atari Corp. 

4 

**==> picture [553 x 522] intentionally omitted <==**

**----- Start of picture text -----**<br>
qMadmac Macro Assembler Page 3<br>*C CDU Start out in a DSP or GPU section instead of 68000, and output .BIN/.SYM<br>‘ files: cpu is either "dsp" or “gpu":<br>! dsp: Jerry's DSP output code (i.e. -cdsp)<br>} gpu: Tom's GPU output code (i.e. -cgpu)<br>i External variables cannot be referenced in files assembled with these options,<br>‘ because BIN files contain only raw binary code with an 8-byte header:<br>i typedef struct {<br>4 long exec_addr; /* values are in big-endian */<br>4 long code_size; /* (Motorola) format */<br>q } BIN_Header,<br>a You can use the -fb option to output BSD symbols and the -g option to output |<br>q source-level debugging information in the .SYM file. Note that the use of .BIN<br>a and .SYM files is mostly for backwards compatibility with code originally<br>al written for the GASM assembler, and is not recommended for new code.<br>Ff -d symbol[=value} This switch permits symbols to be defined on the command line. The name of |<br>the symbol to be defined must immediately follow the switch (no spaces). The<br>: symbol name may optionally be followed by an equals sign ("=") and a decimal<br>a number for the value to be assigned to the symbol. If no value is specified, |<br>, the symbol’s value will be set to zero. The symbol’s attributes are “defined,<br>Pi not referenced, and absolute”. This switch is most useful for enabling<br>conditionally assembled debugging code or test code on the command line.<br>For example:<br>4 -dDEBUG -dLoopCount=999 -dDebugLevel=55<br>4 This would define “DEBUG” and give it a value of zero, “LoopCount” with a<br>@ value of 999, and “DebugLevel” with a value of 55. :<br>| | -aferrorfile] This switch causes Madmac to send error messages to a file instead of the<br>4 console. If a filename immediately follows, error messages are written to the<br>specified filename. If no filename is specified, a filename is created with the |<br>: default extension of “.ERR” and the root name taken from the first input file j<br>q (ie. error messages are written to FILE.ERR if the first input filename is FILE |<br>or FILE.S). | :<br>7 | If no errors are encountered, then no error message file will be created.<br>However, note that if an assembly produces no errors, then any error file from<br>a previous assembly will not be deleted.<br>**----- End of picture text -----**<br>


© 1994 Atari Corp. Confidential Information “PER Property ofAtari Corporation 8 November, 1994 4 1 

Page 4 Madmac Macro Assembler | q ) -f[format| Select object file format to be output: = fa: DRI (default output) + SymbolsSource-levelare debugginglimited to 8 informationcharacters length.cannot be included. =: || No support for proper relocation of MOVE! GPU/DSP instruction. i -fb: BSD (Recommended format for Jaguar programming) q Symbol lengths are unlimited. | & | Source-level debugging information can be included. a Supports proper relocation for MOVE! GPU/DSP instruction. | @ ' -fm: Mark Williams (not applicable to Jaguar programming) | 4 : i Symbols are limited to 8 characters. f G ' Source-level debugging information cannot be included. - | No support for proper relocation of MOVEI GPU/DSP instruction. , 4 -fmu: Mark Williams, except moves leading underscore characters on 4 ; ; ; symbols to be moved to the end of the symbol name (i.e. “_main” e 4 1 becomes “main_” and “__ main” becomes “_main_’). | aa | Output source level debugging information (only when using -fb switch to a4 select BSD format object file output). = | -i[path] The -i switch allows automatic directory searching for include files. A list of a a semi-colon separated directory search paths may be listed immediately = following the switch (with no spaces anywhere). For example: | = _ | -im:;c:\include;c:\include\sys _} will cause Madmac to search the current directory of drive M, and the , directories INCLUDE and INCLUDE\SYS on drive C. if the “-i” switch is not specified, Madmac searches for the MACPATH | a environment variable, which is used to specify include file directories in the [ ae | same way. For example: 4 set MACPATH=m:;c:\include;c:\include\sys { s | will cause Madmac to search the same directories as the previous example. , oe (Some command line interpreters may use “setenv” instead of “set” to set an a environment variable instead of a shell variable.) ee ‘ it is recommended that you set the MACPATH environment variable to point at Ee ; your global include files, and use the -i option only to override or add to the ] a paths specifed by MACPATH. _ If you are using a MAKE utility, and in your MAKEFILE you need to use the -i ) _ option to specify a certain include path for specific files, but you also need q access to the paths specifed by MACPATH, you can do something like this: 4 | -~iproject\inc;$(MACPATH) j | And the $(MACPATH) macro will be expanded by your MAKE utility into the contents of the MACPATH environment variable. This is a standard feature of 7 of nearly all MAKE utilities. i 8 November, 1994 Confidential Information FR Property ofAtari Corporation © 1994 AtariCorp. 4 

{Madmac Macro Assembler 

Page 5 

**==> picture [558 x 703] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|4|-l[flename}|The|-I|switch|causes Madmac|to|generate an|assembly|listing|file.|If filename|
|rh|immediately|follows|the|switch,|the|listing|is|written|to|the|specified|file.|
|z|if no|filename|is|specified,|a filename|is|created|with|the|default|extension|of|
|4|“.PRN”|and|the|root|name taken|from|the|first|input|file|(i.e.|the|listing|is|written|
|||to|FILE.PRN|if the|first|input filename|is|FILE|or|FILE.S).|
|B|1-0|file|The|-o|switch|causes|Madmac|to|write|its|object code|output|to|the|specified|.|
|£|file.|No|default|extension|is|applied|to the filename,|so you|need|to|specify|
|F|whatever|extension|is|appropriate.|Unlike|most other Madmac command|line|
|2|switches,|a space between|the|switch|and the filename|is|permitted|(but not|
|i|required).|For|example:|
|j|-ojagmand.o|
|||will|produce an|object|file named JAGMAND.O,|regardless of what the source|
|1|file was|named.|||
|rep|The -p|and|-ps|switches cause Madmac to|produce|a GEMDOS format|||
|Fi|-ps|executable|program|file|(with|the|default|extension|of “.PRG”|unless|otherwise|
|4|specified|by the|-o|switch).|
|'|If there|are any unresolved|external|references|at the end|of the assembly,|an|
|;|error message|is|emitted|and|no|executable|file|is|created.|
|||
|:|||The -ps|switch adds symbols|(Alcyon format)|to the output|file.|
|This|switch|is|not|applicable|to|Jaguar|programming.|
|The -q|switch|was|used|originally|on|the|Atari|to|install|Madmac|as|a memory-|1|
|q|resident|program.|This|was|intended|to|reduce|load|times|for|multipie|calls|to|
|j|Madmac on|floppy-disk|based|systems.|
|:|This|switch|is not available|in the PC/MS-DOS|version|of Madmac.|
|Ef|-r[size]|The|-r|switch|causes|Madmac|to|automatically|pad|the|size|of|each|segment|
|in|the|output|file|until|the|size|is|an|integral|multiple|of the|specified|boundary.|q|
|size|is|a|letter that|specifies|the desired|boundary:|||
|-rw|word|(2|bytes,|default|alignment)|q|
|-ri|long|(4|bytes)|||
|-rp|phrase|(8|bytes)|||
|-rd|double|phrase|(16|bytes)|
|-rq|quad|phrase|(32|bytes)|i|
|||
|For|example,|if the TEXT|segment|of the|output|file would|normally|be 434|
|;|bytes|long,|then|using|the|“-rp"|switch|would|cause|it to|be padded|in|length|to|
|440|bytes|long,|which|would|make the|end|of|the|segment|fall|on|a|phrase|{|
|boundary.|||
|:|The|-s|switch|causes|Madmac|to|generate|warning|messages|about|possible|
|;|unoptimized|forward|short|branches|in|68000|code.|This|is|used|to|point|out|
|{|branches|that could have been|short|(e.g.|“bra” could|be|“bra.s”).|:|
|popu|The|-u|switch|causes|Madmac|to force|all|referenced|and|undefined|symbols|4|
|4|to|be|global,|as though|they|had|been|explicitly|specified|with|the|.extern|or|||
|glob!|directives,|or defined|using a double-colon.|(See Symbols and Scope|||
|g|for more|information.)|
|This|switch|can|be used as a short|cut when|you|have|a|large number|of|;|
|:|external|symbols,|and|don’t want to use|individual|.extern|or .globl|directives|||
|||to|declare|each|one.|
|© 1994 Atari Corp.|Confidential Information|“A®®|Property ofAtari Corporation|& November,|1994|:|

**----- End of picture text -----**<br>


Madmac Macro Assembler 

1 ; : va = = = - z= 7 a ; = t 3 = | 2 i i ;| ee — a ' | og es = , oe a; { ; { q } : 3 j : 

; 

| } 

' 

|‘<br>||Page6<br>EET<br>wv|HO|Madmac Macro AssemblerMacro AssemblerAssembler<br> leeETETETETTETTEeeeTFFTFTEONNONoNoOOeee<br>Setverbose mode. This will causeMadmacto printoutthenames ofeach|
|---|---|---|---|
||<br>|<br>:<br>{<br>j|-y[pagelen]||sourcefileandincludefileasthey areprocessed. Verbosemodeis<br>automaticallyenteredwhenMadmaciscalled withnocommand fineand<br>promptsforyour input.<br>The-yswitch, followedimmediately byadecimalnumber (with no intervening<br>spaces), setsthenumberof lines inapagefortheassembly listing (if a listing<br>isrequestedwiththe -I switch).|
|||||Forexample,-y90wouldsetthenumberoflinesperpageto90.|
|||||ifthenumberoflines ismissing, or lessthan 10,an errormessage is<br>generated.|
|||UsingMadmac|||



Let’s assemble some sample files. Load your favorite text editor and create a small text file that looks like this: 

**==> picture [307 x 67] intentionally omitted <==**

**----- Start of picture text -----**<br>
start: - include “jaguar.inc”<br>| move .w #SFF80,BG<br>j illegal<br>-end<br>**----- End of picture text -----**<br>


Save the file as plain ASCII text to the filename TEST.S. Exit your editor, and at the DOS command line, type the following command: 

## mac test.s 

Assuming your system is setup correctly, this will call Madmac, which will assemble TEST.S and produce an object module file named TEST.O. If you see an error message telling you that Madmac cannot find the “JAGUAR.INC” file, then chances are you do not have your MACPATH environment variable set correctly. See the Getting Started section of your Jaguar Developer Documentation for information on how to set your environment variables. 

So now we have an object module, which isn’t of much use by itself until you run it through the linker, probably with other object modules, to create an executable program. But if you have been reading carefully, then you know that Madmac can generate an executable program file without requiring an external linker. This is useful for making small stand-alone programs that don’t require external references or library routines. For example, the following two commands: 

Mac test.s | aln -e -a 802000 x 4000 -o test.cof test.o | could be replaced by the single command: | mac -a 802000 x 4000 -o test.cof test.s i. 8 November, 1994 Confidential Information Information FPR Property ofAtari Corporation 

Confidential Information Information FPR Property ofAtari Corporation 

© 1994 Atari Corp. 

Page7 

| 

## Wadmac Macro Assembler 

To a certain degree, this can also be used to assemble multiple files at once, but it’s probably easier in Fmost cases to take advantage of the linker at that point. Now let’s try a few other command line options. Reload your text editor and load TESTS into it again. Change the text to look like this: 

|r|.include|“Jaguar.inc”|
|---|---|---|
|start:|||
||.if|colorl|
||move.w|#SFF80,BG|
|||-else||
|||move.w|4SFF40,BG|
||endif||
|||illegal||
|:|.end||



, 

Again, save the file as plain ASCII text. This time use the filename TEST2.S. Exit your editor, and at | the DOS command, type the following command: 

mac -ltest2.lst -y95 -o test2. cof -as 802000 x 4000 -Dcolorl=1 test2.s is produces an assembly listing file named TEST2.LST with 95 lines per page, writes an executable | program file (with symbols) to a file named TEST2.COF, and defines the symbol “color!” to have a | value of 1 when the TEST2.S file is assembled. F Download and run the program we just created to the Jaguar using the command line: 

| ' | ' | 

| rdbjag test2.cof -g -q 

! You'll see that all this program does is change the background color of the Jaguar screen by writing a | value to the BG register. Depending on how color] is defined, you will different colors. 

pace Mode Oe | Ifyou invoke Madmac with an empty command line, it will print a copyright message and prompt you for more commands with an asterisk character (*). This is useful if you want to assemble several files in | succession without reloading the assembler for each assembly. 

| 1 

In interactive mode, the assembler is also in verbose mode, as if you had specified “-v” on each || command line: 

**==> picture [78 x 26] intentionally omitted <==**

**----- Start of picture text -----**<br>
© 1994 Atari Corp.<br>**----- End of picture text -----**<br>


Confidential Information “JFR Property ofAtari Corporation 

8 November, 1994 

| 

Page 8 8 

| 

q 4 : | ‘ s = a i : 3 { t ; ; eo Ly @ 4 = q 4 | a | a : 7 , a : -— | | = | 1 = ] Po : ao 

| 

| | } | | | | : | | 

| . Page 8 8 ; Madmac Macro Assembler E: \JAGUAR\SRC\JAGMAND>mac -€ MADMAC Atari Macro Assembler : Copyright 1987-94 Atari Corp. 4 V3.03 Aug 20 1994 4 * -fb -g jagmand.s q {Including: jagmand.s] 4 {Including: jaguar.inc]} : {Leaving: jaguar.inc] | (Including: cry.pal] ‘ [Leaving: cry.pal] s {Leaving: jagmand.s]} = {Writing BSD object file: jagmand.o] a 33K used, 367 lines i * : 3 

You can see that Madmac gave a “blow-by-blow” account of the files it processed, as well as a summary of the assembler’s memory usage, and the number of lines processed (including macro and repeat-block expansion as appropriate). After the assembly is finished, Madmac prompts for another command line with the asterisk. At this point, you can either type in a new command line to be processed, or you can exit Madmac by hitting {Enter} on an empty line. 

## Things YouShouldBeAwareOf 

Madmac is a one pass assembler. This means that it gets all of the work done by reading each source file exactly one time, and then “back-patching” to fix up forward references. This one-pass nature is usually transparent to the programmer, with the following important exceptions: 

- ° Error messages may appear at the end of the assembly, referring to earlier source lines that contained undefined symbols. 

- ° All object code generated must fit in memory. Running out of memory is a fatal error that you must deal with by splitting up your source code files, resizing them, or by increasing your available memory.” 

- ° Forward branches (including BSR instructions) are never optimized to their short forms (because this would change the length of the code which has already been generated). To get a short forward branch, it is necessary to explicitly use the “.s” suffix in the source code. 

**==> picture [8 x 6] intentionally omitted <==**

**----- Start of picture text -----**<br>
val<br>**----- End of picture text -----**<br>


2 The PC/MSDOS version of Madmac is a DOS Protected Mode Interface program and is not subject to the 640K memory limitations of MS-DOS versions 6.22 and earlier. & November, 1994 Confidential Information PO® Property ofAtari Corporation © 1994 Atari Corp. 

**==> picture [44 x 47] intentionally omitted <==**

**----- Start of picture text -----**<br>
j i<br>4 ms<br>q “<br>**----- End of picture text -----**<br>


|Madmac Macro Assembler 

, | 

**==> picture [28 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 9<br>**----- End of picture text -----**<br>


## foward Branches 

Madmac does not automatically optimize forward branches for you, but it will tell you about them if you use the “-s” switch on the command line: 

| E:\JAGUAR\SRC\JAGMAND>mac -s example.s “example.s”, line 20: warning: unoptimized short branch 

1 With the “-e” switch, you can redirect the error & warning output to a file, and determine by hand (or | using editor macros) which forward branches are save to explicitly declare as short. 

j Madmac expects source code files to conform to the following rules: 

° Files must contain characters with ASCII values less than 128. Characters with ASCII values above 127 must be contained in strings (i.e. between single or double quotes) or in comments. 

, Lines of text are terminated by carriage return/linefeed, linefeed-only, or carriage return only. 4 (Carriage Return is ASCII value 13. Linefeed is ASCH value 10.) , ° The file is assumed to end with the last terminated line or with a Control-Z (ASCII 26). If there ; is text beyond the last line terminator, it is ignored. 

> [contain][up][to][ four][fields][ which][are][identified][ by][order][of][ appearance][and][terminating] |[A][ statement][ may] | characters. The general form of an assembler statement is: 

label: 

## operator operand(s) ; comment 

The label and comment fields are optional. An operand field may not appear without an operator field. | Operands are separated with commas. Blank lines are legal. If the first character on a line is an asterisk | (*) or semi-colon (;) then the entire line is a comment. A semi-colon anywhere on the line (except in a | string) begins a comment field which extens to the end of the line. 

| The label, if it appears, must be terminated with one or two colons. If it is terminated with a double | colon, it is automatically declared as a global. It is illegal to declare a confined symbol as global (see | Symbols and Scope). 

| 

## P Gqudtes ©9 

A statement may also take one of these special forms: 

Confidential Information “A@® Property ofAtari Corporation 

8 November, 1994 

1 

Madmac Macro Assembler 

4 ] i» iq - & . 7 ’ 3 7 = j f | 3 | oa = ; ‘ : a= _ ea , 4 ; @ ‘ = | oo q 3 : : a ; q a4 | oe | = | = q a q eS . aN ) - ] = 

| | 

’ 

; 

**==> picture [296 x 93] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 10<br>: symbol equ expression<br>| symbol = expression<br>| symbol == expression<br>symbol set expression<br>symbol req expression<br>**----- End of picture text -----**<br>


The first two forms are identical; they equate the symbol the value of an expression, which must be defined (no forward or external references). The third form, with two equals signs, is similar except that it also makes the symbol global. The fourth form allows a symbol to be set to a value any number of times at different positions within the same file, like a variable. The last form equates the symbol to a 16-bit register mask specifed bya register list. 

It is possible to equate confined symbols. For example: 

|cr<br>lf<br>DEBUG<br>count<br>count|equ<br>=<br>==<br>set<br>set|13<br>10<br>1<br>0<br>count+l|; <br>; <br>; <br>; <br>;|carriage return<br> linefeed<br> global debug flag<br> variable<br> increment the variable|
|---|---|---|---|---|
|-regs<br>-cr|reg<br>=|d3-d7/a3-a6<br>13|; <br>;|register list<br>confined(local)equate|



| SymbolsandScope = Symbols may start with an uppercase or lowercase letter (A-Z, a-z), an underscore (_), a question mark (?), or a period (.). Each remaining character may be any of these characters, except a period, a ’ numerical digit (0-9), or a dollar sign ($). Symbols are terminated with a character that is not a valid symbol continuation character (e.g. a period or comma, whitespace, etc.). 

Case is significant for user-defined symbols, but not for 68000, GPU, or DSP instruction mnemonics, assembler directives, or register names. 

Symbols are limited to 100 characters in length, but may be truncated to 8 characters if the DRI object module format is selected, or 16 characters if the Mark Williams object module format is selected. No warning or error message is given in the event of a conflict created by symbol names being truncated. If BSD object module output is selected, the entire symbol, up to 100 characters, is used. 

## For example, all of the following symbols are legal and unique: 

|reallyLongSymbolName<br>-reallyLongConfinedSymbolName<br>alo|-dc move<br>-move<br>frog|
|---|---|
|-al0|-frog|
|ret<br>dc|-a9<br>ad|



© 1994 Atari Corp. 

8 November, 1994 Confidential Information TER Property ofAtari Corporation 

4 

4 

| 

- .org = G_ RAM RAM P which equates a confined symbol to the value of the G_RAM equate, rather than setting the code generation address which the .ORG directive does (if the equal sign wasn’t there). 

## fMadmac Macro Assembler 

## Page Il 

**==> picture [455 x 200] intentionally omitted <==**

**----- Start of picture text -----**<br>
£2222 :<br>1.222? _fog<br>; 0 ?zippo?<br>F 00 sys$system<br>, .000 atari<br>q el Atari<br>P11 ATARI<br>F111 aTaRi<br>F While all of the following symbols are illegal:<br>| 12days dc.10 dc.z ‘quote<br>F @work ni.there Smoney$ ~tilde<br>| .right.here<br>**----- End of picture text -----**<br>


|£2222<br>:<br>1.222?<br>_fog<br>; 0<br>?zippo?<br>F<br>00<br>sys$system<br>, .000<br>atari<br>qel<br>Atari|||
|---|---|
|q el<br>Atari<br>P11<br>ATARI<br>F111<br>aTaRi|)|
|F While all of thethe following symbols are illegal:||
|| 12days<br>dc.10<br>dc.z<br>‘quote<br>F @work<br>ni.there<br>Smoney$ ~tilde<br>| .right.here<br>**|**<br>1<br>|<br>Symbolsbeginningwith aperiod (.)are confined; theirscope is limited to thespacebetween twonormal<br>:<br>(unconfined) labels. Confined symbolsmaybeeither labelsorequates. It is illegal tomakeaconfined<br>1 symbol global (with the .globl directive, adouble-colon, oradouble-equals). Only unconfined symbols<br>| delimitaconfined symbol’s scope; equates (ofany kind)do notcount. For example, all symbols are||
|F<br>uniqueand have unique values in the following:||
|P<br>zero::<br>subgq.w<br>#1,dl<br>:||
|bmi.s<br>-ret||
||<br>loop:<br>clr.w<br>(a0)+||
|{<br>dbra<br>d0,-loop\||
||<br>yret:<br>rts||
|FF::<br>subq.w<br>#1,dl||
|3<br>bmi.s<br>.99|||
||<br>sloop:<br>move.w<br>#-1,(a0)+<br>4<br>dbra<br>d0,.loop<br>,<br>.99<br>rts|{<br>||



| { | | | : q { 

| Confined symbols are useful as they allow the programmer to be much less inventive about finding ; small, unique names that also have meaning. 

| It is legal to define symbols that have the same name as processor mnemonics (such as “move”or “rts”) i or assembler directives. However, one should be careful when doing so to avoid typographical errors, such as this: -gpu .org = G_ RAM RAM 

**==> picture [1 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
}<br>**----- End of picture text -----**<br>


| Page 12 Madmac Macro Macro Keywords -ee: . The following names, in all combinations of uppercase and lowercase, are reserved keywords and may not be used as symbols (e.g. labels, equates, or macro names): equ set reg sr cer pe sp ssp usp do =. dil qd2 d3 d4 = 4d5 dé d7 : ao al a2oa3#=# a4 +=a5 a6 a7 ‘ rO rl r2 r3 v4 x5 xr6~ x7 rg rg r10 rll ri2 r13 14 ril5 : rl6 r1l7 r18 xr19 r20 r21 r22 123 j r24 r25 126 x27 4r28 429 1¥r30 £31 Constants)aaa re Numbers may be may be be decimal, hexadecimal, octal, binary, or concatenated concatenated ASCII. The default radix is ; decimal, and and it may not be changed. may not be changed. not be changed. be changed. Decimal numbers ar specified numbers ar specified ar specified specified with a string of digits (0-9). a string of digits (0-9). string of digits (0-9). of digits (0-9). digits (0-9). (0-9). Hexadecimal numbers are specified are specified with a leading dollar sign leading dollar sign dollar sign sign ($) followed followed byaa string of digits of digits digits digits (0-9) or uppercase or lowercase or lowercase lowercase letters (a-f, A-F). Octal numbers are specified with a leading at-sign (@) followed by by a string string of octal octal digits (0-7). Binary numbers are are specified with a leading leading percent sign (%) followed bya byaa string of binary digits of binary digits binary digits digits (0-1). Concatenated ASCII ASCII constants are specified by specified by by enclosing from one one to four characters four characters characters in single or double double quotes. For example: i 1234 decimal | $1234 hexadecimal | @777 octal %10111 binary “gn ASCII ‘frog’ ASCII Negative numbers numbers are specified with with a unary minus (-). For example: example: | -5678 -@334 -$4e71 | ~%11011 -'2' —"WIND” | eo ,,,rrrrtrsS—=—‘é#EERReClDU6U©pFpm6mhmfmhmseseseSseee CD \ Strings are contained between double (") or single are contained between double (") or single contained between double (") or single between double (") or single double (") or single (") or single or single single (’) quote marks. quote marks. Strings may contain may contain contain non-printable characters by specifying “backslash” escapes, by specifying “backslash” escapes, specifying “backslash” escapes, “backslash” escapes, escapes, similar to the ones used the ones used ones used used in the C programming C programming programming language. MADMAC will generate will generate generate a warning warning if a backslash a backslash backslash is followed by a character followed by a character by a character a character character not appearing appearing below: f i 

Madmac Macro Macro Assembler 

| j ee radix is (0-9). of digits digits digits (0-9) or (@) percent sign (%) by enclosing ’ 1 F ] { | | 1 CD non-printable language. ] below: { © 1994 Atari Corp. 1994 Atari Corp. Atari Corp. Corp. ; 

Constants)aaa re ee 

Keywords -ee: 

**==> picture [529 x 632] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|
|equ|set|reg|sr|cer|pe|sp|ssp|usp|
|do|=.|dil|qd2|d3|d4|=|4d5|dé|d7|
|ao|al|a2oa3#=#|a4|+=a5|a6|a7|
|rO|rl|r2|r3|v4|x5|xr6~|x7|
|rg|rg|r10|rll|ri2|r13|14|ril5|
|rl6|r1l7|r18|xr19|r20|r21|r22|123|
|r24|r25|126|x27|4r28|429|1¥r30|£31|
|Constants)aaa|re|ee|
|Numbers may be may be be|decimal,|hexadecimal,|octal,|binary,|or concatenated concatenated|ASCII.|The|default|radix|is|
|decimal, and and|it may not be changed. may not be changed. not be changed. be changed.|Decimal numbers ar specified numbers ar specified ar specified specified|with a string of digits (0-9). a string of digits (0-9). string of digits (0-9). of digits (0-9). digits (0-9). (0-9).|
|Hexadecimal|numbers are specified are specified|with|a leading dollar sign leading dollar sign dollar sign sign|($) followed followed|byaa|string of digits of digits digits digits|(0-9)|or|
|uppercase or lowercase or lowercase lowercase|letters|(a-f,|A-F).|Octal|numbers|are|specified|with|a|leading|at-sign|(@)|
|followed by by|a string string|of octal octal|digits|(0-7).|Binary|numbers are are|specified|with|a leading leading|percent|sign|(%)|
|followed bya byaa|string of binary digits of binary digits binary digits digits|(0-1).|Concatenated ASCII ASCII|constants|are specified by specified by by|enclosing|
|from one one|to four characters four characters characters|in|single|or double double|quotes.|For example:|
|1234|decimal|
|$1234|hexadecimal|
|@777|octal|
|%10111|binary|
|“gn|ASCII|
|‘frog’|ASCII|
|Negative numbers numbers|are|specified with with|a|unary|minus|(-).|For example: example:|
|-5678|-@334|-$4e71|
|~%11011|-'2'|—"WIND”|
|eo|,,,rrrrtrsS—=—‘é#EERReClDU6U©pFpm6mhmfmhmseseseSseee CD|
|Strings are contained between double (") or single are contained between double (") or single contained between double (") or single between double (") or single double (") or single (") or single or single single|(’) quote marks. quote marks.|Strings may contain may contain contain|non-printable|
|characters by specifying “backslash” escapes, by specifying “backslash” escapes, specifying “backslash” escapes, “backslash” escapes, escapes,|similar|to the ones used the ones used ones used used|in|the C programming C programming programming|language.|]|
|MADMAC will generate will generate generate|a warning warning|if a backslash a backslash backslash|is followed by a character followed by a character by a character a character character|not appearing appearing|below:|{|
|November, 1994 1994|Confidential Information|F7®®|Property ofAtari Corporation|© 1994 Atari Corp. 1994 Atari Corp. Atari Corp. Corp.|;|

**----- End of picture text -----**<br>


8 November, 1994 1994 

Madmac Macro Assembler 

Page 13 

|||\\|$5C|backslash|
|---|---|---|---|---|
||:|\n|$0A|line feed (newline)|
|||\b|$08|backspace|
||||\t|$09|tab|
|||\r|$0D|Carriage Return|
|||\f|$0C|form-feed|
|||\e|$1B|escape|
|||\|$27|single quote|
|||\”|$22|doublequote|



It is possible for strings (but not symbols) to contain characters with their high bits set (i.e. character codes 128... 255). 

You should be aware that backslash characters are popular in MS-DOS and GEMDOS path names, and that you may have to escape backslash characters in your source code. For example, to get the filename "C:\AUTO\AHDL.S” you would specify the string “C:\AUTO\\AHDI .S". 

Register lists are special forms used with the movem 68000 mnemonic and the reg directive. They are J 16-bit values, with bits 0 through 15 corresponding to registers DO through A7. A register list consists of " aseries of register names or register ranges separated by slashes. A register range consists of two register names, Rm and Rn, m < n, separated by a dash. For example: 

Note: older versions of Madmac supported the use of register names RO, RI, ... R15 as register names. This is no longer supported because these are now reserved as Jaguar GPU & DSP register names. 

|Resister list|Value|
|---|---|
|d0-d7/a0-a7|SFFFF|
|d2-d7/a0/a3-a5|$39FC|
|d0/d1/a0-a3/d7/a6-a7|SCF83|
|dd|$0001|



Register lists and resister equates may be used in conjunction with the movem 68000 mnemonic, as in this example: 

**==> picture [529 x 155] intentionally omitted <==**

Page 14 

| 

Madmac Macro Assembler 

fl i : 

i : . . i + S ae i 2 “ | BS | 7 

## , iii 

All values are computed with 32-bit 2's complement arithmetic. For Boolean operations (such as if or assert) zero is considered false, and non-zero is considered true. 

Expressions are evaluated Strictly left-to-right, with no regard for operator precedence. 

Thus the expression "1 + 2 * 3~ evaluates to 9, not 7. However, precedence may be forced with parenthesis (()) or square brackets ({])- 

. Expressions belong to one of three classes; undefined, absolute or relocatable. An expression is undefined if it involves an undefined symbol (e.g. an undeclared symbol, or a forward reference). An expression is absolute if its value will not change when the program is relocated (for instance, the values).number 0, all labels declared in an ABS section, and all Jaguar hardware register locations are absolute 

section.An expression is relocatable if it involves exactly one symbol that is contained in a text, data or BSS Only absolute values may be used with operators other than addition (+) or subtraction (-). It is illegal, for instance, to multiply or divide by a relocatable or undefined value. Subtracting a relocatable value from another relocatable value in the same section results in an absolute value (the distance between them, positive or negative). Adding (or subtracting) an absolute value to or from a relocatable value yields a relocatable value (an offset from the relocatable address). . 

It is important to realize that relocatable values belong to the sections they are defined in (e.g. text, data or 1355), and it is not permissible to mix and match sections. For example, in this code: linel: dc.l line2, linel+8 line2: dc.1 linel, line2-8 error:line3: dc.ldc.1 linelt+line2,line2-linel. line28 ,» 1, line3/4 

Line 1 deposits two long words that point to line 2. Line 2 deposits two long words that point to line 1. Line 3 deposits two long words that have the absolute value eight. The fourth line will result in an assembly error, since the expressions (respectively) attempt to add two relocatable values, shift a relocatable value night by one, and divide a relocatable value by four. labelThe pseudo-symbol “*”bar": (asterisk) has the value that the current section's location counter had at the | beginning of the current source line. For example, these two Statements deposit three pointers to the foo: dc.l *+4 | bar: dc-I *, * 

8 November, 1994 

Confidential Information FPR Property ofAtari Corporation 

© 1994 Atari Corp. 

| | | | | ; | | | | || | | | |[|] | ' | | 

Page 15 

: ; j 

## |Madmac Macro Assembler 

Similarly, the pseudo-symbol “$” has the value of the current section's location counter, and it is kept up to date as the assembler deposits information “across” a line of source code. For example, these two ’ statements deposit four pointers to the label "zip"; 

zip: dc.l $+8, $+4 zop: dc.1 $, $-4 

## Se 

r,r,,rti“=~””-CsCOWsz‘CSCOCOWCO®SCOWCOOC;Cétsdt 

**==> picture [279 x 98] intentionally omitted <==**

**----- Start of picture text -----**<br>
Operator Description<br>- Unary minus (2's complement).<br>| Logical (Boolean) NOT.<br>~ Tilde: bitwise not (I's complement).<br>“defined symbol True if symbo! has a value.<br>“referenced symbol True if symbo! has been referenced.<br>“Astreq stringi string2 True if the strings are equal.<br>ssmacdef macroName __ True if the macro is defined.<br>**----- End of picture text -----**<br>


| a The Boolean operators generate the value 1 if the expression is true, and 6 if it is not. fe A symbol is referenced if it is involved in an expression. A symbol may have any combination of : attributes: undefined and unreferenced, defined and unreferenced (i.e. declared but never used), undefined and referenced (in the case of a forward or external reference), or defined and , referenced. ,r,rt-r—r~—=“#Cs<é ésC;C*;S#“S§SOQpannOCCC eo eo | 

**==> picture [417 x 144] intentionally omitted <==**

**----- Start of picture text -----**<br>
 ,r,rt-r—r~—=“#Cs<é ésC;C*;S#“S§SOQpannOCCC eo eo<br>Operator Description<br>f+-*/ _| The usual arithmetic operators.<br>r&i* _| Bit-wise AND, OR and Exdusive Or.<br><> ___| Bit-wise shift left and shift right.<br>f< <= >= > | Boolean magnitude comparisons.<br>f=_——_| Boolean equality.<br>P<>_|!= Boolean inequality.<br>**----- End of picture text -----**<br>


° All binary operators have the same precedence: expressions are evaluated strictly left to right. 

> p ° Division or modulo by zero yields an assembly error. ° The "<>" and 'l=" operators are synonyms. _ Note that the modulo operator (%) is also used to introduce binary constants (see: Constants). A ' percent sign should be followed by at least one space if it is meant to be a modulo operator, and is followed by a '0' or ‘1’. 

**==> picture [5 x 8] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


;[Ol][994][Atari Corp.] 

Confidential Information ‘JPR. Property ofAtari Corporation 

8 November, 1994 

: 

, 

**==> picture [1 x 24] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


Page 16 Madmac Macro Assembler | : Special Form Description 2 |**time|**date| The current system date (GEMDOS format). | = —___| The current system time (GEMDOS format). | = ° The “date” special form expands to the current system date, in GEMDOS format. The format , is a 16-bit word with bits ... .4 indicating the day of the month (1... 31), bits 5. .8 indicating the 4 5 month (I... 12), and bits 9... 15 indicating the year since 1980, in the range 0... 119. | = ° The “**time" special form expands to the current system time, in GEMDOS format. The format : y 4 is a 16-bit word with bits 0-4 indicating the current second divided by 2, bits 5-10 indicating the Es current minute (0-59), and bits 11-15 indicating the current hour (0-23). . > Example Expressions © 95 ee ; e. line address contents source code : 1 00000000 =.4480 labl: neg.1 do | oe 2 00000002 427900000000 ~—iab2: clr.w labi | 3 =00000064 equi = 100 | Pa 4 =00000096 equ2 = equl + 50 a 5 00000008 00000064 de.1 labl + equl “ 6 0000000c 7FFFFFE6 dc.1 (equl + ~-equ2) » 1 7 00000010 0001 dc.w ““defined equl : 4 8 00000012 0000 dc.w ““referenced lab2 ] “ 9 00000014 00000002 de.1 lab2 | 10 00000018 0001 de .w “*referenced lab2 , 11 OO0O001A 0001 dc.w labl = (lab2 - 6) q 4 Lines 1 through44 are used to set up the rest of the example. used to set up the rest of the example. to set up the rest of the example. up the rest of the example. the rest of the example. rest of the example. of the example. the example. example. Line 5 deposits a relocatable pointer deposits a relocatable pointer a relocatable pointer to the the : . location 100 bytes beyond the label lab]. bytes beyond the label lab]. beyond the label lab]. the label lab]. label lab]. lab]. Line 6 is a nonsensical 6 is a nonsensical is a nonsensical a nonsensical nonsensical expression that uses the ~ and that uses the ~ and uses the ~ and the ~ and ~ and and rightj a shift operators. operators. Line 7 deposits a word of 7 deposits a word of deposits a word of a word of word of of 1 because the symbol equJ symbol equJ is defined (in defined (in (in line 3). Line 8 8 deposits a word of 0 because a word of 0 because word of 0 because of 0 because 0 because because the symbol lab2, defined in symbol lab2, defined in lab2, defined in defined in in line 2, has not been has not been not been been referenced. But the the ] expression in line 9 references the symbol references the symbol the symbol symbol lab2, so line so line line 10 (which (which is a copy of line a copy of line copy of line of line line 8) deposits a word of deposits a word of a word of word of of 1 Finally, line 11 deposits a word of a word of word of of 1 because the boolean boolean equality operator evaluates operator evaluates to true. ‘ operators ““defined and “referenced ““defined and “referenced “referenced are particularly useful useful in conditional assembly. For conditional assembly. For assembly. For For . instance, you can automatically you can automatically can automatically automatically include debugging code debugging code if the debugging code the debugging code debugging code code is referenced, referenced, as in: 4 

Lines 1 through44 are used to set up the rest of the example. used to set up the rest of the example. to set up the rest of the example. up the rest of the example. the rest of the example. rest of the example. of the example. the example. example. Line 5 deposits a relocatable pointer deposits a relocatable pointer a relocatable pointer to the the location 100 bytes beyond the label lab]. bytes beyond the label lab]. beyond the label lab]. the label lab]. label lab]. lab]. Line 6 is a nonsensical 6 is a nonsensical is a nonsensical a nonsensical nonsensical expression that uses the ~ and that uses the ~ and uses the ~ and the ~ and ~ and and rightshift operators. operators. Line 7 deposits a word of 7 deposits a word of deposits a word of a word of word of of 1 because the symbol equJ symbol equJ is defined (in defined (in (in line 3). Line 8 8 deposits a word of 0 because a word of 0 because word of 0 because of 0 because 0 because because the symbol lab2, defined in symbol lab2, defined in lab2, defined in defined in in line 2, has not been has not been not been been referenced. But the the expression in line 9 references the symbol references the symbol the symbol symbol lab2, so line so line line 10 (which (which is a copy of line a copy of line copy of line of line line 8) deposits a word of deposits a word of a word of word of of 1. Finally, line 11 deposits a word of a word of word of of 1 because the boolean boolean equality operator evaluates operator evaluates to true. The operators ““defined and “referenced ““defined and “referenced “referenced are particularly useful useful in conditional assembly. For conditional assembly. For assembly. For For instance, you can automatically you can automatically can automatically automatically include debugging code debugging code if the debugging code the debugging code debugging code code is referenced, referenced, as in: 

| 

8 November, 1994 

Confidential Information FPR Property ofAtari Corporation 

© 1994 Atari Corp. 4 

Page 17 

| | | | | | | | | ! | | ' ] { ' ‘ ] | 

**==> picture [466 x 148] intentionally omitted <==**

**----- Start of picture text -----**<br>
Madmac Macro Assembler<br>lea string,aO > aQ -> message<br>! | jsxr debug ; print a message<br>j rts ; and return<br>string:<br>4 dc.b "Help me, Spock!",0 ; (the message)<br>.iif **defined debug, -.include "debug.s"<br>**----- End of picture text -----**<br>


| The jsr statement references the symbol debug. Near the end of the source file, the .iif statement includes the file "debug.s" if the symbol debug was referenced. In production code, presumably all references to the debug symbol will be removed, and the DEBUG.S debugging source code file will not be included. (We could have as easily made the symbol debug external, instead of including another source file). 

Ce, _sdséCiésazsrédo,slr’tzwtONtC(#(;t#C.LlU | Assembler directives may be any mix of upper- or lowercase. The leading periods are optional, though t they are shown here and their use is encouraged. Directives may be preceeded by a label; the label is | defined before the directive is executed. Some directives accept size suffixes (.b, .s, .w or .1); the default is word (.w) if no size is specified. The .s suffix is identical to .b. 

_sdséCiésazsrédo,slr’tzwtONtC(#(;t#C.LlU 

|#<br>|<br>:|Directive|Description<br>Switch to6502 assembly mode. The location counter is undefined, and must be set<br>withthe .orgdirectivebeforeany code canbegenerated.<br>Insidea6502segment, thede.w directive will produce6502-formatwords (little-<br>endian,with lowbyte first).||
|---|---|---|---|
|||Thereservedkeywordsforothersections(d0-d7/a0-a7/ssp,usp,andsoon)remain<br>reserved (and thus unusable) while inthe6502section.||
|||The directives globl, dc.!, deb.!, text, data, bss, abs, even andcomm are illegal in||
|||the 6502 section.||
|||It is permitted, though probably not useful, to generate both6502and68000code in<br>thesame objectfile.||
|||Please note thatthe6502assemblycapabilities ofMADMAChavenotbeentested<br>since theaddition oftheJaguarGPUandDSPassemblymodes.<br>Itisquitepossible|||
|||thatthe6502capabilitiesarebroken incurrentversions ofMADMAC.||
||.68000|Switchto680x0assembly mode. Thisdirectivemustbeused withintheTEXTor<br>DATAsegments. Instructions forthe6502, JaguarGPU, andJaguarDSPmay not||
||assertexpression<br>[expression]|beassembled while in 680x0 assembly mode.<br>Assertthattheconditionsaretrue (non-zero). Ifany ofthecomma-separated<br>expressions evaluates tozeroan assemblerwarning is issued. Forexample:||
|||-assert *-start = $76<br>assertstacksize>=$400||



a ©1994 Atari Corp. Confidential Information “PER Property ofAtari Corporation 8 November, 1994 

; 

j { . 1 j 

**==> picture [578 x 726] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 18 Madmac Macro Assembler<br>VWV7——AA<br>: -AUTOEVEN Enables automatic word alignment between directives and instructions. For example,<br>: if you do:<br>-DC.B $12<br>] -DC.L $3456789A<br>1 a ligned,nd the addressthen Madmac at the DC.Lwill pa d irectivewith a followingzero byte thebefore .DC.Bthe directiveDC.L directive.is not word-This<br>‘ results in $12 $00 $34 $56 $78 $9A being output. This is the default mode of<br>| -bss operation.Switch to the BSS, DATA or TEXT segments.<br>| -text.data The TEXT segment typically contains your executable program code. The DATA<br>segment typically contains pre-initialized data (strings, tables, etc.). The BSS<br>] segment is used for uninitialized data storage.<br>' Instructions and data may not be assembled into the BSS segment, but symbols may<br>. be defined and storage may be reserved with the .ds directive. Each assembly starts<br>out in the text segment.<br>: -cargs Compute stack offsets to C (and other language) arguments. Each symbol is<br>7 [#expression,] assigned an absolute value (like equ) which starts at expression and increases by<br>symbol|.size] the size of each symbol, for each symbol. If the expression is not supplied, the<br>[. symbol{.size}...] default starting value is 4. For example:<br>-cargs #8, .fileName.1, openMode, .bufPointer.1<br>could be used to declare offsets from register A6 to a pointer to a filename, a word<br>containing an open mode, and a pointer to a buffer. (Note that the symbols used here<br>are confined). Another example, a C-style "string-length" function, could be written<br>as:<br>strilen:: j<br>f -cargs -Sstring ; declare arg i<br>' move.1l .string(sp),ao ; a0 -> string :<br>. . moveq #-1,d0 ; initial size = -1 4<br>addgq.1 #1,d0 ; bump size<br>tst.b (aO)+ ; at end of string? Po<br>'<br>bne -1 7 (no -~ try again) 4<br>5 rts ; return string length ‘<br>' -CCDEF expression Allows you to define names for the condition codes used by the JUMP and JR<br>' instructions for GPU/DSP code. For example: 7<br>Always - CCDEF 0 ]<br>| jump Always, (x3) : ‘Always' is actually 0 :<br>-CCUNDEF Undefines a register name previously assigned using the .CCDEF directive. This is -<br>registername only implemented for GPU/DSP code sections. dq<br>: -CLEAR After this directive, Madmac allows the use of the CLR.L instruction for the 680x0. 4<br>The CLR.L instruction does not work properly on the Jaguar when accessing iE<br>q hardware register locations. The default state is .CLEAR. : :<br>| comm symbol, Specifies a label and the size of a common region. The label is made global, thus .<br>‘ expression confined symbols cannot be made common. The linker groups all common regions of |}<br>' thethe samefile is name;linked. the largest size determines the real size of the common region when | > q|<br>i 8 November, 1994 Confidential Information 7PR Property ofAtari Corporation © 1994 AtariCorp. 3<br>**----- End of picture text -----**<br>


**==> picture [566 x 757] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|ji|
|j|Madmac Macro Assembler|Page 19|||
|.DC.I expression|This directive generates|long|data values and|is|similar to the DC.L directive,|except|
|||
|7|GPU/DSP|MOVEL|instruction.|
|||
|f|| .de[.size]|expression|Deposit initialized|storage|in the current section.|If the specified|size|is word|(.w)|or|
|||| Lexpression...|]|long|(.b),|the assembler|will|execute an|.even|directive|before|depositing|data.|If the|
|'|size|is byte|(.b), then|strings that are not part of arithmetic expressions are deposited|||
|'|byte-by-byte.|||
|'|if no size|is specified, the default is .w.|||
|This|directive|cannot|be|used|in|the|BSS|section.|
|;|||.deb[.size]|Generate an|initialized|block of expression?|bytes,|words|or longwords|of the value|
|expression?|expression2.|If the specified|size|is word|or long, the assembler will|execute an|:|]|
|||Lexpression2,...]|.even directive before generating data.|-|||
|ql|i|
|.|If no size|is specified, the default|is|.w.|||
|q|This|directive cannot be used|in the BSS|section.|
|f||}.DPHRASE|Align the program counter to the next integral double phrase boundary|(16 bytes).|||
|q|,|are actually|part|of the TEXT or DATA segments.|Therefore,|to|align GPU/DSP|]|
|F|a Notecode,thatalignGPU/DSPthe|currentcodesectionsectionsbefore areand not containedafter the GPU/DSP within theircode.own segments, and|||
|||||.ds[.size]|expression|Reserve space|in the current segment for the appropriate number of bytes, words or|
|4|longwords.|If the size|is word|or|long,|the assembler|will|execute|an|.even|directive|||
|,|before|reserving|space.|||
|If no|size|is|specified,|the|default|size|is|.w.|
|||
|This|directive can|only|be|used|in|the BSS|or ABS|sections|(in TEXT|or DATA,|use|
|‘|.dc.b to reserve large chunks|of|initialized|storage.)|
|:|Switch to Jaguar DSP assembly mode.|This|directive must be used within the TEXT|
|q|or DATA segments.|All DSP instructions, as defined|in the Jaguar Software|||
|Po Reference|Manual -|Tom And Jerry, may be assembled while in DSP assembly|
|Teject-~*«||mode.|
|:|IssueapageEnd|the assemblyejectinthelistngfle,of the|current|file.|In|an|include|file,|ends the|include|file and|
|resumes assembling|the|superior|file.|This statement|is|not|required,|nor|are warning|
|messages|generated|if|it|is missing|at the|end|of a|file.|This|directive may be used|
|inside|conditional|assembly,|macros|or|.rept|blocks.|
|F|| LEQUR expression|Allows you to namea|register.|This|is only implemented for GPU/DSP code|
|sections.|For|example:|
|Clipw|.EQUR|r19|||
|]|add|ClipW,r0|;|ClipW|actually|is|ri9|
|LE.|| registername|only implemented|for GPU/DSP code|sections.|||
|1|If the|location|counter for the|current section|is odd,|make|it even|by adding|one to|it.|
|:|In text and|data|sections a zero|byte|is deposited|if necessary.|See also the|]|
|.|directives .long,|.phrase,|.dphrase,|and|.qphrase.|1|
|a|ia|
|-|©1994 Atari Corp.|Confidential Information|TER|Property ofAtari Corporation|8 November,|1994|4|

**----- End of picture text -----**<br>


**==> picture [593 x 726] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|Page 20|Madmac Macro Assembler|
|eeeen|EEL|||
|-|||.globl|symbol|Each symbol|specified|is made global.|if the symbol|is defined|in the assembly,|the|
|‘|[symbol...]|symbol|is|exported|in|the|object|file.|If the symbol|is|undefined|at the end|of the|
|j|assembly, and|it was|referenced|(i.e.|used|in an|expression),|then the symbol|vaiue|
|||extern|symbol|is imported|as an|external|reference|that|must be|resolved|by the|linker.|
|[.symbol...]|
|j|None|of the symbols may be confined|symbols|(those|starting|with a|period).|
|||goto|/abel|ThisThe .externdirectivedirectiveprovidesisunstructured merely a synonymflow|of forcontrol .globi.within|a|macro|definition.|It|will|
|||transfer|control|to|the|line|of the|macro|containing the|specified|goto|label.|A|goto|
|label|is|a|symbol|preceeded|by|a|colon|that|appears|in|the|first column|of a|source|]|
|line|within|a|macro|definition;|
|:label|
|||where|the|label|itself can|be any|valid symbol|name,|followed|immediately|by|
|q|whitespace and a|valid|source|line|(or end|of|line).|The colon|must appear|in the|first|
|:|column.|
|:|The goto-label|is removed from the source line prior to macro expansion-|to|all|]|
|||expansionintents and does purposesnot take the labelplace withinis invisiblethe|label. except to the .goto directive. Macro|;|
|For example,|here|is a|silly way to count from|1|to|10 without using|.rept:|
|-macro|Count|4|
|f|count|set|1|
|||: loop|dc.w|count|
|:|i|count|set|count|+|1|
|:|iif|count|<=|10,|goto|loop|:|
|7|-endm|F|
|Switch|to Jaguar GPU|assembly|mode.|This|directive|must|be|used|within|the TEXT|
|||or DATA segments.|All GPU|instructions,|as|defined|in the Jaguar Software|||
|]|Reference|Manual|- Tom And Jerry,|may be assembled|while|in GPU|assembly|
|:|mode.|||
|1|.if expression|Start a block|of conditional|assembly.|If the expression|is true|(non-zero)|then|q|
|:|.else|assemble|the statements|between|the|if and|the|matching|endif or|else.|if the|1|
|||endif|expression|is false,|ignore the statements|unless a matching|else|is encountered.|4|
|Conditional|assembly may be nested|to any depth.|A|
|[|It|is possible to exit a conditional assembly block early from|within|an|include|file|(with|E|
|4|end)|or a macro|(with endm).|j|
|Jif expression,|Immediate version|of|if.|If the expression|is true|(non-zero)|then|the statement,|which|j|
|Statement|may|be an|instruction,|a|directive or a|macro,|is|executed.|If the|expression|is|false,|4|
|the statement|is|ignored.|No|.endiif|is|required.|For|example:|||
|i|
|;|-iif|age|<|21,|canDrink|=|0|q|
|j|-iif|weight|>|500,|dangerFlag|=|1|i|
|-iif|!(“°“defined|DEBUG).|include|dbsre|‘|
|||8 November, 1994|Confidential Information|7O®|Property of|Atari Corporation|© 1994 Atari Corp.|

**----- End of picture text -----**<br>


| | | : | || | q | | | | | | | : 

**==> picture [556 x 736] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|
|@|=|Madmac Macro|Assembler|Page 21|
|ip|fae|.INCBIN filename|include a binary file in your source at the present position.|The syntax is the same as|
|a|the INCLUDE|directive.|If no filename extension|is specified, then|.BIN|is added|
|||automatically.|The data in the binary|file is included verbatim|in the output file.|For|
|4|example:|
|a|picture_dat::|
|a|.INCBIN|“picture.dat"|
|i|will|include the data within the file PICTURE.DAT|at the|position following the|
|i|4|picture_dat|label.|
|&.|Note that for large files,|it's much more efficient to use the|"-i" or|“-ii"|switch of the|
|(a|ALN|linker rather than the .INCBIN|directive; your compile times and object file sizes|
|4|will|be|significantly|shorter.|
|3|include|“file”|-|includea|file.|If the filename|is not enclosed|in quotes, then a default extension|of|".s”|
|7|is applied to|it.|if the filename|is quoted, then the name is not changed|in any way.|
|Note:|_|If the filename|is not quoted and|not a valid symbol, then the assembler will|
|.|
|.|generate an error message. You should enclose filenames such as “ATARI.S”|in|
|t|a|quotes, because such names are not valid symbols.|
|q|if the include file cannot be found|in the current directory, then the directory search|
|4|path,|as specified|by|-i on the conunandiine, or by the MACPATH|
|||4|enviroment string,|is traversed.|
|—|||nit|size}|Generalized|initialization directive.|The size specified on the directive becomes the|
|yy|‘|[#expression]|default size for the rest of the line. (The "default" default size is .w.)|A comma-|
|_|expression({.size]|separated|list of expressions follows the directive; an expression may be followed by|
|zz|[,|.--]|a size to override the default size. An expression|may be preceeded by a sharp sign,|||
|1|q|an expression and a comma, which specifies a repeat count to be applied to the next|
|4|expression.|For example;|
|7|-init.1|-1,|O.w,|#16,'z'.b,|#3,0,|11.b|
|2.| a|will deposit a longword|of -1, a word of zero, sixteen bytes of lower-case|‘2’, three|
|.|longwords|of zero, and a byte of|11.|No auto-alignment|is performed within the line,|
|a|but a even|is done once at the beginning|(before the first value is deposited)|if the|
|a|default size is word or long.|
|zz|After this directive,|a NOP|instruction will|automatically|be added after each JUMP or|
|q|'|JR|instruction|in GPU or DSP assembly mode.|The default|is for padding to be|
|7|turned|off.|Each time you switch sections using the .GPU or .DSP directives,|
|||a|padding|is turned|off.|
|gy|Enable|or disable source code|listing.|These directives|increment and decrement|an|
|]|q|internal counter, so they may be appropriately nested. They have no effect|if the -!|
|'|4|switch|is not specified on the commandline.|
|.|«LONG|Align the program counter to the next integral long boundary|(4 bytes).|Note that|
|:|a|GPU/DSP code sections are not contained within their own segments, and are|
|j|a|actually part of the TEXT or DATA segments.|Therefore,|to align GPU/DSP code,|
|fg|align the current section before and after the GPU/DSP code.|
|j|i.|«macro name [formal,|Define a macro called name with the specified formal|arguments. The macro|
|~|.|3|formal,|...]|definition|is terminated with a .endm statement. A macro may be exited|early with the|
|yy|endm|_exitm directive.|See the chapter on Macros for more information.|,|
|.exitm|
|:|7 ann|
|Ge|«=© 1994 Atari Corp.|Confidential Information|‘PER|Property ofAtari Corporation|8 November, 1994|

**----- End of picture text -----**<br>


mi { } ] ] ' ; | | : j | 1 : : q z= q 

**==> picture [559 x 607] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|Page 22|Madmac Macro Assembler|
|q|-macundef|Remove the macro definition for the specified macro names.|If reference|is made to a|
|macroName|macro that|is|not defined,|no|error message|is printed and the name|is|ignored.|
|[.macroName...]|
|:|Older versions|of Madmac|recognized|the|.undefmac|directive.|In|current|versions|
|‘|formerly known|as:|of MADMAC,|the .undefmac|directive has been|replaced|by the .macundef|
|;|-undefmac|directive.|
|q|macroName|
|:|[.macroName...]|
|-NOAUTOEVEN|Disables|automatic|word|alignment|between|directives|and|instructions.|For|
|||example,|if you|do:|
|-DC.B|$12|
|||-DC.L|$3456789A|
|;|then|Madmac will output $12 $34 $56 $78 $9A regardless|of the alignment of the|
|data.|This|directive|does|not|affect the|directives|.EVEN,|.LONG,|.PHRASE,|
|j|-DPHRASE,|.QPHRASE|or|"-r"|commandline|switch.|The|default mode|of operation|
|j|is|AUTOEVEN.|
|:|-NOCLEAR|After this|directive,|Madmac no|longer|allows the use|of the CLR.L|instruction|for the|
|q|680x0.|The CLR.L|instruction|does|not work|properly on|the Jaguar when|accessing|
|.|hardware|register|locations.|The|default|state|is|CLEAR.|
|-NOJPAD|After this|directive,|NOP|instructions|will|no|longer|be added|automatically|after|each|
|:|-NOLIST|Turns|off the assembly|listing|output.|This|is|basically the same as|the|.NLIST|
|:|-Offset|[/ocation]|Start an|absolute section,|beginning|with the specified|/ocation|(or zero,|if no|location|
|:|is|specified).|An|absolute|section|is|much|like|BSS,|except|that|locations|declared|
|:|formerly known|as:|with|the|.ds|directive|are|absolute|and|not|relocatable|by the|linker.|This|directive|is|
|:|-abs|[location]|useful|for declaring|structures|or hardware|locations.|For example,|the following|
|||equates:|
|||VPLANES|=|ft)|
|i|||VWRAP|=|2|
|‘|CONTRL|=|4|
|:|INTIN|=|8|
|PISIN|=|12|
|could|be|as|easily|defined|as:|
|4|.abs|
|:|VPLANES|:|ds.w|1|
|q|VWRAP:|ds.w|1|
|.|,|CONTRL:INTIN:|ds.1ds.1|11|
|:|PTSIN:|ds.1|1|
|j|Older versions|of MADMAC|recognized|the .abs|directive.|In|current|versions|of|
|{|MADMAC,|the .abs|directive|has been|replaced|by the|.offset directive.|

**----- End of picture text -----**<br>


8November, 1994 Confidential Information FER Property ofAtari Corporation © 1994 AtariCorp. 

| | | | . / | | | } | | |: | 

|||||Page23|
|---|---|---|---|---|
|a<br>MadmacMacroAssembler<br>jij ZN ORGexpession<br>|||||Definetheoriginaddressusedforcodegeneration. Itsetsthevalueofthelocation<br>counter (orpc)tothevaluespecifiedbyexpression,whichmustbedefined, and<br>absolute.|
|:<br>|||||The.ORG directive is intendedforJaguarGPU, JaguarDSP, or6502code. It is not<br>legal in68000sections. For6502sections,theaddressspecifiedmustbejessthan<br>$10000 (the upper limit ofthe6502address range.)|
|j|:<br>j<br>|||PRINTexpression|Allsymboitsgeneratedfollowingthisdirectivewillbenon-relocatable.<br>Aligntheprogramcountertothe nextintegralphraseboundary (8 byte). Notethat<br>GPU/DSPcodesections arenotcontainedwithintheirownsegments, andare<br>actually partoftheTEXTorDATAsegments. Therefore, toalignGPU/DSPcode,<br>alignthecurrentsection beforeandaftertheGPU/DSPcode.<br>ThePRINTdirectiveissimilartotheStandard‘Clibraryprintf() functionand isused<br>toprintusermessagesfromtheassemblyprocess. Youcanprintanystringorvalid<br>expression. Ifanexpression isundefined,Madmacwilloutput"<222>" instead ofthe<br>value. Severalformatfiagsthatcanbeusedtoformatyouroutputarealso<br>supported.<br>Ifthevalue isalabelwithavalue relativetothestartoftheTEXT, DATA,<br>orBSSsegments, itwillbedisplayed inaformat like"TEXT +x".|
|||||||
|||||Ix<br>hexadecimal|
||||||id<br>signed decimal|
|||||fu<br>unsigned decimal|
|;<br>Bz||||Iw<br>word<br>i<br>long|
|||a||For example:|
|.|||||
|||<br>||||MASK<br>.EQU<br>SFFF8<br>VALUE<br>.EQU<br>-100000<br>-print "Mask: $",/x/w MASK<br>|<br>«print "Value:<br>", /da/1 VALUE|
||q||.QPHRASE|This will print "Mask: SFFF8" and "Value: -100000"<br>Aligntheprogram countertothenextintegralquadphraseboundary(32bytes).<br>NotethatGPU/DSPcodesectionsarenotcontainedwithintheirownsegments,and<br>areactually partoftheTEXTorDATAsegments. Therefore,toalignGPU/DSP<br>code, alignthecurrent sectionbeforeandaftertheGPU/DSPcode.|
|||<br>1<br>]||rept expression<br>.endr<br>title “string”<br>subtt! [-1 "string"|assembler.<br>Thestatementsbetweenthe.reptand"endrdirectiveswillberepeatedexpression<br>times. Iftheexpression iszeroornegative, nostatements willbeassembled. No<br>labelmayappearon alinecontaining eitherofthesedirectives.<br>Setthetitleorsubtitleonthelistingpage.Thetitleshouldbespecifiedonthe thefirst<br>lineofthesourceprogram inordertotakeeffectonthefirstpage.Thesecondand<br>subsequentusesoftitlewillcausepageejects.Thesecond andsubsequentusesof<br>suhtt! willcausepageejectsunless thesubtitle string ispreceededbyadash (-).|
|y1®|y1®<br>a<br>a|y1®|y1®Notes OnAssembly Directives:<br>me<br>e<br>ThedirectivesINIT,.CARGS,.TEXT,.DATA,and.BSSareforbidden while inGPUorDSP<br>sections.||



q * ©1994 Atari Corp. Confidential Information PPR Property ofAtari Corporation 8 November, 1994 

Madmac Macro Assembler 5 . 

; 

= | 

‘ 

= . 2 | | . : Bo | a : ~~ | 3 q = | = we 7 a ) a { e 

| 

**==> picture [336 x 39] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 24<br>Macros ct —<br>**----- End of picture text -----**<br>


A macro definition is a series of statements of the form: 

**==> picture [250 x 74] intentionally omitted <==**

**----- Start of picture text -----**<br>
-macro name [formal-arg, ...]<br>statements making up the macro body<br>-endm<br>**----- End of picture text -----**<br>


The name of the macro may be any valid symbol that is not also a 68000, GPU, or DSP instruction mnemonic or an assembler directive. (The name may begin with a period - macros cannot be made locally confined like labels or equated symbols.) The formal argument list is optional; it is specified with a comma-separated list of valid symbol names. Note that there is no comma between the name of the macro and the name of the first forma! argument i A macro body begins on the line after the macro directive. All instructions and directives, except other macro definitions, are legal inside the body. The macro ends with the .endm directive. If a label appears on the line with this directive, the label is ignored and a warning is generated. 

| 

Within the body, formal parameters may be expanded with the special forms: 

## \name \{name} 

The second form (enclosed in braces) can be used in situations where the characters following the formal parameter name are valid symbol continuation characters. This is usually used to force concatentation, as 

## \{frog}star 

## \{godzilla}vs\{reagan} 

| The formal parameter name is terminated with a character that is not valid in a symbol (e.g. whitespace | Or puncuation); optionally, the name may be enclosed in curly-braces. The names must be symbols appearing on the formal argument list, or a single decimal digit (\1 corresponds to the first argument, \2 to the second, \9 to the ninth, and \0 to the tenth). It is possible for a macro to have more than ten formal arguments, but arguments 11 and on must be referenced by name, not by number. 

Other special forms are: 

‘ j { 4 : 4 q 

i 

. 

8 November, 1994 

Confidential Information “7P® Property ofAtari Corporation 

© 1994 Atari Corp. : 

Page 25 

| | | | | | 1 | | | { | ] { | 

j 

**==> picture [134 x 21] intentionally omitted <==**

**----- Start of picture text -----**<br>
@. = Madmac Macro Assembler<br>**----- End of picture text -----**<br>


n i“ Special Form Description ' [_\-___| a unique label of the form “Mn” & |\#___| the number of arguments actually specified 4 the ‘dot-size" specified on the macro invocation : conditional expansion 7 Y\?{name}—_| conditional expansion The last two forms are identical: if the argument is specified and is non-empty, the form expands to a @ “1”, otherwise (if the argument is missing or empty) the form expands to a “QO”. | The form “\!” expands to the “dot-size” that was specified when the macro was invoked. This can be used to write macros that behave differently depending on the size suffix they are given, as in this macro | which provides a synonym for the "dc" directive: .macro deposit value | dc\! \value : -endm deposit.b 1 ; byte of 1 iz deposit.w 2 : word of 2 : deposit.1 3 ; longword of 3 Baa deposit 4 ; word of 4 (no explicit size) B mocmemien < OO ee ' _Apreviously-defined macro is called when its name appears in the operation field of a statement. Arguments may be specified following the macro name; each argument is seperated by a comma. : Arguments may be empty. Arguments are stored for substitution in the macro body in the following i; manner: 

= . ° Numbers are converted to hexadecimal. a * All spaces outside strings are removed. t ° Keywords (such as register names, dot sizes and “”*”operators) are converted to lowercase. t ° Strings are enclosed in double-quote marks ("). ; For example, a hypothetical call to the macro mymacro, of the form: q mymacro ad, , ‘zZorch’ / 32, “ADEFINED foo, , , tick tock E@ will result in the translations: 

| 

d § ° ©1994 Atari Corp. 

Confidential Information “FER Property ofAtari Corporation 

8November, 1994 

1 

| | 

Page 26 

Madmac Macro Assembler 

4 , a = 4 4 P q ; | aa ¥ { | 3 ; | q q 

| | 

_ a ¥ aa 4 ay = q 3 Eo j s. 1 a : a 1 a 1 2 4 ee 4 & j 2 ; . j 4 ‘’ : j 

| 

| 

**==> picture [345 x 107] intentionally omitted <==**

**----- Start of picture text -----**<br>
Argument Expansion Comment<br>| \t |asf “a0” converted to lower-case<br>"Zorch"/$20__| ‘Zorch" in double-quotes, 32 in hexadecimal<br>pS ““defined foo_| empty“**DEFINED” converted to lower-case<br>spaces removed (note concatenation)<br>**----- End of picture text -----**<br>


**==> picture [69 x 30] intentionally omitted <==**

**----- Start of picture text -----**<br>
pS<br>**----- End of picture text -----**<br>


The .exitm directive will cause an immediate exit from a macro body. Thus the macro definition: 

-macro foo source -lif !\?source, .exitm ; exit if source is empty move \source.d0 ; otherwise, deposit source . -endm 

will not generate the move instruction if the argument “source” is missing from the macro invocation. 

The .end, .endif and .exitm directives all pop-out of their include levels appropriately. That is, if a macro performs a include to include a source file, and executed .exitm directive within the include-file will pop out of both the include file and the macro. 

Macros may be recursive or mutually recursive to any level, subject only to the availabilityof memory. When writing recursive macros, take care in the coding of the termination condition(s). A macro that repeatedly calls itself will cause the assembler to exhaust its memory and abort the assembly. 

## ExampleMacros. 

The Gemdos macro is used to make file system calls. It has two parameters, a function number and the number of bytes to clean off the stack after the call. The macro pushes the function number onto the stack and does the trap to the file system. After the trap returns, conditional assembly is used to choose an addq or as add.w to remove the arguments that were pushed. 

**==> picture [466 x 111] intentionally omitted <==**

**----- Start of picture text -----**<br>
-macro Gemdos trpno, clean<br>move .w #\trpno,-(sp) ; push trap number<br>trap #1 ; do Gemdos trap<br>-if \clean <= 8<br>-addg #\clean,sp ; Clean-up up to 8 bytes<br>-else<br>add.w #\clean,sp ; Clean-up more than 8 bytes<br>endif<br>**----- End of picture text -----**<br>


-endm 

**==> picture [1 x 3] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


The Fopen macro is supplied two arguments; the address of a filename, and the open mode. Note that plain move instructions are used, and that the caller of the macro must Supply an appropriate addressing mode (¢.g. immediate) for each argument. Additionally, the Fopen macro calls another macro. 

- 

8 November, 1994 

Confidential Information “FOR Property ofAtari Corporation 

© 1994 Atari Corp. “| 

@ = Madmac Macro Assembler Page 27 27 j map macro Fopen file, mode . move .w \mode,-(sp) ; push open mode | move.1 \file,-(sp) ; push address of file nane : Gemdos $3d,8 ; do the GEMDOS call g -endm ' } The String macro is used to allocate storage for a string, and to place the string's address somewhere. @ = The first argument should be a string or other expression acceptable in a de.b directive. The second @ argument is optional; it specifies where the address of the string should be placed. If the second = argument is omitted, the string's address is pushed onto the stack. The string data itself is kept in the data F segment. j ® = macro String str,loc . -if . \?loc ; if loc is defined f move.1 #.\~,\loc ; put the string's address there a .else ; otherwise 7 pea -\ ; push the Btring's address | -endif q .data ; put the string data ro UNH: dc.b \str,0 ; in the data segment | 4 text > and switch back to the text y, segment a .endm The construction “.\~” will expand to a label of the form ".Mn" (where 7 is a unique number for every 1 a macro invocation), which is used to tag the location of the string. The label should be confined because f the macro may be used along with other confined symbols. 

Page 27 27 

| | { | | | | | : | ' | | | i 

@ _— Unique symbol generation plays an important part in the art of writing fine macros. For instance, if we @ needed three unique symbols, we might write “.a\~”, “.b\~” and “.c\~”. 

. 4 ' : | 

ie ° q F Repeat blocks can also be used to duplicate identical pieces of code (which are common in bitmap@esgraphics routines). For example, 7 SSC 

**==> picture [521 x 132] intentionally omitted <==**

**----- Start of picture text -----**<br>
| Repeat-blocks provide a simple iteration capability. A repeat block allows a range of statements to be<br>Be sépatted a specified number of times. For instance, to generate a table consisting of the numbers 255<br># sithrough 0 (counting backwards) you could write:<br>; 4 -count set 255 ; initialize counter<br>4 rept 256 + repeat 256 times:<br>-_ de.b count ; deposit counter<br>] 4 count set count - 1 ; and decrement it<br>F 4 .endr - (end of repeat block)<br>**----- End of picture text -----**<br>


{ 

Page 28 

Madmac Macro Assembler 

., 

: 

| { j q 

: i ; i { | : a 7 

| | 4 E 4 : 4 4 | § 

| Branches. eee q Since MADMAC is a one pass assembler, forward branches cannot be automatically optimized to their ; short form. Instead, unsized forward branches are assumed to be long. Backward branches are always : optimized to the short form if possible. A table that lists “extra” branch mnemonics (common synonyms for the Motorola defined mnemonics) appears below. 

I 

-rept 16 ; Clear 16 words clr.w (a0)+ ; starting at AO -endr 

## SB000Mode 

All of the standard Motorola 68000 mnemonics and addressing modes are supported; you should refer to The Motorola M68000 Programmer's Reference Manual for a description of the instruction set and the allowable addressing modes for each instruction. With one major exception (forward branches) the assembler performs all the reasonable optimizations of instructions to their short or address register forms. 

Register names may be in upper or lower case. The alternate forms RO through R15 may be used to specify DO-D7 and AO-A7. All register names are keywords, and may not be used as labels or symbols. None of the 68010 or 68020 register names are keywords (but they may become keywords in the future). 

**==> picture [297 x 169] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||
|---|---|---|---|---|---|
|Assembler|Syntax|Description|
|Address|register|direct|
|Address|register|indirect|
|Address|register|indirect|postincrement|
|Address|register|indirect|predecrement|
|disp(An)|Address|register|indirect|with|displacement|
|bdisp(An,|Xi[.size))|| Address|register|indirect indexed|
|Absolute|short|
|abs|Absolute|(long|or|short)|
|Forced|absolute|long|
|disp(PC)|Program|counter|with displacement|
|bdisp(PC,|Xi)|Program counter indexed|

**----- End of picture text -----**<br>


## Branches. eee 

- 8 November, 1994 

Confidential Information PPR Property ofAtari Corporation 

© 1994 Atari Corp. 4 

| 

‘ | ~Madmac Macro Assembler a 

**==> picture [330 x 119] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page 29<br>Alternate Name Becomes:<br>ee<br>**----- End of picture text -----**<br>


**==> picture [2 x 34] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


| It is not possible to make an external reference that will fix up a byte. For example: | extern frog a move.1 frog(pc,d0),dl 1 _ is illegal (and generates an assembly error) when frog is external, because the displacement occupies a : byte field in the 68000 offset word, which the object file cannot represent . 

> OptimizationsandTranslations =ee oe 1 The assembler provides “creature comforts” when it processes 68000 mnemonics: 

; ° CLR.x An will really generate SUB.x An,An. -- - } poe ADD, SUB and CMP with an address register will really generate ADDA, SUBA and CMPA. ° The ADD, AND, CMP, EOR, OR, and SUB mnemonics with immediate first operands will j generate the “I” forms of their instructions (ADDI, etc.) if the second operand is not register a direct. poe All shift instructions with no count value assume a count of one. : , ° MOVE.L is optimized to MOVEGQ if the immediate operand is defined and in the range -128 to g 127. However, ADD and SUB are never translated to their quick forms; ADDQ and SUBQ must S be explicit. 

| | | | | | ; | | 

4 3 | don’t think this applies to output of BSD object modules. 

| 

Page 30 Jaguar GPU/DSP Mode 

Madmac Macro Assembler ee- 

: 

| 

4 | = 

| 

| 

Motorola-Style a ; CC (Carry Clear) = %00100 | 2 CSEQ (Carry(Equal) Set) == %01000%00010 ] 2 MI (Minus) = %11000 | | ae NE (Not Equal) = %00001 | = HI (Higher) = %00101 T (True) = %00000 ; | a | = Intel-Style* . ANBE == %00101%00101 , i 3 AE [ oF = %00100 _ B = %01000 , NAE = %01000 fo E (Equal) = %00010 r NE (Not Equal) = %00001 ee NZ (Not Zero) = %00001 % NS = %01110 S = %10010 q4 Z Optimizations and Translations - 2 The assembler provides “creature comforts” when it processes GPU/DSP mnemonics: j @ In GPU/DSP code sections, you can use JUMP (Rx) in place of JUMP T,(Rx) and JR (Rx) in ] place of JR T,(Rx) | 4 Unfortunately, we have been unable to track down the definitions of all of the Intel-style condition code mnemonics, ] although their meanings can be derived by comparison with the Motorola-style mnemonics. They are included primarily 4 for purposes of backwards compatibility with the GASM assembler. ¥ 8 November, 1994 Confidential Information FOR Property ofAtari Corporation © 1994 Atari Corp. 4 

| 

| 

| 

| | | 

| | 

MADMAC will generate code for the Atari Jaguar GPU and DSP custom RISC (Reduced Instruction Set Computer) processors. See the Jaguar Software Reference Manual - Tom & Jerry for a complete listing of Jaguar GPU & DSP assembler mnemonics and addressing modes. Condition Codes EOet a oe 

The following condition codes for the GPU/DSP JUMP and JR instructions are built-in: 

| 

- 

| | | | | | | | | | : | | : ] { 

" Madmac tests all GPU/DSP restrictions, and corrects them whenever possible (such as inserting a |g NOP instruction when needed). ‘ e The "(Rx+N)" addressing mode for GPU/DSP instructions is optimized to "(Rx)" when "N" is | 4 zero. A warning is displayed. j e Older versions of Madmac supported the use of the register names RO, R1, R2, ... Ri5 in 68000 1 7 code sections. This is no longer supported because of the conflict with Jaguar GPU/DSP register 4 4 names. Use DO to D7, AO to A7, and SP instead. | 

**==> picture [534 x 491] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|
|Me|G0eSipport|a|
|:|]|Please note that the 6502 assembly capabilities ofMADMAC have not been tested since the addition of|
|:|a|the|Jaguar GPU and|DSP assembly modes.|It|is quite possible that the 6502 capabilities are broken|in|
|@|current|versions|ofMADMAC.|
|q|MADMAC will generate code|for the Motorola 6502 microprocessor.|This chapter describes|extra|
|Be|addressing modes and|other|features|used|to|support|the|6502.|
|.|Be|As the 6502 object code|is|not linkable|(currently|there|is no|linker)|external|references may|not be|
|eee|made. (Nevertheless, MADMAC may reasonably be used for large, all-inclusive assemblies because of|
|@|its blinding speed.)|
|:|.|All standard 6502 addressing modes are supported, with the exception of the accumulator|addressing|
|@|~SCs form, which must be omitted|(e.g.|"ror a" becomes|"ror").|Five extra modes, synonyms|for existing|
|ie|oonees, are included|for compatibility|with the Atari Coinop|assembler.|
|4|4|empty|implied or accumulator (e.g. {Sx OF ror)|
|i|4|expr|absolute|or zeropage|
||||g|#expr|immediate|
|||4|(expr,x)|indirect X|
|.|4|(expr),y|indirect Y|
|4|(expr)|indirect|
|a|eXpr,x|indexed X|
|q|7|expr,y|indexed Y|
|_|@expr(x)|indirect X|
|_|@xpr(y)|indirect Y|
|4|@expr|indirect|
|||4|X,expr|indexed X|
|rf|4|y,expr|indexed Y|
|.|y|While MADMAC lacks|high" and|‘low" operators, high bytes of words may be extracted with the|shift|
|1|4|(») or divide|(/) operators, and low bytes may be extracted with the bitwise AND (a) operator.|

**----- End of picture text -----**<br>


## a 

4 © 1994 Atari Corp. 

Confidential Information “PER Property ofAtari Corporation 

8 November, 1994 

. Page 32 . . Madmac Macro Assembler 5 . . . « . . . . a See the descriptions of the .6502, -org, and .68000 directives in the Directives section for information ; on how these directives affect 6502 assembly mode. org location | : This directive is only legal in non-68000 sections. It sets the value of the location counter (or pc) to . location, an expression that must be defined, absolute, and less than $10000 (the upper limit of the 6502 © address range.) . WARNING eer eee . . It is possible to assemble "beyond" the microprocessor's 64K address space, but attempting to do so will c probably screw up the assembler. DO NOT attempt to generate code like this: . org SFFFE | os nop j : nop nop Po. 

/ | 

j 

| , | 

| | | : 

the third nop in this example, at location $10000, may cause the assembler to crash or exhibit spectacular schizophrenia. In any case, MADMAC will give no warning before flaking out. 

## Object Code Format 

## — 

FF ] | { q | ; ];; ; 

This is a little bit of a kludge. An object file consists of images, a page map, followed by one or more page followed by a normal Alcyon 68000 object file. If the page map is all zero, it is not written. The byte page map contains a byte for each of the 256 256-byte pages in the 6502’s 64K address space. The is zero ($0) if the page contained only zero bytes, or one ($01) if the page contained any non-zero bytes. If a page is flagged with a one, then it is written (in order) following the page map. The following code: org-6502 $8000 { -de.b q org 1 | -de.b $8100 ; 1 org-de.b $8300i ];; end ; 

The following code: 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


will generate a page map that looks (to a programmer) something like: 

4 

8 November, 1994 

Confidential Information FER Property ofAtari Corporation 

© 1994 Atari Corp. 4 

| : : | | . : | 4 { | | 1 | 

> |. filename is followed by followed by by a comma, comma, the word word ‘line", and a line number, and and finally a colon colon and the the message. The filename message. The filename The filename filename “(*top*)” indicates that the assembler could that the assembler could the assembler could assembler could not determine which determine which which file § soproblem. j a The following sections list warnings, errors and fatal errors in alphabetical order, along with a short @ = description of what may have caused the problem. 

1 : | 

j Madmac Macro Assembler Page 33 Ww r '<$80 bytes of zero> por 01 01 00 01 <$7C more bytes of zero, for $100 total> 4 <image of page $80> | ; <image of page $81> : <image of page $83> @ Following the last page image is an Alcyon-format object file, starting with the magic number $601A. It may contain 68000 code (although that is probably useless), but the symbol table is valid and available : for debugging purposes. 6502 symbols will be absolute (not in text, data or bss sections). - S GorWessagesFT Bo WhenthingsGoWrong” | | : Most of MADMAC's error messages are self-explanatory. They fall into four classes: warnings about #@ _ Situations that you (or the assembler) may not be happy about, errors that cause the assembler to not . ’ generate object files, fatal errors that cause the assembler to abort immediately, and internal errors that ; should never happen.° We, YOu can write editor macros (or sed or awk scripts) to parse the error messages MADMAC generates. Lime When a message is printed, it is of the form: 

"filename", line line-number: message 

The first element, a filename enclosed in double quotes, indicates the file that generated the error. The filename is followed by followed by by a comma, comma, the word word ‘line", and a line number, and and finally a colon colon and the text of the message. The filename message. The filename The filename filename “(*top*)” indicates that the assembler could that the assembler could the assembler could assembler could not determine which determine which which file had the soproblem. 

## a,,r~«~—«Cis §$®lLhLLlrrrS (eo ee 

4 1 bad backslash code in string . : 4 You tried to follow a backslash in a string with a character that the assembler didn't recognize. q Remember that MADMAC uses a C-language style escape system in strings. 

@ sabe ignored ’ $B You specified a label before a macro, rept or endas directive. The assembler GZ warning you that the label will not be defined in the assembly. 

4 4 5 Of course, if you come across an internal error, Atari would appreciate it if you would contact Developer Support and let 4% us know about the problem. i . © 1994 Atari Corp. Confidential Information “PER Property ofAtari Corporation 8 November, November, 1994 

8 November, November, 1994 

;| Page 34 Madmac Macro Macro Assembler - unoptimizede cJ short branch | This warning is only generated if the -s switch is specified on the command line. The message refers to a | forward, unsized long branch that you could have made short (.s). 

Madmac Macro Macro Assembler 

| | 

| j :} 

: 

q 

| 

| | 

## Fatal Errors 

## ee =—_—KaL'r—amVx_kac.wKCee 

## cannot continue 

As a result of previous errors, the assembler cannot continue processing. The assembly is aborted. 

line too long as a result of macro expansion thanWhena source line within a macro was expanded, the resulting line was too long for MADMAC (longer 200 characters or so). 

## memory exhausted 

- . 

The assembler ran out of memory. You should (1) split up your source files and assemble them seperately, or (2) if you have any ramdisks or RAM-resident programs (like desk accessories) decrease their size so that the assembler has more RAM to work with. As a rule of thumb, pure 68000 code will use up to twice the number of bytes contained in the source files, whereas 6502 code will use 64K of RAM right away, plus the size of the source files. The assembler itself uses about 80K bytes. Get out your calculator... 

## too many ENDMs 

The assembler ran across an endm directive when it wasn 't expecting to see one. The assembly is aborted. Check the nesting of your macro definitions - you probably have an extra endm. 

q 

SHOPS ee oe= -cargs syntax Syntax error in .cargs directive. 

4 F ] qj 4 4 a q q ’ 4 

comm symbol already defined You tried to .comm a symbol! that was already defined. -ds permitted only In BSS You tried to use the .ds directive in the text or data section. 

.init not permitted in BSS or ABS You tried to use .init in a BSS or ABS section. -org permitted only in .6502 section You tried to use .org in a 68000 section. 

Cannot create: filename The assembler could not create the indicated filename. 

- 

8 November, 1994 

Confidential Information FPR Property ofAtari Corporation 

© 1994 Atari Corp. q 

| | | | | | 1 | 

See xternal quick reference B® You tried to make the immediate operand of a movegq, subq or addq instruction external. 

‘GBR bad 6502 addressing mode . “Ue 6502 mnemonic will not work with the addressing mode you specified. 

1 a There's a syntax a syntax syntax error in the expression expression you typed. : 3 bad size specified (You tried to use an inappropriate size suffix for the instruction. Check your 68000 manual for allowable Be sizes. @e bad size suffix Be You can't use .h (byte) mode with the movem instruction. ie@e cannotYou tried.glob! to makelocal a confinedsymbol symbol global or common. ae cannot initialize non-storage (BSS) section Me You tried to generate instructions (or data, with the de directive) in the BSS or ABS section. $e cannot use '.h’ with an address register Be You tried to use a byte-size suffix with an address register. The 68000 does not perform byte-sized ie address register operations. 

| | | | 

j 

**==> picture [130 x 31] intentionally omitted <==**

**----- Start of picture text -----**<br>
1 “7 ladmac Macro Assembler<br>**----- End of picture text -----**<br>


## Page 35 

. PC-relative expr across sections You tried to make a PC-relative reference to a location contained in another section. 

|Be 4 You[[bwsl] tried must to follow follow a dot‘.’ in in symbol a symbol name with something other than one of the four characters ‘B’, 3 ‘Ww’, ‘Ss’, or ‘L’. 

@ addressing mode syntax @e You made a syntax error in an addressing mode. @ assert failure @® One of your assert directives failed! j , bad (section) expression Me You tried to mix and match sections in an expression. 

Céad expression 1 a There's a syntax a syntax syntax error in the expression expression you typed. 

> | 4 

directive illegal in .6502 section * You tried to use a 68000-oriented directive in the 6502 section. 

; ' 

Page 36 

Madmac Macro Assembler 

{ 4 , 4‘ 4; 1 : 4 . q; q d e4 4 4 4 aq . Jj 

| 

_ divide by zero The expression you typed involves a division by zero. 

## expression out of range 

The expression you typed is out of range for its application. 

## external byte reference 

allow.You tried to make a byte-sized reference to an external symbol, which the object file format will not 

## external short branch 

You tried to make a short branch to an external symbol, which the linker cannot handle. extra (unexpected) text found after addressing mode commentMADMAC thought it was done processing a line, but it ran up against “extra” stuff. Be sure that any on the line begins with a semicolon, and check for dangling commas, etc. forward or undefined assert The a oneexpressionpass assembler. you typed© after a assert directive had an undefined value. Remember that MADMACis hit EOF without finding matching endif forgotThe assembleran .endif fell somewhere. off the end of last input file without finding an .endif to match an .if. You probably 

illegal 6502 addressing mode The 6502 instruction you typed doesn't work with the addressing mode you specified. 

| 

illegal absolute expression You can't use an absolute-valued expression here. 

## illegal bra.s with zero offset 

> You can't do a short branch to the very next instruction (read your 68000 manual). 

| illegal byte-sized relative reference The object file format does not permit bytes contain relocatable values; you tried to use a byte-sized | relocatable expression in an immediate addressing mode. illegal character thisYourcategory.)source file contains a character that MADMAC doesn't allow. (Most control characters fall into 

illegal initialization of section You tried to use .de or .deb in the BSS or ABS sections. 

: 

' 

8 November, 1994 

Confidential Information JPR Property ofAtari Corporation 

© 1994 Atari Corp. ] 

| | | || \ | | | 

1 E 

Madmac Macro Assembler 

Page 37 

ya~ @ 

feplegal relative address Therelative address you specified is illegal because it belongs to a different section. 

| | | | 

. 1 | | 

{ 

@ illegal word relocatable (in PRG mode) @ =«-You can't have anything other than long relocatable values when you're generating a .PRG file. @ __sinappropriate addressing mode = The mnemonic you typed doesn't work with the addressing modes you specified. Check your 68000 @ manual for allowable combinations. | 7. _ @ invalid addressing mode The combination of addressing modes you picked for the movem instruction are not implemented by the @ 68000. Check your 68000 reference manual for details. ] # invalid symbol following ** = What followed the ** wasn't a valid symbol at all. | mis-nested .endr = The assembler found a .endr directive when assembler found a .endr directive when found a .endr directive when a .endr directive when .endr directive when directive when when it wasn't wasn't prepared to find find one. Check your repeat-block your repeat-block repeat-block 

| mis-nested .endr @ = The assembler found a .endr directive when assembler found a .endr directive when found a .endr directive when a .endr directive when .endr directive when directive when when it wasn't wasn't prepared to find find one. Check your repeat-block your repeat-block repeat-block Be _séne ting. Wy mismatched .else "es The assembler found a .else directive when it wasn't prepared to find one. Check your conditional =—sassembly nesting. : mismatched .endif m= The assembler found a .endif directive when it wasn't prepared to find one. Check your conditionai @. —_ assembly nesting. 

@ __smiissing ‘}? @ _imissing argument name @ missing close parenthesis ‘)’ @ __imissing close parenthesis ‘y' @ somnissing comma @ _icnissing filename @ missing string @ missing symbol @ missing symbol or string 1 4 The assembler expected to see a symbol/filename/string (etc...), but found something else instead. In BE _most cases the problem should be obvious. bp: i misuse of ‘.’, not allowed in symbols j - You tried to use a dot (.) in the middle of a symbol name. 

(© 1994 Atari Corp. 

Confidential Information P@® Property of Atari Corporation 

8 November, 1994 

Page 38 

Madmac Macro Assembler 

| @ 4 - . | = | 2 : | ' | a ] o | / s , 8 . ‘ a 4 oo q a 4 " 4 

| 

| 

| : 

} 

| 

mod (%) by zero 

The expression you typed involves a modulo by zero. 

| multiple formal argument definition The list of formal parameter names you supplied for a macro definition includes two identical names. multiple macro definition You tried.to define a macro which already had a definition. | non-absolute byte reference 1 You tried to make a byte reference to a relocatable value, which the object file format does not allow. | non-absolute byte value : You tried to de.b or deb.b a relocatable value. Byte relocatable values are not permitted by the object file format. | register list order You tried to specify a register list like D7-DO, which is illegal. Remember that the first register number must be less than or equal to the second register number. register list syntax You made an error in specifying a register list for a -reg directive or a movem instruction. 

symbol list syntax You probably forgot a comma between the names of two symbols in a symbol list, or you left a comma dangling on the end of the line. 

syntax error This is a “catch-all” error message for errors which are not covered by other messages. 

| undefined expression : The expression has an undefined value because of a forward reference, or an undefined or external symbol. 

unimplemented addressing mode You tried to use 68020 "square-bracket" notation for a 68020 addressing mode. MADMAC does not support 68020 addressing modes. 

unimplemented directive You have found a directive that didn't appear in the documentation. It doesn't work. 

unimplemented mnemonic You've found an assembler (or documentation) bug. 

unknown. symbol! following ** You followed a ** with something other than one of the names defined, referenced or streq. 

4 4 i % 4 4 ; ‘ fl Y 4 | 4 4 q q 

- 

8 November, 1994 

Confidential Information FPR Property ofAtari Corporation 

© 1994 Atari Corp. “4 

- he a 

: 7 Madmac Macro Assembler 

Page 39 

i 

| 

we Cr'ite error Ges The assembler had a problem writing an object file. This is usually caused by a full disk, or a bad sector ae SCOiéoonn the media. 

| 

BR epee 

NE A I 

ve 

unsupported 68020 addressing mode Mee The assembler saw a 68020-type addressing mode. MADMAC does not assemble code for the 68020 or 

@e iunterminated string Be Yow specified a string starting with a single or double quote, hut forgot to type the closing quote. 

