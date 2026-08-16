" Vim syntax file
" Language:	ClassyC
" Maintainer:	ClassyC contributors
" Filenames:	*.cy *.c (when ft=cy)
"
" ClassyC is C11 + modern extensions (classes, String, dict, List/Map/Set,
" owned/defer, try/catch, f-strings, generics, interfaces/Any, lambdas).
" This file started from vala.vim; it has been re-tuned for ClassyC sources
" under examples/ and cy-validate/.
"
" Setup (optional):
"   autocmd BufRead,BufNewFile *.cy setfiletype cy
" or put this file where filetype detection can load it as 'cy'.

if exists("b:current_syntax")
  finish
endif

let s:cy_cpo_save = &cpo
set cpo&vim

" ── Types (C + ClassyC) ──────────────────────────────────────────────────────
" Built-in / C scalars
syn keyword cyType		void char short int long float double
syn keyword cyType		signed unsigned
syn keyword cyType		size_t ssize_t ptrdiff_t intptr_t uintptr_t
syn keyword cyType		int8_t int16_t int32_t int64_t
syn keyword cyType		uint8_t uint16_t uint32_t uint64_t
syn keyword cyType		bool _Bool
" ClassyC first-class types (capital S matters)
syn keyword cyType		String dict auto
" Common generic / library type names (also matched as generics below)
syn keyword cyType		List Map Set Any Exception
syn keyword cyType		HttpResponse Http Url Request Response
syn keyword cyType		FILE

" ── Storage / structure ──────────────────────────────────────────────────────
syn keyword cyStorage		class interface enum struct union typedef
syn keyword cyStorage		impl

" ── Control flow ─────────────────────────────────────────────────────────────
syn keyword cyRepeat		break continue do for return while
syn keyword cyConditional	else if switch
syn keyword cyLabel		case default
" for (auto x in coll) / for (auto k, v in m)
syn keyword cyRepeat		in

" ── Modifiers & ownership ────────────────────────────────────────────────────
syn keyword cyModifier		const static extern inline volatile restrict
syn keyword cyModifier		owned unowned readonly move
syn keyword cyStatement		new delete defer
syn keyword cyStatement		detach attach

" ── Constants ────────────────────────────────────────────────────────────────
syn keyword cyConstant		NULL true false

" ── Exceptions ───────────────────────────────────────────────────────────────
syn keyword cyException		try catch throw

" ── Other statements / operators-as-keywords ─────────────────────────────────
syn keyword cyStatement		this sizeof
syn keyword cyStatement		nameof typeof
" any<I>(expr) factory — keyword form of the Any erasure
syn keyword cyStatement		any

" ── Preprocessor (C-style; ClassyC is not Vala `using`) ──────────────────────
syn match  cyInclude		display "^\s*\zs#\s*include\>"
syn region cyIncluded		display contained start=+"+ skip=+\\\\\|\\"+ end=+"+
syn match  cyIncluded		display contained "<[^>]*>"
syn match  cyInclude		display "^\s*\zs#\s*include\>\s*["<]" contains=cyIncluded
syn region cyDefine		start="^\s*\zs#\s*\(define\|undef\)\>" skip="\\$" end="$" keepend contains=cyCommentL
syn region cyPreCondit		start="^\s*\zs#\s*\(if\|ifdef\|ifndef\|elif\|else\|endif\)\>" skip="\\$" end="$" keepend contains=cyCommentL
syn match  cyPreProc		display "^\s*\zs#\s*\(pragma\|line\|error\|warning\)\>"

" ── Generics: List<T>, Map<K,V>, Any<View>, List<String>* ────────────────────
" Balanced-ish angle brackets after an identifier (type/ctor position).
syn match  cyGeneric		"\<\u\w*\(\s*\)\?<\(\_[^<>;{}]\)*>"

" ── Methods / function names at call sites ───────────────────────────────────
syn match  cyMethod		"\(@\)\?\w\(\w\)*\(\s\+\)\?("he=e-1,me=e-1

" ── Operators & delimiters ───────────────────────────────────────────────────
syn match  cyOperator		display "\%(+\|-\|/\|*\|=\|\^\|&\||\|!\|>\|<\|%\|?\)=\?"
syn match  cyOperator		display "->\|\.\.\.\|::"
syn match  cyDelimiter		display "(\|)\|\[\|\]\|,\|;\|:\|{\|}"

" ── Enum-style CAPS fields after a dot: Alert.RED, Sector.VEIL ───────────────
syn match  cyEnumField		"\.\([A-Z_]\)\+\([A-Z_0-9]\)\+"hs=s+1

" ── Comments ─────────────────────────────────────────────────────────────────
syn cluster cyCommentGroup	contains=cyTodo
syn keyword cyTodo		contained TODO FIXME XXX NOTE HACK BUG

if exists("cy_comment_strings")
  syn match  cyCommentSkip		contained "^\s*\*\($\|\s\+\)"
  syn region cyCommentString		contained start=+L\=\\\@<!"+ skip=+\\\\\|\\"+ end=+"+ end=+\*/+me=s-1 contains=cySpecialChar,cyCommentSkip
  syn region cyComment2String		contained start=+L\=\\\@<!"+ skip=+\\\\\|\\"+ end=+"+ end="$" contains=cySpecialChar
  syn cluster cyCommentStringGroup	contains=cyCommentString,cyCharacter,cyNumber

  syn region cyCommentL		start="//" end="$" keepend contains=@cyCommentGroup,cyComment2String,cyCharacter,cyNumber,cySpaceError,@Spell
  syn region cyComment		matchgroup=cyCommentStart start="/\*" end="\*/" contains=@cyCommentGroup,@cyCommentStringGroup,cyCommentStartError,cySpaceError,@Spell extend fold
  syn region cyDocComment	matchgroup=cyCommentStart start="/\*\*" end="\*/" keepend contains=@cyCommentGroup,@cyCommentStringGroup,cyCommentStartError,cySpaceError,@Spell
else
  syn region cyCommentL		start="//" end="$" keepend contains=@cyCommentGroup,cySpaceError,@Spell
  syn region cyComment		matchgroup=cyCommentStart start="/\*" end="\*/" fold contains=@cyCommentGroup,cyCommentStartError,cySpaceError,@Spell
  syn region cyDocComment	matchgroup=cyCommentStart start="/\*\*" end="\*/" fold keepend contains=@cyCommentGroup,cyCommentStartError,cySpaceError,@Spell
endif

syn match  cyCommentError	display "\*/"
syn match  cyCommentStartError	display "/\*"me=e-1 contained
syn match  cyComment		"/\*\*/"

" ── #if 0 blocks ─────────────────────────────────────────────────────────────
if !exists("cy_no_if0")
  syn region cyCppOut		start="^\s*#\s*if\s\+0\+\>" end=".\@=\|$" contains=cyCppOut2 fold
  syn region cyCppOut2		contained start="0" end="^\s*#\s*\(endif\>\|else\>\|elif\>\)" contains=cySpaceError,cyCppSkip
  syn region cyCppSkip		contained start="^\s*#\s*\(if\>\|ifdef\>\|ifndef\>\)" skip="\\$" end="^\s*#\s*endif\>" contains=cySpaceError,cyCppSkip
endif

" ── Attributes (GCC / ClassyC): __attribute__((...)), [[...]] ────────────────
syn region cyAttribute		start="__attribute__\s*((" end="))" contains=cyComment,cyString keepend
syn region cyAttribute		start="\[\[" end="\]\]" contains=cyComment,cyString keepend

" ── User content / escaped id ────────────────────────────────────────────────
syn match  cyUserContent	display "@\I*"

" ── Strings, f-strings, characters, numbers ──────────────────────────────────
syn match  cySpecialError	contained "\\."
syn match  cySpecialCharError	contained "[^']"
syn match  cySpecialChar	contained +\\["\\'0abfnrtvx]+
syn match  cyFormatChar		contained +%\(%\|\([-]\)\?\([+]\)\?\([0-9]\+\)\?\(\.\)\?\([0-9]\+\)\?\(l\?[dfiu]\|ll\?[diu]\|c\|g\|hh\?[iu]\|s\|p\|z\)\)+

" Ordinary C string
syn region cyString		start=+"+ skip=+\\\\\|\\"+ end=+"+ contains=cySpecialChar,cySpecialError,cyUnicodeNumber,@Spell,cyFormatChar
" ClassyC f-string: f"Hello {user}, score={score}"  /  f'{a}+{b}'
syn match  cyFStringBrace	contained "{[^}]*}"
syn region cyFString		start=+f"+ skip=+\\\\\|\\"+ end=+"+ contains=cySpecialChar,cySpecialError,cyUnicodeNumber,cyFStringBrace,@Spell
syn region cyFString		start=+f'+ skip=+\\\\\|\\'+ end=+'+ contains=cySpecialChar,cySpecialError,cyFStringBrace,@Spell
" Multi-line-ish raw triple quotes (rare; keep for README snippets)
syn region cyVerbatimString	start=+"""+ end=+"""+ contains=@Spell,cyFormatChar

syn match  cyUnicodeNumber	+\\\(u\x\{4}\|U\x\{8}\)+ contained contains=cyUnicodeSpecifier
syn match  cyUnicodeSpecifier	+\\[uU]+ contained
syn match  cyCharacter		"'[^']*'" contains=cySpecialChar,cySpecialCharError
syn match  cyCharacter		"'\\''" contains=cySpecialChar
syn match  cyCharacter		"'[^\\]'"
syn match  cyNumber		display "\<\(0[0-7]*\|0[xX]\x\+\|\d\+\)[uUlL]*\>"
syn match  cyNumber		display "\(\<\d\+\.\d*\|\.\d\+\)\([eE][-+]\=\d\+\)\=[fFlL]\="
syn match  cyNumber		display "\<\d\+[eE][-+]\=\d\+[fFlL]\=\>"
syn match  cyNumber		display "\<\d\+\([eE][-+]\=\d\+\)\=[fFlL]\>"

" ── Lambdas: (int x) => x + 1   /   (Ship s) => s.heat ───────────────────────
" Defined after parens so the match can win over bare method calls.
syn match  cyLambdaDef		"(\_[^)]*)\s*=>"

" ── Optional trailing / tab whitespace ───────────────────────────────────────
if exists("cy_space_errors")
  if !exists("cy_no_trail_space_error")
    syn match cySpaceError	display excludenl "\s\+$"
  endif
  if !exists("cy_no_tab_space_error")
    syn match cySpaceError	display " \+\t"me=e-1
  endif
endif

" ── Sync / fold ──────────────────────────────────────────────────────────────
if exists("cy_minlines")
  let b:cy_minlines = cy_minlines
else
  let b:cy_minlines = 50
endif
exec "syn sync ccomment cyComment minlines=" . b:cy_minlines

syn region cyBlock		start="{" end="}" transparent fold

" ── Highlight links ──────────────────────────────────────────────────────────
hi def link cyType			Type
hi def link cyStorage			StorageClass
hi def link cyRepeat			Repeat
hi def link cyConditional		Conditional
hi def link cyLabel			Label
hi def link cyModifier			StorageClass
hi def link cyConstant			Constant
hi def link cyException			Exception
hi def link cyStatement			Statement
hi def link cyInclude			Include
hi def link cyIncluded			String
hi def link cyDefine			Macro
hi def link cyPreCondit			PreCondit
hi def link cyPreProc			PreProc
hi def link cyGeneric			Type
hi def link cyMethod			Function
hi def link cyLambdaDef			Function
hi def link cyOperator			Operator
hi def link cyDelimiter			Delimiter
hi def link cyEnumField			Constant

hi def link cyCommentError		Error
hi def link cyCommentStartError		Error
hi def link cySpecialError		Error
hi def link cySpecialCharError		Error
hi def link cySpaceError		Error

hi def link cyTodo			Todo
hi def link cyCommentL			cyComment
hi def link cyCommentStart		cyComment
hi def link cyCommentSkip		cyComment
hi def link cyComment			Comment
hi def link cyDocComment		Comment
hi def link cyAttribute			PreCondit

hi def link cyCommentString		cyString
hi def link cyComment2String		cyString
hi def link cyString			String
hi def link cyFString			String
hi def link cyFStringBrace		Special
hi def link cyVerbatimString		String
hi def link cyCharacter			Character
hi def link cySpecialChar		SpecialChar
hi def link cyFormatChar		SpecialChar
hi def link cyNumber			Number
hi def link cyUnicodeNumber		SpecialChar
hi def link cyUnicodeSpecifier		SpecialChar
hi def link cyUserContent		Special

if !exists("cy_no_if0")
  hi def link cyCppSkip			cyCppOut
  hi def link cyCppOut2			cyCppOut
  hi def link cyCppOut			Comment
endif

let b:current_syntax = "cy"

let &cpo = s:cy_cpo_save
unlet s:cy_cpo_save

" vim: ts=8
