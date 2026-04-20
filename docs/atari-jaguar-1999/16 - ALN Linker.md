| ; | | | | 1 1 | \ | | | | 4 : : | 

4 7 

ALN Linker : 

**==> picture [28 x 15] intentionally omitted <==**

**----- Start of picture text -----**<br>
Page I]<br>**----- End of picture text -----**<br>


i meee eee j The ALN linker takes object modules or libraries of object modules, created by an assembler or high| Jevel language compiler, and links them together to form a single executable program file. ] ALN can also link in binary files created by art tools, music tools, sound tools, and other such programs which create data files with information that has to be included in your program. By accepting these | files directly, ALN can save you time and disk space. 

Below is the basic format of the ALN command line: 

aln [options] <input files> @ @©ALN understands a wide variety of command line switches which affect its mode of operation. These | amy at listed and described below. 4 (For input files, ALN understands both Alcyon-format! and BSD-format? object files and object archive ; libraries. ALN can create either Alcyon-format or COFF encapsulated format executable files, either @ swith or without symbols and debugging information. 5 { ‘CommandLine Options —= rts=—=S—s—‘C‘CSCOCiCSCs*sS ee @ = Asummary of ALN’s command line options is shown below. Note that all of these options must be B sspecified before any of the input files are listed, with the exceptions of the -x, -i, and -ii options. @—sCThe ALL linker was originally distributed as part of the Atari ST computer developer’s kit, and has | updated to support the requirements of the development system for the Atari Jaguar. As a result, ; some of ALN’s original features and command line options are not really applicable to Jaguar | 4 programming. They are listed for completeness and noted where appropriate, but the description of @ sithese features will be minimal. 

> ‘ iy) 1 The Alcyon format is also known to some people as the DRI format. It is a common object file format used on the 4 3 Atari computer, originally by the Alcyon C compiler and associated tools in the Atari Computer’s Development Kit. ; @ It’s a basic, but not overly flexible object module format. 4 4 2 The BSD format is a very commonly used format for object modules on a wide variety of systems, primarily UNIX and q q similarily oriented systems. It is a very flexible format that allows for a wide variety of linker patch-up information and . 4 debugging information. j q * ©1995 Atari Corp. Confidential Information IPR Property ofAtari Corporation 5 June, 1995 

5 June, 1995 

**==> picture [613 x 662] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|
|Page|2|
|Switch|ALN|Linker|/|4|4|
|||Description|aa|
|2?|Print ALN|usage|information.|
|-a|text,|data,|bss|Output absolute executable|file (ABS or .COF).|This is the recommended|ipos|
|||output option for Jaguar Programming.|=|
|text|= Address for TEXT segment|||@|i|
|data|= Address|for DATA segment|ae|lt|
|bss|= Address|for BSS segment|“y|
|Values for text,|data,|and bss can|be:|3|f|
|a hexadecimal|value to be used as the address.|ie|Ft|
|||r:|relocatable segment|(not useful for Jaguar programs)|:|a|;|:|
|x:|contiguous segment|(contiguous|with|previous segmeni)|2 :|
|For example|“-a 802000 x 4000" would|put the TEXT segment|at $802000,|= :|
|the DATA segment immediately|after that, and the BSS section|at $4000.|-|B|
|By default, an Aicyon format executable will be created|(*.ABS)|unless the -e|q|a :|
|-b|option|is also used.|q|a|
|-c|Don't remove|multiply defined|local labels|4|=|:|
|[fnamel|Add contents|of fname to the command|line.|They are read and processed|a|¢|
|as though they appeared on the command|line.|Any command|line options|4|s|;|
|may be used.|Arguments|in ihe file may be delimited by whitespace|(tabs,|1|a.|
|spaces,|newlines)|or commas.|As with the regular command|line,|only the-i,|J|||Ef|
|-ii,|or -x options may be used|after the first|input|file|is specified.|4|s|
|i|This|option|is used|to get around the system's|limitation|of 128|byte maximum|||
|.|command|line length.|It|is typically the|last option on the main command|line,|:|t|
|but|it can appear anywhere.|See the Command|Files section and the|A|:|
|-d|example|in the Using ALN section|for more|information|.|
|Wait for keypress before exiting,|after|link|is finished.|This|gives the user|4|“|
|time to read any|error messages.|This can be|useful|if running ALN|directly|4|:|
|from a graphic user interface|instead|of|a command|prompt.|4|.|
|Note that|if you|start ALN with|no arguments|(entering|interactive|mode),|then|4|
|-e|the -d option|is|implied.|;|
|Output COFF encapsulated|executable|(absolute|only,|must be used with the|44|
|-f|-a|option.)|a|
|Add|file symbols to output|(Alcyon format|only).|When the -f option|is|used,|4|
|ALN|will generate a symbol|matching the filename|of each|object|module,|dl|
|archive|library,|or binary|file included|in the|link.|(i.e.|If you have an|object|a|
|module named OUTPUT.O then you|will|get a symbol named|OUTPUT.)|ay|
|The|‘4|
|-f option|automatically|sets|the -s option|as well,|unless|the|-I|option|is|fi|
|used,|if|
|See the section|File Symbols for more|information.|]|
|Output source-level|debugging|information|(only works with -e|option to|4|
|-h|produce COFF format executable|files)|4|
|value|i|Set header values (PRG output|only)|4|
|This|7|
|option does not apply to Jaguar programming|E|

**----- End of picture text -----**<br>


5June, 1995 

Confidential Information “FPR Property ofAtari Corporation 

© 1995 Atari Corp: 4 | 

: : q j | | | | | | | { 1 | | 4 i | : j | 1 | | { 1 

**==> picture [556 x 713] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||
|---|---|---|---|---|---|---|---|
|we|Switch|Description|)|
|ae|||-i fname label|includes the binary data contained|in the file specified by fname in the|link.|
|1|The contents|of the file are placed verbatim|into the DATA section.|ALN|
|@|I|-li fname label|creates a global symbol named label with the value of the starting address|
|a|and another global symbol! name labelx with the value of the ending|
|||address+1.|(e.g.|if label is “picture” then you get a label named “picture” at|
|3|the start and a second|label named “picturex” at the end).|
|2|| Wihcharacters the -i option,length. the(The symbol end symbol created willwill be be truncated truncated toto 7 alabels maximumbefore the of 8|‘x’|
|os|is added,|for a total|of 8 characters.)|
|Lf|With the -ii option, the symbol will not be truncated (assuming that you have|||
|specified COFF-format output).|{|
|;|This option is used within the|list of input files.|It's similar to the MADMAC|
|directive|.incbin.|
|Bee|eseat symborto tre|
|a|||Add local symbols to output file (as well as global symbols)|
|ag|This option|is|like a stronger version|of the -s option.|
|by|-m|Produce load symbols map on standard output. The load map contains each|
|||symbol's name, value, and type.|The load map lists only global symbols|
|7|unless the -I option|is used.|The symbol types are encoded as follows:|
|i|
|B|C:Common|_|F:|File|
|i|ad|G: Global|A: Archive (only with "File’)|
|=|E: External|§ Q: eQuated|
|1g|L: Local|R: Register|
|i|
|&q Po|Outputsections) no file header to|ABS file (output raw image of|TEXT & DATA|{|
|La|-o fname|Set output filename to fname.|lf fname has an extension|(e.g. “.COF"), then|
|8|that extension is used.|Otherwise,|a default extension|is appended (".COF”|
|P||for|a COFF-format absolute executable,|“ABS” for an Alcyon format absolute|
|q|7|executable,|or “.PRG” for|a GEMDOS-format|relocatable executable).|
|||
|if the -o option|is|not specified,|the output file name|is taken from the first|
|,|q4|linked file on the command|line (including archives specified with -x and data|
|,|||files specified with the -i or -ii options),|plus the appropriate extension.|Note|
|Pa|that|if this would make the output file name the same as the first input file|
|.|4|(e.g.|“aln-p A1.0 {A2.0" which would use "A1.0" as the output file name|
|,|||because we are only doing a partial link), ALN will abort:|in this case, -o must|t|
|.|3|be specified.|
|q|create a single object module,|suitable for tater passes through ALN.|
|;&4 Se|thaPar|t|ialall link symbois with nailed-downin the COMMON BSS. sectionThis is are resolved the same as the -pinto the option, BSS section. except|
|Mi|«(©1995|Atari Corp.|Confidential Information|JER|Property ofAtari Corporation|5|June, 1995|

**----- End of picture text -----**<br>


**==> picture [610 x 668] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||||||||||
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
|Page 4|ALN Linker|e|
|Switch|Description|i|
|-t[size]|Section|alignment|size.|Automatically|pad the|size|of each|object|module's|
|1|TEXT,|DATA,|and BSS sections so that the size|is an|integral|multiple of the|.|
|specified|size..|size|is one|of:|#|
|w:|word|(2|bytes)|»|
|:|I:|long|(4|bytes)|4|
|'|p:|phrase|(8|bytes,|default alignment)|a|
|j|||d:|double phrase (16 bytes)|ee|
|'|a:|quad|phrase|(32|bytes)|foo|
|For example,|the option -rp would cause the TEXT.|DATA.|and BSS sections|:|
|of each|object|module|in the|link|to be padded|in|size|until they were|a|;|||
|multiple|of|8|bytes.|;|
|Generate|a|symbol|table|in|the|output|file,|and|include|all|global|symbols.|
|Use the|-!_|option|(by|itself)|to|include|local|symbols|as|well|as|globals.|
|*u|Don't|abort|on|unresolved,|externally|defined|symbols.|The|unresolved|!|
|}|symbols|are|listead|on|standard|output,|but the|link|proceeds|as|if their|
|“Vv|valuesSet verbosewere mode.zero.|Causes ALN|to|print a banner at the start|of the|link,|and|||;'|
|show|memory|usage|statistics|at|the|end.|
|j|archiveUse -v -vlibrary, for extraor data verbosefile)|mode;is|printedtheas nameit|is|linked.of each|file|(object module,|f|§|
|j|+|&|
|Use|-v|-v|-v and ALN|will|also|print the name|of each|module|it|uses|from any]|
|j|archive|libraries|included|in|the|link.|.|
|Set warnings|on|(for|multiple|defines,|etc...)|See the|section|Duplicate|j|
|Symbols|in|Modules|for|more|information.|
|:|-x fname|Includes|all object modules from the archive library specified by fname,|in the|4|
|||order they|are found.|in the|case|of|multiply|defined|global|symbols,|the|first|
|one found|is the|one which|gets|used,|which|is|opposite from|the|usual|4|
|behaviour.|
|:|This|option|is|used|within|the|list|of|input|files.|
|:|Eyffname]|Set|library|path|||
|Below|is|a|sample command|line passed|to ALN:|;|||
|aln|-e|-f|-l|-rp|-u|-w|-v|~v|-a|802000|x|4000|-o|showimg.cof|start.o|-j|
|keypad.o|draw.o|init.o|video.o|sound.o|objlist.o|~i|image.dat|img|data|
|This would|run ALN with|options|for COFF output|(-e),|place symbols|in|the output|file|(-f),|include|.|
|local|symbols|(-1),|align|each|segment|of each|file on|a phrase|boundary|(-rp),|continue|past|unresolved|4|
|symbol|errors|(-u),|show|warnings|(-w),|show|extra verbose|status|information|(-V|-v),|create|an|:|
|||absolute executable file with TEXT & DATA segments starting at $802000 and a BSS segment|at|j|
|$4000|(-a 802000|x 4000),|output|to SHOWIMG.COF|(-0|showimg.cof).|:|

**----- End of picture text -----**<br>


5 June, 1995 

Confidential Information FPR Property ofAtari Corporation 

© 1995 Atari Corp : 

ALN Linker 

Page 5 

| | | | | ' | : 1 | | | | | : j | | | | 

Bi r The input object modules would be START.O, KEYPAD.O, DRAW.O, INIT.O, VIDEO.O, #@ SOUND.O, and OBJLIST.O. Also included would be the binary data file IMAGE.DAT, which would q be referenced via the img_data label. s Unfortunately, the command command line above would would never work work in real life because because it is longer than 127 a bytes. Both MSDOS and MSDOS and and the Atari computer’s GEMDOS GEMDOS operating systems have a maximum command have a maximum command a maximum command maximum command command line length of 127 bytes. To get around this, we we need to have a linker command command file that specifies some some a of the command the command command line options and/or input options and/or input input files. Normally, you would specify would specify specify your options options in the the first § part of the command of the command the command command line and put put the names of your input names of your input of your input your input input files into the the linker command command file. So we we | would probably really do something like this instead: 

Bo 

PS” 

| filenamesandthe LibraryPath ALN looks for files, both object modules and archive libraries, in both the current default directory and in the directory named as the library path. This is specified cither by the ALNPATH environment j variable, or named on the command line using the “-y” option. If both the ALNPATH variable and command line option are present, then the command line specification takes precedence. 1| | TheThe completelibrary pathpath,shouldincluding be a fulldrivepathname whichletter, should namesbe specified.a single directory, like “E:JAGUAR(LIB”. When ALN tries to open a file, it looks in a number of places. First it tries to open the file exactly as ' specified, in the current directory. If that fails, ALN then appends a “.O” extension and tries again. If | that fails, then ALN looks in the library path directory for the specified filename. If that still fails, then j ALN then appends a “.O” extension again looks in the library path directory again. If none of these a methods work, then ALN gives up. For example, if you specified “mathsubs” to include z “E:\LIB\MATHSUBS.O”, then ALN would iook for: 

, 

I { 

Unfortunately, the command command line above would would never work work in real life because because it is longer than 127 bytes. Both MSDOS and MSDOS and and the Atari computer’s GEMDOS GEMDOS operating systems have a maximum command have a maximum command a maximum command maximum command command line length of 127 bytes. To get around this, we we need to have a linker command command file that specifies some some of the command the command command line options and/or input options and/or input input files. Normally, you would specify would specify specify your options options in the the first part of the command of the command the command command line and put put the names of your input names of your input of your input your input input files into the the linker command command file. So we we would probably really do something like this instead: 

| aln -e -f -l -rp -u -w -v -v -a 802000 x 4000 -o showimg.cof ~c showimg. lnk 

| The first part of the commandline is the same, but then it ends with the -c showimg.ink option instead of a list of input files to be linked. This option tells ALN that there are more linker commands in the | text file SHOWIMG.LNK. This file would contain something like this: | start.o keypad.o draw.o video.o sound.o objlist.o -i image.dat img data 

The command file can be as long as required to specify all of your input files and options. 

**==> picture [255 x 70] intentionally omitted <==**

**----- Start of picture text -----**<br>
Attempt Filename searched for Result<br>E:\LIB\mathsubs.o succeeds!<br>**----- End of picture text -----**<br>


**==> picture [21 x 18] intentionally omitted <==**

**----- Start of picture text -----**<br>
=<br>**----- End of picture text -----**<br>


© 1995 Atari Corp. 

Confidential Information “ZO Property ofAtari Corporation 

5 June, 1995 

Page 6 

ALN Linker | matching file is found, ALN stops is found, ALN stops found, ALN stops ALN stops stops looking. A filename can filename can can also contain contain a partial partial the archive “E:\LIB\LOCAL\MYLIB” and your library path path is “E:\LIB” “E:\LIB” then } ee on the command command line is sufficient. sufficient. ALN will look for: . Attempt Filename searched for Result a Pt | LOCALIMYLIB | @ 3 LOCALMYLIB.O 8 E\LIB\LOCAL\MYLIB | succeeds! | 1 the “.O” extension to a filename filename that already has an an extension. Also, ALN ALN _ path for filenames filenames that start with with “\” or or “/” or which or which which contain a colon colon (:). The f og filenames are based ona are based ona based ona onaa specific drive or the root directory of the the current drive, j " to the library path specification would would not work. work. 1 4 

| | Of course, as soon as a matching file is found, ALN stops is found, ALN stops found, ALN stops ALN stops stops looking. A filename can filename can can also contain contain a partial partial name: if you want tc use the archive “E:\LIB\LOCAL\MYLIB” and your library path path is “E:\LIB” “E:\LIB” then : listing “LOCAL\MYLIB” on the command command line is sufficient. sufficient. ALN will look for: 

| 

: 

ALN never tries to append the “.O” extension to a filename filename that already has an an extension. Also, ALN ALN will not look in the library path for filenames filenames that start with with “\” or or “/” or which or which which contain a colon colon (:). The assumption is that such filenames are based ona are based ona based ona onaa specific drive or the root directory of the the current drive, and therefore adding them to the library path specification would would not work. work. 

| AbsoluteLinking. : An absolute link is one for which the -a option is specified. This is the type of link normally usedfor Jaguar Development. Note that the -a option takes three arguments: the base address for the TEXT, **_** DATA, and BSS segments, respectively. The base address can be specified in the following ways: 

4 : 4 

- A hexadecimal value, which is taken as the starting address of the segment. 

E 

- The letter “r’, which stands for “relocatable”. 

- The letter ‘x’, which stands for “contiguous with the previous segment” (whether that segment is absolute or relocatable). 

During an absolute link, an absolute object module is produced, which includes the base address of each segment in its header. In Jaguar development, this file can be used directly with the debugger as an executable program file.4 See the section File Formats for more details. 

1 4 7 4 4 : 

**==> picture [2 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
.<br>**----- End of picture text -----**<br>


In an absolute object module, all references to an absolute segment have already been resolved; that is, «§ there is no relocation information for them, because they are not relocatable. References 1o relocatable 4 segments still have relocation information associated with them. If there are no references to relocatable 3 segments (either because there are no such segments, or no references to them), the relocation 4 information is missing entirely, and a flag in the header indicates this. “4 For example, when linking a program to be placed in ROM, ALN might be used to link with the TEXT 4 and DATA segments contiguous, starting at the address of the ROM (say, $802000), and with the BSS 4 segment at some address in RAM (say, $4000) This can be done with ALN as follows: a aln -o rom.abs -a 802000 x 4000 romfile.o q 

q j 

**==> picture [81 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
© 1995 Atari Corp.<br>**----- End of picture text -----**<br>


3 This is typically the desired output for Jaguar programming. 5 June, 1995 Confidential Information PO® Property ofAtari Corporation 

Page 7 

4 4 

| q | | J | j | ] i | | 

' | 

@ ALN Linker bs : ternatively, a program with its data segment in ROM, but with relocatable text and BSS segments, @ could be linked as follows: ; : aln -o romdata.abs -a © 802000 r romfile.o ' Of course, it would be up to the program loader to perform the TEXT and BSS relocation at execute q 3 time. and this does not really apply to Jaguar programming. { | ALN will generate file symbols when the -f option is used. A file symbol appears at the start of each ' / abject module in the symbol table. Its name is the name of the module, its value is the start of the text @ segment of that module, and its type is TEXT FILE ($0280). With these symbols, you can determine q 4 which object module a given symbol came from, because the symbols from a module immediately #_ follow its file symbol. 7 ALN also generates a file symbol at the start of each archive: this is a special symbol in that its name is Me the name of the archive, but its type is TEXT FILE ARCHIVE ($02C0). Furthermore, a second symbol wm is generated at the end of the archive: it has the same type, but its name is blank. This signals the end of iim the previous archive. - q j The use of bit 6 of the type field to mean “archive” is not an original part of the Alcyon symbol-table : q standard. As such, some older tools can not be expected to understand it. ® FileFormatsLLL LLLA I OO 4 , There are three basic types of files that ALN deals with: object modules, archive libraries (containing ; F object modules), and executable program files.4 There are two different styles of file format for each of 4 these file types: Alcyon format and BSD/COFF tormat. 4 The different Alcyon formats originate with the Alcyon C compiler, an original component of the Atari 4 ; Computer Development Kit dating back to 1985, and on other systems before that. We will discuss f them first. 

q 4 Alcyon format object modules and executable program files have the same basic format: Header | | (information describing the file contents), image of Text segment (program code), image of Data me «= segment (pre-initialized data), Symbol Table (debugging information), Relocation Information (used by wm linker during link and/or by OS when loading program into memory). » ‘ | 4 We don’t consider data files included via the -{ or -ii options as part of this list, because ALN doesn’t really care what ] q the contents of such files might be; they are simply included verbatim into the DATA section of the output tile. j q © 1995 Atari Corp. Confidential Information JPR Property ofAtari Corporation 5 June, 1995 

i 

; Page 8 ALN Linker | | The header includes information such as the sizes of the other segments and the actual file type / (encoded in a “magic” number). Any segment may be empty or missing except the header. 1 | Alsvon-Fomnat Obleet Modules Ss ae ox geet ereenees: A standard Alcyon-format (relocatable) object module header has the following format: struct oheader { | int magic; /* the magic number 0x601A */ :’ ; longlong dsize;tsize; /*/* datatext segmentsegment sizesize */*/ 4 1 long bsize; /* bss segment size */ q long ssize; /* size of the symbc! table + J }; : char reserved{10j; /* ten unused bytes (must be zero) «/ j : ‘i ; All values are in Motorola (big-endian) format. Following the header is the module’s text segment, the F i information.module’s initialized data segment, the symbol table information, and then the module’s relocation fixup J _ Alcyon-format executable programs (.PRG files) have almost the same format as relocatable object 4 | modules. The header is the same (except that the magic field is $601B instead of $601A), and the text di and data segments, plus the symbo! table, follow. The overall file format could be defined in ‘C’ as: 4 . struct oheader theHeader; j charchar data_segment[text_segment/[theHeader.tsize}theHeader.dsize} q char symbol table[theHeader.ssize] 4 ' op | char fixup_info[]; /* arbitrary size +/ P AlcyonThis type offile is not used in Jaguar programming. 1t is mentioned here because it is similar to the 4 : flavor of the executable file format is typically used for Jaguar programming (described in the :. j [MF1}following section). 4 a : This file format is similar to the standard object module and relocatable executable file formats, except _ J that there is normally no relocation information to allow the file to be loaded at any address. Instead, 49 | the address references in the code and data have been absolutely positioned by the linker. The file 4 header has been expanded to specify the load address for the TEXT, DATA, and BSS segments. The 4 absolute object module header has the following format: 4 

5June, 1995 

Confidential Information 7PR Property ofAtari Corporation 

© 1995 Atari Corp.- 

Page 9 

j | | | ' | | | j 1 : | | : , 

: q ALN Linker me 4 btructintabshdrmagic;{ /* the magic number Ox601B */ q long tsize; /* text segment size */ 4 long dsize; /* data segment size «/ . 4 long bsize; /* bss segment size ~/ = long ssize; /* size of the symbol table */ . 3 long reserved; /* an unused longword */ q long textbase; /* the base of the text segment */ , 4 int relocflag; /+* zero if reloc info exists */ _ long database; /* the base of the data segment */ 4 long bssbase; /x the base of the bss segment */ @e =} :*theHeader; @esCcchar text_segment [theHeader .tsize] Be ochar data_segment [theHeader .dsize] i 4 charNormally,symbol _table[ a relocatable theHeader.ssizeifile uses a base address of $00000000 for all internal references, and relies on : E the system loader to use the relocation table to relocate the references as necessary to the address where 4 the file’s TEXT segment is loaded. In contrast, an absolute-linked file uses a base address for each 1 7 segment that is defined at link time, and normally does not include relocation information. However, ii q . is possible for an absolute file to contain relocation information. Be si there is any relocation information, the relocflag field in the header will be zero, and that information mam Will follow the symbol table (if any). If the relocflag field is not zero (and in particular if it is minus 5) s a ‘one), there is no relocation information. This is always the case when none of the three segments is F relocatable, but it can also happen if there are no references to a relocatable segment (e.g. the text ; . segment is relocatable, but contains position-independent code, and the data and BSS segments are He sabsolute). . iAlcyon-Format Archive Librariesee= = j 4 Archives are files containing other files, usually relocatable object modules. The "header" of an archive j s file is simply the magic number $FF65 (hex). The archived files consist of a header, then the object j 4 module file itself. The next file follows immediately. A zero word follows the jast file in the archive. q The archived-file header is as follows: 4 4 struct arheader { : a char a_fname[{14]; /* the file name */ | 4 long a_modti; /* the last-modified time =/ _ char a_userid; /* not used in TOS */ . @ char a_gid; /* not used in TOS */ q q int a_fimode; /* the file's mode word */ . 4 long a_fsize; /* the file's size in bytes */ : a int reserved; /* zero */ Dy The remainder of the archive file, which is a_fsize bytes in length, immediately follows the header. 

4 

| ©1995 Atari Corp. 

Confidential Information JPR Property ofAtari Corporation 

5 June, 1995 

| 

| 

Page 10 ALN Linker : BSD-Format ObjectModules = sces itd COFF-Format Absolute Executable Program Files = = =... EB ¢ BSD-Format Object Module Archive Libraries ee Information on these tile formats has not been been folded into the main ALN documentation as yet. 1 I This intormation will be available in a future revision. | | r Duplicate Symbols InModules OR When the same symbol is exported (decalred as global) from multiple object modules, the symbol value 4 F exported from the first such module will take precedence. When the same symbol is exported by aq multiple modules in one archive, the last such module will take precedence. Therefore, in the case of 4 \ two archives exporting the same symbol (from modules exporting needed symbols), the last definition : a : in the first archive is the one which wili be used. : [- ae However, if an archive is included with -x, the modules are read in archive order, and the first instance 1 _ of a symbol is the one which prevails. [_ & Unless the -w flag is used, you will get no notification that multiple files exported the same symbol. ‘ Since the dependency information is built from the archive, certain conditions can cause it to be out-ofd : ; date with respect to a given link. 1 For example, if archive Z contains modules M and N, and M depends on N because it needs symbol! S, d the index file tor Z will reflect this. But if the symbol S is exported by a file Y earlier in a particular q link, then module N is not actually needed at all. q ALN will read module N from the archive, but will then notice that both N and Y are exporting symbol S. This will produce a warning message if the -w option is specified. Finally, since Y occurs earlier in 4 the link than N, the value of symbol S is taken from Y. ALN will notice that module N is not in fact 4 used in the linking process, and will discard N completely, with another warning message. q ErrorMessages§=§«. ss Most of the common error messages from ALN are self-explanatory; for instance, "File <x> is notan 4 archive." In some cases, however, a little more explanation is in order. 4 Some errors refer to a 16-bit fixup overflow. This means that in resolving an external reference in the { file, a value greater than 32767 or less than -32768 had to be put in a single word. This can happenif SJune,1995—~—~—~—~-—ConfidentialInformation “FAR Property of Atari Corporation ‘© 1995 Atari Corps 4 

ALN Linker 

Page 11 

| . | 1 

1 { | | | | : . 1j 

## BR. 

paeerey ou have a PC-relative reference to a symbol more than 32K away. This is only a warning, since you Be somight be using the value as an unsigned integer (in which case it might not be an overflow). : ‘ Other errors report that they occurred at a given offset (always hex) in a given module. The offset is @ always in bytes, counting trom the beginning of the text segment of that module. : a If the solution to eliminating the source of an error is giving you difficulty, please contact Jaguar @ Developer Support for assistance. (§ DOINDEX -- Aicyon-Format Archives And Their indexes ’ 4 ALN requires that an index file exist for each Alcyon-format archive library which is included in a link We (but not BSD-format archive libraries). This index file has the same name as the archive, with the @ . - extension “.NDX”, and should be in the same disk directory as the archive itself. 1f ALN can not find an index file for an archive you name, it will produce an error message to that effect and abort. 

@ ] 4 The DOINDEX utility builds an index file for the named archive (regardless of whether one already me cxists). If desired, DOINDEX will also print a human-readable index of the archive on standard output, We. and inform you of symbols which are declared global in more than one module in the archive. The last Mi such declaration is the one which will prevail when that archive is used in the linking process. : The command line options for DOINDEX are as follows: ‘ a Option Description q a Index: print an index of the archive to the standard output, including the name of each module, the | a global symbols it exports, and the external-symbols it imports. Finally, list the symbois which are ; 4 external to the archive (imported by modules in the archive but not exported by any of them). @ [-w__| Warnings: produce warnings about duplicate symbols in the archive. @® The last argument to doindex is the name of an archive. Doindex opens that archive, builds its index @® file, and writes that file to file.ndx in the same disk directory as file itself. 3 The index file contains dependency information so the linker does not have to go through the whole @e archive to resolve all the symbols. ft consists of information about each module in the archive, the Wy Csoname of each symbol exported by any module in the archive and the module which exports it, and a # dependency list for each module, stating, “if you need module A, you will also need modules B, C, and @ ~sC#@+«{" During linking, this information is collected together for each symbol which is unresolved at the We sittime the archive appears in the command line, and only the needed modules are read in from the ie archive. 

**==> picture [9 x 21] intentionally omitted <==**

**----- Start of picture text -----**<br>
7<br>**----- End of picture text -----**<br>


