| 37 | rp 4 Manual Date 93/11/15 | 

1 

a | 4 fq = g j i] | 4 4 = = g 4 } 4 & by a gg & = a = f 4 | | Pa = | & | 4 4 = | 4 ; | an es 4 , 8 

| | | | | | { | : | | | | | : 

## Table of Contents 

|Chapter 1: DB: THEATARI DEBUGGER|Chapter 1: DB: THEATARI DEBUGGER|||1-1|
|---|---|---|---|---|
|USAGE<br>OPTIONS<br>-g||||1-1<br>1-1<br>1-1|
|-bN||||1-2|
|-S||||1-2|
|-ifile<br>TERMS||||1-3<br>1-3|
|USINGTHE DEBUGGER<br>Chapter 2: EXPRESSIONS, RANGES, AND <br>EXPRESSIONS<br>SIMPLEEXPRESSIONS||STRINGS||1-4<br>2-1<br>2-1<br>2-1|
|hex constant<br>@decimal constant||||2-1<br>2-1|
|%binary constant||||2-1|
|symbol<br>‘variable<br>&variable<br>$||||2-2<br>2-2<br>2-2<br>2-3|
|COMPLEXEXPRESSIONS<br>RANGES<br>STRINGS<br>Chapter 3: THE CLIENT, BREAKPOINTS,AND<br>ANOVERVIEW||ANDCHECKPOINTS:|CHECKPOINTS:|2-3<br>2-6<br>2-7<br>3-1|
|RUNNINGTHECLIENTPROGRAM<br>BREAKPOINTS<br>MEMORYCHECKPOINTS||||3-1<br>3-1<br>3-2|
|Chapter4: COMMANDS<br>BREAKPOINTSAND CHECKPOINTS|||||4-1<br>4-2|
|b<br>nb [address<br>#index}|]|||4-2<br>4-3|
|nmf{ {address<br>#index|}|]||4-6|
|TRACEANDGO||||4-6|
|t{ {count<br>x w}<br>]<br>u[{count<br>x }<br>]||||4-7<br>4-7|
|v£{uw}<br>J][counr]||||4-7|



i j | | : 1 | | | 

| | | | | | | | 

|g [ range ]<br>MEMORY<br>1 [range<br>]}<br>d{{w 1}] [range]<br>$ f[{w 1}] rangevalue<br>frangestring<br>THECLIENTANDSYMBOLS<br>exec [ {program [args...] on off} J<br>args [args...]<br>getsymprogram[ textbase ]<br>symnamevalue<br>nosym<br>? [symbol ]<br>where [expression ]<br>stack<br>REGISTERSANDVARIABLES<br>set [ variable [value] ]<br>x<br>[variable [value] ]<br>vars<br>stubstate<br>REMOTEDEBUGGINGCOMMANDS<br>wait<br>check<br>terminate<br>continue<br>PROCEDURESANDALIASES<br>procedure [name [args...] ]<br>plist<br>[name...]<br>global [name... ]<br>local [name... ]<br>gotolabel<br>alias [name [ expansion ] ]<br>.<br>unalias name<br>noalias<br>FILESANDSCRIPTS<br>read [file<br>[ address ] ]<br>write file [range ]<br>load file<br>unload<br>reload|.|4-8<br>4-9<br>4-9<br>4-10<br>4-11<br>4-12<br>4-12<br>4-13<br>4-13<br>4-14<br>4-14<br>4-15<br>4-15<br>4-15<br>4-16<br>4-17<br>4-18<br>4-18<br>4-18<br>4-19<br>4-19<br>4-19<br>4-19<br>4-19<br>4-20<br>4-20<br>4-20<br>4-20<br>4-20<br>4-21<br>4-21<br>4-21<br>4-22<br>4-23<br>4-23<br>4-23<br>4-23<br>4-24<br>4-24<br>4-25<br>4-25|’<br>j<br>j<br>4<br>:<br>:<br>;<br>1<br>1<br>q<br>‘<br>4<br>:<br>|<br>‘<br>q<br>:<br>q<br>1<br>;<br>1<br>]<br>;<br>q<br>i<br>4<br>;<br>4<br>q<br>J<br>q<br>q|:|
|---|---|---|---|---|



| 4 | 3 ; q 4, 4 | | | Dod 1 4 F 4 | @ | 4 fg | 4 | @ f 4 . 4 . P | q _. . 4 . Gf . | | 4 ' 4 , 4 f a 4 , 4 | 4 a ‘ q : 4 ; 7 J 4q _ j q 

| | , | | : | | | ] | | | | 1 

||g [range ]<br>MEMORY<br>1 [range ]<br>d{{w 1}] [range]<br>S|||4-8<br>4-9<br>49<br>4-10<br>4-11||||
|---|---|---|---|---|---|---|
||f{[{w 1}] rangevalue<br>frange string<br>THECLIENTANDSYMBOLS<br>exec [ {program [args...] <br>args<br>[ args... ]<br>getsymprogram[ textbase ]<br>symnamevalue<br>nosym<br>? [symbol }<br>where[ expression ]<br>stack<br>REGISTERSANDVARIABLES<br>set [variable [value } ]<br>x [ variable [value]<br>]|on off}]|4-16|4-12<br>4-12<br>4-13<br>4-13<br>4-14<br>4-14<br>4-15<br>4-15<br>4-15<br>4-17<br>4-18<br>4-18<br>4-18|||
||vars|||4-19|||
|.|stubstate<br>REMOTEDEBUGGINGCOMMANDS<br>wait<br>check<br>terminate<br>continue<br>PROCEDURESANDALIASES<br>procedure [name [args...]<br>plist [name...<br>]<br>global [name...<br>]<br>local [name...<br>]<br>gotolabel<br>alias [name [ expansion } ]<br>unalias name<br>noalias<br>FILESANDSCRIPTS<br>read [file<br>[ address ]<br>]<br>write file [range ]<br>load file<br>unload<br>reload|]||4-19<br>4-19<br>4-19<br>4-19<br>4-20<br>4-20<br>4-20<br>4-20<br>4-20<br>4-21<br>4-21<br>4-21<br>4-22<br>4-23<br>4-23<br>4-23<br>4-23<br>4-24<br>4-24<br>4-25<br>4-25||.|



: | | | | | | | 

= 4 4 4 Pod fy | | 3 4 f 4 | a 4 | 4 L 4 

fi 

L a 

. 

: 

**==> picture [412 x 216] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||
|---|---|---|---|
|ALIAS|||7-5|
|AUTO-EXECUTE ALIASES|7-6|
|COMPOUND COMMANDS,|introduced|7-7|
|DEFER|7-7|
|COMPOUND COMMANDS,|explained|7-8|
|Chapter 8:|OPERATING SYSTEM CONSIDERATIONS|8-1|
|DB AND GEMDOS|8-1|
|DB AND MARK WILLIAMS C|8-1|
|DB AND THE XBIOS TRAP|8-3|
|THE SHELL COMMAND IN DETAIL|8-3|
|EXCEPTIONS|8-4|
|.|DB, TOS, AND 68030|8-4|
|DEBUGGER MEMORY USAGE|8-5|
|Chapter|9:|REMOTE DEBUGGING|9-1|

**----- End of picture text -----**<br>


**==> picture [1 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


;4 ]4 

] | | 4 q ; q j 4 , ’ q 7 { a q P 4 ; 4 | q 

| | | | 

4 4 If started as a TTP program from the desktop, the arguments line looks the same | 4 without the word db at the beginning. | OPTIONS : / Db can use many different devices can use many different devices use many different devices many different devices different devices devices for its input and output. and output. output. This makes debugging makes debugging debugging ' q graphics- and keyboard-oriented programs keyboard-oriented programs programs easier. : j These options on the command line select the output device to use: : q Use GEMDOS to access the ST screen and keyboard. This is the default : 4 case, but it does have limitations. See the section DB AND GEMDOS in q a the chapter OPERATING SYSTEM CONSIDERATIONS for more : ] information. 

## Chapter 1 DB: THE ATARI DEBUGGER 

**==> picture [2 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


Db is a debugger for the Atari ST and TT series of 68000-family computers. It is not a source-level debugger, but it does handle Alcyon C, Mark Williams C, GCC and HiSoft Lattice (new and old) symbol table formats. 

Db can use any of the ST’s character devices for its input and output, including the screen, the serial port, and the MIDI port. The I/O device is selected with a switch on the command line (or in the TTP window if started from the desktop). 

Db is capable of debugging programs running on one machine while the bulk of the debugger runs on another. This is called remote debugging, and permits debugging of operating systems while they boot, for example. This feature is described in the chapter REMOTE DEBUGGING. 

## USAGE 

## From a command shell, db can be started as follows: 

db [options ] [ program [{ args...] ] 

Db can use many different devices can use many different devices use many different devices many different devices different devices devices for its input and output. and output. output. This makes debugging makes debugging debugging graphics- and keyboard-oriented programs keyboard-oriented programs programs easier. 

Lg 

_— poe : : ‘ : 1 § j q { j } q 1 ‘ 3 : 4 q : { 4 ; | 3 i 4 i. ’ 3 

| Use the BIOS to access the ST screen and keyboard. Sometimes this helps when debugging a program which itself does BIOS I/O, because using GEMDOS calls can mess up type-ahead and the like. **|** You can (optionally) specify which which BIOS device to use by placing the use by placing the by placing the placing the BIOS device number after the -b: number after the -b: after the -b: the -b: -b: "-b3" means “use BIOS means “use BIOS “use BIOS BIOS calls for input and input and and output, and use BIOS device number 3 number 3 3 (the MIDI port). The argument argument is : in decimal. decimal. Any number at number at at all may be used may be used be used used here, including numbers which numbers which which are not in fact BIOS BIOS device numbers; numbers; in this case, the debugger will probably crash, and it is likely that you you will have have to reset your your machine. “s Use the serial (RS232) port. A terminal or an ST running a terminal program must be connected via a “null modem" cable, and its keyboard and screen are used for communicating with the debugger. (You can even use a modem connection to a terminal or computer, but this is extreme.) The baud rate, parity, etc. for the serial port must be set before starting the debugger in this mode. 

You can (optionally) specify which which BIOS device to use by placing the use by placing the by placing the placing the BIOS device number after the -b: number after the -b: after the -b: the -b: -b: "-b3" means “use BIOS means “use BIOS “use BIOS BIOS calls for input and input and and output, and use BIOS device number 3 number 3 3 (the MIDI port). The argument argument is in decimal. decimal. Any number at number at at all may be used may be used be used used here, including numbers which numbers which which are not in fact BIOS BIOS device numbers; numbers; in this case, the debugger will probably crash, and it is likely that you you will have have to reset your your machine. 

Use the MIDI port. An ST running a terminal program which uses the MIDI port must be connected with a double-MIDI cable (i.e. MIDI OUT-> MIDI IN and MIDI IN -> MIDI OUT). 

In the last two modes, the debugger controls the serial or midi port hardware directly, without going through GEMDOS or the BIOS, so there are fewer limitations on debugging programs which use GEMDOS or the BIOS. However, the limitations with respect to the | operating system always apply, except when remote debugging. See the section DB AND GEMDOS in the chapter OPERATING SYSTEM CONSIDERATIONS for more information. Also, see iodev and bdev in the section on debugger variables. Each of the options -g, -b, -s, and -m can be followed by the letter x: this controls the | printing of non-standard characters when using the d (dump) command. Non-standard characters are those with ASCII codes 128 and up. Normally, these are printed in the | ASCII part of the dump command’s output. When-s, -m, or -b with a device-number | code is used, printing of these characters is suppressed, because they confuse most | terminals. The presence of the letter x (e.g. -sx or -bxl) re-enables printing of these characters, which can be useful if your terminal is in fact another Atari computer with the same extended character set. The x modifier also controls the use of inverse video for 

’ : _ 

| 

1 q : ' P 4 4 4 4 q ; 3 | ‘ q q ye ~ w 

| | { 

. @ q 7 | 4 j 4a i 1 | @ i 7 4 4 3 j q 4 . = ’ - 

| 

error messages: if the Atari ST extended character set is used, the VI52 code for inverse video will be used too. 

In addition, the following option controls loading of the initialization script: 

The debugger normally searches for and executes a startup file when it is run. The -i option disables this. With the optional file argument, the normal startup file is not loaded, and file is loaded in its place. There must not be a space between the -i option and the file argument: "-imyfile". See the section USING THE DEBUGGER in this chapter for more information. 

**==> picture [345 x 102] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||
|---|---|---|---|---|---|---|
|Usage examples:|
|db|start the debugger;|use GEMDOS|for I/O.|
|db|-s myprog.prg -z|use the serial|port|for I/O;|load|myprog.prg|
|for execution,|with|the command-line|
|argument -z.|

**----- End of picture text -----**<br>


Several terms are used throughout this document which must be defined here. 

The client is the program you are debugging. 

The head is the part of the debugger which handles all user input and output. The commands you type are translated by the head into commands for the stub. It is the stub which causes the client to run, processes breakpoints, and catches exceptions like bus error. The stub reports these events to the head, which reports them to you. 

When you are remote debugging, the head runs on the master machine, and the stub and client run on the slave machine. The head gives commands to the stub and receives the stub’s responses through the communications layer, which actually talks over an interface cable. 

The term debugger is used to refer to the head, stub, and communications; in short, everything but the client (program) and the user (human). 

| | 

4 : ] 1 - S : i 1 , : 1 : j q ] ; : 4 : ] 4 1 3 ] j q : j q 4 q 4 4 ] 4 E 3 7 j { 

: ‘ | 4 | } | 

You cause the client to execute instructions with the g (go), t (trace), u (untrace), and v (verbose-trace) commands, collectively known as trace/go commands. A stop is anything which causes a trace/go to stop: a bus error, address error, or other processor exception, a breakpoint whose count has reached zero, or a memory checkpoint which becomes true. Memory checkpoints are evaluated at times called opportunities, which occur when processing exceptions, including the illegal-instruction exception caused by breakpoints and the trace exception which happens between instructions of a trace. 

You can put a list of commands to be executed in a file, and cause those commands to be executed by the debugger using the load command. Such files are called scripts. Also, procedures consisting of debugger commands, arguments, and local variables are available. 

## USING THE DEBUGGER 

When the debugger is started, it processes its GEMDOS command line first. If there are any options (like -m or -s) they are checked and dealt with. Then, if there is a program argument, that program is loaded and set up for executing. It becomes the client. If there are any args they are placed in the client’s basepage, as GEMDOS command-line arguments to it. When the client is completely set up and ready to run, the debugger prints out its basepage information (text size, environment pointer, etc.) This client set-up amounts to the same thing as using the exee command. 

The debugger then looks for and loads your configuration file (that is, it executes the commands found there; such files are called scripts). The first place it looks is the current directory, for a file called db.re. If that file doesn’t exist, it looks for the file named in the environment variable DBRC. If there is no such environment variable, it looks for the file db.re in the directory named by the environment variable HOME. If none of these files exists, the debugger simply continues with the start-up procedure. 

When remote debugging, the autoload procedure is the same, except that the debugger looks for rdb.re, then the file named in the environment variable RDBRC, followed by rdb.re in the HOME directory. 

In either case, the -i option on the debugger’s command line inhibits the loading of a startup file. If the -i option has a file argument, that file is loaded instead. The debugger searches for the file in the current directory first, then in the HOME directory. 

1-4 

4 ‘ Whether or not there was a program argument to execute and/or a startup file, the f| 4 a= debuggerprompt, the ultimatelydebugger displaysis waitingits prompt,for you toa colontype a(":").commandAny timeline. youCommandsee the colonlines . 4 consist of commands and their arguments. Multiple commands on one command q 4 line are separated by semicolons (";"). Multiple-letter commands must be . | separated from their arguments by a space (e.g. "where 12322"), while 4 single-letter commands don’t need a space (e.g. “d12322" or "d 12322"). | j 1 You can always use ~S (control-S) to stop the debugger’s output and ~Q to start { 4 it again. You can usually use *C to abort a command, especially commands , 4 which generate long listings. ] 4 All numbers printed by the debugger are in hex. All numbers you type are { q assumed to be hex, unless prefixed with @ (decimal) or % (binary). ; 4 When debugging programs compiled under Mark Williams C, you need to play a j a trick before you start the program. See the section DB AND MARK WILLIAMS 4 C in the chapter OPERATING SYSTEM CONSIDERATIONS for more | & information. el When remote debugging, the debugger will display its version number, then } ‘ wait for the stub to respond before loading the configuration script. 

| | | : 1 

| 

| | | i | | | 1 

' 

, 

Ws . Simple expressions contain no operators and are not enclosed in ; 4 parentheses. There may not be any spaces in a simple expression. Simple g expressions take one of the following forms: : : hex constant . $hex constant : a A hex constant has the obvious value. hex constant has the obvious value. constant has the obvious value. has the obvious value. the obvious value. obvious value. value. The leading’ leading’ $’ is optional: optional: gg with no prefix, no prefix, prefix, a number number is assumed to be hexadecimal. assumed to be hexadecimal. to be hexadecimal. be hexadecimal. hexadecimal. Hex f constants consist of an of an an optional sign sign (+ or -) followed by one or by one or one or or & more of the of the the digits 0-9, A-F, A-F, and a-f. 4 Examples: 0, 1, 3FA, 13aD4, $ffffa4do, $-5b30 (same as $ffffa4d0). . | @decimal constant 1 4 A decimal constant begins with an at-sign ("@"), then an optional decimal constant begins with an at-sign ("@"), then an optional constant begins with an at-sign ("@"), then an optional begins with an at-sign ("@"), then an optional with an at-sign ("@"), then an optional an at-sign ("@"), then an optional at-sign ("@"), then an optional ("@"), then an optional then an optional an optional optional : sign (+ or -), then one or more -), then one or more then one or more one or more or more more digits 0-9. 0-9. It has the obvious has the obvious the obvious obvious value. , 4 Examples: @0, @99, @-32768 (same as as $ffff8000). _ %binary constant bi q ’ A binary constant begins with a percent-sign ("%"), then an 

4 ‘ : ; : ' 

| 4 

**==> picture [2 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
;<br>**----- End of picture text -----**<br>


## Chapter 2 EXPRESSIONS, RANGES, AND STRINGS 

This chapter describes how values are entered into the debugger, mostly as arguments to commands. An expression is something which boils down to a single numeric value. A range is something which boils down to a starting address and a length: a range of addresses. A string is something which boils down to a series of single-byte values. A section on each follows. 

## EXPRESSIONS 

An expression can be used any time a numeric value (like an address or count) is expected. All expressions evaluate to 32-bit integers. Overflow is checked when reading a constant (so the hex constant $FFFFFFFFO would cause an error because it requires 36 bits). Overflow is not checked in any other situation. There are two kinds of expressions: simple expressions and complex expressions. 

## SIMPLE EXPRESSIONS 

| 

A hex constant has the obvious value. hex constant has the obvious value. constant has the obvious value. has the obvious value. the obvious value. obvious value. value. The leading’ leading’ $’ is optional: optional: with no prefix, no prefix, prefix, a number number is assumed to be hexadecimal. assumed to be hexadecimal. to be hexadecimal. be hexadecimal. hexadecimal. Hex constants consist of an of an an optional sign sign (+ or -) followed by one or by one or one or or more of the of the the digits 0-9, A-F, A-F, and a-f. 

Examples: 0, 1, 3FA, 13aD4, $ffffa4do, $-5b30 (same as $ffffa4d0). 

A decimal constant begins with an at-sign ("@"), then an optional decimal constant begins with an at-sign ("@"), then an optional constant begins with an at-sign ("@"), then an optional begins with an at-sign ("@"), then an optional with an at-sign ("@"), then an optional an at-sign ("@"), then an optional at-sign ("@"), then an optional ("@"), then an optional then an optional an optional optional sign (+ or -), then one or more -), then one or more then one or more one or more or more more digits 0-9. 0-9. It has the obvious has the obvious the obvious obvious value. Examples: @0, @99, @-32768 (same as as $ffff8000). 

optional sign (+ or -), then one or more digits 0-1. It has the j obvious value. $00008000).Examples: %0, %1010, %1000000000000000 (same as | symbol j A leading period (.’) indicates that what follows is a symbo] | specification. The value of the expression is the 32-bit value in the | i symbol’s value field. A symbol specification can simply be the | | name of the symbol (e.g. ".start") or something more complex. See | informthe ch **a** ptertion. SYMBOLS AND DEBUGGER VARIABLES for more ; : | Examples: .main, -gemlib:xmain: _main:L3 | ‘variable | leading backquote ( * ’) indicates that what follows is a debugger : variable name. The value of this expression is the value in the j corresponding debugger variable. See the chapter SYMBOLS 4 AND DEBUGGER VARIABLES for more information. ] Examples: ‘dO, ‘clientbp, ‘mtype 1 - &variable j A leading ampersand (’ &’) indicates that what follows is a ] debugger variable name, and the value of this expression is the : address of the Storage for the indicated variable in the stub’s ; memory. These variables should not be changed, since the i debugger’s local copy of the variable might overwrite your change. 4 However, these addresses can be used in memory checks to set 3 checkpoints on the values in registers. j See the section DEBUGGER VARIABLES in the chapter 1 SYMBOLS AND DEBUGGER VARIABLES (especially the q subsection Client Registers), and the section MEMORY 4 CHECKPOINTS ON VALUES IN REGISTERS in the chapter F THE CLIENT, BREAKPOINTS AND CHECKPOINTS: DETAIL j for more information. ; 

{ 

2-2 

Examples: &dl, &pe, &sr 

he s- 

: 

| 

| 2 

$ 

The dollar-sign alone is short for ‘$. This temporary variable is set to the result of the last math command (that is, just an expression on the command line). In addition, the f (find) command sets $ to the address of the start of the first match. 

## Example: $ 

## COMPLEX EXPRESSIONS 

| 

The operators you can use in complex expressions are all as in C: 

+-*/%~ & | (arithmetic and bitwise operators) sel= >< >= <= && || (relational operators) [+ -~ (prefix unary operators) >> << (bit-shift operators) ?:= +e -e * = /=H= 7H (the(assignment conditional operator)operators) &= |= >>= <<= (more assignment operators) () (parentheses for grouping) 

In addition, some "function calls” are available: peek(exp) returns the value of the byte at address ’exp’ in the client. wpeek(exp) returns the word, and Ipeek(exp) returns the long. speek(exp) and swpeek(exp) return the byte and word sign-extended into a long. 

. 

Parentheses can be used for grouping. Also, since spaces separate arguments in commands, you need to use parentheses to set off an expression containing spaces as a single argument: 

1 func + 10 is the "list" command with three arguments: the value of .func, the nonsensical argument ’+’, and the number $10. The following two lines both do what you expect: list starting at offset $10 in .func: 

1 .func+10 

## Me 

## w 

] (func + 10) 

Names of machine registers, stub variables, global variables and local variables 

2-3 

j : 1 : : ] 3 ‘ : i i : | 1 i | 4 4 1 1 : ; 

j : ] ] { 1 j j ; 4 4 j 4 : 1 

(when in scope) can all be used and assigned to in expressions. To use program symbols, use a dot as a prefix: ".start" means "the value of the program symbol ’start™ (NOT the value found at that address). 

In an expression, a word like "feed" might be interpreted as either a variable name or a hex number. Words are scanned for meaning in that order. Prefix with a zero (0) ora dollar-sign (’$’) to force interpretation as a hex number ("Ofeed" or "$feed"), ora backquote (’*’)to force interpretation as a variable ("feed"). In addition, ~ ~ dO is the address of dO.1 in the stub’s memory. Unlike C, the && and | | and ?: operators always evaluate all their operands. An example use of the conditional operator: "var = (exp] ? exp2: exp3)" means "If exp] is true, set var to exp2, else set var to exp3." 

To just "do math" at the debugger command prompt you can give a command like "3+3" to print the answer. If the first part of the expression looks like a command, put parens around it: as a command, "dO" means “dump starting at zero" but, "(dO)" means "print the value of the register dO." The special symbol "$" represents the value of the last of these expression-commands (or the address of the first match from an "f" (find) command). These expression-commands normally print the result in hex, decimal, octal, and binary, but if there are any assignment operators then the answer isn’t printed. Thus, "(var += 6)" is a legal command which increments var by 6, assigns the answer to $, but does not print the answer. This is the most logical way to assign and change values of variables in scripts; the "set" and "x" commands can also be used, but they are now obsolete. 

Caveat: an assignment like "(tO = 3 x)" will in fact assign 3 to the variable tO before jumping out with a parse error. 

i] = a | q q 

| 

| 

| | L i 1 4 ( q ' : | ' q 1 : 

&: = & j 4 : q | 3 f 4 : ae to q 4 q 4 4 . a : 4 | a r 4 q 4 ’ ’ 4 ; q a _ p 4 q 4 j 4 

**==> picture [45 x 18] intentionally omitted <==**

**----- Start of picture text -----**<br>
FORMAT<br>**----- End of picture text -----**<br>


**==> picture [322 x 562] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||
|---|---|---|---|---|---|---|---|---|
|FORMAT|COMMENTS|
|(exp]|+|exp2)|Add the expressions|together|
|(exp]|- exp2)|Subtract exp2 from exp1|
|(exp]|* exp2)|Multiply the expressions together|
|(exp1|/ exp2)|Divide exp1|by exp2|
|(exp1|\ exp2)|Return|exp1|modulo exp2|
|BITWISE|
|(exp1|& exp2)|Bitwise AND the expressions|together|
|(exp1|||exp2)|Bitwise OR the expressions together|
|(expl|~|exp2)|Bitwise EXCLUSIVE OR the|
|expressions|
|(exp]|~ exp2)|Bitwise NOT|(invert)|the expression|
|(exp1|>>|exp2)|exp]|>>|exp2|(that|is, exp1|shifted|
|by exp2 bits|(zero|fill))|
|(exp1|<<|exp2)|exp]|<<|exp2|(that|is, exp]|shifted|
|left by|.|
|exp2|bits)|
|LOGICAL|
|(exp1|= exp2)|TRUE|if the expressions are equal|
|(also ==)|
|(exp]|&&|exp2)|Logical AND of the two|expressions|
|(exp1||||exp2)|Logical OR of the two|expressions|
|(exp]|~ *|exp2)|Logical EXCLUSIVE OR of the two|
|expressions|
|(!|exp)|Logical NOT of the expression|
|(expl|>|exp2)|TRUE if exp]|>|exp2|(unsigned)|
|(expl|>|exp2)|TRUE if exp1|< exp2|(unsigned)|
|(exp1 s>|exp2)|TRUE|if exp]|> exp2|(signed)|
|(exp]|s>|exp2)|TRUE|if exp]|< exp2|(signed)|
|MEMORY|
|(lpeek exp)|Returns the longword at address exp|
|(wpeek exp)|Returns the word at address exp|
|(peek exp)|Returns the byte at address exp|

**----- End of picture text -----**<br>


**==> picture [2 x 3] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


ry 

2-5 

| 1 

| : : j 4 : } 

q ;‘ 1 q 

: 

: 

Here are some examples of complex expressions and how they evaluate: 

EXPRESSION VALUE COMMENTS . 2+3+3 8 simple addition 7-5 2 simple subtraction (2+ 1) *3 9 nested complex expressions ‘clientbp + 100 gives the client’s text base (lpeek (4 + ‘dO + *a0)) the addressing mode 4 (d0,a0.1) 

## RANGES 

A range is a way to specify a block of memory. A range consists of a start address and either an end address or a count. For most commands which take a range, the start and count values have defaults, so not all parts of the range need to be typed in. 

A fully-specified range can look like "start, end” or "start{count]" (where start, end, and count are expressions, and the brackets and commas must be typed as shown). If the end address is present, it is the first address not included in the range: 100,200 specifies the range of addresses from 100 to 1FF, inclusive. 

Various parts of the full specification can be omitted. A range which uses the default start address looks like "end " (note the leading comma, showing that start was omitted) or "[count]" (the brackets set off count and show that start was omitted). If you want the default count the range just looks like "start" (which also looks like any other expression). 

Here are some examples and the ranges they specify, assuming the default start is 100 and the default count is 80 (all numbers are hex): 

## a 

**==> picture [295 x 104] intentionally omitted <==**

**----- Start of picture text -----**<br>
RANGE FIRST LAST COMMENTS<br>200[70] 200 26F no defaults; start [count ] form<br>200 200 27F default count of 80<br>{70} 100 16F default start; [count } form<br>80,100 80 FF no defaults; start,end form<br>,200 100 1FF default start; ,end form<br>**----- End of picture text -----**<br>


: ; j . 

] 

| 

2-6 

: Sometimes the start and/or count fields have no defaults; in these cases, a they must be specified. Also, the start{count ] form is not always allowed. q This is the case for the g (go) command, where a count of bytes to execute | does not make sense. ' The default start and count values are listed in the descriptions for all fy commands which take a range argument. . . STRINGS : Strings are used mainly by the f (find) and s (memory set) commands. A q 4 string consists of characters surrounded by double-quotes ("string") or [ q single-quotes (‘string '). The string acts like the sequence of bytes 7 J represented by the characters between the quotes, with the following . 3 escapes: 4 ESCAPE MEANING -[:] q . \b backspace ($08) bed \e escape ($1B) ~ aa \f formfeed ($0C) ; 4 \n linefeed ($OA) | \r carriage return ($0D) & \\ the single character backslash ($5C) } \? the special "wildcard" escape (see find) | 4 \xXX the byte $XX where XX is two hex digits q 4 Quotation marks are also used to set off parts of commands and keep F semicolons from splitting up a command. See the chapter { q PROCEDURES, IF, GOTO, DEFER, AND ALIAS for more information. 

| 

1 1 1 1 : ‘ 

| ‘ Chapter 3 4q THE CLIENT, BREAKPOINTS, AND CHECKPOINTS: AN OVERVIEW S RUNNING THE CLIENT PROGRAM : : Once there is a client ready to run (loaded with the exec command or with a P program argument on the debugger’s command line), you can cause it to run with p 4 the g (go), t (trace), u (untrace), and v (verbose-trace) commands. Collectively, , 4 these are called trace/go commands. What follows are cursory descriptions. See , the chapter THE CLIENT, BREAKPOINTS AND CHECKPOINTS: DETAIL for Ss more information. . 4 The g (go) command runs the client at full speed. It will only stop when q 4 something exceptional happens, like hitting a breakpoint or causing a bus error. = You can also stop it by hitting the stop button, if you have one. See the section , 4 STOP BUTTONS in the chapter REMOTE DEBUGGING for more information. ; The t (trace) and u (untrace) commands cause the client to execute just a few 4 4 instructions (sometimes just one) and then stop and display the registers. The v We . (verbose trace) command causes the client to execute one instruction, display al those registers which have changed, then execute the next instruction, and so on. . | You can "trace through" a subroutine this way, or even trace through entire | **a** programs. The advantage is that the client doesn’t get out of your control: the stub 4 gets an opportunity to check memory checkpoints between each instruction, and ; 4 you can stop the client after executing a certain number of instructions, even if 4 those instructions are-part of (say) an infinite loop. Naturally, tracing is 4 4 significantly slower than full speed, because of all the processing going on in the 1 4 stub. For just one or a few instructions, however, the speed doesn’t really matter , 4 much, anyway. q ] Trace and untrace are almost identical. and untrace are almost identical. untrace are almost identical. are almost identical. almost identical. identical. They differ in their treatment ofthe "trap" differ in their treatment ofthe "trap" their treatment ofthe "trap" treatment ofthe "trap" ofthe "trap" "trap" rf | instruction. See the section TRACE and UNTRACE in the chapter THE the section TRACE and UNTRACE in the chapter THE section TRACE and UNTRACE in the chapter THE TRACE and UNTRACE in the chapter THE and UNTRACE in the chapter THE UNTRACE in the chapter THE in the chapter THE the chapter THE chapter THE THE F CLIENT, BREAKPOINTS AND CHECKPOINTS: BREAKPOINTS AND CHECKPOINTS: AND CHECKPOINTS: CHECKPOINTS: DETAIL for more for more more - information. tf BREAKPOINTS , 4 Breakpoints allow you to stop the client program when you to stop the client program when to stop the client program when stop the client program when the client program when client program when program when when it is about to execute the is about to execute the about to execute the to execute the execute the the j q instruction at a specific address. at a specific address. a specific address. specific address. address. A counted breakpoint allows you to stop the client counted breakpoint allows you to stop the client you to stop the client to stop the client stop the client the client client q the n-th time the instruction n-th time the instruction time the instruction the instruction instruction is executed. executed. MM ; You set breakpoints with the b command. set breakpoints with the b command. breakpoints with the b command. with the b command. the b command. b command. command. When you set breakpoints and use the you set breakpoints and use the set breakpoints and use the breakpoints and use the and use the use the the = trace/go commands, the trace/go commands, the trace/go the trace/go trace/go is stopped stopped if the PC matches any breakpoint the PC matches any breakpoint PC matches any breakpoint matches any breakpoint breakpoint 

| | | | | : 1 ! ' \ ; : 

Trace and untrace are almost identical. and untrace are almost identical. untrace are almost identical. are almost identical. almost identical. identical. They differ in their treatment ofthe "trap" differ in their treatment ofthe "trap" their treatment ofthe "trap" treatment ofthe "trap" ofthe "trap" "trap" instruction. See the section TRACE and UNTRACE in the chapter THE the section TRACE and UNTRACE in the chapter THE section TRACE and UNTRACE in the chapter THE TRACE and UNTRACE in the chapter THE and UNTRACE in the chapter THE UNTRACE in the chapter THE in the chapter THE the chapter THE chapter THE THE CLIENT, BREAKPOINTS AND CHECKPOINTS: BREAKPOINTS AND CHECKPOINTS: AND CHECKPOINTS: CHECKPOINTS: DETAIL for more for more more information. BREAKPOINTS 

Breakpoints allow you to stop the client program when you to stop the client program when to stop the client program when stop the client program when the client program when client program when program when when it is about to execute the is about to execute the about to execute the to execute the execute the the instruction at a specific address. at a specific address. a specific address. specific address. address. A counted breakpoint allows you to stop the client counted breakpoint allows you to stop the client you to stop the client to stop the client stop the client the client client the n-th time the instruction n-th time the instruction time the instruction the instruction instruction is executed. executed. You set breakpoints with the b command. set breakpoints with the b command. breakpoints with the b command. with the b command. the b command. b command. command. When you set breakpoints and use the you set breakpoints and use the set breakpoints and use the breakpoints and use the and use the use the the trace/go commands, the trace/go commands, the trace/go the trace/go trace/go is stopped stopped if the PC matches any breakpoint the PC matches any breakpoint PC matches any breakpoint matches any breakpoint breakpoint 

| 

' address and the count for that breakpoint (if any) has expired. j See the chapter THE CLIENT, BREAKPOINTS AND CHECKPOINTS: ' DETAIL for more information. r | | MEMORY CHECKPOINTS Memory checkpoints cause a stop based on the contents of memory, rather than ‘ before executing a particular instruction. You set checkpoints with the m command. When you set checkpoints and do a trace/go, the trace/go is stopped when any of the checkpoint expressions become TRUE. Note the word "becomes" -- memory checkpoints are "edge triggered" rather than static. 

j | 

| : 

| 

7 

| 

Checkpoints are of two types: range and comparison. Range checkpoints cause a stop when a change is detected in a range of memory (e.g. an array of the screen). Comparison checkpoints cause a stop when the comparison evaluates to TRUE when previously it was FALSE. 

q checUnli **k** pointse breakpoints,need to whichbe evaluat caus **e** d an by exceptionthe stub. inThe thetimes processor, when memorythe stub gets a . chance to evaluate checkpoints are called opportunities. Briefly, opportunities 4 occur between instructions of a trace (verbose or normal) or untrace, and during : the processing of a breakpoint (even if that breakpoint, because of its count, | doesn’t cause a stop). 

Since memory checkpoints only get evaluated during an opportunity, they can only causea stop at those times. Thus, all you know is that the expression became TRUE sometime between the previous opportunity and this one. In the case of trace and untrace, the opportunities come between every instruction. But in the case of a go command, you don’t always know just when the previous opportunity was. Furthermore, the checkpoint might have become TRUE and then FALSE again since the last opportunity. 

_ Breakpoints cause an opportunity even when their counts have not yet expired. You can provide an opportunity explicitly by placing a breakpoint with a count of “never”stop by --themselves, for instance,butatalways the beginningcause an ofaopportunity. loop. Such breakpoints never cause a See the chapter THE CLIENT, BREAKPOINTS AND CHECKPOINTS: DETAIL for more information. 

E | | 1 : : 1 1 ; : :| 4 4 : 4 1 : : 3 ’ q 4 : 4 4q j q 

a CHAPTER 4 | COMMANDS 4 The debugger prompts debugger prompts prompts the user for user for for a command with command with with a colon ; 4 come from text from text text files (see the load command), the load command), load command), command), aliases | q procedures (see the chapter PROCEDURES), the chapter PROCEDURES), chapter PROCEDURES), PROCEDURES), and the client f 4 command). In each each case, multiple commands can commands can can be specified on one | 4 them with with semicolons (";"). If you you really mean to mean to to use a semicolon , 4 argument to to the print or echo commands), print or echo commands), or echo commands), echo commands), commands), the argument OY be enclosed enclosed in quotation marks quotation marks marks ("”) or apostrophes apostrophes 4 chapter PROCEDURES, PROCEDURES, IF, ALIAS, AND DEFER ALIAS, AND DEFER AND DEFER DEFER for more information. | 4 The simplest kind simplest kind kind of command command is simply an simply an an expression. , 4 that expression expression to be evaluated, be evaluated, evaluated, and the result to be the result to be result to be to be be printed , 4 binary. The result result is also placed also placed placed in the debugger the debugger debugger variable ; 4 command (which usually just does some math) just does some math) does some math) some math) math) is ] In the following the following list of debugger commands, of debugger commands, debugger commands, commands, these syntax rules q ne Brackets ("[ ]") surround optional items. items. Italics are used a > b ae type: "d range" means the letter range" means the letter means the letter the letter letter ’d’ followed by a by a a range 4 means the previous item can be repeated one or the previous item can be repeated one or previous item can be repeated one or item can be repeated one or be repeated one or repeated one or one or or more times. ' : in braces and separated by a vertical bar braces and separated by a vertical bar and separated by a vertical bar separated by a vertical bar by a vertical bar a vertical bar vertical bar bar ("{ a a | b . 4 Several items surrounded by both brackets surrounded by both brackets both brackets brackets and braces means you can use one of the 7’ things inside the braces, or nothing nothing at all: ; 3 transcript { { off | flush | 4 means that the following forms are valid: ; transcript none of the alternatives of the alternatives alternatives fo transcript off the off alternative off alternative alternative — transcript flush the flush alternative flush alternative alternative — transcript printer printer the printer alternative printer alternative alternative _< a transcript.. myfile.. the file file alternative.. : = 4 transcript myfile a the file alternative with with q : Note that sometimes the brackets and braces that sometimes the brackets and braces sometimes the brackets and braces the brackets and braces brackets and braces and braces braces should really be typed: 4 q the brackets brackets in range range specifications and the braces and the braces the braces braces in the indirect operand to a memory 4 q checkpoint. The description description of the command the command command should make these exceptions 

| | | ; : if | | : { 4 { : ( i 

The debugger prompts debugger prompts prompts the user for user for for a command with command with with a colon (":"). Commands can also come from text from text text files (see the load command), the load command), load command), command), aliases (see the alias command), procedures (see the chapter PROCEDURES), the chapter PROCEDURES), chapter PROCEDURES), PROCEDURES), and the client (see the indirect command). In each each case, multiple commands can commands can can be specified on one line by separating them with with semicolons (";"). If you you really mean to mean to to use a semicolon (for example, in an argument to to the print or echo commands), print or echo commands), or echo commands), echo commands), commands), the argument containing the semicolon can be enclosed enclosed in quotation marks quotation marks marks ("”) or apostrophes apostrophes (also called "single quotes:" ""). See chapter PROCEDURES, PROCEDURES, IF, ALIAS, AND DEFER ALIAS, AND DEFER AND DEFER DEFER for more information. 

The simplest kind simplest kind kind of command command is simply an simply an an expression. Typing an expression alone causes that expression expression to be evaluated, be evaluated, evaluated, and the result to be the result to be result to be to be be printed in hex, decimal, octal, and binary. The result result is also placed also placed placed in the debugger the debugger debugger variable *$ for future use. This kind of command (which usually just does some math) just does some math) does some math) some math) math) is called a math command. 

In the following the following list of debugger commands, of debugger commands, debugger commands, commands, these syntax rules are used: 

Brackets ("[ ]") surround optional items. items. Italics are used for the* name: of something1 you" > b type: "d range" means the letter range" means the letter means the letter the letter letter ’d’ followed by a by a a range specification. Three dots ("... ) means the previous item can be repeated one or the previous item can be repeated one or previous item can be repeated one or item can be repeated one or be repeated one or repeated one or one or or more times. Several alternatives enclosed in braces and separated by a vertical bar braces and separated by a vertical bar and separated by a vertical bar separated by a vertical bar by a vertical bar a vertical bar vertical bar bar ("{ a a | b }") means either a or b, but not both. Several items surrounded by both brackets surrounded by both brackets both brackets brackets and braces means you can use one of the things inside the braces, or nothing nothing at all: transcript { { off | flush | printer | file(a]}] 

transcript none of the alternatives of the alternatives alternatives transcript off the off alternative off alternative alternative transcript flush the flush alternative flush alternative alternative transcript printer printer the printer alternative printer alternative alternative transcript.. myfile.. the file file alternative.. without: a)‘a transcript myfile a the file alternative with with ‘a’ 

Note that sometimes the brackets and braces that sometimes the brackets and braces sometimes the brackets and braces the brackets and braces brackets and braces and braces braces should really be typed: this is the case for the brackets brackets in range range specifications and the braces and the braces the braces braces in the indirect operand to a memory checkpoint. The description description of the command the command command should make these exceptions clear. 

; 

4 

4-1 

The commands are divided into these groups: 

' ' q ' 4 i : ] / | j q 1 1 | ' : { | i , | : ' | 

|SECTION|COMMANDS|
|---|---|
|Breakpointsandcheckpoints<br>Traceandgo<br>Memoryhandling<br>TheClientandsymbols|b, nb,m,nm<br>Luv, g<br>1, d,s, f<br>exec, args, getsym, sym,nosym, ?, where,<br>stack|
|Registers andvariables<br>Remotecommands|set, x, vars, stubstate<br>wait, check, terminate, continue|
|ProceduresandAliases|procedure, plist, global, local, goto, alias,<br>unalias, noalias|
|Filesand aliases<br>Miscellaneous commands|read, write, load, unload, reload, bgoto,<br>fgoto<br>bind, abort, #, transcript, gag, exit, q, quit,|
||help,echo,print,if,indirect,!,dir|



Memory handling 1, d,s, f | The Client and symbols exec, args, getsym, sym, nosym, ?, where, 1 stack Registers and variables set, x, vars, stubstate 1 Remote commands wait, check, terminate, continue Procedures and Aliases procedure, plist, global, local, goto, alias, q unalias, noalias Files and aliases read, write, load, unload, reload, bgoto, j Miscellaneous fgoto q commands bind, abort, #, transcript, gag, exit, q, quit, i help, echo, print, if, indirect, !, dir ; AND CHECKPOINTS CHECKPOINTS : You use breakpoints to make the client stop at a particular place. You use memory : checkpoints to make the client stop when a particular set of conditions occurs. See q the chapter THE CLIENT, BREAKPOINTS AND CHECKPOINTS: DETAIL for ’ more information. ] b[ — #index} { address [ { count | never } } ] 7 The b command alone lists the active breakpoints. With an address, it sets ' a breakpoint (with a count of one) at that address, and removes all other . breakpoints there. With a count, it sets a counted breakpoint at the ] . address. With never, it sets a breakpoint which will never cause a stop. q (This is useful because it creates an opportunity for memory checkpoints.) q With no arguments, b lists all breakpoints. The list appears in a form q suitable for saving (with transcript) and restoring (with load). 4 If the #index argument is present, the new breakpoint is placed in slot number index. If there was already breakpoint in that slot, the old one is 4 removed first. This option is useful when using auto-execute aliases. See 4 4-2 : 

## BREAKPOINTS AND CHECKPOINTS CHECKPOINTS 

| the section AUTO-EXECUTE ALIASES in the chapter PROCEDURES, 4 IF, GOTO, DEFER, AND ALIAS for more information. 

q | 1 q f 4 _ q 7 | 4 4 q 1 q j q 4 } 3 : 4 4 3 Bd 

: 

| | { : | | | i , 

_ ,| ;j 7 , 4 ; j rf | 4 . . q q ; | : { 4 4 SS. ‘ 7 , 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
.<br>**----- End of picture text -----**<br>


Examples: b list all breakpoints in the table b .main set a breakpoint to stop at the label "main" b .main 1 same as above b .loop 3 set a breakpoint to stop the third time the instruction at "loop" is executed b #4 .loop set a breakpoint at .loop in slot #4, replacing whatever breakpoint was in that slot, and replacing any other breakpoint at that address. 

nb[ {address | #index } } The nb command alone removes all breakpoints. It asks for verification first: space, ’y’, and’Y’ mean "go ahead.” Any other key aborts. With #index, it removes breakpoint number index. With address, it removes all breakpoints at address. 

**==> picture [339 x 77] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||
|---|---|---|---|
|Examples:|
|nb|clear all breakpoints|(asks|for verification)|
|nb #1|clear the breakpoint in slot number 1|
|nb .loop|clear all breakpoints at the label "loop"|

**----- End of picture text -----**<br>


m [ #index[}][ range] 

m [ #index ] address.size[[][.size][]][op {value] | {iaddr} | old } m [ #index[]][ address] 

The m command alone lists all memory checkpoints. The list appears in a form suitable for saving (with transcript) and restoring (with load). With a range, it sets a range-type checkpoint. The default count for range is 2 (a word); there is no default start. With an address and size it sets a comparison checkpoint which will become TRUE when the value there changes. The command "m address.size != old" does the same thing. 

**==> picture [35 x 22] intentionally omitted <==**

**----- Start of picture text -----**<br>
_<br>**----- End of picture text -----**<br>


4-3 

> | a 

a 1 2 4: 5 4 { 1 ] i ]‘ . j " 4 j 1 ; 4 a j 4 4 § 1 j j ’ | q 

j j | : i 

: 1 

| 

| 

| 

: 

Note that address may be a complex expression; see the examples. 

The last form sets a comparison checkpoint, as follows: 

Theis pre . **s** izeent,fieldthereis eithermust be.b,no .w,space or .1.betweenThe sizeit fieldan the canaddress be omitted, butargument.if it That is, ".flag.b" is correct for a byte-size checkpoint address and size, while ".flag .b" is not. If .size is missing, the default is .w (two bytes). 

(Unfortunately, since the memory checkpoint command treats the trailing part of the address argument as a size indicator (.b, .w, and .1), you can't have a checkpoint on a compound symbol specifier whose last component is a two-character symbol starting with a period (.’).) 

The op (operator) can be one of the following: 

OPERATOR COMMENTS S> S<=s>=s< Signed comparison u>u<=u>=u< - Unsigned comparison =al= Equal, not equal vs vc Overflow set, overflow clear ><=>=<= SameSame asas signed== cs cc Same as u< and u>= 

If the operand is enclosed in braces, it is indirect: iaddr is the address of the operand used for the comparison. When the checkpoint is evaluated, as many bytes are fetched from iaddr as are used at address -- that is, the size of the checkpoint controls them both. 

If the operand is the word old, it means to use the initial value at address ) for the subsequent comparisons. This lets you catch a byte, word, or long value when it changes, and is faster than the equivalent range-type checkpoint. The "old" value is reloaded internally at the start of each trace/go command. 

Otherwise, the operand is evaluated as an expression, and its value is used for the comparisons. 

Note that for the indirect comparison type, a pair of braces encloses the second operand ("{iaddr}" in the example). You really type the braces; 

7 they are not there to show syntax. : 4 If the #index argument is present, the new checkpoint is placed in slot : 3 number index. If there was already a checkpoint in that slot, the old one is ; ‘ removed first. This option is useful when using auto-execute aliases. See Pd the section AUTO-EXECUTE ALIASES in the chapter PROCEDURES, 4 IF, GOTO, DEFER, AND ALIAS for more information. : 

| | : | | | | | | | |i 

& Examples: , | m List memory checkpoints 4 m .foo > 10 Stop when foo.w (default size) > 16 ($10). q 7 m .foo > old Stop when foo.w exceeds its initial value. / gi m .buf[10] Stop when anything in the 16 bytes starting at buf ; 4 changes. q 4 m #3 .buf[10] Same as above, but place the checkpoint in slot ; 3 #3. ee. m 438.1 Same as "m 438[4]" we m (2 + 2).w Same as "m 4.w" and "m 4[2]" . my m 438.1 < {43C} Stop when the (long) value at 438 is less than the |g (long) value at 43C. | 4 m 12030 != old Stop when 12030.w changes value. , 4 m 12030[2] Stop when 12030.w changes value (see below). 1 m 12030 Same as above (default count is 2). ] Notice the last three examples. They all seem to do the same thing: stop | 4 when either of the two bytes starting at 12030 changes. The range type is _ less desirable, though, for checking small areas (one, two, or four bytes), f 4 because the range type computes the CRC (cyclic redundancy check) for 4 4 the range, and compares it to what the CRC was when the trace/go ' q started. This takes a long time, and, more importantly, changes can ] : actually be missed if both the original and new contents result in the same _ CRC value. 

q : 

4-5 

i | . A 1 a a 2 | { : q | { 4 j 4 ] { | 1 q } ; 4 F ‘ ] j F j j ; 

| | : j 

| 

' 

nm [{address | #index }] The nm command alone, like the nb command, clears all the memory checkpoints. It asks for verification first: space, ’y’, and 'Y’ mean "go ahead," any other key aborts. With address, the command clears all checkpoints with that address. With #index, the command clears checkpoint number index. 

**==> picture [334 x 87] intentionally omitted <==**

**----- Start of picture text -----**<br>
Examples:<br>nm Clear all checkpoints. Ask for verification first.<br>nm #3 Clear checkpoint number three.<br>nm .flag Clear all checkpoints with the value of "flag" as<br>the address.<br>**----- End of picture text -----**<br>


## TRACE AND GO 

The trace and go commands are the only ones which cause the client to execute instructions. When they stop, the reason for the stop is printed (e.g. "Breakpoint"), the client’s registers are displayed, and the instruction at the (new) PC is disassembled. 

When the conditional branch instructions are disassembled at an address that matches the current PC, either because you used the set command with no arguments, or after a trace, or during a verbose trace, the letter 'T’ or’F’ will appear between the address and the opcode: means the condition is false, and the branch will not be taken. This applies to other conditional instructions as well, such as beq. It does not apply to conditional floating-point instructions. 

The CPU register display includes a mnemonic display of the SR. The mnemonics are as follows: 

**==> picture [2 x 1] intentionally omitted <==**

**----- Start of picture text -----**<br>
)<br>**----- End of picture text -----**<br>


**==> picture [210 x 118] intentionally omitted <==**

**----- Start of picture text -----**<br>
SU __ supervisor mode<br>TR trace bit set<br>IPL=x x is the IPL<br>CS, CC carry set, clear<br>ZR, NZ zero set, clear<br>vs, VC overflow set, clear<br>XS, XC extended carry set, clear<br>MI, PL sign bit set, clear<br>**----- End of picture text -----**<br>


‘ 4 t[{count | x | w}] ; j The t (trace) command causes command causes causes the client to execute client to execute to execute execute in "trace mode." mode." With _ no count, the client executes one instruction. count, the client executes one instruction. client executes one instruction. executes one instruction. one instruction. instruction. With a count, the client client : . executes that many many instructions. With a count of ’x’, count of ’x’, of ’x’, ’x’, the client executes , 4 "forever" -- until a breakpoint, breakpoint, memory checkpoint, checkpoint, or exception causes a 4 stop. / 4 With a count of ’w, the t command executes count of ’w, the t command executes of ’w, the t command executes ’w, the t command executes the t command executes t command executes command executes executes one instruction at full speed. speed. ; 4 This is handy handy if it it is a "jsr" or “bsr" or “bsr" “bsr" instruction: in those those cases, the whole whole ‘ q subroutine is executed executed all at once, once, and the trace stops at the the instruction , following the "jsr" or or "bsr." . | See the section TRACE AND UNTRACE in the chapter THE CLIENT, . 4 BREAKPOINTS AND CHECKPOINTS: DETAIL for more information. = u[{count | x}] 

| 

, | | | ( it i | i : ’ 

The t (trace) command causes command causes causes the client to execute client to execute to execute execute in "trace mode." mode." With no count, the client executes one instruction. count, the client executes one instruction. client executes one instruction. executes one instruction. one instruction. instruction. With a count, the client client executes that many many instructions. With a count of ’x’, count of ’x’, of ’x’, ’x’, the client executes "forever" -- until a breakpoint, breakpoint, memory checkpoint, checkpoint, or exception causes a stop. 

With a count of ’w, the t command executes count of ’w, the t command executes of ’w, the t command executes ’w, the t command executes the t command executes t command executes command executes executes one instruction at full speed. speed. This is handy handy if it it is a "jsr" or “bsr" or “bsr" “bsr" instruction: in those those cases, the whole whole subroutine is executed executed all at once, once, and the trace stops at the the instruction following the "jsr" or or "bsr." 

**==> picture [41 x 39] intentionally omitted <==**

**----- Start of picture text -----**<br>
=<br>* ‘7<br>**----- End of picture text -----**<br>


The u (untrace) command is just like the t (trace) command, except that the client executes in "untrace mode." This means that trap-type instructions are not treated specially. Note that uw doesn’t make uw doesn’t make doesn’t make make sense and isn’t allowed. 

q. 44 instructions are not treated specially. Note that uw doesn’t make uw doesn’t make doesn’t make make sense 4 and isn’t allowed. : 4 v({{u | w}) [count] 4 4 The v (verbose-trace) command begins another kind of trace: before each bi4 ezq instruction. . is. executed, iteois disassembled. and displayed. on the screen. q q After it executes, the values of all registers which changed are displayed. q .. Then the next instruction is disassembled, and so on. Use ~S to pause the : a trace, ~Q to continue it, and *C to stop it. 4 4 With no count, the v command will trace forever (until a stop or until *C 4 E is used). The verbose trace executes in “trace” mode, meaning that a trap 43 4a handler is. executed as though it. were a single: instruction.. . With: a count, 3 q that many instructions are disassembled and executed. q q With ’u’, this command traces instructions in "untrace” mode. q a With ’w’, instructions are traced (in trace mode), but the bsr and jsr 4 q commands are treated specially: they are executed at full speed, like the t tw command. Also, the vw command stops when it encounters the rtd, i rtr, rte, or rts instruction. 

q { \ : ; { 1 : 4 q 4 ' i ] j : I 4 ; 7 4 . j q 

; j ; : : : ' F 

: } 

Examples: 

**==> picture [294 x 126] intentionally omitted <==**

**----- Start of picture text -----**<br>
t trace one instruction<br>t4 trace four instructions<br>tx trace forever (until a stop)<br>tw execute through a subroutine at full speed<br>u untrace one instruction<br>u4 untrace four instructions<br>ux untrace forever (until a stop)<br>v9 verbose-trace 9 instructions<br>Vv verbose-trace forever (until a stop)<br>**----- End of picture text -----**<br>


g [ range ] 

The g (go) command causes the client to execute at full speed. It turns control of the computer over to the client, after setting the breakpoints. The go will only stop when a breakpoint, exception, or the stop button causes a stop. 

The default start address for range is the current PC. The default count means "forever." In fact, you can’t specify a count for this range; you can only use the "start" or "start, end" or ",end" forms of the range. If you specify an end, a temporary breakpoint is set at that address. This is sometimes called "go until,” because you are saying, "Go until this spot, then stop." See the examples below for more. 

Examples: g Go forever (until an exception or breakpoint) g .main Set PC to main, then go. g ,.subproc Set a temp. breakpoint at subproc, then go. g .main,.subproc Set PC to main, set a breakpoint at subproc, and go. 

Note that the "go until" forms actually go until the end address or some other exception. Note also that they clear the temporary breakpoint when the go stops, for whatever reason. Finally, note that there must be at least one breakpoint slot available for the "go until" to work. 

q 

{ 

4-8 

. 1 

4 { The I (list) command disassembles memory into 68000 mnemonics. The ; default start for range is the place where the last | command left off, but i 4 the exec command and all the trace/go commands set the default start to 1 q the new PC after the command is finished. The default count for range | q produces 12 lines of disassembly, not any particular number of bytes. q The list command takes the range as a guideline: the last instruction it & disassembles is the one containing the last byte of the range, even if the ; instruction extends beyond that byte. 1 q The disassembly listing you get looks something like this: 4 myprog: 7 ? 00012214 move.| #$12214,al myprog Zs . 0001221A lea! $12214(PC),al myprog _ 0001221E move.| al,$12004 myvarl . 4 00012224 move. $12004,$12008 myvarl,myvar2 4 0001222E move.| $4BA.w,dl clock 4 00012232 addq.l #3,d1 4 00012234 bra.b $12214 myprog ] 1 The listing has four columns: the disassembly address is printed in the first | 4 column, then the opcode and size, then the operands, and finally any | 4 symbols matching the values used in the operands. 1 ] If there are any symbols with the same value as the address of the , 4 instruction being disassembled, they are printed out above the disassembly | | line (like the label "myprog:” above). q q The names in the right-hand column are the names of symbols matching _— the operands, separated by commas. If there are two numeric operands, q 7 and there is at least one symbol matching each of them, the symbols for 7 each operand are separated by a semicolon. 

| 

| : ; 

| 

i 

~ j q Z) 4; 

## MEMORY 

The following commands display and set memory in various ways. 

## I [ range ] 

Operands which are less than $100 do not get matching symbols printed: it would be too confusing, since so many symbols lie in this range, and picking out the one which mattered in any particular instruction would be 

: 

: { 

7 

q 

' 

: 

| 

| | | 

' 

‘ 

impossible for the debugger and difficult for the user. You can list all the symbols with a given value using the where command. 

If you are on a 68020 or 68030, the 68881 floating-point coprocessor instructions are disassembled, and the 68030 PMMU instructions are disassembled. (The 68851 PMMU shares some instructions in common with the 68030’s PMMU, but no effort has been made to disassemble for the 68851 specifically.) See the description of disepu in the chapter DEBUGGER VARIABLES for more information. 

If an instruction cannot be disassembled, the listing will show ".dc.w 20x" where .00xx is the value at that address. 

|Examples:||
|---|---|
|l|list 12 lines startingwhere the last 1|
||left off|
|| .main[10]|listfrom the label "main"up toand<br>including the instruction which ends at|
|1 ‘pe[1]}|oraftermain+$10<br>listthe (single) instruction at the|
||currentPC|



## d{{w | 1} ] [range ] 

The d (dump) command dumps memory. The default start for range is the place where the last dump left off. The default count is 128 bytes. If w or l is specified, the command dumps words or longwords, respectively. If neither is present, bytes are dumped. 

The memory dump consists of lines with the starting address on the left, the memory bytes (or words or longs) in the middle, and the ASCII representation of the memory on the right. The ASCII representation ) shows the character associated with each byte in memory, if that character is in the "printing character" set (32-127, 160-254 on the Atari ST). See the section OPTIONS in the chapter DB: THE ATARI DEBUGGER for more information. 

The range argument is rounded up to a multiple of the size (2 for w and 4 for 1). The d command alone, with no range or size specifier, dumps 128 bytes starting where the last dump left off, and in the last format used. The 

: ; ] 4 3 ] j : i j j 4 4 j ; | q 

4-10 

: 1 1 q 4 4 4 

; 4 dw ff dl 4 d [10] 4 dl 8{1) ; | dw ‘sp , | di 1000[@256] ! ‘ s[{{w | 1}] (addr { value...) ] rf 4 s{{w | 1} ] range value ; | s addr string bi ° The s (memory set) command s (memory set) command (memory set) command set) command command ae memory. In the the first two two forms, . @ or longwords longwords are to be set. to be set. be set. set. q 4 In the the first form, form, if any any values are present, the byte 1 4 at addr addr is set to the set to the to the the first value. _ memory consecutively consecutively starting at addr and incrementing addr by the , 4 appropriate number number (1, 2, or 4 bytes). j i If value value is not present, not present, present, 4 printed on the screen, on the screen, the screen, screen, | a there. At this point you can this point you can point you can you can can just hit the "return" key to skip to the next 4 q location, or type a new or type a new new value ; q or a single period a single period single period period (".") (plus "return") ] q will also terminate the also terminate the terminate the the command. Typing "*" will go back one entry. . Typing "<" will repeat the "<" will repeat the will repeat the repeat the the current entry, ’ 3 locations or shared memory. or shared memory. shared memory. memory. 1 j The second form second form form fills the the specified , 4 value. If the size of the size of size of of the range , 4 it is rounded up. rounded up. up. Ss. The third form sets the third form sets the form sets the sets the the memory starting at addr to the bytes represented by , | string. The string string is placed placed , | 

: | 

| : 

command "d10" dumps 32 longwords starting at Zero, if followed simply by "d” another 32 longwords will be dumped: the size specifier is preserved. A d command with a range will reset the size to word, long, or byte (if neither w nor 1 is specified). 

|preserved. A d command with a range will reset the size to word,d command with a range will reset the size to word,command with a range will reset the size to word,<br>byte (if neitherneither w nor|A d command with a range will reset the size to word,d command with a range will reset the size to word,command with a range will reset the size to word,with a range will reset the size to word,a range will reset the size to word,range will reset the size to word,will reset the size to word,the size to word,size to word,to word,word, long, oror<br> 1 is specified).|
|---|---|
|Examples:||
|d|dump 128 bytes inthe last format|
|dw<br>dl<br>d [10]<br>dl 8{1)8{1)<br>dw ‘sp<br>di 1000[@256]1000[@256]|dump64word (128bytes)<br>dump32longs (128 bytes)<br>dump 16bytes<br>dumpthebus-errorexceptionvector<br>dumpthestack (aswords)<br>dump64longs(256bytes)startingat1000|



The s (memory set) command s (memory set) command (memory set) command set) command command is used to change the contents of the client’s memory. In the the first two two forms, the presence of w or1 indicates that words or longwords longwords are to be set. to be set. be set. set. If neither w nor 1 is present, bytes are set. 

In the the first form, form, if any any values are present, the byte (or word or longword) at addr addr is set to the set to the to the the first value. If there are many values, they are placed in memory consecutively consecutively starting at addr and incrementing addr by the appropriate number number (1, 2, or 4 bytes). 

If value value is not present, not present, present, memory is set interactively. A memory address is printed on the screen, on the screen, the screen, screen, followed by the (byte, word, or long) value currently there. At this point you can this point you can point you can you can can just hit the "return" key to skip to the next location, or type a new or type a new new value (plus "return") to be placed at that address, or a single period a single period single period period (".") (plus "return") to terminate the set command. ~C will also terminate the also terminate the terminate the the command. Typing "*" will go back one entry. Typing "<" will repeat the "<" will repeat the will repeat the repeat the the current entry, this is useful in examining 1/O locations or shared memory. or shared memory. shared memory. memory. 

The second form second form form fills the the specified range with the (byte, word, or long) value. If the size of the size of size of of the range is not a multiple of the unit (1, 2, or 4 bytes), it is rounded up. rounded up. up. 

The third form sets the third form sets the form sets the sets the the memory starting at addr to the bytes represented by string. The string string is placed placed in client memory as-is: it is not null-terminated. 

4-11 

q f 4 1 q 

1 

j 1 j ' : 4 : i ‘ ! q 4 : ; j j q 1 q { | : q j 

| 

4 

; | i 

| | j | | | 

If exactly two or four bytes are being set, and they start at an even address, the move.w or move.| instructions are used. This can be important if the address in question refers to a memory-mapped I/O device. 

See the section STRINGS in the chapter EXPRESSIONS, RANGES, AND STRINGS for more information. 

## Examples: 

s 400 set bytes interactively starting at $400 sl 400 set longs interactively starting at $400 sw 380[80} 1234 Fill 64 words with $1234 sw 6FO FF20 12 0 -2 set these words at 6FO..6F7:FF20 0012 0000 FFFE s OFO "Testing\r\n\x00" — Set a C-type string (null-terminated) at 6FO 

## FL{w| 1} ] range value... 

f range string 

The f (find) command prints out the beginning address of areas of memory within range which match the target pattern. It also sets the debugger variable $ to the address of the first match. 

The first form takes a size specifier (w for word, I for long, or nothing for byte) and a sequence of values. The values are treated as being of the indicated size, and are used as the target pattern for the find. The asterisk ("*") is a special value which will match any byte (or word or long): it is a wildcard. 

The second form takes a string as the target of the find. See the section STRINGS in the chapter EXPRESSIONS, RANGES, AND STRINGS for more information. For the find command (and only the find command), the string escape "\?" is a one-byte wildcard, which matches any value. 

Note that each individual value is expanded or truncated to the size of the find (byte, word, or long), then split into the component bytes. Ultimately, the target is always a sequence of bytes. This means that a fw or fl command can actually find matches at odd boundaries. 

| 

4-12 

| 

: q 4 ‘ q rr 4 - 4 : ! _ _ ; q 4 ; q . WE, , 1 1 q : : 1 _ _— 1 1 4 4 : ] : q 4 4 ‘ q : q a 

: | | ! | ' | | 

The find command always lists the address of each match. If what you are looking for is found often, the list will be long and useless. You might consider using the gag command to suppress the list; the $ variable will still be set to the address of the first match. See the gag command for details. 

Examples: fl 0,400 FCO008 Find the four bytes 00 FC 00 08 in the range 0. .3FF fw ‘a1[100]} 100 * 300 Find the six bytes 01 00 * * 03 00 f ‘a7[100] "x\?z" Find the three bytes 78 * 7A 

THE CLIENT AND SYMBOLS | These commands load the client and manage the debugger’s symbol table. See the chapter SYMBOLS AND DEBUGGER VARIABLES for more information. exec [ { program [ args...] on | off} ] The exec command loads the named program and sets it up for execution. It also loads the symbols from that program, and sets the GEMDOS command-line arguments to args, if any. The debugger variable "clientbp” is set to the basepage of the loaded program. Finally, the basepage information of the client is displayed. With no arguments, exec displays the basepage information at ‘clientbp (usually the basepage of the last-execed client). 

Normally, whena client uses Pexec and executes a child, a message is sent to the debugger with that child’s basepage address. The "exec off" command disables this. "Exec on" re-enables it. When remote debugging with the resident stub, exec is off by default; you can enable it with "exec on" when the client is stopped (e.g. because you hit the stop button). When you start the debugger with a program argument on the command line, it performs an exec command for that program and any args following it. 

**==> picture [2 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


**==> picture [32 x 22] intentionally omitted <==**

**----- Start of picture text -----**<br>
=<br>**----- End of picture text -----**<br>


4-13 

1 : 

j 

j j , : q ; j 

; 

: d : | j ‘ | | 

4 4 

4 ; : 4 : q 4 q : 4 

When you are not remote debugging, you can use exec to load clients. You must exercise care, however. Once you load one client, you may not be able to load another. The first must either terminate or execute the GEMDOS call Mshrink, to make memory available to the second client. Also, if you stop one client while it is in a GEMDOS trap, then try to use the exec command to load another client, GEMDOS will bomb ungracefully: it is not reentrant. See the chapter OPERATING SYSTEM CONSIDERATIONS for more information. 

You can’t use exec to load programs when remote debugging. The first form still works, however, to display basepage information, and the exec on and exec off commands work. 

Examples: 

exec display basepage information exec myprog.prg load myprog with no arguments exec myprog.prog -o xyz load myprog with command-line arguments "-o xyz" 

## args [args ...] 

The args command sets the command-line arguments for the most recently exec-ed client to args. If there are no args, the command-line arguments in the client’s basepage are cleared out. 

Examples: args clear out the argument area of the client args -O xyz set the argument area to "-o xyz" 

## getsym program [ textbase ] 

The getsym command loads symbols from the named program file. GEMDOS programs are relocatable, so you must supply the textbase argument to relocate the symbols. Some programs, notably those which are placed in ROM, are absolute, and need no relocation. You don’t need a textbase argument for these. This command is used to get symbols for a program which is already loaded, usually when remote debugging. Be sure that the program file you 

q load symbols from matches the file that the client was loaded from; ' 4 otherwise, the symbols may not match up. | 1 The exec command loads symbols from the client program file ' q automatically: no additional getsym command is necessary. 4 : When not remote debugging, do not use this command if you have stopped , 4 the client in the middle of executing a GEMDOS system call: this command , 9 uses GEMDOS to read the file, and GEMDOS is not reentrant. See the , 4 chapter OPERATING SYSTEM CONSIDERATIONS for more 4 information. | Examples: ] j getsym myprog.prg “pc load symbols from myprog-prg, relocating 4 them by the current PC. Right after an q | exec, ‘pc is the text segment base address . * of the process. ys getsym myfile.rom load symbols (absolute: no relocating) 1 : sym name value fq q The sym command creates a new symbol in the symbol table. Name and 4 value are used for its name and value. The new symbol will be treated just q 4 like all the existing symbols in the symbol table. Ff nosym : / The nosym command deletes the entire symbol table. Because ofthe way _ the debugger stores symbols, this memory is not recoverable: if the symbol ‘ ' table took up 12K and you use the nosym command, you will simply lose _ that 12K from the debugger’s memory space for the rest of the session. Db , 4 will ask for verification before doing this, and will report that the memory , was "dropped on the floor." j | ? [ symbol ] | & The ? command displays the symbol table. Ifa symbol argument is F 3 present, it lists from that symbol onward. Otherwise, it starts at the ™ beginning. Use ~S to pause the listing, ~ Q to resume it, and ~Cto 

/ 

: : | | | | | ] | | j | | i | | | 

: 

**==> picture [35 x 23] intentionally omitted <==**

**----- Start of picture text -----**<br>
rf<br>**----- End of picture text -----**<br>


4-15 

1 , 4 4 & . ’ 1 

‘ : { :| : 

| ' 

The symbol list consists of the symbol’s name, its value, and its type, both in hex and in English: each bit of the type has a name associated with it, and if that bit is set the name is printed. If the bit is clear no name is printed. In parentheses, the name of the symbol’s segment is displayed . using Mark Williams C’s conventions, if the type field indicates one of the 1 MWC segments. j Examples: list the whole symbol table. 4 ? main list the symbol table, starting with { "main" j ? .main same as above 4 expression ]The where command shows symbols with where command shows symbols with command shows symbols with shows symbols with symbols with with values at or or : value of expression. expression. If expression expression is absent, the current PC is used. 4 Where shows shows the value of expression, value of expression, of expression, expression, then lists the symbols with symbols with with that { value. If there there are none, it looks for the the next lower valued valued symbol, and j lists all symbols with that value, with their offset from the expression. A Consider the following examples, assuming that the symbols "myprog” and 4 "start" have the value 12000 (hex), "main" has the value 12030, and "loop" q has the value 12038. 7 

where[ expression ]The where command shows symbols with where command shows symbols with command shows symbols with shows symbols with symbols with with values at or or before the value of expression. expression. If expression expression is absent, the current PC is used. 

Where shows shows the value of expression, value of expression, of expression, expression, then lists the symbols with symbols with with that value. If there there are none, it looks for the the next lower valued valued symbol, and lists all symbols with that value, with their offset from the expression. 

**==> picture [342 x 134] intentionally omitted <==**

**----- Start of picture text -----**<br>
COMMAND OUTPUT<br>(a) where 12030 12030: main<br>(b) where 12034 12034: main + 4<br>(c) where 12038 12038: loop<br>(d) where 1203A 1203A: loop + 2<br>(e) where 12000 12000: myprog, start<br>(f where 12006 12006: myprog, start + 6<br>(g) where 30040 12FFE: loop + 1E002<br>(h) where 0 No symbols at or before 0.<br>**----- End of picture text -----**<br>


The last few examples need more explanation. Examples (e) and (f) show that two symbols with the same value will both be printed if necessary. Example (g) shows that the output of where is not always meaningful: 

j 4 q 7 q j 3 q q 4 4 4q 

: 

: 

By ‘ 4 , 4 4 4 ; 4 ; 4 q : , 4 f : , 4 q ; 4 q q zz ] q We - a «Cl _ , 4 _ ; q ’ b ; & _ q q 1 7 q 1 4 4 q 

: . : j | | q | 

30040 is probably well beyond the intended scope of the label "loop", but since that is the symbol with the next lower value, it is displayed. Example (h) shows what happens when there are no symbols at or before the value of expression. 

The where command with no argument shows the where list for the current PC. This is useful when a trace/go command has stopped because of, say, a bus error: you can find out what procedure the PC is in just by typing where. 

## stack 

The stack command tries to perform a stack traceback using the Alcyon C calling conventions. The traceback listing always starts with the current PC, and shows a where-type list for that location. Then the frame pointer (a6) and stack pointer (a7) are reloaded like an unlk (unlink) instruction, and the new PC is taken off the stack. The new PC and a where-type list for it are printed, and the process repeats. 

The traceback stops when the end of the stack is reached (i.e. the new frame pointer is zero), or there is some error in the traceback (odd or zero address, etc.). The stack command tries to be clever: if the current instruction is “link,” it deduces that you are at the start of a procedure, and that the top longword on the stack is the return PC. If the current instruction is "rts," it assumes that the unlk instruction has already executed, and, again, the top longword on the stack is the return address. These are not always valid assumptions, but they work well enough for un-optimized Alcyon C compiler output, and for most other compilers using the link/unlk conventions. If your program does not follow the C calling conventions, or follows them differently (e.g. using something other than a6 as the frame pointer), this traceback will do you no good. 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


**==> picture [42 x 23] intentionally omitted <==**

**----- Start of picture text -----**<br>
rg<br>**----- End of picture text -----**<br>


4-17 

‘ | 

| q j j : q q 4 d j j 

; 

P 

pe : a , | ' a 1 

{ 4 : i } j q 

## REGISTERS AND VARIABLES 

. 

These commands manipulate the client’s registers and the debugger variables. 

set [ variable ( value ]} ] x [ variable [ value } ] 

The set command alone displays the client’s CPU registers: the PC, both stack pointers, the SR, and all the data and address registers. In addition, it disassembles the instruction at the PC (like I‘pef1] would). 

With a variable argument, set displays the value of the given variable. With both a variable argument, set displays the value of the given variable. With both a variable and a value argument, variable is set to value. 

The x command is just an alias for set: it’s there for compatibility and because some people like one-character commands. 

When the conditional branch instructions are disassembled because you used the set command with no arguments, after a trace, or during a verbose trace, the letter I’ means the condition is TRUE, and the branch will be taken; ’F’ means the condition is false, and the branch will not be taken. This applies to other conditional instructions as well, such as seq. It does not apply to conditional floating-point instructions. 

The CPU register display includes a mnemonic display of the SR. The mnemonics are as follows: 

**==> picture [314 x 122] intentionally omitted <==**

**----- Start of picture text -----**<br>
SU supervisor mode<br>TR trace bit set<br>IPL=x x is the IPL<br>CS, CC carry set, clear<br>/ ZR, NZ zero set, clear<br>VS, VC overflow set, clear<br>XS, XC extended carry set, clear<br>MI, PL sign bit set, clear<br>**----- End of picture text -----**<br>


] 

4-18 

4 : 1 ; = , 4 PF 

’ 1 vars ’ | The vars command vars command command lists 4 q as a reminder. q | ; stubstate : q The stubstate command stubstate command command the section DEBUGGER VARIABLES “— DEBUGGER VARIABLES VARIABLES | 4 REMOTE DEBUGGING COMMANDS : ; The following commands only following commands only commands only only have meaning when remote debugging. pF 4 not available when when debugging on on 4 DEBUGGING for more more 4 1 wait { ] The wait command wait command command is ; q slave machine machine is reset, or the terminate or continue commands 4 4 used, or any other time any other time other time time 4 q check : The check command check command command is used to check the integrity of the connection 4 ‘ between the head and the head and head and and q q presents you with you with with a list of keys ; 1 When an an asterisk (’*’) , : When the letter ’S’ . 4 stub. When the the letter I’ I’ } : command. When the 

| 

| : | | | 

4 

! | 

. ; 

**==> picture [49 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
Examples:<br>**----- End of picture text -----**<br>


x show the CPU state set sr show the SR set sr 0700 set the SR to 0700 (IPL 7) set tl show the debugger variable t1 a 

| 

The vars command vars command command lists all the debugger’s built-in variables. It is provided as a reminder. 

The stubstate command stubstate command command displays the stub variables and their values. See the section DEBUGGER VARIABLES in the chapter SYMBOLS AND DEBUGGER VARIABLES VARIABLES for more information. 

The following commands only following commands only commands only only have meaning when remote debugging. They are not available when when debugging on on a single machine. See the chapter REMOTE DEBUGGING for more more information. 

The wait command wait command command is used to synchronize the head and the stub after the slave machine machine is reset, or the terminate or continue commands are used, or any other time any other time other time time that the head is out of synch. 

The check command check command command is used to check the integrity of the connection between the head and the head and head and and the stub. It is meant for debugging the debugger. It presents you with you with with a list of keys it responds to, and begins a feedback test. When an an asterisk (’*’) appears, a successful turnaround has occurred. When the letter ’S’ appears, the head could not send a command to the stub. When the the letter I’ I’ appears, the stub did not respond to the command. When the letter 'Z’ appears, the size of the responding packet 

**==> picture [1 x 18] intentionally omitted <==**

**----- Start of picture text -----**<br>
,<br>**----- End of picture text -----**<br>


4-19 

} | j 1 ; ; { ; ] q q 1 : 4 1 | ' 4 q ; | q q 4 q q 4 3 4 

: { | : j | q : 

; 

. 

: | 

was not as requested. In normal debugging, this command is not used. 

## terminate 

The terminate command causes the client program to terminate. What actually happens is that the stub executes the GEMDOS call Pterm, which terminates whatever the current GEMDOS process is. Thus, this can be used to terminate the client, or a child of the client. 

## continue 

The continue command gives the stub a "go" command, but does not wait for the "go" to stop. It returns immediately to the command prompt. At this point, you may use any command which does not require access to the stub state, the stub variables, the client registers, or any other memory on the slave machine or interaction with the stub. Basically, this means the getsym command and math commands (i.e. just type an expression at the command prompt). Two more commands you can use after a continue are wait, to resynch when the "go" stops, and quit, to leave the head while the client is still running. Finally, the ! (shell) command can be used to mun a program locally. 

## PROCEDURES AND ALIASES 

## procedure [ name [ args...] ] 

The procedure command allows you to define procedures. See the chapter PROCEDURES, IF, GOTO, DEFER, and ALIAS for more information. 

The procedure command alone lists the names and argument lists of all procedures currently known by the debugger. This can serve as a reminder of what a procedure does and how to use it, if the procedure’s name and its arguments’ names are well chosen. 

## plist [name ...] 

The plist command lists procedures (including name, argument list, and body). With no arguments, it lists all procedures currently known by the debugger. With one or more arguments, it lists those procedures. The list appears in a form suitable for saving (with transcript) and restoring (with load). 

4-20 

4 

4 ’ , | ’ L 4 4 , 4 rf 4 | 3 , 3 . 4 { q 4 q _ j q an an ; t q . 4 | 4 , 3 1 4 4 ; 4 fg q 7 1 ; . , 4 1 4 

; 

. | | 

## global [ name ...] 

The global command creates global variables by name. One or more names can be specified to create one or more global variables. If one of the names already exists as global, nothing happens. 

With no arguments, all global variables and their values are listed. The list appears in a form suitable for saving (with transeript) and restoring (with load). 

Ifa name argument begins with a minus sign ("-"), any global variable with that name is removed. 

## local [ name .. .] 

The local command creates local variables by name. One or more names may be specified to create one or more local variables. Local variables are visible only inside the procedure where they were created, or at the top level (outside all procedures). When the procedure exists, they are removed. They do not hold their values from one invocation of a procedure to another. 

With no arguments, all local variables and their values are listed. The list appears in a form suitable for saving (with transcript) and restoring (with load). (This is mainly useful for debugging procedures, not for actually saving the state of local variables.) 

If a name argument begins with a minus sign ("-"), any local variable with that name is removed. 

## goto label 

The goto command causes a jump in a procedure from the current point to the specified label. Labels in procedures look like comments ("#:label"). Labels must be on otherwise empty lines. 

The goto command can be used to create very powerful constructs. With auto-execute aliases, the possibilities are virtually unlimited: a breakpoint can cause a script to be loaded or a procedure to execute, and with if and goto anything can happen. 

4 

4-21 

i! | 

. 

; ; ; ] ‘ 1 | j ‘ q : ‘ 4 ‘ 4 4 q 4 q ; . : q 

: : : i 

: | 

q 

1 f | | 

## SAMPLE PROCEDURE 

procedure sample maxval 

# This procedure shows the first ‘maxval integers. local count ; set count 0 

if (‘argc < 1) abort Too few args =: loop print -n -d ‘count set count (*count + 1) if (‘count < *‘maxval) goto loop print 

Arter loading this procedure (that is, typing it in or loading it from a script), this might happen: 

: sample Too few args : sample 3 012 : sample @20 0123456789 10 11 12 13 14 15 16 17 18 19 

See the chapter PROCEDURE, IF, GOTO, DEFER, AND ALIAS for mere information. 

## alses [ nome [ expansion ] J 

The alias command lets you create your own commands which are cembinations of other debugger commands. The easiest explanation is by example: if you use the command “alias foo dl O[8]" and later enter the command "foo," the expansion of "foo" (in this case, “dl 0[8]") will be executed. In other words, once you alias a name to an expansion, sudsequent uses of that name as a command result in the expansion being used in its place. 

There may be several commands in an expansion -- enclose the whole exransion in quotes, and separate the commands with semicolons, like this: alias show "dw .var1[2] ; dw .var2[2]" 

| 

Ar alias may contain other aliases. For instance, if you alias "dumpword" 

; 

**==> picture [3 x 18] intentionally omitted <==**

**----- Start of picture text -----**<br>
;<br>**----- End of picture text -----**<br>


| 4-22 

{ | to expand to "dw", the above alias could be written alias show . & "“dumpword .var1[2] ; dumpword .var2[2]" : j To change an alias, just redefine it with another alias command. To ; _remove an alias, use unalias. q j Alias with no arguments lists all aliases. Alias with one argument . ; | displays the alias for that name. The list appears in a form suitable for _— saving (with transcript) and restoring (with load). : 7 If an alias contains itself, or contains an alias which contains the first, an : q infinite loop can result. To prevent this, the debugger will only expand [ q 256 aliases in one line; more than that, and it assumes an infinite loop has q 4 occurred and reports the fact. The debugger might also run out of memory = for keeping track of aliases before this happens. 1 ; See the chapter PROCEDURES, IF, GOTO, DEFER, AND ALIAS for mm more information. " q unalias name... : ’ The unalias command deletes all the names from the alias list. You can ] replace an alias simply by redefining it: you don't need to remove it first. 4 | noalias { 1 The noalias command deletes all aliases. It asks for verification before 4 ‘ doing so. = FILES AND SCRIPTS : : These commands have to do with data files and script files. q read[ file [ address[]][]] ’ 3 The read and write commands are used to transfer data from disk to the q j client’s memory and back. The “disk” in question is always the one local to 7 the head: this is not the same as the stub’s disk in a remote-debugging a system. E q Read with no arguments displays the starting address and size of the last - 3 file read. With two arguments, it reads the named file into the client's 4 7 memory starting at the given address. 

| 

2 | | 

**==> picture [5 x 194] intentionally omitted <==**

**----- Start of picture text -----**<br>
;<br>|<br>:<br>**----- End of picture text -----**<br>


7 

4-23 

i q q a : | : | i i I 

the directory given directory given given by the environment variable DBPATH. the environment variable DBPATH. environment variable DBPATH. variable DBPATH. DBPATH. : , When not remote-debugging, not remote-debugging, remote-debugging, a third form is allowed: third form is allowed: form is allowed: is allowed: allowed: with a file argument argument ] but no address, no address, address, read will use the operating-system use the operating-system the operating-system operating-system call Malloe to allocate Malloe to allocate to allocate allocate enough memory for the named memory for the named for the named the named named file, then read then read it into that memory. into that memory. that memory. memory. This is : useful for patchinga for patchinga patchingaa file, because you don’t care where it gets loaded because you don’t care where it gets loaded you don’t care where it gets loaded don’t care where it gets loaded care where it gets loaded where it gets loaded it gets loaded gets loaded loaded in. | Note that there must be enough memory available that there must be enough memory available there must be enough memory available must be enough memory available be enough memory available enough memory available memory available available to the operating system the operating system operating system system for the the file, or the Malloc will or the Malloc will the Malloc will Malloc will will fail. This is especially a problem especially a problem a problem problem if you €xec a program but don’t let a program but don’t let program but don’t let but don’t let don’t let let it return memory to the OS: return memory to the OS: memory to the OS: to the OS: the OS: OS: it is likely to to have all of memory allocated memory allocated allocated to it. file [ range range ] The write command command is the companion the companion companion to read. read. With both afile and range both afile and range afile and rangefile and range and range range argument, it writes writes the memory in that range to the memory in that range to the in that range to the that range to the range to the to the the file. With only a only a a file : argument, it uses uses the start and and size information from the the last read read command. If the file already exists, the file already exists, file already exists, already exists, exists, the user is asked to verify that he asked to verify that he to verify that he that he he 4 wants to overwrite overwrite it. : If thefile cannot be found in the current directory Db will search thefile cannot be found in the current directory Db will searchfile cannot be found in the current directory Db will search cannot be found in the current directory Db will search found in the current directory Db will search in the current directory Db will search the current directory Db will search directory Db will search Db will search will search search for it in it in in { the directory given by the environment variable DBPATH. directory given by the environment variable DBPATH. given by the environment variable DBPATH. by the environment variable DBPATH. the environment variable DBPATH. environment variable DBPATH. variable DBPATH. DBPATH. ] The load command causes debugger commands load command causes debugger commands command causes debugger commands causes debugger commands debugger commands commands to be read from be read from read from from a file ; Tather than from the keyboard. than from the keyboard. from the keyboard. the keyboard. keyboard. The file must contain normal ASCII file must contain normal ASCII must contain normal ASCII contain normal ASCII normal ASCII ASCII text, ; with lines separated with CR/LF. lines separated with CR/LF. separated with CR/LF. with CR/LF. CR/LF. Each line is read in and interpreted read in and interpreted in and interpreted and interpreted interpreted 7 exactly as as if it was typed at the debugger’s colon it was typed at the debugger’s colon was typed at the debugger’s colon typed at the debugger’s colon at the debugger’s colon the debugger’s colon debugger’s colon (":") prompt. prompt. Other input, input, 1 such as verification, as verification, verification, still comes from the keyboard. comes from the keyboard. from the keyboard. the keyboard. keyboard. q If the file is not found in the current directory, Db will check for it in the directory named by the environment variable DBPATH. ‘ These files are called scripts. By convention, script files (except for the 4 startup files db.re and rdb.re) have the extension ".DB," as in q "SETUP.DB." j A script can contain the load command itself. In this respect, load can be j used as something of a subroutine call. No check is made for infinite loops. 4-24 ] 

: 

If thefile cannot be found in the current directory, Db will search for it in the directory given directory given given by the environment variable DBPATH. the environment variable DBPATH. environment variable DBPATH. variable DBPATH. DBPATH. 

, When not remote-debugging, not remote-debugging, remote-debugging, a third form is allowed: third form is allowed: form is allowed: is allowed: allowed: with a file argument argument but no address, no address, address, read will use the operating-system use the operating-system the operating-system operating-system call Malloe to allocate Malloe to allocate to allocate allocate enough memory for the named memory for the named for the named the named named file, then read then read it into that memory. into that memory. that memory. memory. This is useful for patchinga for patchinga patchingaa file, because you don’t care where it gets loaded because you don’t care where it gets loaded you don’t care where it gets loaded don’t care where it gets loaded care where it gets loaded where it gets loaded it gets loaded gets loaded loaded in. | Note that there must be enough memory available that there must be enough memory available there must be enough memory available must be enough memory available be enough memory available enough memory available memory available available to the operating system the operating system operating system system for the the file, or the Malloc will or the Malloc will the Malloc will Malloc will will fail. This is especially a problem especially a problem a problem problem if you €xec a program but don’t let a program but don’t let program but don’t let but don’t let don’t let let it return memory to the OS: return memory to the OS: memory to the OS: to the OS: the OS: OS: it is likely to to have all of memory allocated memory allocated allocated to it. write file [ range range ] 

The write command command is the companion the companion companion to read. read. With both afile and range both afile and range afile and rangefile and range and range range argument, it writes writes the memory in that range to the memory in that range to the in that range to the that range to the range to the to the the file. With only a only a a file argument, it uses uses the start and and size information from the the last read read command. If the file already exists, the file already exists, file already exists, already exists, exists, the user is asked to verify that he asked to verify that he to verify that he that he he wants to overwrite overwrite it. If thefile cannot be found in the current directory Db will search thefile cannot be found in the current directory Db will searchfile cannot be found in the current directory Db will search cannot be found in the current directory Db will search found in the current directory Db will search in the current directory Db will search the current directory Db will search directory Db will search Db will search will search search for it in it in in the directory given by the environment variable DBPATH. directory given by the environment variable DBPATH. given by the environment variable DBPATH. by the environment variable DBPATH. the environment variable DBPATH. environment variable DBPATH. variable DBPATH. DBPATH. load file The load command causes debugger commands load command causes debugger commands command causes debugger commands causes debugger commands debugger commands commands to be read from be read from read from from a file _ Tather than from the keyboard. than from the keyboard. from the keyboard. the keyboard. keyboard. The file must contain normal ASCII file must contain normal ASCII must contain normal ASCII contain normal ASCII normal ASCII ASCII text, with lines separated with CR/LF. lines separated with CR/LF. separated with CR/LF. with CR/LF. CR/LF. Each line is read in and interpreted read in and interpreted in and interpreted and interpreted interpreted exactly as as if it was typed at the debugger’s colon it was typed at the debugger’s colon was typed at the debugger’s colon typed at the debugger’s colon at the debugger’s colon the debugger’s colon debugger’s colon (":") prompt. prompt. Other input, input, such as verification, as verification, verification, still comes from the keyboard. comes from the keyboard. from the keyboard. the keyboard. keyboard. 

: , 4 : . | 4 | : ; : ] q j q = ; 4 i q ~ . ; 4 , , 4 FO q 3 ‘ 4 

| | | | | | | 

Some commands are only meaningful when used in a script; they are bgoto, fgoto, unload and reload. 

In a script file, long commands can be split onto several lines. Whena line in a script ends with a backslash ( \’), the next line is tacked onto it as though it was a continuation of the same line. This is not the case for lines read from the keyboard. 

unload Unload causes the script currently being loaded (with the load command) to end. If you think of load as a subroutine call, this can be used as a premature "return" statement. This amounts to an fgoto command to the end ofthe script, but is faster. 

It is an error to use this command when not loading a script. reload 

Reload causes the script currently being loaded to be rewound to the beginning. It amounts to a bgoto to the start of the file, but is faster. It is an error to use this command when not loading a script. bgoto label fgoto label The bgoto and fgoto commands change the flow of control in scripts. The label argument is the exact text of the line you wish to go to, and may only be one word. Usually, this is a comment, like "#begin" or "#loop." 

**==> picture [3 x 22] intentionally omitted <==**

**----- Start of picture text -----**<br>
j<br>**----- End of picture text -----**<br>


**==> picture [5 x 22] intentionally omitted <==**

**----- Start of picture text -----**<br>
q<br>**----- End of picture text -----**<br>


4-25 

. : q ' 

q 

| 

, : | 

| i | ' ; 1 

1 

For example, consider the following text file: | echo line 1 ] xtO 0 ; #begin : printif (*tO-n < *to10) bgoto #begin . echo echo end of loop Loading this file will cause the following output: j line 1 4 0123456789 ABCDEFE q end of loop 3 The fgoto command has the limitation that the line containing its label ; argument must be after the current position in the script. The bgoto ] command rewinds the file, then compares each line against the label q argument, while fgoto does not rewind the file first. If the label is after 4 the current point in the file, fgoto is faster, especially in large scripts. 4 It is an error to use these commands outside of a script. q As a rule, script files are best used for setup scripts and loading procedures. ’ Use aliases for little things you plan to do more than once, and procedures 7 for complex things with looping and such. Aliases and procedures are kept 4 in memory, not in disk, and in procedures, the labels are indexed so a 4 goto executes much faster. The fgoto and bgoto commands are really 4 leftovers from the days when the debugger didn’t have procedures. 3 MISCELLANEOUS COMMANDS j bind[ string [ code } ] q The bind command allows you to bind a string to a key. After that, when q you use that key, the string will be used as if it had been typed from the 4 keyboard. code is the ASCII code of the key to bind to: codes 0 through 31 are allowed (the control keys), except for 13, which is carriage-return. 4 | (Rebinding carriage-return would be disastrous!) 4 With no arguments, bind lists the current key bindings. The list appears . 4 in a form suitable for saving (with transcript) and restoring (with load). 3 

**==> picture [7 x 26] intentionally omitted <==**

**----- Start of picture text -----**<br>
q<br>**----- End of picture text -----**<br>


| 

4-26 

4 4 4 

. = j 1 4 & 

| | 

| | | | | | | 

j ’ ; 1 ; 1 4 ; q ~ a: ; Py 

J q ; : 4 ‘ 1 j a 1 , 4 q 1 4 q 4 q : ; 4 q - ~ 

- | 

With one argument, bind prompts you to hit the key to which you want the string bound. This is useful if you don’t know the key’s code offhand. You should use the actual keystroke here: hold down "Control" and press _the key in question. : 

**==> picture [3 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


Examples: bind list bindings. bind "1 ‘pef{1]\r" 1 bind the string to “A. bind "dw.xlist{10]\r" prompt for a key; bind to that key. 

| 

## abort [ args... .] 

The abort command prints out its arguments (usually an error message of some sort) in exactly the same way as the print command, and then it returns to the command prompt. Any script which was loading, procedure which was executing, alias which was executing, or deferred commands which were pending are forgotten: the debugger is reset to the very top level, and waits for user input. i 

The # command introduces a comment. The rest of the line is ignored. The # character isn’t properly a command at all: it is processed by the command-line reader. When it appears in the position where a command is expected, the rest of the line it’s is thrown out. 

## transcript [ {file[a] | off | flush 

| [ printer} ] 

Transcript starts a transcript of all the output from the debugger, and all the input from the user. The file argument tells what file to keep the transcript in. When you leave the debugger for any reason (short of resetting the head machine) the transcript file is saved and closed. The command "transcript off" stops the transcript explicitly, and saves and closes the file. 

Transcript printer causes debugger output to go to the printer as well as the screen. Note that output is buffered in a transcript buffer, so the printer will always be slightly behind what is on the screen. (The buffer 

**==> picture [1 x 18] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


4-27 

4 1 { ; : 4 4 q ] q : j ] i 4 j : 4 q ] ‘ q : q j q 4 j 4 : 4 

7 

j 

; : 

ic 

| | j 

, ' i i 

size might be as much as 4K.) BIOS calls are used to send the transcript data to BIOS device 0. 

Transcript flush flushes the buffered transcript information explicitly. . This is especially useful when transcripting to the printer, because ~ otherwise recent information will not have been printed yet. 

Transcript alone tells the state of transcripting (on or off). 

The a option to the transcript file form means "append" and causes the transcript to be appended to the transcript file; otherwise, any existing file with that name is removed (without warning). 

The transcript command must be used carefully unless you are remote debugging. On a single-machine system, you should be careful not to stop the client while it is processing a GEMDOS call. When the transcript buffer fills up, it needs to be flushed to disk, and this is done with GEMDOS calls. If the client is in the middle of a GEMDOS call, this will crash your system. 

Note that the only ways to stop the client while it is in GEMDOS are to use the u (untrace) command when at a "trap #$1" instruction, use the stop button, or cause a bus error or other exception in GEMDOS. If you avoid these conditions, transcripting should be safe even when debugging locally. See the chapter OPERATING SYSTEM CONSIDERATIONS for more information. 

## gag[{on | off}] 

The gag command causes output to be suppressed. With no arguments, or with the on argument, output is suppressed until the next time the debugger needs to wait for user input. With the off argument, the suppression stops, and output resumes. You might use the gag command in conjunction with transcript, so the information goes to the transcript ; file without also being printed on the screen: : transcript disasm ; gag on; 1.main[{2000] ; gag off ; transcript off 

The above commands disassemble eight kilobytes of code and place the disassembly in a file called "disasm." The disassembly is not displayed on the screen. Without the gag command, the text would scroll by on the screen, taking a much longer time. 

4-28 

q f 

= _ ‘ } 4 1 } ; | | fo 4 

{ 

q 

! 

4 

## exit 

The exit command is used to terminate the stub and leave the debugger. Whether remote debugging or not, this command causes all machines _involved to return to a quiet state. In a single-machine model, the — debugger will remove itself and the stub and return to the desktop or shell. If you are remote debugging, the head will tell the stub to remove itself, then remove the communications layer from the slave, and finally remove the communications layer from the master. Again, both machines should return to the desktop or shell. 

**==> picture [4 x 15] intentionally omitted <==**

**----- Start of picture text -----**<br>
’<br>**----- End of picture text -----**<br>


4-29 

4 quit 

j 

: ‘ 7 4 : F 

j i q q ‘ ; i 4 4 ’ 2 4 4 3 4 3 ; 4 q 4 7 4 q 4 4 3 a 

q = : r | 4 | a : ; . 

The quit command exits the debugger. If you are remote debugging, it does not cause the slave to terminate or exit or even to stop. It can be used after a continue command, or after stopping a wait condition with *C, to let the client run while you do something else on the master machine. When not remote debugging, this command is identical to exit. 

help[ topic ] 

The help command alone lists the debugger commands, the operators for complex expressions, and the built-in variables, with a brief reminder of what they do. Withonly a topic, the command gives a little help on that topic. Currently the topics available are command names and build-in debugger variable names. 

## echo [-n] [-i] [- ] args... 

The echo command writes the args to the debugger output device (usually the screen). The args are written on one line, each separated bya single space. The -n switch will suppress the newline at the end of the output; this can be used to concatenate the output of multiple echo or print commands. The -i switch causes the output to be in inverse video, like error messages from the debugger. The - switch Gust a dash with no letter after it) is used when the args start with a dash: it means "don’t try to interpret the next argument as a switch." 

Examples: echo echo nothing plus a newline to the output device. echo -i Error echo the word "Error" in inverse video. echo -n Error echo the word "Error" with no newline after it. echo -i-n Error echo “Error” in inverse with no newline. echo - -fooecho the word -foo-. Note that "echo -foo-" wouldn’t work, because echo would try to interpret "-f" as a switch. 

| : 4 - Ff , fo ] 4 f 4 Pd = q 4 : ’ rf 4 4= 4 : : 1 . | q q | 4 ‘ : 1 4 ' 1 4 ] q ’ 3 ; q j ' J ! : q _ 

: : 7 | | | | | | 

## print args ... 

**==> picture [364 x 245] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||
|---|---|---|
|The print command prints (most of)|its arguments to the screen.|
|Arguments which begin with a dash are switches, and modify what gets|
|printed rather than appearing in the outpnt.|Without any conversion|
|switches the arguments are printed verbatim, so the "echo" command is|
|now just an alias for print.|
|Normally, output is printed in "regular" (not inverse) video, and after each|
|argument a single space appears in the output, and a newline is output|
|after the last argument.|The following "modifier" switches change this|
|default behavior:|
|MODIFIER|||MEANING|
|-n|don’t output a newline at the end ofthe line.|
|-i|enter inverse video until -r or end of line.|
|-r|regular video: cancel inverse video.|
|-t|do not output a space between arguments.|
|-T|do output a space between arguments.|

**----- End of picture text -----**<br>


The -t switch inhibits spaces starting with the one after the NEXT argument; see the examples. Other switches, called conversions, indicate that the next argument is to be interpreted as an expression, and tell how to output the result. 

**==> picture [231 x 112] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||
|---|---|---|
|CONVERSION OUTPUT RESULT AS...|
|-X|hex|
|-d|decimal|
|-O|octal|
|-b|binary|
|“Cc|a character (low 8 bits of|result)|
|-S|string (see below)|

**----- End of picture text -----**<br>


After the conversion character you can specify a field width. The output will bepadded with leading spaces to that width. If the first character of the field width is a zero, the output will be padded with leading zeros to that width. 

The -s conversion means "string:” the result of evaluating the next argument is taken to be an address in client memory, anda string is read from there up to a null byte or the maximum field width. If the field width starts with a zero, the string is padded with trailing spaces to that width. 

4-31 

: 

‘ } * Po , 

, 

: j : 4 4 d 4 q | ] q 4 ; 4 ‘ ‘ | 4 | q 

; ' | : j : ' | a 

s 

: | : ’ : ' ‘ |. 

The field width and zero-fill flags are ignored for the -c conversion. If you actually want to print something that starts with a dash or contains multiple spaces or unbalanced parentheses, use a String (q-V.). 

, PRINT COMMAND EXAMPLES 

command: _ print two plus two is -d 2+2 and 4 + 4 is -d (4 + 4) output: two plus two is 4 and 4 + 4 is 8 

(As elsewhere in the debugger, if an expression contains spaces you must wrap it in parentheses.) 

command: print sixteen hex is -t $ -x 10. output: sixteen hex is $10. 

(The -t modifier prevents the spaces between arguments. It inhibits spaces starting with the one that would come after the NEXT argument, so in this case there IS a space between "is" and "$") 

command: print -t "funcall(" -x8 lpeek(sp+4) , -x08 Ipeek(sp+8) ")" output: funcall( 12D342,00285F20) 

(more fun with -t, field width, and zero-filled field width: there are no "automatic" spaces between args because of -t; the first arg is space-padded to 8 chars, and the second is zero-padded to 8 chars. Also, the presence of unbalanced parentheses means they have to be in strings.) 

command: print test"(a ’ [b" test output: test(a’ [btest (If you want a leading dash, multiple spaces or unbalanced parentheses or quotes, you need to quote the argument.) 

/ command: print value: -d(wpeek(.width)) .value str: -sO@32 .string ! output: value: 321 str: abcde ! 

(You can compute the field width; it can be any expression. If you also want to specify "zero-filled" you put the zero before the expression. "Zero-filled" for strings really means "padded with trailing spaces.") 

‘ 

4-32 

. 

; | j q | : a P| 4 ] q j , 4 ] . rp 4 og . Ro , q ' rf | 4 } _ | ; : : ‘ j { 1 q ; - 4 F; 4 

: 

1 . | | | | | | | | | | | 

## if predicate command 

The if command works as you might expect: if the predicate evaluates to TRUE (nonzero), the command is executed. If the predicate is FALSE _ (zero), the command is not executed. 

The command part of an if command can be several commands, in the same way that an alias can be several commands: if the command argument is enclosed in quotes (single or double), it may contain several commands separated by semicolons. 

## Examples: 

**==> picture [340 x 61] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||
|---|---|---|---|---|---|
|if (\dO|=|0)|echo dO|is zero.|Simple condition.|
|if (tO <|10)|goto begin|Part of a loop|in a procedure|
|if ((wpeek|‘sp)|=|1) \|
|"print (lpeek|(‘sp|+|2));defer g"|See below.|

**----- End of picture text -----**<br>


The last example above might be an auto-execute alias for a breakpoint: if the word at the top of the stack is 1 when the breakpoint is hit, the longword on the stack after that is printed and the client is allowed to start up again. Note the compound command, with the semicolon protected by quotes, and the use of defer to start the client the next time the debugger would normally display the prompt. 

See the chapter PROCEDURES, IF GOTO, DEFER, AND ALIAS for more information. 

## indirect addr 

The indirect command causes the client memory starting at addr to be read into a local buffer and executed as if it was typed at the command prompt. The command ends with the first zero byte. 

## EXAMPLE: 

| 

## :s .buf "echo hello\x00" ; indirect .buf 

This example sets the string "echo hello” (plus a zero byte) into the client memory, then executes the command at that address. Obviously, it prints the word "hello" on the screen. 

**==> picture [66 x 18] intentionally omitted <==**

**----- Start of picture text -----**<br>
ia<br>**----- End of picture text -----**<br>


4-33 

![ command-name { args... ] ] 

: d : q i: | i | i a | 1 

.command. The first word word of the argument the argument argument should be the the full filename ‘ (including the drive and path) drive and path) and path) path) of a GEMDOS GEMDOS program file (usually of type of type type q -PRG, .APP, .TOS, or .TTP). .TTP). When the program finishes, you will be the program finishes, you will be program finishes, you will be finishes, you will be you will be will be be : **re** potutu **r** t thened toexit the code debuggerof the rightprogram. where youned toexit the code debuggerof the rightprogram. where you toexit the code debuggerof the rightprogram. where youexit the code debuggerof the rightprogram. where you the code debuggerof the rightprogram. where you code debuggerof the rightprogram. where you debuggerof the rightprogram. where youof the rightprogram. where you the rightprogram. where you rightprogram. where youprogram. where you where you left off, and the debugger will and the debugger will the debugger will debugger will will q With no arguments, the shell command attempts to create a shell by q executing the file whose name is the value of the environment variable SHELL. Under some shells, the command-name need not be a full-blown pathname. { See the section THE SHELL COMMAND IN DETAIL in the chapter { OPERATING SYSTEM CONSIDERATIONS for more information. _ j The dir command showsa dir command showsa command showsa showsaa directory listing. listing. With no pathname pathname argument, 4 it lists lists all files in the the current directory. With a pathname argument, pathname argument, argument, it lists 4 expression,files in the directoryor mayfiles in the directoryor may in the directoryor may the directoryor may directoryor mayor may may consist specified of a bypathpathname.followedPathname by a wild-card may beexpression a wildcard specified of a bypathpathname.followedPathname by a wild-card may beexpression a wildcard of a bypathpathname.followedPathname by a wild-card may beexpression a wildcard bypathpathname.followedPathname by a wild-card may beexpression a wildcardpathpathname.followedPathname by a wild-card may beexpression a wildcardpathname.followedPathname by a wild-card may beexpression a wildcardfollowedPathname by a wild-card may beexpression a wildcardPathname by a wild-card may beexpression a wildcard by a wild-card may beexpression a wildcard a wild-card may beexpression a wildcard may beexpression a wildcard beexpression a wildcardexpression a wildcard a wildcard wildcard 4: (e.g. "*.*" or "sre\db??.c’). or "sre\db??.c’). "sre\db??.c’). 4 Be careful careful of ending dir commands with ending dir commands with commands with with a backslash backslash ("\") in scripts: scripts: the { trailing backslash backslash will be taken as a be taken as a taken as a as a a continuation character, and the the next q line will be tacked onto the current one. will be tacked onto the current one. be tacked onto the current one. tacked onto the current one. onto the current one. the current one. current one. Using, for instance, "A:\*.*" q rather than than "A:\" has the same same effect and avoids and avoids avoids the problem problem entirely. q Examples: 4 dir list all files in current directory 1 dir A:\*.* list all files in the root of drive A 4 dir src\*.c list all files in the subdirectory sre with a extension ".c" (C program source files) 4 

. 7 . | . 

The ! (shell) command attempts to execute its argument as a GEMDOS .command. The first word word of the argument the argument argument should be the the full filename (including the drive and path) drive and path) and path) path) of a GEMDOS GEMDOS program file (usually of type of type type -PRG, .APP, .TOS, or .TTP). .TTP). When the program finishes, you will be the program finishes, you will be program finishes, you will be finishes, you will be you will be will be be **re** potutu **r** t thened toexit the code debuggerof the rightprogram. where youned toexit the code debuggerof the rightprogram. where you toexit the code debuggerof the rightprogram. where youexit the code debuggerof the rightprogram. where you the code debuggerof the rightprogram. where you code debuggerof the rightprogram. where you debuggerof the rightprogram. where youof the rightprogram. where you the rightprogram. where you rightprogram. where youprogram. where you where you left off, and the debugger will and the debugger will the debugger will debugger will will 

dir [ pathname] _ The dir command showsa dir command showsa command showsa showsaa directory listing. listing. With no pathname pathname argument, it lists lists all files in the the current directory. With a pathname argument, pathname argument, argument, it lists expression,files in the directoryor mayfiles in the directoryor may in the directoryor may the directoryor may directoryor mayor may may consist specified of a bypathpathname.followedPathname by a wild-card may beexpression a wildcard specified of a bypathpathname.followedPathname by a wild-card may beexpression a wildcard of a bypathpathname.followedPathname by a wild-card may beexpression a wildcard bypathpathname.followedPathname by a wild-card may beexpression a wildcardpathpathname.followedPathname by a wild-card may beexpression a wildcardpathname.followedPathname by a wild-card may beexpression a wildcardfollowedPathname by a wild-card may beexpression a wildcardPathname by a wild-card may beexpression a wildcard by a wild-card may beexpression a wildcard a wild-card may beexpression a wildcard may beexpression a wildcard beexpression a wildcardexpression a wildcard a wildcard wildcard (e.g. "*.*" or "sre\db??.c’). or "sre\db??.c’). "sre\db??.c’). Be careful careful of ending dir commands with ending dir commands with commands with with a backslash backslash ("\") in scripts: scripts: the trailing backslash backslash will be taken as a be taken as a taken as a as a a continuation character, and the the next line will be tacked onto the current one. will be tacked onto the current one. be tacked onto the current one. tacked onto the current one. onto the current one. the current one. current one. Using, for instance, "A:\*.*" rather than than "A:\" has the same same effect and avoids and avoids avoids the problem problem entirely. 

**==> picture [4 x 23] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


q 

4-34 

g 7 q 

@ 

q The client’s memory is accessed by the debugger in chunks of anywhere from one q j byte up to one kilobyte. Asa rule, when the head wants to examine the client's ' q memory, it asks the stub to copy some into a buffer and send it over. Such copying BB is done as bytes, to avoid address errors. q However, if the head asks for exactly two or four bytes at an even address, the F move.w or move.] instruction will be used. This means that word-addressed 1/0 _ registers will behave as expected. j The following commands show some times when this happens, assuming the addrs _ are even): . Command Comments dwaddr [1] Dumps exactly one word. | § dwaddr [2] Also dumps one word (the number in _ brackets is always the number of bytes in ‘ question, not the number of "things”). ' 4 sladdr Begins interactively setting longwords. (By , reading and writing them as longs.) ; 4 (wpeek addr ) Both wpeek and Ipeek act this way. ; maddr.w != { addr 2 } The operands of a comparison memory ff check are read as words or longs, as . 4 appropriate. ] | The f (find) command always treats the thing you are looking for as a stream of 4 bytes, so words and longs don’t have meaning. The indirect command, the 9 3 special message type FOxx, and the -s form of print also read client memory in 4 ’ chunks, not as words or longs. 

. | | 

| | | | | | 

x j q 

, | 

## CHAPTER 5 THE CLIENT, BREAKPOINTS AND CHECKPOINTS: DETAIL 

This chapter goes into more detail concerning the client, breakpoints, and checkpoints. 

## THE CLIENT’S MEMORY 

|The client’s memory is accessed by the debugger in chunks of anywhere from oneclient’s memory is accessed by the debugger in chunks of anywhere from onememory is accessed by the debugger in chunks of anywhere from oneis accessed by the debugger in chunks of anywhere from oneaccessed by the debugger in chunks of anywhere from oneby the debugger in chunks of anywhere from onethe debugger in chunks of anywhere from onedebugger in chunks of anywhere from onein chunks of anywhere from onechunks of anywhere from oneof anywhere from oneanywhere from onefrom oneone<br>byte up to one kilobyte.up to one kilobyte.to one kilobyte.one kilobyte.kilobyte. Asa<br>rule, when the head wants to examine the client'swhen the head wants to examine the client'sthe head wants to examine the client'shead wants to examine the client'swants to examine the client'sto examine the client'sexamine the client'sthe client'sclient's<br>memory, it asks the stub to copy some into a buffer and sendasks the stub to copy some into a buffer and sendthe stub to copy some into a buffer and sendstub to copy some into a buffer and sendto copy some into a buffer and sendcopy some into a buffer and sendsome into a buffer and sendinto a buffer and senda buffer and sendbuffer and sendand sendsend it over.over. Such copyingcopying<br>is done as bytes,done as bytes,as bytes,bytes, to avoid address errors.<br>However, ifthe head asks for exactly two or four bytes at an even address, thehead asks for exactly two or four bytes at an even address, theasks for exactly two or four bytes at an even address, thefor exactly two or four bytes at an even address, theexactly two or four bytes at an even address, thetwo or four bytes at an even address, theor four bytes at an even address, thefour bytes at an even address, theat an even address, thean even address, theeven address, theaddress, thethe<br>move.w or move.]or move.]move.] instruction will be used.will be used.be used.used. This means that word-addressedmeans that word-addressedthat word-addressedword-addressed 1/0<br>registers will behavebehave as expected.|The client’s memory is accessed by the debugger in chunks of anywhere from oneclient’s memory is accessed by the debugger in chunks of anywhere from onememory is accessed by the debugger in chunks of anywhere from oneis accessed by the debugger in chunks of anywhere from oneaccessed by the debugger in chunks of anywhere from oneby the debugger in chunks of anywhere from onethe debugger in chunks of anywhere from onedebugger in chunks of anywhere from onein chunks of anywhere from onechunks of anywhere from oneof anywhere from oneanywhere from onefrom oneone<br>byte up to one kilobyte.up to one kilobyte.to one kilobyte.one kilobyte.kilobyte. Asa<br>rule, when the head wants to examine the client'swhen the head wants to examine the client'sthe head wants to examine the client'shead wants to examine the client'swants to examine the client'sto examine the client'sexamine the client'sthe client'sclient's<br>memory, it asks the stub to copy some into a buffer and sendasks the stub to copy some into a buffer and sendthe stub to copy some into a buffer and sendstub to copy some into a buffer and sendto copy some into a buffer and sendcopy some into a buffer and sendsome into a buffer and sendinto a buffer and senda buffer and sendbuffer and sendand sendsend it over.over. Such copyingcopying<br>is done as bytes,done as bytes,as bytes,bytes, to avoid address errors.<br>However, ifthe head asks for exactly two or four bytes at an even address, thehead asks for exactly two or four bytes at an even address, theasks for exactly two or four bytes at an even address, thefor exactly two or four bytes at an even address, theexactly two or four bytes at an even address, thetwo or four bytes at an even address, theor four bytes at an even address, thefour bytes at an even address, theat an even address, thean even address, theeven address, theaddress, thethe<br>move.w or move.]or move.]move.] instruction will be used.will be used.be used.used. This means that word-addressedmeans that word-addressedthat word-addressedword-addressed 1/0<br>registers will behavebehave as expected.|
|---|---|
|The following commands show some times when this happens,following commands show some times when this happens,commands show some times when this happens,show some times when this happens,some times when this happens,times when this happens,when this happens,this happens,happens, assuming the addrsthe addrs||
|are even):||
|Command<br>dwaddr [1]<br>dwaddr [2]<br>sladdr<br>(wpeek addr )addr ))<br>maddr.w !=!= { addr 2addr 22 }|Comments<br>Dumps exactly one word.one word.word.<br>Also dumps one word (the number indumps one word (the number inone word (the number inword (the number in(the number innumber inin<br>brackets is always the number of bytes inalways the number of bytes inthe number of bytes innumber of bytes inof bytes inbytes inin<br>question, not the number of "things”).not the number of "things”).the number of "things”).number of "things”).of "things”)."things”).<br>Begins interactively setting longwords.longwords. (By<br>reading and writing them as longs.)and writing them as longs.)writing them as longs.)them as longs.)as longs.)longs.)<br>Both wpeek and Ipeek act this way.wpeek and Ipeek act this way.and Ipeek act this way.Ipeek act this way.act this way.this way.way.<br>The operandsoperands of a comparison memorya comparison memorycomparison memorymemory<br>check are read as words or longs,are read as words or longs,read as words or longs,as words or longs,words or longs,or longs,longs, as<br>appropriate.|



Trace and untrace are really two modes ofthe same command. They both single-step through the client. The difference is that trace mode treats instructions which cause traps specially, while untrace mode does not. 

: . 

5-1 

> ; TRAPccline-Fline-F ($Fxxx).instructionHowever,isn’t,instructionHowever,isn’t,However,isn’t,isn’t, either. on a 68020 or 68030, line-F is not treated specially. on a 68020 or 68030, line-F is not treated specially. a 68020 or 68030, line-F is not treated specially. 68020 or 68030, line-F is not treated specially. or 68030, line-F is not treated specially. 68030, line-F is not treated specially. line-F is not treated specially. is not treated specially. not treated specially. treated specially. specially. § If the PC is at one of the special trap instructions and you use the t command, the ; result will be that the trap instruction (and therefore the trap handler) will be executed at full speed. When you next see the prompt, the PC will be at the instruction after the trap. : If you use the u command in the same situation, only the trap instruction itself : will be executed, not the whole handler. When you see the prompt, the PC will be 1 at the first instruction of the trap handler, and the supervisor stack will hold the i trap exception frame. ’ Trace mode treats the trap instructions specially so you don’t have to worry about ’ stopping the client in the middle of the operating system, and so the OS will : execute at full speed. This way you can set memory checkpoints and then say tx j to trace through your program forever, with an opportunity between each instruction, but without slowing down OS calls and without the possibility that you a will stop in the middle of the OS itself (which is deadly when not remote 7 debugging). Untrace mode is provided so you can debug a trap handler itself. : The v verbose-trace command without the u modifier is like trace: it executes a trap handler as though it were one instruction. MESSAGES A message is a special type of communication from the client to the head. Messages don’t come from the stub; they come from the client itself, or from another part of the debugger. For instance, when you use the exec command, a 4 message is sent telling the head the basepage address of the program that was 7 loaded. If the load fails, or the client later terminates, another message is sent to inform the head (and hence the user) of this, too. 1 A program being debugged can send messages, too. Messages consist of a 16-bit | message number and a 32-bit message argument vector. The negative message ] numbers are reserved for use by the debugger, but a client may use the positive 4 message numbers freely. A client sends a message to the head as follows (in C): ] xbios(11 o,msg_number,msg argv); Msg_number is a 16-bit integer and msg_argv is 32 bits (e.g. a pointer or a long 

Instructions which are treated specially are: TRAP, TRAPV, line-A ($Axxx), and TRAPccline-Fline-F ($Fxxx).instructionHowever,isn’t,instructionHowever,isn’t,However,isn’t,isn’t, either. on a 68020 or 68030, line-F is not treated specially. on a 68020 or 68030, line-F is not treated specially. a 68020 or 68030, line-F is not treated specially. 68020 or 68030, line-F is not treated specially. or 68030, line-F is not treated specially. 68030, line-F is not treated specially. line-F is not treated specially. is not treated specially. not treated specially. treated specially. specially. The 

1 : 4 1 ] 4 q 7 ’ j q 4 F 4 “4 j ] j 7 q q 4 : 4 q q 4 4 7 q 

wy m 

j : { = a ff a 

integer). 

1 , 1 j ; rf 4 4 ‘ j _ ; | “a lw | j : = { j ; | | 4 | 4 q q ] | : 4 4 q ' 4 ’ : 4 ~ 4 a ; : 

Remember, negative message numbers are reserved for the debugger’s use. When a message is received by the head with a positive message number, the message number and argument vector are displayed, and the client is stopped. See the section AUTO-EXECUTE ALIASES in the chapter PROCEDURES, IF, GOTO, DEFER, AND ALIAS for more on what happens when messages arrive. 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


Note that messages provide an opportunity as well as a stop when they happen during a trace/go. 

Message types in the range $FO00 to $FOFF are special: they are commands from the client to print something on the user’s screen. The message argument vector holds the starting address of the (ASCII) test to display, and the lower byte of the message number holds the length of the text. If the lower byte is zero (that is, message number $F000), the debugger prints the text up to the first null byte. This means that you can print some text on the debugger’s output (and cause an opportunity anda stop) with the following line (in C): ; 

xbios(11,5,0xf000,"This is my message"); 

Some C macros such as the following would be useful: 

**==> picture [1 x 22] intentionally omitted <==**

**----- Start of picture text -----**<br>
.<br>**----- End of picture text -----**<br>


#define DBMSG(msgnum,msgargv) xbios(11,5,msgnum,msgargv) #define DBTEXT(s) DBMSG(0xf000,s) 

Debugger messages can be used from any language which gives access to the Atari ST’s XBIOS. Note that the stub itself masquerades as XBIOS function code 11 (decimal); do not use this call for anything but sending messages. 

**==> picture [1 x 10] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


## BREAKPOINTS IN DETAIL 

Breakpoints work internally as follows: When a trace/go is started, the instruction at each breakpoint address is saved, and the illegal instruction is placed at those addresses. Then the client is started. If the processor comes across an illegal instruction, it generates an exception, which the stub catches. It checks to see if the address of the illegal instruction matches any of the breakpoints that were set. If so, the count value ofthe breakpoint is decremented (but not through zero). If the result is zero, the trace/go stops and all the instructions with breakpoints are restored to their original values. Otherwise, the trace/go continues, starting with the instruction which was "under" the breakpoint (i.e. the one replaced by the illegal instruction). 

**==> picture [33 x 21] intentionally omitted <==**

**----- Start of picture text -----**<br>
Zz<br>**----- End of picture text -----**<br>


5-3 

i » : a A 2 : : } ; 4 : q ; j q F 4 4 | 1 4 j 4 j q 7 4 I q 4 7 . 

{ ' | 

7 

| MEMORY CHECKPOINTS IN DETAIL Checkpoints have two phases: the initialization phase and the evaluation phase. ’ The initialization phase occurs when the head tells the stub to begin a trace or go. : The evaluation occurs during opportunities such as between instructions of a trace and while processing a breakpoint. 

## Comparison checkpoints 

If the old keyword was used in setting the checkpoint, the value at the address is read into the operand field as the first part of the initialization. Then, all comparison checkpoints are evaluated once, and their current state (true or false) is saved. 

. 

At each opportunity, the comparison checkpoints are evaluated: the state (true or false) is computed again. If it’s the same as the old state, there’s no stop. If the old state was TRUE and the new state is FALSE, the new state is saved, but there’s still no stop. If the old state was FALSE and the new state is TRUE (i.e. the comparison has become true), the checkpoint causes a stop. 

## Range checkpoints 

Range checkpoints are initialized by computing the CRC value for the region in question. That value (16 bits) is stored in the checkpoint slot. When an opportunity arises, the CRC is computed again. If it doesn’t match the initial value, the checkpoint causes a stop. 

Note that the CRC is not an infallible method for detecting changes. Some changes can cause the region to compute the same CRC value as before. | MEMORY CHECKPOINTS ON VALUES IN REGISTERS \ _ With the ampersand prefix (e.g. &d1) you can get the address where the stub | stores the values of CPU registers during checkpoint evaluation. What you have to | realize is that the address you get is the address of the high-order byte of the | value. For memory checks on d1.1, then, "&d1.1" is the correct address | specification for the m command. If you want to perform your memory check on | dl.w, "(&d1 + 2).w" is the address expression you want. For d1.b, "(&d1 + 3).b" is what you would use. | To compare two registers to each other, you would use the indirect comparison 

**==> picture [2 x 19] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


5-4 

4 

, 4 checkpoint type. Say you want to stop when al. is greater than a2.}: the 1 command '"m &a1.1l > {&a2}" accomplishes this. Of course, to compare words, 4 you have to shift the addresses by two: "m (&d1 + 2).w> {(&d2 + 2).w}" = stops when dl.w > d2.w. 

= : q 4 rr § 7 : 4 

Pd 

It is also important to remember that not all CPU registers are longs: the SR is stored as a word, so "&sr.w" is the address for the whole SR, and "(&sr + 1) .b" is the address for the CCR part of the SR. See the section Stub Variablesin the chapter SYMBOLS AND DEBUGGER VARIABLES for a complete list of stub variables. 

**==> picture [2 x 7] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


| | | Ej S g 7 : 4 4 ff } = . 4 ne gg 7 | P| Pg . 4 q j ’ q | 4 , 4 rf 4 1 q 4 i . | , | 

## CHAPTER 6 SYMBOLS AND DEBUGGER VARIABLES 

Db can load symbols from programs and other sources. In addition, the sym command can be used to.create entries in the symbol table to assist debugging. Debugger variables are values the debugger makes available to the user by name, such as the basepage of the program last loaded, and the type and argument vector of the last message, along with eight temporary storage locations for use at the user’s whim. Also, a user can declare new global variables by name, and even local variables within procedures. 

## SYMBOLS 

Symbols are loaded from programs being debugged using the exec and getsym commands. These commands add the symbols from the files they load to the debugger’s internal symbol table. The value of a symbol in the table can be used in an expression by prefixing it with a dot: ’,symx’ yields the number in the value field-of the symbol ’symx’. 

Symbols which refer to addresses in the text, data, or BSS segments of a program are relocatable symbols. In the program file, they have values as though the program were running from absolute address zero. Of course, programs can’t run there, so the program loader (and the debugger) must relocate the values of these symbols to reflect the address at which the program is actually loaded. Db takes care of this automatically. 

**==> picture [6 x 263] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>.<br>j<br>|<br>:<br>**----- End of picture text -----**<br>


If you specify ".main" and there is no symbol main in the symbol table, but there isa_main, the debugger provides the leading underscore for you. Specifically, the following variations are tried: prepend underscore; append underscore; truncate at 8 chars; prepend underscore and truncate at 8 chars. 

Db also supports GST-format symbols (also used by Lattice C from HiSoft). In this format, symbols can be up to 22 characters long. The symbol table looks like an Alcyon symbol table, with 14-byte symbol entries, except that when the $0048 bits are set in a symbol’s type, the next 14-byte entry is actually an extension of the symbol’s name. A new variable, symsearch, contains a bitmap of methods to use to look up symbol names. It is set automatically based on the types of symbol tables encountered by the "getsym" and "exec" commands, including the implicit "exec" when a program name is supplied on the debugger command line. 

**==> picture [1 x 21] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


**==> picture [35 x 17] intentionally omitted <==**

**----- Start of picture text -----**<br>
i |<br>**----- End of picture text -----**<br>


6-1 

( : : 

; ; 7 7 q d 4 3 s ; ; | 4 | ' ' q 

**==> picture [338 x 146] intentionally omitted <==**

**----- Start of picture text -----**<br>
|||||||||
|---|---|---|---|---|---|---|---|
|VALUE|TYPE|METHOD|
|0001|GST|Truncate to 22|chars;|failing|that,|prepend|’’|
|and use|21|chars;|failing|that,|prepend’@’|and||-|
|use|21|chars.|
|0002|MWC|Truncate|to|16|chars;|failing|that, append a|
|0004|ALC|Truncate to 8|chars;|failing that,|prepend|
|and use|7|chars.|

**----- End of picture text -----**<br>


## CONSTRAINED SYMBOLS 

A program file may have been produced by linking several modules together. These modules each had some global symbols and some local symbols. If you ask it to, your linker will include either both kinds of symbols, just the global symbols, or no symbols in the program file. Global symbol names are usually unique in a program file, but local symbol names might not be: there might be a local symbol called "start" in both "filea” and "fileb," for instance. 

If you have ain or another linker following the same conventions, you can specify the file name before the symbol name to differentiate these two: ’filea:start’ is different from ’.fileb:start’. If fileb came from the library (archive) mylib, the full specification is ’smylib:fileb:start’. Furthermore, there is something called a ; "confined" symbol: a symbol whose scope extends to the two unconfined symbols surrounding it. These symbols begin with °.’,’~’, and’L’. 

(Symbols beginning with ’L’ are generated by some compilers (notably Alcyon C) as internal labels. Strictly speaking, they are not confined: they are unique within each source file. However, they are considered confined so when their full specification is printed by the debugger, you can see what procedure they occur within.) 

_ In general, symbols are uniquely identified by the names of all the levels enclosing them: the levels of enclosure are archives, files, unconstrained symbols, and constrained symbols. 

1 j j ; ] { ; 4 4 ; q 4 4 q 4 j 4 j q 4 i 

**==> picture [7 x 27] intentionally omitted <==**

**----- Start of picture text -----**<br>
j<br>**----- End of picture text -----**<br>


| 

| 6-2 

: Take the following code fragment, for example: ; file init.s in archive mylib clrmem: : move.w #COUNT-1,d0 1 move.1 #START,a0 S .loop: clr.b (a0)+ L | dbra dO,.loop : The full specification of the symbol .loop is: 1 ' -mylib:init:clrmem: .loop | Be careful to distinguish between the period which introduces a symbol q1 | specification and the period which is the first character of a symbol’s name. If this Pf loop is the only one in the symbol table, it could be specified simply as ". loop". : 1 Still another way to differentiate symbols, useful for files linked without symbols ‘ . “ of type ’file’, is the number-sign (#’). ".symx#4" refers the fourth occurrence of symx in the symbol table. . f DEBUGGER VARIABLES 1 ‘ Debugger variables carry information which you can variables carry information which you can carry information which you can information which you can which you can you can can read, change, and use in change, and use in and use in use in in P| expressions. You can see the names of all the built-in variables with the vars can see the names of all the built-in variables with the vars see the names of all the built-in variables with the vars names of all the built-in variables with the vars of all the built-in variables with the vars all the built-in variables with the vars the built-in variables with the vars built-in variables with the vars variables with the vars with the vars the vars vars Pg command, you can see or set the value of a variable with the set you can see or set the value of a variable with the set can see or set the value of a variable with the set see or set the value of a variable with the set or set the value of a variable with the set set the value of a variable with the set the value of a variable with the set value of a variable with the set a variable with the set with the set the set set (or x) command, x) command, command, ; | and you can use the value can use the value use the value the value in an expression with the backquote an expression with the backquote expression with the backquote with the backquote the backquote backquote (’*’) prefix. Finally, 4 , you can get the address of the stub variables with the ampersand can get the address of the stub variables with the ampersand get the address of the stub variables with the ampersand the address of the stub variables with the ampersand address of the stub variables with the ampersand the stub variables with the ampersand stub variables with the ampersand variables with the ampersand with the ampersand the ampersand ampersand (‘&’) prefix. The = stub variables variables are special because their true values come from the stub. special because their true values come from the stub. because their true values come from the stub. their true values come from the stub. true values come from the stub. values come from the stub. come from the stub. from the stub. the stub. stub. A copy of copy of of . 4 these variables variables is kept in the head, and when you trace or go, they are written to kept in the head, and when you trace or go, they are written to in the head, and when you trace or go, they are written to the head, and when you trace or go, they are written to head, and when you trace or go, they are written to and when you trace or go, they are written to when you trace or go, they are written to you trace or go, they are written to trace or go, they are written to or go, they are written to go, they are written to they are written to are written to written to to P| the stub. stub. When the trace/go the trace/go trace/go finishes, their (possibly changed) values are read (possibly changed) values are read changed) values are read values are read read | 4 back from the the stub. , 4 (In fact, the true values of all of the stub variables the true values of all of the stub variables true values of all of the stub variables values of all of the stub variables of all of the stub variables all of the stub variables of the stub variables the stub variables stub variables variables is read from the stub when read from the stub when from the stub when the stub when stub when when _ you read or set any of them. read or set any of them. or set any of them. set any of them. any of them. of them. them. If you change a variable, the new values are you change a variable, the new values are a variable, the new values are variable, the new values are the new values are new values are values are are all jt 4 written to the stub the next time you trace or go. to the stub the next time you trace or go. the stub the next time you trace or go. next time you trace or go. time you trace or go. you trace or go. trace or go. or go. go. This saves time when you don’t saves time when you don’t time when you don’t when you don’t you don’t don’t Pg read or set them.) set them.) them.) j : All debugger variables are stored as a longword in the head, and most are stored J : as a longword on the stub. The ones stored as words on the stub have "(word)" after them in the following table. To use these in a comparison-type memory ’ checkpoint, you would use, for example, "&sr.w" to refer to the status register. 4 j Two variables, sfe and dfe, are stored as bytes. 

Debugger variables carry information which you can variables carry information which you can carry information which you can information which you can which you can you can can read, change, and use in change, and use in and use in use in in expressions. You can see the names of all the built-in variables with the vars can see the names of all the built-in variables with the vars see the names of all the built-in variables with the vars names of all the built-in variables with the vars of all the built-in variables with the vars all the built-in variables with the vars the built-in variables with the vars built-in variables with the vars variables with the vars with the vars the vars vars command, you can see or set the value of a variable with the set you can see or set the value of a variable with the set can see or set the value of a variable with the set see or set the value of a variable with the set or set the value of a variable with the set set the value of a variable with the set the value of a variable with the set value of a variable with the set a variable with the set with the set the set set (or x) command, x) command, command, and you can use the value can use the value use the value the value in an expression with the backquote an expression with the backquote expression with the backquote with the backquote the backquote backquote (’*’) prefix. Finally, you can get the address of the stub variables with the ampersand can get the address of the stub variables with the ampersand get the address of the stub variables with the ampersand the address of the stub variables with the ampersand address of the stub variables with the ampersand the stub variables with the ampersand stub variables with the ampersand variables with the ampersand with the ampersand the ampersand ampersand (‘&’) prefix. The stub variables variables are special because their true values come from the stub. special because their true values come from the stub. because their true values come from the stub. their true values come from the stub. true values come from the stub. values come from the stub. come from the stub. from the stub. the stub. stub. A copy of copy of of these variables variables is kept in the head, and when you trace or go, they are written to kept in the head, and when you trace or go, they are written to in the head, and when you trace or go, they are written to the head, and when you trace or go, they are written to head, and when you trace or go, they are written to and when you trace or go, they are written to when you trace or go, they are written to you trace or go, they are written to trace or go, they are written to or go, they are written to go, they are written to they are written to are written to written to to the stub. stub. When the trace/go the trace/go trace/go finishes, their (possibly changed) values are read (possibly changed) values are read changed) values are read values are read read back from the the stub. 

(In fact, the true values of all of the stub variables the true values of all of the stub variables true values of all of the stub variables values of all of the stub variables of all of the stub variables all of the stub variables of the stub variables the stub variables stub variables variables is read from the stub when read from the stub when from the stub when the stub when stub when when first you read or set any of them. read or set any of them. or set any of them. set any of them. any of them. of them. them. If you change a variable, the new values are you change a variable, the new values are a variable, the new values are variable, the new values are the new values are new values are values are are all written to the stub the next time you trace or go. to the stub the next time you trace or go. the stub the next time you trace or go. next time you trace or go. time you trace or go. you trace or go. trace or go. or go. go. This saves time when you don’t saves time when you don’t time when you don’t when you don’t you don’t don’t read or set them.) set them.) them.) 

i 1 

j : : j j ] 4 4 q 1 } 4 ’ j q1 | 7 4 q : 4 q q ; 4 % 

q ’ ' j f 7 

' i Z 

| : ‘ : i | ' : : ; 

## Stub Variables 

, 

The Stub Variables contain information about the stub. 

**==> picture [338 x 179] intentionally omitted <==**

**----- Start of picture text -----**<br>
NAME DESCRIPTION<br>cputype The type of CPU the stub is on (68xxx, word).<br>version The version number of the stub (word).<br>nbreaks The number of breakpoint slots (word).<br>nmems The number of memory checkpoint slots (word).<br>stubcode Pointer to the start of the stub.<br>breakptr Pointer to the breakpoint array.<br>memptr Pointer to the memory checkpoint array.<br>stubbp Basepage address of the stub process (for symbols).<br>clientbp Basepage address of the last-exec’ed client.<br>exspace See below.<br>**----- End of picture text -----**<br>


The exspace variable contains the address of stub memory where exception stack frame information is placed. The whole exception stack frame is copied from the stack to this space: see the processor documentation for the sizes and meanings of the stack frames. 

## Client Registers 

**==> picture [422 x 207] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||
|---|---|---|---|---|
|theTheclient. Client Register variables are the ones which mirror the actual CPU registers of|
|NAME(S)|DESCRIPTION|
|sr|The status register|(word).|
|do - d7|The data registers.|
|.|a0|- a6|The address|registers.|
|ssp|The supervisor stack pointer.|
|usp|The user stack pointer.|
|pe|The program counter.|
|sfc dfc|680x0|registers|(byte).|
|msp vbr cacr caar isp|680x0|registers.|
|a7 sp|Translated|to usp or ssp based on|sr.|

**----- End of picture text -----**<br>


j 

6-4 

j : 

s = : = . 4 | 4 | | fi fi ; q SS ~.. 1 P| : 1 4 q , | , 4 ; | ] , 4 i - 7 ! 

. | 2 ; | 

— 

## Other Build-in Variables 

All other variables are not stored in the stub: they are just in the debugger. 

**==> picture [366 x 182] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||
|---|---|---|
|NAME(S)|DESCRIPTION|
|t0-t7|Eight temporary variables you can use any way at all.|
|$|Holds the value ofthe last match command or the first|
|match address|from the last f (find) command.|
|mtype|Holds the type ofthe last user message received.|
|margv|Holds the argv ofthe last user message received.|
|rwstart|Holds the start address ofthe last file read or written.|
|rwsize|Holds the size of the last|file read or written.|
|iodev|Holds the current I/O device number (see below).|
|bdev|Holds the current BIOS I/O device number (see below).|
|discpu|Holds the CPU type for disassembly|(see below).|

**----- End of picture text -----**<br>


The disepu variable holds the last two digits of the CPU type, in decimal: 00, @10, @20, or @30 for 68000, 68010, 68020, or 68030. Instructions which are legal on a 68030 but not on a 68000 through 68020 will not be disassembled if discpu is not @30. 

The iodev variable holds a number which tells the debugger what I/O device to use: 

**==> picture [280 x 92] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||
|---|---|---|
|VALUE|MEANING|
|0|GEMDOS|(screen / keyboard)|
|1|Serial port|(polled)|
|2|BIOS (see below)|
|3|MIDI|(polled)|

**----- End of picture text -----**<br>


When the value of iodev is 2, BIOS calls are used for input and output. The BIOS calls take a device-number argument, and that device number is taken from the variable bdev. No check is made to see if you have set a sensible value here. 

Normally, the debugger starts up using GEMDOS (iodev value 0). Using the -s, -b, and -m options on the debugger command line causes it to start up using another value (1, 2, and 3, respectively). 

6-5 

: 

| 

| | : 

4 

i: 

A 

## USER-DEFINED VARIABLES 

The global and local commands create new variables by name. local is generally used only in procedures: it creates variables which are visible only while executing in that procedure. global creates variables visible from anywhere. In each case, you use the variables the same way you use any others: you put backquotes before their names. 

es 

4 

6-6 

1 

| L 4 

| 

- j & | S F 1 = 4 fj | : - Ba) , | 7 4 ; : 4 a PF : = 

. 

## CHAPTER 7 PROCEDURES, IF, GOTO, DEFER, AND ALIAS 

## WHAT IS A PROCEDURE 

A procedure is a list of debugger commands which is stored in memory and executed by name. A procedure consists of the following parts: 

1. The procedure name. 

2. The list of arguments. 

3. The list of commands making up the procedure. 

Once you've created a procedure, you call it by using its name as a command, followed by as many expressions as the procedure has arguments. The commands in the procedure body are executed as if they came from the keyboard or a script file. 

Procedures can call other procedures, nesting to any depth (limited by the amount of memory the debugger started with). They can contain any debugger command except procedure itself. 

Procedures can use the local command to create variables which exist only during the execution of the procedure, and are visible only within the body of the procedure. 

One local variable, arge ("argument count’), is created for every procedure. It tells how many arguments were provided for the procedure. You can calla procedure and give it fewer arguments than it calls for. However, if you provide too many arguments, you will get an error message. You can create a procedure that can be called with fewer than the maximum number of arguments and still do something useful. 

**==> picture [27 x 16] intentionally omitted <==**

**----- Start of picture text -----**<br>
Pd<br>**----- End of picture text -----**<br>


7-1 

| 1 | 4 ] jj j ' j 4 { 3 1 E 4 q ; 

: ; 7 iF 

i ; . a : : 7 i i ‘ Es if " q | [ ‘ 

: 

## SAMPLE PROCEDURE 

Here is a sample procedure: 

. 

procedure sample maxval # This prints the first *maxval nonnegative integers. local count ; set count 0 if (‘argc < 1) abort Too few args to procedure sample 

#: loop print -n -d ‘count set count (‘count + 1) if (*count < ‘maxval) goto loop print . 

The first line is the procedure declaration: it starts with the procedure command, then the name of the procedure ("sample"), then the argument list. This procedure takes one argument, "maxval." 

The next line is a comment, telling what the procedure does. The third line is the local command: it creates a local variable, visible only inside this procedure, called "count." Local variables start out with no particular value, so it’s immediately initialized to zero by the set command. The next line is blank. You can have blank lines in procedures. When the procedure is stored, they get translated into lines which start with "#," meaning the whole line is a comment. 

Next, we have an if command. This checks the variable arge to see if the procedure was in fact given an argument. (You can’t provide more arguments than the procedure calls for, but you might provide fewer.) 

The next line (after the second blank one) is a label. You can tell it’s a label because it starts with the two characters #: (hash colon). When the flow of control in the procedure gets to this line, it will be treated as a comment (since it starts with #). When storing the procedure, however, the debugger sees this as a label, and saves this position in the procedure under the name after the colon (in this case, "loop"). 

7-2 

{ 

j | The print and set commands print and set commands and set commands set commands commands do what you'd expect, , 4 after the the if takes takes as its argument ; can be anywhere in the "#:." and have have nothingfollowingfollowing j After the if is another print the if is another print if is another print is another print another print print command: | @ print commands commands with the -n -n { { outside the the loop, and so so is not not 1 programmer, and is used used to make ‘ , The dot on dot on on the last last line is just just } 4 procedure. It’s not a command: a command: command: 4 the end marker. : Running this procedure this procedure procedure looks | j : sample 9 | | 012345678 s @ MORE DETAILS ON PROCEDURES DETAILS ON PROCEDURES ON PROCEDURES PROCEDURES q Procedures needaa little more more = unexpected side-effects. : In the the first place, the goto place, the goto the goto goto command j line. The implementation of implementation of of the goto command q is that it takes effect at the end . | goto will will execute before the the goto | Gg advantage of this this and it might might change p restriction: make sure no command ever comes 

The print and set commands print and set commands and set commands set commands commands do what you'd expect, as does the if. The goto after the the if takes takes as its argument the name of a label in the procedure. The label can be anywhere in the procedure. Labels, remember, begin with the characters "#:." and have have nothingfollowingfollowing them. 

After the if is another print the if is another print if is another print is another print another print print command: this terminates the line which all those print commands commands with the -n -n switch were writing to. This print command is outside the the loop, and so so is not not indented as far. The indentation is totally up to the programmer, and is used used to make the control structures of the procedure clearer. 

. . 

The dot on dot on on the last last line is just just that: a dot, a period. That marks the end of the procedure. It’s not a command: a command: command: it’s recognized in the procedure-creation phase as the end marker. Running this procedure this procedure procedure looks like this (the colon is the debugger prompt): : sample 9 012345678 . MORE DETAILS ON PROCEDURES DETAILS ON PROCEDURES ON PROCEDURES PROCEDURES , 

Procedures needaa little more more explaining. They have some restrictions and unexpected side-effects. 

In the the first place, the goto place, the goto the goto goto command must be the last command on a line. The implementation of implementation of of the goto command is a little strange, and the upshot is that it takes effect at the end of the line it’s found on. Other commands after a goto will will execute before the the goto itself does. You are not encouraged to take advantage of this this and it might might change in the future. Just live under this restriction: make sure no command ever comes after a goto command ona line. 

1 ] 

built-in variables are searched first of all. all. A global or local with with a name like "pc" ; never be be seen; the debugger variable variable "pc" will be used instead. A local with same name as a global, however, will be be seen: q global myvar | set myvar 3 : procedure foo : local myvar } set myvar 10 1 print myvar j foo ; print myvar q The above sequence will above sequence will sequence will will print "10" "10" followed by "3" because by "3" because "3" because because the local myvar myvar is seen 1 inside the procedure, while the global myvar global myvar myvar is seen outside it. 1 COMMANDS The procedure command with no arguments procedure command with no arguments command with no arguments with no arguments no arguments arguments lists the procedure declaration for ; all procedures. This includes the name and and the argument list. This can serve as a j reminder of what of what what a procedure procedure does and how how to use use it, if the the procedure’s name and and q its arguments’ names are well chosen. 4 The procedure command with one procedure command with one command with one with one one or more arguments begins the more arguments begins the arguments begins the begins the the creation of a of a a 4 procedure. The first argument argument is the name name of the procedure to create, and the 4 subsequent arguments arguments are the names of the the procedure’s arguments. 4 When typing a procedure typing a procedure a procedure procedure in from the command prompt from the command prompt the command prompt command prompt prompt (as opposed to loading opposed to loading to loading loading it ] from a a file), the debugger prompts you with prompts you with with a double-colon double-colon ("::") prompt for each each q line. The lines you you type are not interpreted at all, only stored. The end of the of the the ‘ procedure is marked bya byaa line consisting of a period period only. At that point, that point, point, the q debugger scans scans the procedure procedure for labels labels and stores stores the procedure procedure name, its q argument names, and the label positions in the procedure procedure list. Only at this pointis 4 anycreation old procedure of the procedure, by this namea pre-existingremoved procedurefrom thecreation old procedure of the procedure, by this namea pre-existingremoved procedurefrom the old procedure of the procedure, by this namea pre-existingremoved procedurefrom the procedure of the procedure, by this namea pre-existingremoved procedurefrom the of the procedure, by this namea pre-existingremoved procedurefrom the by this namea pre-existingremoved procedurefrom the this namea pre-existingremoved procedurefrom the namea pre-existingremoved procedurefrom thea pre-existingremoved procedurefrom the pre-existingremoved procedurefrom theremoved procedurefrom the procedurefrom thefrom the the withlist:list: if youthat nameuse youthat nameusethat nameuse nameuseuse “Cwill tonotabort have thewill tonotabort have the tonotabort have thenotabort have theabort have the have the the | qj been removed. removed. 4 savingThe plist(with commandtranscrip wiThe plist(with commandtranscrip wi plist(with commandtranscrip wi(with commandtranscrip wi commandtranscrip witranscrip wi wi **t** h) noand argumentrestoringlists(with all proceduresload).) noand argumentrestoringlists(with all proceduresload). noand argumentrestoringlists(with all proceduresload).and argumentrestoringlists(with all proceduresload). argumentrestoringlists(with all proceduresload).restoringlists(with all proceduresload).lists(with all proceduresload).(with all proceduresload). all proceduresload). proceduresload).load). They in begin a formwith sui in begin a formwith sui begin a formwith sui a formwith sui formwith suiwith sui sui **t** ablehehe for : procedure command and end with a period alone on a line. With one or more 4 arguments, the plist command lists those procedures in the argument list. 4 7-4 4 4 

j ; = 

j PROCEDURE-RELATED COMMANDS 

. its arguments’ names are well chosen. : The procedure command with one procedure command with one command with one with one one or more arguments begins the more arguments begins the arguments begins the begins the the creation of a of a a ; procedure. The first argument argument is the name name of the procedure to create, and the : subsequent arguments arguments are the names of the the procedure’s arguments. | When typing a procedure typing a procedure a procedure procedure in from the command prompt from the command prompt the command prompt command prompt prompt (as opposed to loading opposed to loading to loading loading it 4 from a a file), the debugger prompts you with prompts you with with a double-colon double-colon ("::") prompt for each each ; line. The lines you you type are not interpreted at all, only stored. The end of the of the the 1 ; procedure is marked bya byaa line consisting of a period period only. At that point, that point, point, the q ' debugger scans scans the procedure procedure for labels labels and stores stores the procedure procedure name, its 4 argument names, and the label positions in the procedure procedure list. Only at this pointis i4 anycreation old procedure of the procedure, by this namea pre-existingremoved procedurefrom thecreation old procedure of the procedure, by this namea pre-existingremoved procedurefrom the old procedure of the procedure, by this namea pre-existingremoved procedurefrom the procedure of the procedure, by this namea pre-existingremoved procedurefrom the of the procedure, by this namea pre-existingremoved procedurefrom the by this namea pre-existingremoved procedurefrom the this namea pre-existingremoved procedurefrom the namea pre-existingremoved procedurefrom thea pre-existingremoved procedurefrom the pre-existingremoved procedurefrom theremoved procedurefrom the procedurefrom thefrom the the withlist:list: if youthat nameuse youthat nameusethat nameuse nameuseuse “Cwill tonotabort have thewill tonotabort have the tonotabort have thenotabort have theabort have the have the the 1 been removed. removed. i savingThe plist(with commandtranscrip wiThe plist(with commandtranscrip wi plist(with commandtranscrip wi(with commandtranscrip wi commandtranscrip witranscrip wi wi **t** h) noand argumentrestoringlists(with all proceduresload).) noand argumentrestoringlists(with all proceduresload). noand argumentrestoringlists(with all proceduresload).and argumentrestoringlists(with all proceduresload). argumentrestoringlists(with all proceduresload).restoringlists(with all proceduresload).lists(with all proceduresload).(with all proceduresload). all proceduresload). proceduresload).load). They in begin a formwith sui in begin a formwith sui begin a formwith sui a formwith sui formwith suiwith sui sui **t** ablehehe for 

I ' : 

Second, remember that local variables are searched before global variables, but built-in variables are searched first of all. all. A global or local with with a name like "pc" will never be be seen; the debugger variable variable "pc" will be used instead. A local with the same name as a global, however, will be be seen: 

The above sequence will above sequence will sequence will will print "10" "10" followed by "3" because by "3" because "3" because because the local myvar myvar is seen inside the procedure, while the global myvar global myvar myvar is seen outside it. 

) 

The procedure command with no arguments procedure command with no arguments command with no arguments with no arguments no arguments arguments lists the procedure declaration for all procedures. This includes the name and and the argument list. This can serve as a reminder of what of what what a procedure procedure does and how how to use use it, if the the procedure’s name and and its arguments’ names are well chosen. 

j 4 

& a i 4 

1 S i 4 bis = , | | ‘ : 4 4 & ; | f 4 1 ; 4 1 , | , | q 

## DEFER AND ALIAS 

This section describes the defer and alias commands, and offers some advanced advice on using the debugger. 

It is unfortunate but true that you may have to read this whole section before you can really understand and use any of it. The explanations of alias, defer, and compound commands are of necessity given in terms of each other. Please be patient and read through this a couple of times. 

The alias command takes two arguments: a name, and an expansion for that name. After this, any time the name appears as a command, it is replaced (textually) with the expansion. (In the examples in this chapter, a line beginning with a colon (:’) shows a command which you can type into the debugger. The colon itself should not be typed; it represents the debugger’s prompt.) 

: alias foo dw.table[10] 

After using the above command to define an alias for "foo" the "command" foo can be used, and it will expand to "dw.table[10]" (which dumps the 16 bytes starting at the label "table" as words). This is a very simple example ofthe alias command, but still quite a timesaver for commands you usea lot. 

Aliases are expanded in place in the command line, and any arguments to the alias appear at the end of the expansion. The following (extremely useful) alias illustrates this: 

- alias rwfind f*rwstart[*rwsize] 

Now, the new "commana" rwfind can be used to find values or text in the file which has just been read with the read command: "rwfind ’some test" expands to the command “f* rwstart[*rwsize] ’some test” which will find all occurrences of the quoted text in the file. 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
,<br>**----- End of picture text -----**<br>


2 

| 4 j | : : j q 1 ] j j : 4 : j q 4 7 : ; q q J F j 

j 

1Breakpoint aliases start with br and end with the slot number they are attached to (as one upper-case hex digit): brO through brF if there are 16 : | breakpoint slots. Memory checkpoint aliases start with me and end with : q the memory checkpoint slot number, also as one upper-case hex digit. j | Message checkpoints start with msg and end with the message number q 4 they handle, as four upper-case hex digits: msg0000 for message type 1 : zero, msgOFCA for message type $Ofca. ] : When several events happen at the same time, such as multiple j ; checkpoints or a checkpoint and a breakpoint, only one auto-execute alias j j is executed. Breakpoint aliases are checked for first (in ascending : : numerical order), then checkpoints (also:in order), and finally messages. 4 - The first one of these which exists, and only that one, is executed. 

| 

= 

4 automatically. See the examples the examples examples below for more. more. | When you set an auto-execute alias, be careful to remember that it is there. 4a For instance, if you set a breakpoint someplace, and create an auto-execute [ alias for that breakpoint, and then you remove the breakpoint, the i auto-execute alias is still there. If you set another breakpoint and it | happens to go in the same slot as the first one, the auto-execute alias will 1 be triggered by it, probably resulting in something you didn’t expect or i want. Unalias is the command which removes one or more aliases from | the debugger’s alias table. 

| | 

## AUTO-EXECUTE ALIASES 

Auto-execute aliases have special names: they start with the letters "br" or "me" or "msg" and they are executed when a corresponding breakpoint, -memory checkpoint, or message event happens, respectively. For example, when the breakpoint in slot zero causes a stop, the debugger looks for an alias called "brO" and executes it if it exists. 

If none of these exists, the default action is taken: the breakpoint, checkpoint, and message type and vector are displayed on the screen. 

Note that the auto-execute alias for an event can itself cause a trace/go. If it does, and that trace/go is stopped by an event, the auto-execute aliases are checked again and the first matching one is executed, so the right combination of events and auto-execute aliases can cause a lot to happen automatically. See the examples the examples examples below for more. more. 

4 

a 

q j : gg 4 S Z = : 4 4 ‘ 

4 { 1 , 4 4 ’ 4 : ] 4 -— | F j q 1 = - | } 

## COMPOUND COMMANDS, introduced 

If you enclose the expansion argument to alias (or defer or if) in quotation marks, it can contain more than one command: 

- alias foo "echo xtable;dw.xtable[10];echo ytable;dw.ytable[10]" 

Now, when you use foo, four commands (two echoes and two dumps) will be executed. Again, this can be a great timesaver. As explained below, it can be the key to really powerful macros. 

| 

## DEFER 

The defer command takes one argument: a command to be executed the next time the debugger returns to the top level for user input. That is, when the debugger is about to print its prompt, the last thing it does is execute any deferred command. The purpose of this is to allow for automatic execution of the client and looping in macros, without using the alias stack. 

Only the last defer command is remembered. Defer with no arguments causes the debugger to forget any existing deferred command. 

Here is an example of the use of the defer command: 

: b .endloop :m #0 &d7.1!= old : alias mcO "print d7 changed: new value \d7;defer tx" If the client is about to start a loop, and the user wishes to be notified when d7 changes, the above sequence will do the trick. It will stop with the breakpoint at the end of the loop, and each time d7 changes the auto-execute alias mcO will be executed. This alias displays the new value of d7, then tells the client to continue executing rather than returning to the command level. 

The above example would still work if the last command in the alias were simply tx rather than defer tx, but it would soon fill up available memory with the stacking of alias expansions: using one alias in another amounts to a procedure call. 

: Defer can also be used asa trick to allow arguments to an alias. Remember that J 3 an alias expands from a command (like mwc or rwfind) into the expansion text, in ‘ 4 place. Any arguments to the alias are tacked on after the expansion: q 4 t alias foo “echo one two" : | would cause "foo x y Z" to expand to "echo one two x yz." A macro to print the q : Nth longword in a table starting at .table might be as follows: ] : : : alias nthlong “defer print (1peek (.table + (*tO * j ; 4))): nthlong3;xto" 3 |j | This works because the command nthlong is substituted with the text of the alias, j ; and the ’3’ is tacked to the end of that. Because of the defer, the command "xt0 j ' 3" will be executed before the print command, so tO will have the value 3 by then, 4 j and the value at (.table + (3 * 4)) gets printed. : ' When the “argument” you're trying to provide is a number, it’s far better to use a 1 j procedure: 4 | ; procedure nthlong n 3 | print (1peek (.table + (*n * 4))) q : This use of defer is really just a leftover from when the debugger didn’t have 1 procedures. It’s still useful for string arguments, though. (or it will be until the | debugger gets strings as a data type...) q : COMPOUND COMMANDS, explained j 4 As you can see from the examples above, the if, defer, and alias commands each : take a command as an argument. That argument can be a compound command . , consisting of more than one simple command if it is enclosed in quotation marks: ; . 4 a alias mycmd “echo start mycmd;l;print end of mycmd" q i Executing the above command, then the command "mycmd," will cause the legend 4 “start mycmd" to appear, then a disassembly listing of 12 lines starting at the 4 current disassembly pointer, then the legend "end of mycmd." (Sure, it's silly, but 4 it’s just an example.) 4 

e = 

1 g j q 4 S | 4 4 g 

| } . , | q : j 7 3 { . | 1 7 = { 1 = 

The important point is that the semicolons are enclosed in quotes, making them part of the argument to “alias” rather than being interpreted as separating the alias command from the 1 and the print command. Without quotes, 

alias mycmd echo start mycmd;l;print end of mycmd - 

the alias for mycmd would be "echo start mycmd" -- the echo command stops with the first semicolon, and the 1 and print commands are executed in turn. 

The alias for Mark Williams C argument string handling uses this trick: the alias itself consists of two commands, a find (f) and a set (s): : alias mvc ’f (1peek (*clientbp + 2c))[800] "ARGV=" ; s $ 5a’ 

Note the use of single quotes around the alias, and double quotes around the string argument to the find command. Single quotes match single quotes and double matches double, but their interpretations are identical. 

## You can nest these expansions: 

alias setbp \ "b #2 .mainloop; alias br2 ’echo\ loop;dw.table[10];defer g’" 

Once you create this alias, if you use the command setbp a breakpoint will be set, and an alias will be created which will get executed when that breakpoint is hit (see the section AUTO-EXECUTE ALIASES in this chapter). The alias which setbp creates, called br2 (to attach it to breakpoint slot 2), contains a compound command as its expansion: the compound command prints a message, dumps the first eight words of a table (16 bytes), and then lets the program continue executing. 

(Unfortunately, you can’t nest more than two levels of compound commands, because only the single- and double-quote characters protect semicolons, and any more of them would look like closing, not opening, quotes.) 

| 

{ | ; ; ; 4 4 : 1 

Another use for auto-execute aliases might be to show something on the screen at the start and end of a certain procedure: 

: b #3 .myproc 

- | alias br3 “echo entering myproc : defer g" : 1 .myproc[800] 4e5e4e75 - : b #4 $ : alias br4 "print myproc returns “dO : defer g" 

] Note the f (find) command in this sequence: it searches from the start of the ; procedure, for 2K bytes, for the longword $4e5e4e75. That is two 68000 opcodes: ; UNLK and RTS. Every procedure compiled with Alcyon C ends with these two instructions, and the likelihood of finding that exact byte pattern anywhere in the : procedure except the end is very small, so the chances are that breakpoint #4 will ; be set at the UNLK instruction. (If the procedure is more than 2K bytes long, the ; find should have a longer count.) ] The f (find) command will dump the locations of all matches on the screen, even 4 ' though all we are interested in is getting $ set to the address of the first one. You q can suppress this needless output by surrounding the f command with gag on and gag off. See the section GAG in the chapter COMMANDS for more information. 

fl | 

**==> picture [3 x 8] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


7-10 

| Db must operate within the constraints imposed by the Atari ST operating system. When a these constraints prevent using db in the manner needed, the user should consider remote 4 debugging. See the chapter REMOTE DEBUGGING for more information. | = DB AND GEMDOS 1 When you don’t specify a command-line option like -s or -m for input and output, the , 4 debugger uses GEMDOS to access the screen and keyboard. It is important to know, then, : _ that two programs can’t be using GEMDOS at the same time. If you stop the client while | it is executing a GEMDOS system call (like Fopen or Cconout), and the debugger uses = GEMDOS to print to the screen, GEMDOS will lose track of the client, and the next g & command will create havoc. : If you use the t, v, and g (trace, verbose-trace, and go) commands exclusively, and avoid 4 u and vu, there should be no problem, because they will never stop while the PC is in P - GEMDOS. However, if you use the u (untrace) or vu (verbose-untrace) commands, you ae could stop while in GEMDOS, and that would be bad news. q Furthermore, if the debugger is using GEMDOS for input and output, and you hit the q STOP button while the PC is in GEMDOS, you are in the same boat. So the lesson is to Pg use t, v, and g exclusively when using GEMDOS for input and output, and don’t use the 4 stop button unless you are sure the PC is not in GEMDOS or the BIOS. 1 Even when it’s not using GEMDOS for its input and output, the debugger uses GEMDOS , | for certain commands, like exec (to load a file and set it up for execution) and getsym ; (to load symbols froma file). Thus, you should be sure that the client is not in the middle 1 4 of GEMDOS when using these commands. Another command which can cause even more , trouble is transeript, because the uset has little control over when the buffer will be , 4 written to disk. When debugging locally, use transcript with extreme care, making sure zz that you don’t stop while the PC is in GEMDOS. : ‘ When remote debugging, none of this applies, because the slave and the master have two , | independent GEMDOSes. D4 DB AND MARK WILLIAMS C ] Mark Williams C uses a different symbol table format from Alcyon’s. Notably, symbols are = 4 stored in 16 characters, not just 8. Also, global variables in C get an underscore character . appended to them, rather than prepended as is the convention among most C compilers. ; : (The reason for doing this is so you don’t have to worry about name collisions with . 8-1 

: 

## CHAPTER 8 

## OPERATING SYSTEM CONSIDERATIONS 

assembly language: by not using a leading underscore (or trailing, in the case of MWC), 1 you know you won't be using the same name as a C variable.) Db correctly interprets : Mark Williams C symbol tables, both in the old (before version 3.0) land version 3.x : formats. ; characters’Mark Williamsworth C andof com soMark Williamsworth C andof com so Williamsworth C andof com soworth C andof com so C andof com so andof com soof com so com so so **m** and-line othere other other **e** nvironmentsargumentsarguments usetoto their a trick programs. to pass moreThe a trick programs. to pass moreThe trick programs. to pass moreThe programs. to pass moreThe to pass moreThe pass moreThe moreTheThe trick thanis 127 to use the- thanis 127 to use the-is 127 to use the- 127 to use the- to use the- use the- the-’ environment variable ARGV, variable ARGV, ARGV, because the value the value of an an environment variable variable can be any be any any , length at all. There is a problem with problem with with this approach, however: since the environment environment is 1 inherited from one process to another, the child can’t can’t tell if the ARGV in the ARGV in ARGV in in its environment environment really came from came from from its parent. MWC programs programs will take take the debugger’s arguments as their | own. The way way to fix this is to force the MWC program to think that there are no arguments in i its environment. environment. There is an automatic way way to do do this: place this alias command command in your your / db.re file and use it after you you exec an MWC program, MWC program, program, but before the first trace/go : command: : alias mwc ’f (lpeek (‘clientbp + 2c)) [800] "ARGV="; s $ Sa’ - This alias searches in the the client’s environment (the address of which which is at ‘clientbp+$2c) ' for the word "ARGV=" and changes the word "ARGV=" and changes word "ARGV=" and changes "ARGV=" and changes and changes changes the first letter of that word of that word that word word to a’Z’. This prevents : MWC argument-parsing argument-parsing code from from finding “ARGV=" “ARGV=" in its environment (because it now now y reads "ZRGV=") "ZRGV=") and the program program will therefore look in the basepage for command command line arguments. If you you don’t understand understand this whole whole discussion, or why the why the the alias above works, works, that’s okay: : just place place the alias in your autoload autoload file (usually "db.rc"), and type the command the command command "“mwc" | after you exec a you exec a exec a a client compiled with MWC. MWC. Then use the args command command to pass pass 

/ @ : : : be a fogES ; . i . j i 4 ; 3 4 ] q : q q 4 : 4 q q q 

characters’Mark Williamsworth C andof com soMark Williamsworth C andof com so Williamsworth C andof com soworth C andof com so C andof com so andof com soof com so com so so **m** and-line othere other other **e** nvironmentsargumentsarguments usetoto their a trick programs. to pass moreThe a trick programs. to pass moreThe trick programs. to pass moreThe programs. to pass moreThe to pass moreThe pass moreThe moreTheThe trick thanis 127 to use the- thanis 127 to use the-is 127 to use the- 127 to use the- to use the- use the- the-environment variable ARGV, variable ARGV, ARGV, because the value the value of an an environment variable variable can be any be any any length at all. There is a problem with problem with with this approach, however: since the environment environment is inherited from one process to another, the child can’t can’t tell if the ARGV in the ARGV in ARGV in in its environment environment really came from came from from its parent. MWC programs programs will take take the debugger’s arguments as their own. 

The way way to fix this is to force the MWC program to think that there are no arguments in its environment. environment. There is an automatic way way to do do this: place this alias command command in your your db.re file and use it after you you exec an MWC program, MWC program, program, but before the first trace/go command: 

This alias searches in the the client’s environment (the address of which which is at ‘clientbp+$2c) for the word "ARGV=" and changes the word "ARGV=" and changes word "ARGV=" and changes "ARGV=" and changes and changes changes the first letter of that word of that word that word word to a’Z’. This prevents the MWC argument-parsing argument-parsing code from from finding “ARGV=" “ARGV=" in its environment (because it now now reads "ZRGV=") "ZRGV=") and the program program will therefore look in the basepage for command command line arguments. 

If you you don’t understand understand this whole whole discussion, or why the why the the alias above works, works, that’s okay: just place place the alias in your autoload autoload file (usually "db.rc"), and type the command the command command "“mwc" after you exec a you exec a exec a a client compiled with MWC. MWC. Then use the args command command to pass pass arguments to the client. 

**==> picture [2 x 8] intentionally omitted <==**

**----- Start of picture text -----**<br>
4<br>**----- End of picture text -----**<br>


q 

8-2 

: ; | = ' 4 = ; 4 1 } | a = | , 4 a 1 1 4 . | : j P| 4 = ' ; | 1 mm j : 4 q | 

## DB AND THE XBIOS TRAP 

Db uses XBIOS function code 11 (that is, trap #$e when the word on the top of the stack is $000b). The program you are debugging may install a handler for trap 14. However, if the program is.a resident utility (sometimes called "TSR" for "terminate and stay resident’) you have to be careful when debugging it. Specifically, the debugger replaces the old vector for trap 14 when it exists. Since your program linked into the trap after the debugger did, the debugger can’t know how to remove itself from the linkage, so it simply clobbers the trap 14 vector, removing your handler from the trap. 

You can still debug TSRs which use trap 14, however. You can either run the TSR before running the debugger, or run the debugger, and then exec your TSR, let it run until it terminates (and stays resident), and then exec a program to test it, all without leaving the debugger. If you mun the TSR before running the debugger, you should arrange for the TSR to let you know its text base address, so you will be able to use getsym to load its symbols for debugging. 

Naturally, since the debugger itself uses trap 14 function code 11, no user program should use that same function code. 

## THE SHELL COMMAND IN DETAIL 

The ! (shell) command can be used to leave the debugger temporarily, execute a command, and re-enter the debugger where you left off. What it does is execute its argument as a command, with the GEMDOS Pexec function. This requires that there be enough memory available to the operating system to run that program. This is often not the case; if you try it and get "insufficient memory" then that is the problem. 

Some shells use the system variable _shell_p in a special way. Db tries to detect these shells. The presence of such a handler lets you pass _shell_p a command like "grep foo *.c" and let the shell figure out where to find grep, how to load it, and how to pass "foo and "*.c" (or “all the files which end in .c") as its arguments. 

You tell the debugger that you have such a shell by setting the environment variable SHELL[P][to][ the][ value]["yes"][ before][starting][Db.][In][ most][ shells,][the][ command][to][ do][ this][is] 

"setenv SHELLP[yes’] or ’setenv SHELL P=yes’ 

With no arguments, the ! command looks in your environment for the variable SHELL. If it’s found, the value is assumed to be the full filename, including the path and file type, of your shell, and that file is executed. 

**==> picture [2 x 17] intentionally omitted <==**

**----- Start of picture text -----**<br>
,<br>**----- End of picture text -----**<br>


8-3 

When the program you execute (or the shell from $SHELL) exits, you re-enter the | debugger exactly where you left off, with the same state you had before you left. = i Note that this command will only work when it is okay to make GEMDOS calls. See the : this chapter for more information. 4 : section DB AND GEMDOS[in] EXCEPTIONS | | The trace/go commands can all cause the program being debugged to execute instructions q which cause exceptions in the 68000 processor. Most of these exceptions are caught by ‘- the debugger. In particular, bus error, address error, etc. (exception numbers 2 through ' 4 ; A 9) are caught, as well as the spurious and uninitialized interrupt vectors. In addition, the 7 : debugger has a provision for a "stop button:" hitting the stop button will cause the client = G to stop. See the section STOP BUTTONS in the chapter REMOTE DEBUGGING for | 4 i more information. gg : The debugger containsa list of those exception vectors which it takes over. The debugger 7 fs restores all the vectors it takes over on exit. If your program or some program in your ; & : system uses a vector which the debugger considers an error, like one of the reserved ) q vectors, or "spurious interrupt," or “format error," then you are just out of luck; you will —_ .7 have to use the debugger carefully or not at all. oo Whena trace/go command causes one of these exceptions to occur, execution is i | immediately stopped and control is returned to the debugger. The pe and sr are saved | 4 5 from the exception stack frame, and all other registers keep their values. Note that after | 4 bus error and address error on a 68000, the pe will not have a reliable value: the | instruction causing the exception is near the pe, probably somewhere from two to ten 4 bytes before it. 4 j When[a][trace/go][ command][ stops][ because][ of][ an][ exception,][ the][ where][ command][is] f : convenient to determine what procedure was executing at the time: it reports name of the | 3 symbol closest to, but not after, the current pe. ; i DB, TOS, AND 68030 = , The debugger and TOS both run on 68030's debugger and TOS both run on 68030's and TOS both run on 68030's TOS both run on 68030's both run on 68030's run on 68030's on 68030's 68030's (the Atari TT), but some shoehorning was Atari TT), but some shoehorning was TT), but some shoehorning was but some shoehorning was some shoehorning was shoehorning was was required. One such shoehom wasa such shoehom wasa shoehom wasa wasaa privilege violation handler. violation handler. handler. On the the 68000, the the instruction "move "move sr, d0" is not protected. protected. On the 68010 and up, 68010 and up, and up, up, itis. Some ST programs ST programs programs | use this instruction, this instruction, instruction, especially to save the condition code register (CCR), which to save the condition code register (CCR), which save the condition code register (CCR), which the condition code register (CCR), which code register (CCR), which register (CCR), which (CCR), which which is part of part of | 

, The debugger and TOS both run on 68030's debugger and TOS both run on 68030's and TOS both run on 68030's TOS both run on 68030's both run on 68030's run on 68030's on 68030's 68030's (the Atari TT), but some shoehorning was Atari TT), but some shoehorning was TT), but some shoehorning was but some shoehorning was some shoehorning was shoehorning was was required. One such shoehom wasa such shoehom wasa shoehom wasa wasaa privilege violation handler. violation handler. handler. On the the 68000, the the instruction "move "move sr, d0" is not protected. protected. On the 68010 and up, 68010 and up, and up, up, itis. Some ST programs ST programs programs | use this instruction, this instruction, instruction, especially to save the condition code register (CCR), which to save the condition code register (CCR), which save the condition code register (CCR), which the condition code register (CCR), which code register (CCR), which register (CCR), which (CCR), which which is part of part of | the SR. j To make those programs work on the 68030, Atari placed a privilege violation handler in | the OS. Ifa "move from sr" instruction caused the violation, the handler writes a new 

1 

8-4 

; i 

; Since the debugger catches debugger catches catches ; the debugger debugger has to do do the same same program and you and you you run it on on a 68010, ; ] a "move "move from ccr’ instruction. If this causes your program Lj DEBUGGER MEMORY USAGE : 1 The debugger must share memory debugger must share memory must share memory share memory memory with the = client being being debugged. Under TOS, = | free memory, memory, and if they plan they plan plan to memory back to TOS. | The debugger program has debugger program has program has has a variable which = TOS. That variable variable can be be found (4 data segment of the debugger segment of the debugger of the debugger the debugger debugger program 

7 | 4 j / 4 , 4 ; | rf 4 = 

instruction in that place: "move ccr.dO" (of course, this works for any destination, not just 

Since the debugger catches debugger catches catches exceptions (because they usually mean bugs in your program), the debugger debugger has to do do the same same thing. If you have a "move from sr" instruction in your program and you and you you run it on on a 68010, 68020, or 68030, the debugger might demote it into a "move "move from ccr’ instruction. If this causes your program to fail, now you know why. 

The debugger must share memory debugger must share memory must share memory share memory memory with the rest of the operating system and with the client being being debugged. Under TOS, all programs are allocated the largest single block of free memory, memory, and if they plan they plan plan to start up other processing they must give some of that memory back to TOS. The debugger program has debugger program has program has has a variable which controls how much memory it gives back to TOS. That variable variable can be be found from the outside because it is the first longword of the data segment of the debugger segment of the debugger of the debugger the debugger debugger program file. (This also applies to rdb, the remote debugger.) 

In addition to the client, this "outside" memory is-used by the read command when no specific address was provided. The debugger’s internal memory is used for such things as storing procedures and aliases, user variables, and stack frames when executing procedures and expanding aliases. Finally the ! (shell) command uses this "outside" memory. 

If you find that the mix of debugger memory and client memory does not suit you, either because the debugger takes too much (the client can’t load or reports that it’s "out of memory" somehow), or because the debugger takes too little (the debugger reports "out of memory" when you load symbols or execute procedures), you can change this variable. 

i 1 

| 1 | | sy q { : j 

_ q 4 j 1 j 7 a 7 : f = : q | : q 

The variable controls the debugger’s memory usage by controlling how much of the initial block the debugger keeps, and how much it returns to TOS: 

**==> picture [336 x 142] intentionally omitted <==**

**----- Start of picture text -----**<br>
||||||||||
|---|---|---|---|---|---|---|---|---|
|_VALUE|MEANING|
|-1|Keep|the whole|block.|Not|very|useful|for a|debugger.|
|0|Keep|only a|bare minimum.|Not|likely|to|last|long.|
|1|Keep|1/4|of the|block,|free|3/4|for|clients.|
|2|Keep|1/2.|.|
|3|Keep|3/4,|free|only|1/4|for|clients.|
|+other|Positive numbers keep that many bytes|exactly.|
|-other|Negative|numbers|return that many|bytes|exactly.|

**----- End of picture text -----**<br>


The first two values (-1 and 0) are not likely to be useful. If the debugger keeps all of memory, there isn’t any left for the client. If the debugger keeps hardly any memory, it might not have enough to keep track of its internal data structures. 

For a local debugger (not remote debugging), a value of 1 is usually right. This leaves lots of room for the client, but the debugger keeps enough for symbols, procedures and the like. If you have a great many symbols and a small program, you might need to bump this up to 2. 

For a remote debugger, 2 or 3 are usually good enough. A remote debugger uses the memory it keeps the same way as a local one, but the external memory is used only for the ! (shell) command. If you have a great many symbols, -1 might even be necessary, but in that case you will not be able to use the shell command. 

**==> picture [1 x 9] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


8-6 

= a 4 

; = 4 Pf g 

F .P| g 

fd f 1 j : | 

BO] 

| 

You configure the debugger by actually changing the program file on disk. Once the debugger has started, it’s too late for that debugger. Here is an example debugger session where the user creates a new debugger program file (called "DB3.TOS") which has the value 3 in this control variable: 

A : read db.tos B Done. Start=17D240, size=27DC2 Cc : sl (*rwstart + 1C + (lpeek (*rwstart + 2))) D 196340: 00000001 3 E 196344: XxXXXxxx . F : write db3.tos G Done. Start=17D240, size=27DC2 H : exit 

On line A, the user reads the executable file in. The debugger reports the result on line B. Line C is an s (memory set) command: look at the complex expression carefully, and you'll see that the address is ultimately the first longword of the data segment. (Or just type it in as shown: it'll work even if you don’t understand it.) 

8-7 

. CHAPTER 9 { REMOTE DEBUGGING | You can use Db as a remote debugger. This means that you can have the main body of q the debugger (the head) on one machine (the master), and a little bit of the debugger (the . # stub) plus the program you are debugging (the client) on another machine (the slave). a The advantages are that the debugger doesn't use up the slave’s memory and other Fj resources (screen, keyboard, disk), and the program being tested doesn’t put the debugger = machine (presumably the one with all your files on the hard disk) at risk. Also, there are = no restrictions in terms of GEMDOS use between the client and the debugger, since there | are two machines and possibly two GEMDOSes. Finally, you can use the debugger to 1 debug an operating system: on one machine, you would need a working GEMDOS to load 4 the debugger, but when remote debugging you can actually debug the OS as it boots, and , | you can set breakpoints in interrupt handlers. | When remote debugging, the master (the machine with the bulk of the debugger) : ; communicates with the slave (the machine with the stub and client) through a PY bidirectional connection. ‘ To use remote debugging, you have to load the stub into the slave machine. There are 4 two ways to do this: you can start a program containing the stub which initializes itself , and then loads your client program, or you can arrange for the stub to be resident in the , | machine and then load the client the way you do any other program. In both cases, you run the remote debugger head, rdbxxx, on the master machine. ‘ j The first method involves using the program "STUB.TTP" on the slave. method involves using the program "STUB.TTP" on the slave. involves using the program "STUB.TTP" on the slave. using the program "STUB.TTP" on the slave. the program "STUB.TTP" on the slave. program "STUB.TTP" on the slave. "STUB.TTP" on the slave. on the slave. the slave. slave. This program program takes rr 4 the name of the client name of the client of the client client (and any arguments any arguments arguments to it) it) as command-line arguments, command-line arguments, arguments, loads the the : j stub, then loads the client. then loads the client. loads the client. the client. client. When the client the client client is loaded and ready, the stub sends a message loaded and ready, the stub sends a message and ready, the stub sends a message ready, the stub sends a message the stub sends a message sends a message a message message F | to the head. the head. head. Then, you debug the client as usual. you debug the client as usual. the client as usual. client as usual. as usual. When the client terminates, the client terminates, client terminates, terminates, the stub stub f 4 sends another message to the head. another message to the head. message to the head. to the head. the head. head. If you use the exit command on the head, the stub use the exit command on the head, the stub the exit command on the head, the stub exit command on the head, the stub command on the head, the stub on the head, the stub the head, the stub head, the stub the stub stub . | will be told to exit as well. be told to exit as well. told to exit as well. to exit as well. exit as well. as well. well. It terminates the client, unloads the stub, and both machines terminates the client, unloads the stub, and both machines the client, unloads the stub, and both machines client, unloads the stub, and both machines unloads the stub, and both machines the stub, and both machines stub, and both machines and both machines both machines machines Rd will return return to the desktop or shell. shell. 4 | The second method requires second method requires method requires requires that you you establish a resident stub. resident stub. stub. This can be done by can be done by done by by , 4 running a “terminate a “terminate and stay resident” program stay resident” program program (called "STUBRES.PRG") "STUBRES.PRG") on the slave the slave slave ; | machine. 4 | When remote debugging using the resident remote debugging using the resident debugging using the resident using the resident resident stub, you will not get messages when you will not get messages when will not get messages when not get messages when get messages when messages when when . programs start up. start up. up. In all other respects, all other respects, other respects, respects, the stub stub is active (i.e. it still informs the head informs the head the head head of bus errors, etc.). You have to stop to stop stop the slave slave (with the stop button) button) and explicitly enable enable L j client-startup reporting with with the command exec command exec exec on. 

**==> picture [2 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
|<br>**----- End of picture text -----**<br>


The first method involves using the program "STUB.TTP" on the slave. method involves using the program "STUB.TTP" on the slave. involves using the program "STUB.TTP" on the slave. using the program "STUB.TTP" on the slave. the program "STUB.TTP" on the slave. program "STUB.TTP" on the slave. "STUB.TTP" on the slave. on the slave. the slave. slave. This program program takes the name of the client name of the client of the client client (and any arguments any arguments arguments to it) it) as command-line arguments, command-line arguments, arguments, loads the the stub, then loads the client. then loads the client. loads the client. the client. client. When the client the client client is loaded and ready, the stub sends a message loaded and ready, the stub sends a message and ready, the stub sends a message ready, the stub sends a message the stub sends a message sends a message a message message to the head. the head. head. Then, you debug the client as usual. you debug the client as usual. the client as usual. client as usual. as usual. When the client terminates, the client terminates, client terminates, terminates, the stub stub sends another message to the head. another message to the head. message to the head. to the head. the head. head. If you use the exit command on the head, the stub use the exit command on the head, the stub the exit command on the head, the stub exit command on the head, the stub command on the head, the stub on the head, the stub the head, the stub head, the stub the stub stub will be told to exit as well. be told to exit as well. told to exit as well. to exit as well. exit as well. as well. well. It terminates the client, unloads the stub, and both machines terminates the client, unloads the stub, and both machines the client, unloads the stub, and both machines client, unloads the stub, and both machines unloads the stub, and both machines the stub, and both machines stub, and both machines and both machines both machines machines will return return to the desktop or shell. shell. 

**==> picture [1 x 2] intentionally omitted <==**

**----- Start of picture text -----**<br>
:<br>**----- End of picture text -----**<br>


The second method requires second method requires method requires requires that you you establish a resident stub. resident stub. stub. This can be done by can be done by done by by running a “terminate a “terminate and stay resident” program stay resident” program program (called "STUBRES.PRG") "STUBRES.PRG") on the slave the slave slave machine. When remote debugging using the resident remote debugging using the resident debugging using the resident using the resident resident stub, you will not get messages when you will not get messages when will not get messages when not get messages when get messages when messages when when programs start up. start up. up. In all other respects, all other respects, other respects, respects, the stub stub is active (i.e. it still informs the head informs the head the head head of bus errors, etc.). You have to stop to stop stop the slave slave (with the stop button) button) and explicitly enable enable client-startup reporting with with the command exec command exec exec on. 

4 

9-1 

j 

When remote debugging, the normal cycle is like this: The user starts Brdb on the master } | machine, then starts the client on the slave machine, either with STUB.TTP or after j ' executing STUBRES.PRG. The head simply waits for the first activity from the stub. : Eventually, the stub sends a message to the head (e.g. CLIENT, STOP BUTTON, BUS = ERROR) and waits for the head to send it instructions. In response to commands from the a user, the head sends instructions to the stub (e.g. a user command "dump" means the & head has to ask for the contents of the client’s memory from the stub). When the head ; q sends a command to the stub, it waits for the reply before doing anything else. Usually, 1 7 the replies come quickly; a one-instruction trace, for instance, takes only a fraction of a & ' millisecond to execute. When the reply comes, the head can continue its business. | J a 4 : On the other hand, the reply may be a long time away, or may never come: consider a F 4 a ' the(go) head commandwould whichbe waiting leads theforever client to intofind anout infinite what the loop.resultTheof reply the "go" will was. never come, and 4| i For this reason, the debugger does not wait forever for trace/go commands to finish. & q After about 10 seconds, the message "Waiting . .. Press ~C to stop waiting." appears. The = 4 head goes on waiting for the stub to respond, but if you hit * C (control-C), the head will q ; a| stop waiting and return you to the prompt. The client is still running, and the effect is like a continue command. J | The continue command causes the head to tell the stub to run the client like a "go" j j command, but it doesn’t wait for a reply. | c- When the slave is busy running the client, either because of a continue command or 7 q because a go command didn’t reply and was stopped with ~C, the debugger returns you | j to the command prompt. You can continue issuing debugger commands. Naturally, since P| | the slave is busy running the client, you can’t issue any commands which need to access 4 the stub. This leaves only a couple of useful commands: the symbol-table commands 4 : getsym, where and ?, and the expression-type commands (where you type an expression and see the answer). ’ = : One command which is especially useful is quit, which, when remote debugging, doesn’t 4 touch the slave at all, but returns the master to the desktop. If you want to let the client id | run and then leave the debugger, just type continue and then quit. The client will ; : communicationscontinue to run. withYou can eventhe stub when re-enterthe theclient debugger,stops. and it will reestablish g| | : [Sometimes, the head and stub cannot reestablish communications, because they are out 3 \ of synchronization. When this happens, you have to reset the client, hit ~C on the ‘ debugger and say quit, and start over.] 3 

9-2 

4 a : a & = 

, 

If you issue a command which needs to use the stub, but the slave is busy running the client, you will get the message "You must stop the client and use the wait command." This is how you resynchronize the head and the stub. Where continue issues a command and doesn’t wait for the reply, wait waits for a reply without issuing a command. What it gets might be a repetition of the reply to the previous command, or it might be a new reply (as after a continue or timeout). 

9-3 

