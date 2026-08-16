	.file	"keyboard.cpp"
	.text
/APP
	.section .note.GNU-stack,"",@progbits
/NO_APP
	.p2align 4
	.type	_ZN7drivers8keyboard16process_scancodeEh.part.0, @function
_ZN7drivers8keyboard16process_scancodeEh.part.0:
	pushq	%rbp
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L17g_extended_prefixE(%rip), %r10d
	movl	%edi, %edx
	notl	%edx
	shrb	$7, %dl
	movq	%rsp, %rbp
	pushq	%r13
	pushq	%r12
	movl	%edi, %r12d
	pushq	%rbx
	andl	$127, %r12d
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L17g_extended_prefixE(%rip)
	testb	%r10b, %r10b
	je	.L2
	leal	-28(%r12), %eax
	cmpb	$65, %al
	ja	.L3
	movzbl	%al, %eax
	jmp	*.L5(,%rax,8)
	.section	.rodata
	.align 8
	.align 4
.L5:
	.quad	.L135
	.quad	.L20
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L136
	.quad	.L3
	.quad	.L3
	.quad	.L137
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L17
	.quad	.L16
	.quad	.L15
	.quad	.L3
	.quad	.L14
	.quad	.L3
	.quad	.L13
	.quad	.L3
	.quad	.L12
	.quad	.L11
	.quad	.L10
	.quad	.L9
	.quad	.L8
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L7
	.quad	.L6
	.quad	.L4
	.text
	.p2align 4,,10
	.p2align 3
.L2:
	cmpb	$88, %r12b
	ja	.L3
	movzbl	%r12b, %eax
	jmp	*.L23(,%rax,8)
	.section	.rodata
	.align 8
	.align 4
.L23:
	.quad	.L3
	.quad	.L138
	.quad	.L139
	.quad	.L92
	.quad	.L91
	.quad	.L90
	.quad	.L89
	.quad	.L88
	.quad	.L87
	.quad	.L86
	.quad	.L85
	.quad	.L84
	.quad	.L83
	.quad	.L82
	.quad	.L81
	.quad	.L80
	.quad	.L79
	.quad	.L78
	.quad	.L77
	.quad	.L76
	.quad	.L75
	.quad	.L74
	.quad	.L73
	.quad	.L72
	.quad	.L71
	.quad	.L70
	.quad	.L69
	.quad	.L68
	.quad	.L67
	.quad	.L66
	.quad	.L65
	.quad	.L64
	.quad	.L63
	.quad	.L62
	.quad	.L61
	.quad	.L60
	.quad	.L59
	.quad	.L58
	.quad	.L57
	.quad	.L56
	.quad	.L55
	.quad	.L54
	.quad	.L53
	.quad	.L52
	.quad	.L51
	.quad	.L50
	.quad	.L49
	.quad	.L48
	.quad	.L47
	.quad	.L46
	.quad	.L45
	.quad	.L44
	.quad	.L43
	.quad	.L42
	.quad	.L41
	.quad	.L40
	.quad	.L39
	.quad	.L38
	.quad	.L37
	.quad	.L36
	.quad	.L35
	.quad	.L34
	.quad	.L33
	.quad	.L32
	.quad	.L31
	.quad	.L30
	.quad	.L29
	.quad	.L28
	.quad	.L27
	.quad	.L26
	.quad	.L25
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L3
	.quad	.L24
	.quad	.L22
	.text
.L53:
	movb	%dl, _ZN7drivers8keyboard12_GLOBAL__N_1L12g_left_shiftE(%rip)
	movl	$26, %eax
	movl	$42, %r11d
.L93:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L12g_left_shiftE(%rip), %esi
	movl	$1, %ebx
	testb	%sil, %sil
	jne	.L98
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L13g_right_shiftE(%rip), %esi
	movzbl	%sil, %ebx
.L98:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L14g_left_controlE(%rip), %r9d
	movl	$1, %r8d
	testb	%r9b, %r9b
	jne	.L99
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L15g_right_controlE(%rip), %r9d
	movl	%r9d, %r8d
.L99:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L10g_left_altE(%rip), %ecx
	movl	$1, %edi
	testb	%cl, %cl
	jne	.L100
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L11g_right_altE(%rip), %edi
.L100:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L11g_caps_lockE(%rip), %r13d
	cmpw	$34, %ax
	ja	.L101
	movzbl	CSWTCH.61(%rax), %ecx
	testb	%cl, %cl
	je	.L101
	testb	%r9b, %r9b
	jne	.L175
	leal	-32(%rcx), %eax
	cmpb	%sil, %r13b
	cmovne	%eax, %ecx
	.p2align 4
	.p2align 3
.L103:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L11g_caps_lockE(%rip), %eax
	movzwl	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_headE(%rip), %esi
	movzwl	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_tailE(%rip), %r13d
	movl	%esi, %r9d
	subl	%r13d, %r9d
	cmpw	$127, %r9w
	ja	.L176
	sall	$8, %eax
	movq	%rsi, %r9
	movzbl	%dl, %edx
	addl	$1, %esi
	orl	%ebx, %eax
	andl	$127, %r9d
	sall	$8, %eax
	leaq	(%r9,%r9,4), %r13
	orl	%r10d, %eax
	movb	%cl, _ZN7drivers8keyboard12_GLOBAL__N_1L8g_eventsE+2(%r13,%r13)
	sall	$8, %eax
	movb	%r12b, _ZN7drivers8keyboard12_GLOBAL__N_1L8g_eventsE+3(%r13,%r13)
	orl	%edx, %eax
	movb	%r8b, _ZN7drivers8keyboard12_GLOBAL__N_1L8g_eventsE+8(%r13,%r13)
	movw	%r11w, _ZN7drivers8keyboard12_GLOBAL__N_1L8g_eventsE(%r13,%r13)
	movl	%eax, _ZN7drivers8keyboard12_GLOBAL__N_1L8g_eventsE+4(%r13,%r13)
	movb	%dil, _ZN7drivers8keyboard12_GLOBAL__N_1L8g_eventsE+9(%r13,%r13)
	movw	%si, _ZN7drivers8keyboard12_GLOBAL__N_1L6g_headE(%rip)
	popq	%rbx
	popq	%r12
	popq	%r13
	popq	%rbp
	ret
.L41:
	movb	%dl, _ZN7drivers8keyboard12_GLOBAL__N_1L13g_right_shiftE(%rip)
	movl	$38, %eax
	movl	$54, %r11d
	jmp	.L93
	.p2align 4,,10
	.p2align 3
.L101:
	cmpw	$75, %r11w
	ja	.L104
	movzwl	%r11w, %eax
	jmp	*.L106(,%rax,8)
	.section	.rodata
	.align 8
	.align 4
.L106:
	.quad	.L104
	.quad	.L131
	.quad	.L130
	.quad	.L129
	.quad	.L128
	.quad	.L127
	.quad	.L126
	.quad	.L125
	.quad	.L124
	.quad	.L123
	.quad	.L122
	.quad	.L121
	.quad	.L120
	.quad	.L119
	.quad	.L143
	.quad	.L118
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L117
	.quad	.L116
	.quad	.L107
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L115
	.quad	.L114
	.quad	.L113
	.quad	.L104
	.quad	.L112
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L111
	.quad	.L110
	.quad	.L105
	.quad	.L104
	.quad	.L109
	.quad	.L104
	.quad	.L108
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L104
	.quad	.L107
	.quad	.L104
	.quad	.L105
	.text
	.p2align 4,,10
	.p2align 3
.L175:
	subl	$96, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L176:
	lock addq	$1, _ZN7drivers8keyboard12_GLOBAL__N_1L16g_dropped_eventsE(%rip)
	popq	%rbx
	popq	%r12
	popq	%r13
	popq	%rbp
	ret
	.p2align 4,,10
	.p2align 3
.L104:
	xorl	%ecx, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L3:
	movl	%r10d, %ecx
	movl	$65520, %eax
	xorl	%r11d, %r11d
	xorl	$1, %ecx
.L96:
	cmpw	$58, %r11w
	jne	.L93
	testb	%cl, %cl
	je	.L93
	testb	%dil, %dil
	js	.L97
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L15g_caps_key_downE(%rip), %ecx
	testb	%cl, %cl
	jne	.L97
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L11g_caps_lockE(%rip), %ecx
	xorl	$1, %ecx
	movb	%cl, _ZN7drivers8keyboard12_GLOBAL__N_1L11g_caps_lockE(%rip)
.L97:
	movb	%dl, _ZN7drivers8keyboard12_GLOBAL__N_1L15g_caps_key_downE(%rip)
	movl	$58, %r11d
	jmp	.L93
	.p2align 4,,10
	.p2align 3
.L107:
	movl	$10, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L105:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$-16, %ecx
	addl	$63, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L116:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$-32, %ecx
	addl	$125, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L113:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$-30, %ecx
	addl	$126, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L112:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$-32, %ecx
	addl	$124, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L118:
	movl	$9, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L117:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$-32, %ecx
	addl	$123, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L124:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$17, %ecx
	addl	$38, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L123:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$14, %ecx
	addl	$42, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L122:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$17, %ecx
	addl	$40, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L121:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$7, %ecx
	addl	$41, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L120:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$-50, %ecx
	addl	$95, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L119:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$18, %ecx
	addl	$43, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L115:
	movl	$59, %ecx
	subl	%esi, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L114:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$5, %ecx
	addl	$34, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L108:
	movl	$32, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L129:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$-14, %ecx
	addl	$64, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L128:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$16, %ecx
	addl	$35, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L127:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$16, %ecx
	addl	$36, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L126:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$16, %ecx
	addl	$37, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L111:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$-16, %ecx
	addl	$60, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L110:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$-16, %ecx
	addl	$62, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L130:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$16, %ecx
	addl	$33, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L125:
	cmpb	$1, %sil
	sbbl	%ecx, %ecx
	andl	$-40, %ecx
	addl	$94, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L143:
	movl	$8, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L109:
	movl	$42, %ecx
	jmp	.L103
	.p2align 4,,10
	.p2align 3
.L131:
	movl	$27, %ecx
	jmp	.L103
.L8:
	xorl	%ecx, %ecx
	movl	$70, %eax
	movl	$86, %r11d
	jmp	.L96
.L7:
	xorl	%ecx, %ecx
	movl	$71, %eax
	movl	$87, %r11d
	jmp	.L96
.L6:
	xorl	%ecx, %ecx
	movl	$72, %eax
	movl	$88, %r11d
	jmp	.L96
.L4:
	xorl	%ecx, %ecx
	movl	$73, %eax
	movl	$89, %r11d
	jmp	.L96
.L20:
	movb	%dl, _ZN7drivers8keyboard12_GLOBAL__N_1L15g_right_controlE(%rip)
	movl	$58, %eax
	movl	$74, %r11d
	jmp	.L93
.L16:
	xorl	%ecx, %ecx
	movl	$62, %eax
	movl	$78, %r11d
	jmp	.L96
.L15:
	xorl	%ecx, %ecx
	movl	$63, %eax
	movl	$79, %r11d
	jmp	.L96
.L14:
	xorl	%ecx, %ecx
	movl	$64, %eax
	movl	$80, %r11d
	jmp	.L96
.L13:
	xorl	%ecx, %ecx
	movl	$65, %eax
	movl	$81, %r11d
	jmp	.L96
.L12:
	xorl	%ecx, %ecx
	movl	$66, %eax
	movl	$82, %r11d
	jmp	.L96
.L11:
	xorl	%ecx, %ecx
	movl	$67, %eax
	movl	$83, %r11d
	jmp	.L96
.L10:
	xorl	%ecx, %ecx
	movl	$68, %eax
	movl	$84, %r11d
	jmp	.L96
.L9:
	xorl	%ecx, %ecx
	movl	$69, %eax
	movl	$85, %r11d
	jmp	.L96
.L135:
	movl	$57, %eax
	movl	$73, %r11d
	jmp	.L93
.L136:
	movl	$75, %r11d
	xorl	%ecx, %ecx
	movl	$59, %eax
	jmp	.L96
.L137:
	movb	%dl, _ZN7drivers8keyboard12_GLOBAL__N_1L11g_right_altE(%rip)
	movl	$60, %eax
	movl	$76, %r11d
	jmp	.L93
.L17:
	xorl	%ecx, %ecx
	movl	$61, %eax
	movl	$77, %r11d
	jmp	.L96
.L22:
	movl	$1, %ecx
	movl	$56, %eax
	movl	$72, %r11d
	jmp	.L96
.L25:
	movl	$1, %ecx
	movl	$54, %eax
	movl	$70, %r11d
	jmp	.L96
.L24:
	movl	$1, %ecx
	movl	$55, %eax
	movl	$71, %r11d
	jmp	.L96
.L91:
	movl	$1, %ecx
	movl	$65524, %eax
	movl	$4, %r11d
	jmp	.L96
.L90:
	movl	$1, %ecx
	movl	$65525, %eax
	movl	$5, %r11d
	jmp	.L96
.L89:
	movl	$1, %ecx
	movl	$65526, %eax
	movl	$6, %r11d
	jmp	.L96
.L88:
	movl	$1, %ecx
	movl	$65527, %eax
	movl	$7, %r11d
	jmp	.L96
.L87:
	movl	$1, %ecx
	movl	$65528, %eax
	movl	$8, %r11d
	jmp	.L96
.L86:
	movl	$1, %ecx
	movl	$65529, %eax
	movl	$9, %r11d
	jmp	.L96
.L85:
	movl	$1, %ecx
	movl	$65530, %eax
	movl	$10, %r11d
	jmp	.L96
.L84:
	movl	$1, %ecx
	movl	$65531, %eax
	movl	$11, %r11d
	jmp	.L96
.L83:
	movl	$1, %ecx
	movl	$65532, %eax
	movl	$12, %r11d
	jmp	.L96
.L82:
	movl	$1, %ecx
	movl	$65533, %eax
	movl	$13, %r11d
	jmp	.L96
.L81:
	movl	$1, %ecx
	movl	$65534, %eax
	movl	$14, %r11d
	jmp	.L96
.L80:
	movl	$1, %ecx
	movl	$65535, %eax
	movl	$15, %r11d
	jmp	.L96
.L79:
	movl	$1, %ecx
	xorl	%eax, %eax
	movl	$16, %r11d
	jmp	.L96
.L78:
	movl	$1, %ecx
	movl	$1, %eax
	movl	$17, %r11d
	jmp	.L96
.L77:
	movl	$1, %ecx
	movl	$2, %eax
	movl	$18, %r11d
	jmp	.L96
.L76:
	movl	$1, %ecx
	movl	$3, %eax
	movl	$19, %r11d
	jmp	.L96
.L42:
	movl	$1, %ecx
	movl	$37, %eax
	movl	$53, %r11d
	jmp	.L96
.L40:
	movl	$1, %ecx
	movl	$39, %eax
	movl	$55, %r11d
	jmp	.L96
.L39:
	movb	%dl, _ZN7drivers8keyboard12_GLOBAL__N_1L10g_left_altE(%rip)
	movl	$40, %eax
	movl	$56, %r11d
	jmp	.L93
.L38:
	movl	$1, %ecx
	movl	$41, %eax
	movl	$57, %r11d
	jmp	.L96
.L37:
	movl	$1, %ecx
	movl	$42, %eax
	movl	$58, %r11d
	jmp	.L96
.L36:
	movl	$1, %ecx
	movl	$43, %eax
	movl	$59, %r11d
	jmp	.L96
.L35:
	movl	$1, %ecx
	movl	$44, %eax
	movl	$60, %r11d
	jmp	.L96
.L34:
	movl	$1, %ecx
	movl	$45, %eax
	movl	$61, %r11d
	jmp	.L96
.L33:
	movl	$1, %ecx
	movl	$46, %eax
	movl	$62, %r11d
	jmp	.L96
.L32:
	movl	$1, %ecx
	movl	$47, %eax
	movl	$63, %r11d
	jmp	.L96
.L31:
	movl	$1, %ecx
	movl	$48, %eax
	movl	$64, %r11d
	jmp	.L96
.L30:
	movl	$1, %ecx
	movl	$49, %eax
	movl	$65, %r11d
	jmp	.L96
.L29:
	movl	$1, %ecx
	movl	$50, %eax
	movl	$66, %r11d
	jmp	.L96
.L28:
	movl	$1, %ecx
	movl	$51, %eax
	movl	$67, %r11d
	jmp	.L96
.L27:
	movl	$1, %ecx
	movl	$52, %eax
	movl	$68, %r11d
	jmp	.L96
.L26:
	movl	$1, %ecx
	movl	$53, %eax
	movl	$69, %r11d
	jmp	.L96
.L75:
	movl	$1, %ecx
	movl	$4, %eax
	movl	$20, %r11d
	jmp	.L96
.L74:
	movl	$1, %ecx
	movl	$5, %eax
	movl	$21, %r11d
	jmp	.L96
.L73:
	movl	$1, %ecx
	movl	$6, %eax
	movl	$22, %r11d
	jmp	.L96
.L72:
	movl	$1, %ecx
	movl	$7, %eax
	movl	$23, %r11d
	jmp	.L96
.L71:
	movl	$1, %ecx
	movl	$8, %eax
	movl	$24, %r11d
	jmp	.L96
.L70:
	movl	$1, %ecx
	movl	$9, %eax
	movl	$25, %r11d
	jmp	.L96
.L69:
	movl	$1, %ecx
	movl	$10, %eax
	movl	$26, %r11d
	jmp	.L96
.L68:
	movl	$1, %ecx
	movl	$11, %eax
	movl	$27, %r11d
	jmp	.L96
.L67:
	movl	$1, %ecx
	movl	$12, %eax
	movl	$28, %r11d
	jmp	.L96
.L66:
	movb	%dl, _ZN7drivers8keyboard12_GLOBAL__N_1L14g_left_controlE(%rip)
	movl	$13, %eax
	movl	$29, %r11d
	jmp	.L93
.L65:
	movl	$1, %ecx
	movl	$14, %eax
	movl	$30, %r11d
	jmp	.L96
.L64:
	movl	$1, %ecx
	movl	$15, %eax
	movl	$31, %r11d
	jmp	.L96
.L63:
	movl	$1, %ecx
	movl	$16, %eax
	movl	$32, %r11d
	jmp	.L96
.L62:
	movl	$1, %ecx
	movl	$17, %eax
	movl	$33, %r11d
	jmp	.L96
.L61:
	movl	$1, %ecx
	movl	$18, %eax
	movl	$34, %r11d
	jmp	.L96
.L60:
	movl	$1, %ecx
	movl	$19, %eax
	movl	$35, %r11d
	jmp	.L96
.L59:
	movl	$1, %ecx
	movl	$20, %eax
	movl	$36, %r11d
	jmp	.L96
.L58:
	movl	$1, %ecx
	movl	$21, %eax
	movl	$37, %r11d
	jmp	.L96
.L57:
	movl	$1, %ecx
	movl	$22, %eax
	movl	$38, %r11d
	jmp	.L96
.L56:
	movl	$1, %ecx
	movl	$23, %eax
	movl	$39, %r11d
	jmp	.L96
.L55:
	movl	$1, %ecx
	movl	$24, %eax
	movl	$40, %r11d
	jmp	.L96
.L54:
	movl	$1, %ecx
	movl	$25, %eax
	movl	$41, %r11d
	jmp	.L96
.L52:
	movl	$1, %ecx
	movl	$27, %eax
	movl	$43, %r11d
	jmp	.L96
.L51:
	movl	$1, %ecx
	movl	$28, %eax
	movl	$44, %r11d
	jmp	.L96
.L50:
	movl	$1, %ecx
	movl	$29, %eax
	movl	$45, %r11d
	jmp	.L96
.L49:
	movl	$1, %ecx
	movl	$30, %eax
	movl	$46, %r11d
	jmp	.L96
.L48:
	movl	$1, %ecx
	movl	$31, %eax
	movl	$47, %r11d
	jmp	.L96
.L47:
	movl	$1, %ecx
	movl	$32, %eax
	movl	$48, %r11d
	jmp	.L96
.L46:
	movl	$1, %ecx
	movl	$33, %eax
	movl	$49, %r11d
	jmp	.L96
.L45:
	movl	$1, %ecx
	movl	$34, %eax
	movl	$50, %r11d
	jmp	.L96
.L44:
	movl	$1, %ecx
	movl	$35, %eax
	movl	$51, %r11d
	jmp	.L96
.L43:
	movl	$1, %ecx
	movl	$36, %eax
	movl	$52, %r11d
	jmp	.L96
.L138:
	movl	$65521, %eax
	movl	$1, %r11d
	jmp	.L93
.L139:
	movl	$1, %ecx
	movl	$65522, %eax
	movl	$2, %r11d
	jmp	.L96
.L92:
	movl	$1, %ecx
	movl	$65523, %eax
	movl	$3, %r11d
	jmp	.L96
	.size	_ZN7drivers8keyboard16process_scancodeEh.part.0, .-_ZN7drivers8keyboard16process_scancodeEh.part.0
	.p2align 4
	.type	_ZN7drivers8keyboard12_GLOBAL__N_1L16drain_controllerEv, @function
_ZN7drivers8keyboard12_GLOBAL__N_1L16drain_controllerEv:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%r12
	xorl	%r12d, %r12d
	pushq	%rbx
	movl	$32, %ebx
	jmp	.L184
	.p2align 4,,10
	.p2align 3
.L179:
	subq	$1, %rbx
	je	.L177
.L184:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $100, %al
# 0 "" 2
/NO_APP
	movl	%eax, %edx
	testb	$1, %al
	je	.L177
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $96, %al
# 0 "" 2
/NO_APP
	andl	$32, %edx
	jne	.L179
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L23g_pause_bytes_remainingE(%rip), %edx
	testb	%dl, %dl
	jne	.L190
	cmpb	$-31, %al
	je	.L191
	cmpb	$-32, %al
	je	.L192
	movzbl	%al, %edi
	call	_ZN7drivers8keyboard16process_scancodeEh.part.0
.L181:
	addq	$1, %r12
	subq	$1, %rbx
	jne	.L184
.L177:
	movq	%r12, %rax
	popq	%rbx
	popq	%r12
	popq	%rbp
	ret
	.p2align 4,,10
	.p2align 3
.L190:
	subl	$1, %edx
	movb	%dl, _ZN7drivers8keyboard12_GLOBAL__N_1L23g_pause_bytes_remainingE(%rip)
	jmp	.L181
	.p2align 4,,10
	.p2align 3
.L191:
	movb	$5, _ZN7drivers8keyboard12_GLOBAL__N_1L23g_pause_bytes_remainingE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L17g_extended_prefixE(%rip)
	jmp	.L181
	.p2align 4,,10
	.p2align 3
.L192:
	movb	$1, _ZN7drivers8keyboard12_GLOBAL__N_1L17g_extended_prefixE(%rip)
	jmp	.L181
	.size	_ZN7drivers8keyboard12_GLOBAL__N_1L16drain_controllerEv, .-_ZN7drivers8keyboard12_GLOBAL__N_1L16drain_controllerEv
	.p2align 4
	.globl	_ZN7drivers8keyboard10handle_irqEv
	.type	_ZN7drivers8keyboard10handle_irqEv, @function
_ZN7drivers8keyboard10handle_irqEv:
	jmp	_ZN7drivers8keyboard12_GLOBAL__N_1L16drain_controllerEv
	.size	_ZN7drivers8keyboard10handle_irqEv, .-_ZN7drivers8keyboard10handle_irqEv
	.p2align 4
	.globl	_ZN7drivers8keyboard10initializeEv
	.type	_ZN7drivers8keyboard10initializeEv, @function
_ZN7drivers8keyboard10initializeEv:
	pushq	%rbp
	movl	$1, %edi
	movq	%rsp, %rbp
	call	_ZN7drivers3pic4maskEh
	movl	$_ZN7drivers8keyboard10handle_irqEv, %esi
	movl	$1, %edi
	movw	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L6g_headE(%rip)
	movw	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L6g_tailE(%rip)
	movq	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L16g_dropped_eventsE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L12g_left_shiftE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L17g_extended_prefixE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L13g_right_shiftE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L23g_pause_bytes_remainingE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L14g_left_controlE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L15g_right_controlE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L10g_left_altE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L11g_right_altE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L11g_caps_lockE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L15g_caps_key_downE(%rip)
	call	_ZN4arch6x86_6410interrupts20register_irq_handlerEhPFvvE
	movl	%eax, %ecx
	testb	%al, %al
	jne	.L224
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L13g_initializedE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L23g_controller_configuredE(%rip)
.L194:
	movl	%ecx, %eax
	popq	%rbp
	ret
	.p2align 4,,10
	.p2align 3
.L224:
	movl	$100000, %edx
	jmp	.L195
	.p2align 4,,10
	.p2align 3
.L244:
	xorl	%eax, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $128
# 0 "" 2
/NO_APP
	subq	$1, %rdx
	je	.L198
.L195:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $100, %al
# 0 "" 2
/NO_APP
	testb	$2, %al
	jne	.L244
	movl	$-83, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $100
# 0 "" 2
/NO_APP
	movl	$32, %edx
	jmp	.L201
	.p2align 4,,10
	.p2align 3
.L199:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $96, %al
# 0 "" 2
/NO_APP
	subq	$1, %rdx
	je	.L202
.L201:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $100, %al
# 0 "" 2
/NO_APP
	testb	$1, %al
	jne	.L199
.L202:
	movl	$100000, %edx
	jmp	.L200
	.p2align 4,,10
	.p2align 3
.L245:
	xorl	%eax, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $128
# 0 "" 2
/NO_APP
	subq	$1, %rdx
	je	.L207
.L200:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $100, %al
# 0 "" 2
/NO_APP
	testb	$2, %al
	jne	.L245
	movl	$32, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $100
# 0 "" 2
/NO_APP
	movl	$100000, %edx
	jmp	.L206
	.p2align 4,,10
	.p2align 3
.L246:
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $128
# 0 "" 2
/NO_APP
	subq	$1, %rdx
	je	.L207
.L206:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $100, %al
# 0 "" 2
/NO_APP
	andl	$1, %eax
	je	.L246
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $96, %al
# 0 "" 2
/NO_APP
	movl	$100000, %edx
	movl	%eax, %edi
	jmp	.L209
	.p2align 4,,10
	.p2align 3
.L247:
	xorl	%eax, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $128
# 0 "" 2
/NO_APP
	subq	$1, %rdx
	je	.L207
.L209:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $100, %al
# 0 "" 2
/NO_APP
	testb	$2, %al
	jne	.L247
	movl	$96, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $100
# 0 "" 2
/NO_APP
	movl	$100000, %edx
	jmp	.L211
	.p2align 4,,10
	.p2align 3
.L248:
	xorl	%eax, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $128
# 0 "" 2
/NO_APP
	subq	$1, %rdx
	je	.L207
.L211:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $100, %al
# 0 "" 2
/NO_APP
	testb	$2, %al
	jne	.L248
	movl	%edi, %eax
	andl	$-82, %eax
	orl	$65, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $96
# 0 "" 2
/NO_APP
	jmp	.L204
	.p2align 4,,10
	.p2align 3
.L207:
	xorl	%ecx, %ecx
.L204:
	movl	$100000, %edx
	jmp	.L213
	.p2align 4,,10
	.p2align 3
.L249:
	xorl	%eax, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $128
# 0 "" 2
/NO_APP
	subq	$1, %rdx
	je	.L198
.L213:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $100, %al
# 0 "" 2
/NO_APP
	testb	$2, %al
	jne	.L249
	movl	$-82, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $100
# 0 "" 2
/NO_APP
	movl	$32, %edx
	testb	%cl, %cl
	jne	.L216
	jmp	.L198
	.p2align 4,,10
	.p2align 3
.L214:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $96, %al
# 0 "" 2
/NO_APP
	subq	$1, %rdx
	je	.L217
.L216:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $100, %al
# 0 "" 2
/NO_APP
	testb	$1, %al
	jne	.L214
.L217:
	movl	$3, %edi
.L215:
	movl	$100000, %edx
	jmp	.L219
	.p2align 4,,10
	.p2align 3
.L250:
	xorl	%eax, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $128
# 0 "" 2
/NO_APP
	subq	$1, %rdx
	je	.L198
.L219:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $100, %al
# 0 "" 2
/NO_APP
	testb	$2, %al
	jne	.L250
	movl	$-12, %eax
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $96
# 0 "" 2
/NO_APP
	movl	$100000, %edx
	jmp	.L221
	.p2align 4,,10
	.p2align 3
.L251:
/APP
# 52 "kernel\drivers\keyboard.cpp" 1
	outb %al, $128
# 0 "" 2
/NO_APP
	subq	$1, %rdx
	je	.L198
.L221:
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $100, %al
# 0 "" 2
/NO_APP
	andl	$1, %eax
	je	.L251
/APP
# 61 "kernel\drivers\keyboard.cpp" 1
	inb $96, %al
# 0 "" 2
/NO_APP
	cmpb	$-6, %al
	je	.L222
	cmpb	$-2, %al
	jne	.L198
	subq	$1, %rdi
	jne	.L215
.L198:
	xorl	%ecx, %ecx
.L222:
	movl	$1, %edi
	movb	%cl, _ZN7drivers8keyboard12_GLOBAL__N_1L23g_controller_configuredE(%rip)
	call	_ZN7drivers3pic6unmaskEh
	movb	%al, _ZN7drivers8keyboard12_GLOBAL__N_1L13g_initializedE(%rip)
	movl	%eax, %ecx
	testb	%al, %al
	jne	.L194
	movl	$1, %edi
	call	_ZN4arch6x86_6410interrupts22unregister_irq_handlerEh
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L13g_initializedE(%rip), %ecx
	popq	%rbp
	movl	%ecx, %eax
	ret
	.size	_ZN7drivers8keyboard10initializeEv, .-_ZN7drivers8keyboard10initializeEv
	.p2align 4
	.globl	_ZN7drivers8keyboard8shutdownEv
	.type	_ZN7drivers8keyboard8shutdownEv, @function
_ZN7drivers8keyboard8shutdownEv:
	pushq	%rbp
	movl	$1, %edi
	movq	%rsp, %rbp
	call	_ZN7drivers3pic4maskEh
	movl	$1, %edi
	call	_ZN4arch6x86_6410interrupts22unregister_irq_handlerEh
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L13g_initializedE(%rip)
	popq	%rbp
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L23g_controller_configuredE(%rip)
	ret
	.size	_ZN7drivers8keyboard8shutdownEv, .-_ZN7drivers8keyboard8shutdownEv
	.p2align 4
	.globl	_ZN7drivers8keyboard11initializedEv
	.type	_ZN7drivers8keyboard11initializedEv, @function
_ZN7drivers8keyboard11initializedEv:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L13g_initializedE(%rip), %eax
	ret
	.size	_ZN7drivers8keyboard11initializedEv, .-_ZN7drivers8keyboard11initializedEv
	.p2align 4
	.globl	_ZN7drivers8keyboard21controller_configuredEv
	.type	_ZN7drivers8keyboard21controller_configuredEv, @function
_ZN7drivers8keyboard21controller_configuredEv:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L23g_controller_configuredE(%rip), %eax
	ret
	.size	_ZN7drivers8keyboard21controller_configuredEv, .-_ZN7drivers8keyboard21controller_configuredEv
	.p2align 4
	.globl	_ZN7drivers8keyboard4pollEv
	.type	_ZN7drivers8keyboard4pollEv, @function
_ZN7drivers8keyboard4pollEv:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	subq	$24, %rsp
	call	_ZN4arch6x86_6410interrupts7enabledEv
	movl	%eax, %ebx
	call	_ZN4arch6x86_6410interrupts7disableEv
	call	_ZN7drivers8keyboard12_GLOBAL__N_1L16drain_controllerEv
	testb	%bl, %bl
	jne	.L262
	movq	-8(%rbp), %rbx
	leave
	ret
	.p2align 4,,10
	.p2align 3
.L262:
	movq	%rax, -24(%rbp)
	call	_ZN4arch6x86_6410interrupts6enableEv
	movq	-24(%rbp), %rax
	movq	-8(%rbp), %rbx
	leave
	ret
	.size	_ZN7drivers8keyboard4pollEv, .-_ZN7drivers8keyboard4pollEv
	.p2align 4
	.globl	_ZN7drivers8keyboard16process_scancodeEh
	.type	_ZN7drivers8keyboard16process_scancodeEh, @function
_ZN7drivers8keyboard16process_scancodeEh:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L23g_pause_bytes_remainingE(%rip), %eax
	testb	%al, %al
	jne	.L268
	cmpb	$-31, %dil
	je	.L269
	cmpb	$-32, %dil
	je	.L270
	movzbl	%dil, %edi
	jmp	_ZN7drivers8keyboard16process_scancodeEh.part.0
	.p2align 4,,10
	.p2align 3
.L268:
	subl	$1, %eax
	movb	%al, _ZN7drivers8keyboard12_GLOBAL__N_1L23g_pause_bytes_remainingE(%rip)
	ret
	.p2align 4,,10
	.p2align 3
.L270:
	movb	$1, _ZN7drivers8keyboard12_GLOBAL__N_1L17g_extended_prefixE(%rip)
	ret
	.p2align 4,,10
	.p2align 3
.L269:
	movb	$5, _ZN7drivers8keyboard12_GLOBAL__N_1L23g_pause_bytes_remainingE(%rip)
	movb	$0, _ZN7drivers8keyboard12_GLOBAL__N_1L17g_extended_prefixE(%rip)
	ret
	.size	_ZN7drivers8keyboard16process_scancodeEh, .-_ZN7drivers8keyboard16process_scancodeEh
	.p2align 4
	.globl	_ZN7drivers8keyboard14try_read_eventERNS0_8KeyEventE
	.type	_ZN7drivers8keyboard14try_read_eventERNS0_8KeyEventE, @function
_ZN7drivers8keyboard14try_read_eventERNS0_8KeyEventE:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	subq	$24, %rsp
	movzwl	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_tailE(%rip), %eax
	movzwl	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_headE(%rip), %edx
	cmpw	%dx, %ax
	je	.L280
.L272:
	movq	%rax, %rdx
	addl	$1, %eax
	andl	$127, %edx
	leaq	(%rdx,%rdx,4), %rdx
	addq	%rdx, %rdx
	movq	_ZN7drivers8keyboard12_GLOBAL__N_1L8g_eventsE(%rdx), %rcx
	movq	%rcx, (%rdi)
	movzwl	_ZN7drivers8keyboard12_GLOBAL__N_1L8g_eventsE+8(%rdx), %edx
	movw	%dx, 8(%rdi)
	movl	$1, %edx
	movw	%ax, _ZN7drivers8keyboard12_GLOBAL__N_1L6g_tailE(%rip)
.L271:
	movq	-8(%rbp), %rbx
	movl	%edx, %eax
	leave
	ret
	.p2align 4,,10
	.p2align 3
.L280:
	movq	%rdi, -24(%rbp)
	call	_ZN4arch6x86_6410interrupts7enabledEv
	movl	%eax, %ebx
	call	_ZN4arch6x86_6410interrupts7disableEv
	call	_ZN7drivers8keyboard12_GLOBAL__N_1L16drain_controllerEv
	testb	%bl, %bl
	movq	-24(%rbp), %rdi
	jne	.L281
.L273:
	movzwl	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_tailE(%rip), %eax
	movzwl	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_headE(%rip), %ecx
	xorl	%edx, %edx
	cmpw	%cx, %ax
	jne	.L272
	jmp	.L271
	.p2align 4,,10
	.p2align 3
.L281:
	call	_ZN4arch6x86_6410interrupts6enableEv
	movq	-24(%rbp), %rdi
	jmp	.L273
	.size	_ZN7drivers8keyboard14try_read_eventERNS0_8KeyEventE, .-_ZN7drivers8keyboard14try_read_eventERNS0_8KeyEventE
	.p2align 4
	.globl	_ZN7drivers8keyboard13try_read_charERc
	.type	_ZN7drivers8keyboard13try_read_charERc, @function
_ZN7drivers8keyboard13try_read_charERc:
	pushq	%rbp
	xorl	%eax, %eax
	movq	%rsp, %rbp
	pushq	%rbx
	movq	%rdi, %rbx
	subq	$24, %rsp
	movq	$0, -26(%rbp)
	movw	%ax, -18(%rbp)
	.p2align 4
	.p2align 3
.L288:
	leaq	-26(%rbp), %rdi
	call	_ZN7drivers8keyboard14try_read_eventERNS0_8KeyEventE
	testb	%al, %al
	je	.L282
	movzbl	-22(%rbp), %eax
	testb	%al, %al
	je	.L288
	movzbl	-24(%rbp), %edx
	testb	%dl, %dl
	je	.L288
	movb	%dl, (%rbx)
.L282:
	movq	-8(%rbp), %rbx
	leave
	ret
	.size	_ZN7drivers8keyboard13try_read_charERc, .-_ZN7drivers8keyboard13try_read_charERc
	.p2align 4
	.globl	_ZN7drivers8keyboard14pending_eventsEv
	.type	_ZN7drivers8keyboard14pending_eventsEv, @function
_ZN7drivers8keyboard14pending_eventsEv:
	movzwl	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_headE(%rip), %eax
	movzwl	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_tailE(%rip), %edx
	subl	%edx, %eax
	movzwl	%ax, %eax
	ret
	.size	_ZN7drivers8keyboard14pending_eventsEv, .-_ZN7drivers8keyboard14pending_eventsEv
	.p2align 4
	.globl	_ZN7drivers8keyboard14dropped_eventsEv
	.type	_ZN7drivers8keyboard14dropped_eventsEv, @function
_ZN7drivers8keyboard14dropped_eventsEv:
	movq	_ZN7drivers8keyboard12_GLOBAL__N_1L16g_dropped_eventsE(%rip), %rax
	ret
	.size	_ZN7drivers8keyboard14dropped_eventsEv, .-_ZN7drivers8keyboard14dropped_eventsEv
	.p2align 4
	.globl	_ZN7drivers8keyboard12clear_eventsEv
	.type	_ZN7drivers8keyboard12clear_eventsEv, @function
_ZN7drivers8keyboard12clear_eventsEv:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	subq	$8, %rsp
	call	_ZN4arch6x86_6410interrupts7enabledEv
	movl	%eax, %ebx
	call	_ZN4arch6x86_6410interrupts7disableEv
	movzwl	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_headE(%rip), %eax
	movw	%ax, _ZN7drivers8keyboard12_GLOBAL__N_1L6g_tailE(%rip)
	testb	%bl, %bl
	jne	.L297
	movq	-8(%rbp), %rbx
	leave
	ret
	.p2align 4,,10
	.p2align 3
.L297:
	movq	-8(%rbp), %rbx
	leave
	jmp	_ZN4arch6x86_6410interrupts6enableEv
	.size	_ZN7drivers8keyboard12clear_eventsEv, .-_ZN7drivers8keyboard12clear_eventsEv
	.p2align 4
	.globl	_ZN7drivers8keyboard12shift_activeEv
	.type	_ZN7drivers8keyboard12shift_activeEv, @function
_ZN7drivers8keyboard12shift_activeEv:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L12g_left_shiftE(%rip), %eax
	testb	%al, %al
	jne	.L298
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L13g_right_shiftE(%rip), %eax
.L298:
	ret
	.size	_ZN7drivers8keyboard12shift_activeEv, .-_ZN7drivers8keyboard12shift_activeEv
	.p2align 4
	.globl	_ZN7drivers8keyboard16caps_lock_activeEv
	.type	_ZN7drivers8keyboard16caps_lock_activeEv, @function
_ZN7drivers8keyboard16caps_lock_activeEv:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L11g_caps_lockE(%rip), %eax
	ret
	.size	_ZN7drivers8keyboard16caps_lock_activeEv, .-_ZN7drivers8keyboard16caps_lock_activeEv
	.p2align 4
	.globl	_ZN7drivers8keyboard14control_activeEv
	.type	_ZN7drivers8keyboard14control_activeEv, @function
_ZN7drivers8keyboard14control_activeEv:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L14g_left_controlE(%rip), %eax
	testb	%al, %al
	jne	.L301
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L15g_right_controlE(%rip), %eax
.L301:
	ret
	.size	_ZN7drivers8keyboard14control_activeEv, .-_ZN7drivers8keyboard14control_activeEv
	.p2align 4
	.globl	_ZN7drivers8keyboard10alt_activeEv
	.type	_ZN7drivers8keyboard10alt_activeEv, @function
_ZN7drivers8keyboard10alt_activeEv:
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L10g_left_altE(%rip), %eax
	testb	%al, %al
	jne	.L303
	movzbl	_ZN7drivers8keyboard12_GLOBAL__N_1L11g_right_altE(%rip), %eax
.L303:
	ret
	.size	_ZN7drivers8keyboard10alt_activeEv, .-_ZN7drivers8keyboard10alt_activeEv
	.section	.rodata
	.align 32
	.type	CSWTCH.61, @object
	.size	CSWTCH.61, 35
CSWTCH.61:
	.byte	113
	.byte	119
	.byte	101
	.byte	114
	.byte	116
	.byte	121
	.byte	117
	.byte	105
	.byte	111
	.byte	112
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	97
	.byte	115
	.byte	100
	.byte	102
	.byte	103
	.byte	104
	.byte	106
	.byte	107
	.byte	108
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	0
	.byte	122
	.byte	120
	.byte	99
	.byte	118
	.byte	98
	.byte	110
	.byte	109
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L23g_pause_bytes_remainingE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L23g_pause_bytes_remainingE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L17g_extended_prefixE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L17g_extended_prefixE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L15g_caps_key_downE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L15g_caps_key_downE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L11g_caps_lockE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L11g_caps_lockE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L11g_right_altE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L11g_right_altE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L10g_left_altE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L10g_left_altE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L15g_right_controlE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L15g_right_controlE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L14g_left_controlE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L14g_left_controlE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L13g_right_shiftE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L13g_right_shiftE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L12g_left_shiftE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L12g_left_shiftE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L23g_controller_configuredE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L23g_controller_configuredE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L13g_initializedE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L13g_initializedE,1,1
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L16g_dropped_eventsE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L16g_dropped_eventsE,8,8
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_tailE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_tailE,2,2
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_headE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L6g_headE,2,2
	.local	_ZN7drivers8keyboard12_GLOBAL__N_1L8g_eventsE
	.comm	_ZN7drivers8keyboard12_GLOBAL__N_1L8g_eventsE,1280,32
	.ident	"GCC: (GNU) 15.2.0"
