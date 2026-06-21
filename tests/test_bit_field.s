	.text
	.file	"main"
	.globl	main                            # -- Begin function main
	.p2align	4, 0x90
	.type	main,@function
main:                                   # @main
	.cfi_startproc
# %bb.0:                                # %entry
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register %rbp
	subq	$16, %rsp
	movq	$0, -16(%rbp)
	movb	$5, -1(%rbp)
	movb	$1, %al
	testb	%al, %al
	jne	.LBB0_2
# %bb.1:                                # %then
	incq	-16(%rbp)
.LBB0_2:                                # %ifmerge
	movzbl	-1(%rbp), %ecx
	andb	$7, %cl
	orb	$-120, %cl
	movb	%cl, -1(%rbp)
	testb	%al, %al
	jne	.LBB0_4
# %bb.3:                                # %then11
	addq	$2, -16(%rbp)
.LBB0_4:                                # %ifmerge12
	movzbl	-1(%rbp), %eax
	andl	$7, %eax
	cmpl	$5, %eax
	je	.LBB0_6
# %bb.5:                                # %then21
	addq	$4, -16(%rbp)
.LBB0_6:                                # %ifmerge22
	movq	%rsp, %rax
	leaq	-16(%rax), %rsp
	movb	$1, -16(%rax)
	movb	$1, %cl
	testb	%cl, %cl
	jne	.LBB0_8
# %bb.7:                                # %then34
	addq	$8, -16(%rbp)
.LBB0_8:                                # %ifmerge35
	movq	%rsp, %rdx
	leaq	-16(%rdx), %rax
	movq	%rax, %rsp
	movb	$-41, -16(%rdx)
	testb	%cl, %cl
	jne	.LBB0_10
# %bb.9:                                # %then53
	addq	$16, -16(%rbp)
.LBB0_10:                               # %ifmerge54
	movzbl	(%rax), %ecx
	shrb	$2, %cl
	andb	$7, %cl
	cmpb	$5, %cl
	je	.LBB0_12
# %bb.11:                               # %then64
	addq	$32, -16(%rbp)
.LBB0_12:                               # %ifmerge65
	movzbl	(%rax), %ecx
	shrb	$5, %cl
	cmpb	$6, %cl
	je	.LBB0_14
# %bb.13:                               # %then75
	addq	$64, -16(%rbp)
.LBB0_14:                               # %ifmerge76
	andb	$-4, (%rax)
	movb	$1, %al
	testb	%al, %al
	jne	.LBB0_16
# %bb.15:                               # %then88
	subq	$-128, -16(%rbp)
.LBB0_16:                               # %ifmerge89
	movq	%rsp, %rdx
	leaq	-16(%rdx), %rcx
	movq	%rcx, %rsp
	movabsq	$145249953336295679, %rsi       # imm = 0x2040810204080FF
	movq	%rsi, -16(%rdx)
	testb	%al, %al
	jne	.LBB0_18
# %bb.17:                               # %then121
	addq	$256, -16(%rbp)                 # imm = 0x100
.LBB0_18:                               # %ifmerge122
	cmpb	$64, 2(%rcx)
	je	.LBB0_20
# %bb.19:                               # %then131
	addq	$512, -16(%rbp)                 # imm = 0x200
.LBB0_20:                               # %ifmerge132
	movq	-16(%rbp), %rax
	movq	%rbp, %rsp
	popq	%rbp
	.cfi_def_cfa %rsp, 8
	retq
.Lfunc_end0:
	.size	main, .Lfunc_end0-main
	.cfi_endproc
                                        # -- End function
	.section	".note.GNU-stack","",@progbits
