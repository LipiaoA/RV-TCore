
./test:     file format elf64-littleriscv


Disassembly of section .text:

0000000000010120 <_start>:
   10120:	0010011b          	addiw	x2,x0,1
   10124:	01f11113          	slli	x2,x2,0x1f
   10128:	010000ef          	jal	x1,10138 <main>
   1012c:	00050513          	addi	x10,x10,0
   10130:	05d00893          	addi	x17,x0,93
   10134:	00000073          	ecall

0000000000010138 <main>:
   10138:	f9010113          	addi	x2,x2,-112
   1013c:	06113423          	sd	x1,104(x2)
   10140:	06813023          	sd	x8,96(x2)
   10144:	07010413          	addi	x8,x2,112
   10148:	fe042623          	sw	x0,-20(x8)
   1014c:	fe042423          	sw	x0,-24(x8)
   10150:	fe042623          	sw	x0,-20(x8)
   10154:	0280006f          	jal	x0,1017c <main+0x44>
   10158:	fec42703          	lw	x14,-20(x8)
   1015c:	f9840793          	addi	x15,x8,-104
   10160:	00271713          	slli	x14,x14,0x2
   10164:	00f707b3          	add	x15,x14,x15
   10168:	fec42703          	lw	x14,-20(x8)
   1016c:	00e7a023          	sw	x14,0(x15)
   10170:	fec42783          	lw	x15,-20(x8)
   10174:	0017879b          	addiw	x15,x15,1
   10178:	fef42623          	sw	x15,-20(x8)
   1017c:	fec42783          	lw	x15,-20(x8)
   10180:	0007871b          	addiw	x14,x15,0
   10184:	01300793          	addi	x15,x0,19
   10188:	fce7d8e3          	bge	x15,x14,10158 <main+0x20>
   1018c:	fe042623          	sw	x0,-20(x8)
   10190:	03c0006f          	jal	x0,101cc <main+0x94>
   10194:	fec42703          	lw	x14,-20(x8)
   10198:	f9840793          	addi	x15,x8,-104
   1019c:	00271713          	slli	x14,x14,0x2
   101a0:	00f707b3          	add	x15,x14,x15
   101a4:	0007a783          	lw	x15,0(x15)
   101a8:	fec42703          	lw	x14,-20(x8)
   101ac:	0007071b          	addiw	x14,x14,0
   101b0:	00f70863          	beq	x14,x15,101c0 <main+0x88>
   101b4:	00100793          	addi	x15,x0,1
   101b8:	fef42423          	sw	x15,-24(x8)
   101bc:	0200006f          	jal	x0,101dc <main+0xa4>
   101c0:	fec42783          	lw	x15,-20(x8)
   101c4:	0017879b          	addiw	x15,x15,1
   101c8:	fef42623          	sw	x15,-20(x8)
   101cc:	fec42783          	lw	x15,-20(x8)
   101d0:	0007871b          	addiw	x14,x15,0
   101d4:	01300793          	addi	x15,x0,19
   101d8:	fae7dee3          	bge	x15,x14,10194 <main+0x5c>
   101dc:	fe842783          	lw	x15,-24(x8)
   101e0:	00078513          	addi	x10,x15,0
   101e4:	06813083          	ld	x1,104(x2)
   101e8:	06013403          	ld	x8,96(x2)
   101ec:	07010113          	addi	x2,x2,112
   101f0:	00008067          	jalr	x0,0(x1)
