		.module crt0_z80_rel

		.area _CODE
		.area _HOME
		.area _CONST
		; The _GS can be blown away after startup. We don't yet
		; but we should do FIXME
		.area _GSINIT
		.area _GSFINAL
		.area _INITIALIZED
		.area _BSEG
		.area _DATA
		.area _BSS
		; note that the binary builder moves the initialized data
		; from initializer
		.area _INITIALIZER

		.globl ___stdio_init_vars
		.globl _main
		.globl _exit
		.globl _environ
		.globl ___argv
		.globl s__DATA

		.area _CODE

; start at 0x100
start:		jr start2		; must be relative
		nop
		.db 'F'
		.db 'Z'
		.db 'X'
		.db '1'

;
;	Borrowed idea from UMZIX - put the info in known places then
;	we can write "size" tools
;
;	This is confusing. SDCC doesn't produce a BSS, instead it
;	produces an INITIALIZED (which everyone else calls DATA) and a
;	DATA which everyone else would think of as BSS.
;
;	FIXME: we need to automate the load page setting
;
		.db 0x01		; page to load at
		.dw 0			; chmem ("0 - 'all'")
		; These three are set by binman
		.dw 0			; code
		.dw 0			; data
		.dw 0			; bss size
		.dw 0			; spare

start2:
;
;	On entry we are page aligned and de is our base
;
;	s__DATA is the BSS base computed by the compiler. It will get
;	modified by the relocatable binary maker in the header but not
;	in the code. Thus this is actually a pointer to our relocation
;	bytestream.
;
		ld hl,#s__DATA		; will be pre-reloc value (0 based)
		add hl,de		; hl is now the relocations
					; de is the code base
		ld b,#0			; on the code base bits
		ex de,hl		; de is relocatios as loop swaps
relnext:
		; Read each relocatin byte and zero it (because it's really
		; stolen BSS so should start zero)
		ex de,hl
		ld a,(hl)
		inc hl
		ld (hl),#0
		ex de,hl
		; 255 means done, 254 means skip 254, 253 or less means
		; skip that many and relocate (254 and reloc is encoded as
		; 254,0, long runs are 254,254,254,n...
		cp #255
		jr z, relocdone
		ld c,a
		cp #254
		jr z, relocskip
		add hl,bc
		ld a,(hl)
		add e
		ld (hl),a
		jr relnext
relocskip:	add hl,bc
		jr relnext
relocdone:
;		; At this point our calls are relocated so this will go to
		; the right place
		call gsinit

		ld hl, #4
		add hl, sp
		ld (_environ), hl
		pop de			; argc
		pop hl			; argv
		push hl
		ld (___argv), hl	; needed for stuff like err()
		push de
		call _main		; go
		push hl
		call _exit

		.area _GSINIT
;
;	FIXME: we want to work this into the C code so it's not
; automatically sucking in any of stdio at all.
;
gsinit:
		call ___stdio_init_vars
;
;	Any gsinit code from other modules will accumulate between here
;	and _GSFINAL to provide constructors and other ghastly C++isms
;
		.area _GSFINAL
		ret

		.area _DATA
_environ:	.dw 0
