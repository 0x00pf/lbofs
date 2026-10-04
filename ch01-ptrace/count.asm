	global _start

	section .text
_start:
	mov rax, 1
	mov rdi, 1
	mov rsi, msg
	mov rdx, msg_len
	syscall

	xor rbx,rbx
b0:	
	mov r8, rbx
	and r8, 0xF0
	shr r8, 4
	call print_hex	

	mov r8, rbx
	and r8, 0x0F
	call print_hex

	mov r8, 16
	call print_hex
	
	mov rax, 35
	lea     rdi, [rel delay]
        xor     rsi, rsi
	syscall

	inc rbx
	and rbx, 0xFF
	
	jmp b0
	
print_hex:
	mov rax, 1
	mov rdi, 1
	mov rdx, 1
	lea rsi, [tbl + r8]
	syscall
	ret
	
section .data
	msg	   db "Counter Program", 0x0a
	msg_len	   EQU $-msg
	tbl	   db "0123456789ABCDEF", 0x0a
        delay      dq 1
        delay_nsec dq 0
	
