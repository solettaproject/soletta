if exists("b:current_syntax")
  finish
endif

let s:cpo_save = &cpo
set cpo&vim

syn match fbpArrow '->'
syn match fbpNode display "[a-zA-Z0-9_]\+" nextgroup=fbpDecl,fbpPort
syn region fbpDecl start='(' end=')' contains=fbpType,fbpOptions
syn match fbpType contained "[^:()]\+" nextgroup=fbpOptions
"syn region fbpOptions display contained matchgroup=fbpDecl start=':' end=')'
syn match fbpOptions display contained /:[a-zA-Z0-9_=]\{-})/ms=s+1,me=e-1
syn match fbpPort " [A-Z0-9_]\+ "
syn match Comment "#.*"

hi def link fbpArrow Statement
hi def link fbpNode Normal
hi def link fbpDecl Normal
hi def link fbpType Type
hi def link fbpPort PreProc
hi def link fbpOptions String

let b:current_syntax = "fbp"

let &cpo = s:cpo_save
unlet s:cpo_save
