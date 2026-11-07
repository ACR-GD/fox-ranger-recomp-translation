002d: c8000000       enter 0, 0
0031: 06             push es
0032: 1e             push ds
0033: 56             push si
0034: 57             push di
0035: 2ea10a00       mov ax, word ptr cs:[0xa]
0039: 8ec0           mov es, ax
003b: 33ff           xor di, di
003d: c5760a         lds si, ptr [bp + 0xa]
0040: 2ec70624000000 mov word ptr cs:[0x24], 0
0047: 2ec70626000000 mov word ptr cs:[0x26], 0
004e: 2ec70628000000 mov word ptr cs:[0x28], 0
0055: 2ec7062a000000 mov word ptr cs:[0x2a], 0
005c: 8b0c           mov cx, word ptr [si]
005e: d1e9           shr cx, 1
0060: 8b4606         mov ax, word ptr [bp + 6]
0063: 2bc1           sub ax, cx
0065: 2ea32000       mov word ptr cs:[0x20], ax
0069: 2e3b060c00     cmp ax, word ptr cs:[0xc]
006e: 7d0f           jge 0x7f
0070: 2e8b1e0c00     mov bx, word ptr cs:[0xc]
0075: 2e891e2400     mov word ptr cs:[0x24], bx
007a: 2e29062400     sub word ptr cs:[0x24], ax
007f: 0304           add ax, word ptr [si]
0081: 2e3b060e00     cmp ax, word ptr cs:[0xe]
0086: 7e09           jle 0x91
0088: 2e2b060e00     sub ax, word ptr cs:[0xe]
008d: 2ea32600       mov word ptr cs:[0x26], ax
0091: 8b4c02         mov cx, word ptr [si + 2]
0094: d1e9           shr cx, 1
0096: 8b4608         mov ax, word ptr [bp + 8]
0099: 2bc1           sub ax, cx
009b: 2ea32200       mov word ptr cs:[0x22], ax
009f: 2e3b061000     cmp ax, word ptr cs:[0x10]
00a4: 7d0f           jge 0xb5
00a6: 2e8b1e1000     mov bx, word ptr cs:[0x10]
00ab: 2e891e2800     mov word ptr cs:[0x28], bx
00b0: 2e29062800     sub word ptr cs:[0x28], ax
00b5: 034402         add ax, word ptr [si + 2]
00b8: 2e3b061200     cmp ax, word ptr cs:[0x12]
00bd: 7e09           jle 0xc8
00bf: 2e2b061200     sub ax, word ptr cs:[0x12]
00c4: 2ea32a00       mov word ptr cs:[0x2a], ax
00c8: b85000         mov ax, 0x50
00cb: 2e8b1e2200     mov bx, word ptr cs:[0x22]
00d0: 2e031e2800     add bx, word ptr cs:[0x28]
00d5: f7e3           mul bx
00d7: 2e8b1e2000     mov bx, word ptr cs:[0x20]
00dc: 2e031e2400     add bx, word ptr cs:[0x24]
00e1: d1eb           shr bx, 1
00e3: d1eb           shr bx, 1
00e5: 03c3           add ax, bx
00e7: 03f8           add di, ax
00e9: 2e893e1a00     mov word ptr cs:[0x1a], di
00ee: 8b04           mov ax, word ptr [si]
00f0: 2e2b062400     sub ax, word ptr cs:[0x24]
00f5: 2e2b062600     sub ax, word ptr cs:[0x26]
00fa: 3d0000         cmp ax, 0
00fd: 7f03           jg 0x102
00ff: e99f00         jmp 0x1a1
0102: 2ea31e00       mov word ptr cs:[0x1e], ax
0106: 8b4402         mov ax, word ptr [si + 2]
0109: 2e2b062800     sub ax, word ptr cs:[0x28]
010e: 2e2b062a00     sub ax, word ptr cs:[0x2a]
0113: 3d0000         cmp ax, 0
0116: 7f03           jg 0x11b
0118: e98600         jmp 0x1a1
011b: 2ea31c00       mov word ptr cs:[0x1c], ax
011f: 2ea12800       mov ax, word ptr cs:[0x28]
0123: f724           mul word ptr [si]
0125: 2e03062400     add ax, word ptr cs:[0x24]
012a: c57407         lds si, ptr [si + 7]
012d: 03f0           add si, ax
012f: 2ea12400       mov ax, word ptr cs:[0x24]
0133: 2e01062600     add word ptr cs:[0x26], ax
0138: bac403         mov dx, 0x3c4
013b: b002           mov al, 2
013d: ee             out dx, al
013e: 42             inc dx
013f: b011           mov al, 0x11
0141: 2e8b0e2000     mov cx, word ptr cs:[0x20]
0146: 2e030e2400     add cx, word ptr cs:[0x24]
014b: 80e103         and cl, 3
014e: d2c0           rol al, cl
0150: 2ea22c00       mov byte ptr cs:[0x2c], al
0154: 2e8b1e1c00     mov bx, word ptr cs:[0x1c]
0159: 2e8b0e1e00     mov cx, word ptr cs:[0x1e]
015e: 8a24           mov ah, byte ptr [si]
0160: 80fc00         cmp ah, 0
0163: 741b           je 0x180
0165: 2e803e140000   cmp byte ptr cs:[0x14], 0
016b: 740f           je 0x17c
016d: 2e803e140064   cmp byte ptr cs:[0x14], 0x64
0173: 7405           je 0x17a
0175: 80e444         and ah, 0x44
0178: eb02           jmp 0x17c
017a: b401           mov ah, 1
017c: ee             out dx, al
017d: 268825         mov byte ptr es:[di], ah
0180: 46             inc si
0181: d0c0           rol al, 1
0183: 3c11           cmp al, 0x11
0185: 7501           jne 0x188
0187: 47             inc di
0188: e2d4           loop 0x15e
018a: 2e03362600     add si, word ptr cs:[0x26]
018f: 2e83061a0050   add word ptr cs:[0x1a], 0x50
0195: 2e8b3e1a00     mov di, word ptr cs:[0x1a]
019a: 2ea02c00       mov al, byte ptr cs:[0x2c]
019e: 4b             dec bx
019f: 75b8           jne 0x159
01a1: 5f             pop di
01a2: 5e             pop si
01a3: 1f             pop ds
01a4: 07             pop es
01a5: c9             leave 
01a6: cb             retf 
01a7: c8000000       enter 0, 0
01ab: 06             push es
01ac: 1e             push ds
01ad: 56             push si
01ae: 57             push di
01af: 2ea10a00       mov ax, word ptr cs:[0xa]
01b3: 8ec0           mov es, ax
01b5: 33ff           xor di, di
01b7: c5760a         lds si, ptr [bp + 0xa]
01ba: 2ec70624000000 mov word ptr cs:[0x24], 0
01c1: 2ec70626000000 mov word ptr cs:[0x26], 0
01c8: 2ec70628000000 mov word ptr cs:[0x28], 0
01cf: 2ec7062a000000 mov word ptr cs:[0x2a], 0
01d6: 8b0c           mov cx, word ptr [si]
01d8: d1e9           shr cx, 1
01da: 8b4606         mov ax, word ptr [bp + 6]
01dd: 2bc1           sub ax, cx
01df: 2ea32000       mov word ptr cs:[0x20], ax
01e3: 2e3b060c00     cmp ax, word ptr cs:[0xc]
01e8: 7d0f           jge 0x1f9
01ea: 2e8b1e0c00     mov bx, word ptr cs:[0xc]
01ef: 2e891e2400     mov word ptr cs:[0x24], bx
01f4: 2e29062400     sub word ptr cs:[0x24], ax
01f9: 0304           add ax, word ptr [si]
01fb: 2e3b060e00     cmp ax, word ptr cs:[0xe]
0200: 7e09           jle 0x20b
0202: 2e2b060e00     sub ax, word ptr cs:[0xe]
0207: 2ea32600       mov word ptr cs:[0x26], ax
020b: 8b4c02         mov cx, word ptr [si + 2]
020e: d1e9           shr cx, 1
0210: 8b4608         mov ax, word ptr [bp + 8]
0213: 2bc1           sub ax, cx
0215: 2ea32200       mov word ptr cs:[0x22], ax
0219: 2e3b061000     cmp ax, word ptr cs:[0x10]
021e: 7d0f           jge 0x22f
0220: 2e8b1e1000     mov bx, word ptr cs:[0x10]
0225: 2e891e2800     mov word ptr cs:[0x28], bx
022a: 2e29062800     sub word ptr cs:[0x28], ax
022f: 034402         add ax, word ptr [si + 2]
0232: 2e3b061200     cmp ax, word ptr cs:[0x12]
0237: 7e09           jle 0x242
0239: 2e2b061200     sub ax, word ptr cs:[0x12]
023e: 2ea32a00       mov word ptr cs:[0x2a], ax
0242: b85000         mov ax, 0x50
0245: 2e8b1e2200     mov bx, word ptr cs:[0x22]
024a: 2e031e2800     add bx, word ptr cs:[0x28]
024f: f7e3           mul bx
0251: 2e8b1e2000     mov bx, word ptr cs:[0x20]
0256: 2e031e2400     add bx, word ptr cs:[0x24]
025b: d1eb           shr bx, 1
025d: d1eb           shr bx, 1
025f: 03c3           add ax, bx
0261: 03f8           add di, ax
0263: 2e893e1a00     mov word ptr cs:[0x1a], di
0268: 8b04           mov ax, word ptr [si]
026a: 2e2b062400     sub ax, word ptr cs:[0x24]
026f: 2e2b062600     sub ax, word ptr cs:[0x26]
0274: 3d0000         cmp ax, 0
0277: 7f03           jg 0x27c
0279: e98b00         jmp 0x307
027c: 2ea31e00       mov word ptr cs:[0x1e], ax
0280: 8b4402         mov ax, word ptr [si + 2]
0283: 2e2b062800     sub ax, word ptr cs:[0x28]
0288: 2e2b062a00     sub ax, word ptr cs:[0x2a]
028d: 3d0000         cmp ax, 0
0290: 7e75           jle 0x307
0292: 2ea31c00       mov word ptr cs:[0x1c], ax
0296: 2ea12800       mov ax, word ptr cs:[0x28]
029a: f724           mul word ptr [si]
029c: 2e03062400     add ax, word ptr cs:[0x24]
02a1: c57407         lds si, ptr [si + 7]
02a4: 03f0           add si, ax
02a6: 2ea12400       mov ax, word ptr cs:[0x24]
02aa: 2e01062600     add word ptr cs:[0x26], ax
02af: bac403         mov dx, 0x3c4
02b2: b002           mov al, 2
02b4: ee             out dx, al
02b5: 42             inc dx
02b6: b011           mov al, 0x11
02b8: 2e8b0e2000     mov cx, word ptr cs:[0x20]
02bd: 2e030e2400     add cx, word ptr cs:[0x24]
02c2: 80e103         and cl, 3
02c5: d2c0           rol al, cl
02c7: 2ea22c00       mov byte ptr cs:[0x2c], al
02cb: 2e8b0e1c00     mov cx, word ptr cs:[0x1c]
02d0: 51             push cx
02d1: 2e8b0e1e00     mov cx, word ptr cs:[0x1e]
02d6: 8a24           mov ah, byte ptr [si]
02d8: 80fc00         cmp ah, 0
02db: 7409           je 0x2e6
02dd: ee             out dx, al
02de: 2e8a261400     mov ah, byte ptr cs:[0x14]
02e3: 268825         mov byte ptr es:[di], ah
02e6: 46             inc si
02e7: d0c0           rol al, 1
02e9: 3c11           cmp al, 0x11
02eb: 7501           jne 0x2ee
02ed: 47             inc di
02ee: e2e6           loop 0x2d6
02f0: 2e03362600     add si, word ptr cs:[0x26]
02f5: 2e83061a0050   add word ptr cs:[0x1a], 0x50
02fb: 2e8b3e1a00     mov di, word ptr cs:[0x1a]
0300: 2ea02c00       mov al, byte ptr cs:[0x2c]
0304: 59             pop cx
0305: e2c9           loop 0x2d0
0307: 5f             pop di
0308: 5e             pop si
0309: 1f             pop ds
030a: 07             pop es
030b: c9             leave 
030c: cb             retf 
030d: c8000000       enter 0, 0
0311: 06             push es
0312: 1e             push ds
0313: 56             push si
0314: 57             push di
0315: 2ea10a00       mov ax, word ptr cs:[0xa]
0319: 8ec0           mov es, ax
031b: 33ff           xor di, di
031d: c5760a         lds si, ptr [bp + 0xa]
0320: b85000         mov ax, 0x50
0323: 8b5e08         mov bx, word ptr [bp + 8]
0326: f7e3           mul bx
0328: 8b5e06         mov bx, word ptr [bp + 6]
032b: d1eb           shr bx, 1
032d: d1eb           shr bx, 1
032f: 03c3           add ax, bx
0331: 03f8           add di, ax
0333: 2e893e1a00     mov word ptr cs:[0x1a], di
0338: 8b04           mov ax, word ptr [si]
033a: 2ea31e00       mov word ptr cs:[0x1e], ax
033e: 8b4402         mov ax, word ptr [si + 2]
0341: 2ea31c00       mov word ptr cs:[0x1c], ax
0345: c57407         lds si, ptr [si + 7]
0348: bac403         mov dx, 0x3c4
034b: b002           mov al, 2
034d: ee             out dx, al
034e: 42             inc dx
034f: b011           mov al, 0x11
0351: 8b4e06         mov cx, word ptr [bp + 6]
0354: 80e103         and cl, 3
0357: d2c0           rol al, cl
0359: 2ea22c00       mov byte ptr cs:[0x2c], al
035d: 2e8b0e1c00     mov cx, word ptr cs:[0x1c]
0362: 51             push cx
0363: 2e8b0e1e00     mov cx, word ptr cs:[0x1e]
0368: 8a24           mov ah, byte ptr [si]
036a: ee             out dx, al
036b: 268825         mov byte ptr es:[di], ah
036e: 46             inc si
036f: d0c0           rol al, 1
0371: 3c11           cmp al, 0x11
0373: 7501           jne 0x376
0375: 47             inc di
0376: e2f0           loop 0x368
0378: 2e83061a0050   add word ptr cs:[0x1a], 0x50
037e: 2e8b3e1a00     mov di, word ptr cs:[0x1a]
0383: 2ea02c00       mov al, byte ptr cs:[0x2c]
0387: 59             pop cx
0388: e2d8           loop 0x362
038a: 5f             pop di
038b: 5e             pop si
038c: 1f             pop ds
038d: 07             pop es
038e: c9             leave 
038f: cb             retf 
0390: c8000000       enter 0, 0
0394: 06             push es
0395: 1e             push ds
0396: 56             push si
0397: 57             push di
0398: 2ea10a00       mov ax, word ptr cs:[0xa]
039c: 8ec0           mov es, ax
039e: c5760c         lds si, ptr [bp + 0xc]
03a1: 2ec70628000000 mov word ptr cs:[0x28], 0
03a8: 2ec7062a000000 mov word ptr cs:[0x2a], 0
03af: 2ea11000       mov ax, word ptr cs:[0x10]
03b3: 2b4608         sub ax, word ptr [bp + 8]
03b6: 720f           jb 0x3c7
03b8: 2ea32800       mov word ptr cs:[0x28], ax
03bc: 3d1000         cmp ax, 0x10
03bf: 7206           jb 0x3c7
03c1: 5f             pop di
03c2: 5e             pop si
03c3: 1f             pop ds
03c4: 07             pop es
03c5: c9             leave 
03c6: cb             retf 
03c7: 8b4608         mov ax, word ptr [bp + 8]
03ca: 051000         add ax, 0x10
03cd: 2e2b061200     sub ax, word ptr cs:[0x12]
03d2: 720c           jb 0x3e0
03d4: 2ea32a00       mov word ptr cs:[0x2a], ax
03d8: 3d1000         cmp ax, 0x10
03db: 7c03           jl 0x3e0
03dd: e98000         jmp 0x460
03e0: b85000         mov ax, 0x50
03e3: 8b5e08         mov bx, word ptr [bp + 8]
03e6: 2e031e2800     add bx, word ptr cs:[0x28]
03eb: f7e3           mul bx
03ed: 8b5e06         mov bx, word ptr [bp + 6]
03f0: d1eb           shr bx, 1
03f2: d1eb           shr bx, 1
03f4: 03c3           add ax, bx
03f6: 8bf8           mov di, ax
03f8: 2ea31a00       mov word ptr cs:[0x1a], ax
03fc: 2ea12800       mov ax, word ptr cs:[0x28]
0400: d1e0           shl ax, 1
0402: 03f0           add si, ax
0404: bac403         mov dx, 0x3c4
0407: b002           mov al, 2
0409: ee             out dx, al
040a: 42             inc dx
040b: b011           mov al, 0x11
040d: 8b4e06         mov cx, word ptr [bp + 6]
0410: 80e103         and cl, 3
0413: d2c0           rol al, cl
0415: 2ea22c00       mov byte ptr cs:[0x2c], al
0419: 8b5e0a         mov bx, word ptr [bp + 0xa]
041c: b91000         mov cx, 0x10
041f: 2e2b0e2800     sub cx, word ptr cs:[0x28]
0424: 2e2b0e2a00     sub cx, word ptr cs:[0x2a]
0429: 51             push cx
042a: b90800         mov cx, 8
042d: 8a3c           mov bh, byte ptr [si]
042f: 8a6401         mov ah, byte ptr [si + 1]
0432: ee             out dx, al
0433: d0e7           shl bh, 1
0435: 7303           jae 0x43a
0437: 26881d         mov byte ptr es:[di], bl
043a: d0e4           shl ah, 1
043c: 7304           jae 0x442
043e: 26885d02       mov byte ptr es:[di + 2], bl
0442: d0c0           rol al, 1
0444: 3c11           cmp al, 0x11
0446: 7501           jne 0x449
0448: 47             inc di
0449: e2e7           loop 0x432
044b: 2e83061a0050   add word ptr cs:[0x1a], 0x50
0451: 2e8b3e1a00     mov di, word ptr cs:[0x1a]
0456: 2ea02c00       mov al, byte ptr cs:[0x2c]
045a: 83c602         add si, 2
045d: 59             pop cx
045e: e2c9           loop 0x429
0460: 5f             pop di
0461: 5e             pop si
0462: 1f             pop ds
0463: 07             pop es
0464: c9             leave 
0465: cb             retf 
0466: c8000000       enter 0, 0
046a: 06             push es
046b: 1e             push ds
046c: 56             push si
046d: 57             push di
046e: 2ea10a00       mov ax, word ptr cs:[0xa]
0472: 8ec0           mov es, ax
0474: c5760c         lds si, ptr [bp + 0xc]
0477: 2ec70628000000 mov word ptr cs:[0x28], 0
047e: 2ec7062a000000 mov word ptr cs:[0x2a], 0
0485: 2ea11000       mov ax, word ptr cs:[0x10]
0489: 2b4608         sub ax, word ptr [bp + 8]
048c: 720f           jb 0x49d
048e: 2ea32800       mov word ptr cs:[0x28], ax
0492: 3d1000         cmp ax, 0x10
0495: 7206           jb 0x49d
0497: 5f             pop di
0498: 5e             pop si
0499: 1f             pop ds
049a: 07             pop es
049b: c9             leave 
049c: cb             retf 
049d: 8b4608         mov ax, word ptr [bp + 8]
04a0: 051000         add ax, 0x10
04a3: 2e2b061200     sub ax, word ptr cs:[0x12]
04a8: 720f           jb 0x4b9
04aa: 2ea32a00       mov word ptr cs:[0x2a], ax
04ae: 3d1000         cmp ax, 0x10
04b1: 7206           jb 0x4b9
04b3: 5f             pop di
04b4: 5e             pop si
04b5: 1f             pop ds
04b6: 07             pop es
04b7: c9             leave 
04b8: cb             retf 
04b9: b85000         mov ax, 0x50
04bc: 8b5e08         mov bx, word ptr [bp + 8]
04bf: 2e031e2800     add bx, word ptr cs:[0x28]
04c4: f7e3           mul bx
04c6: 8b5e06         mov bx, word ptr [bp + 6]
04c9: d1eb           shr bx, 1
04cb: d1eb           shr bx, 1
04cd: 03c3           add ax, bx
04cf: 8bf8           mov di, ax
04d1: 2ea31a00       mov word ptr cs:[0x1a], ax
04d5: 2e03362800     add si, word ptr cs:[0x28]
04da: bac403         mov dx, 0x3c4
04dd: b002           mov al, 2
04df: ee             out dx, al
04e0: 42             inc dx
04e1: b011           mov al, 0x11
04e3: 8b4e06         mov cx, word ptr [bp + 6]
04e6: 80e103         and cl, 3
04e9: d2c0           rol al, cl
04eb: 2ea22c00       mov byte ptr cs:[0x2c], al
04ef: 8b5e0a         mov bx, word ptr [bp + 0xa]
04f2: b91000         mov cx, 0x10
04f5: 2e2b0e2800     sub cx, word ptr cs:[0x28]
04fa: 2e2b0e2a00     sub cx, word ptr cs:[0x2a]
04ff: 80ff00         cmp bh, 0
0502: 7530           jne 0x534
0504: 51             push cx
0505: b90800         mov cx, 8
0508: 8a3c           mov bh, byte ptr [si]
050a: ee             out dx, al
050b: d0e7           shl bh, 1
050d: 7303           jae 0x512
050f: 26881d         mov byte ptr es:[di], bl
0512: d0c0           rol al, 1
0514: 3c11           cmp al, 0x11
0516: 7501           jne 0x519
0518: 47             inc di
0519: e2ef           loop 0x50a
051b: 2e83061a0050   add word ptr cs:[0x1a], 0x50
0521: 2e8b3e1a00     mov di, word ptr cs:[0x1a]
0526: 2ea02c00       mov al, byte ptr cs:[0x2c]
052a: 46             inc si
052b: 59             pop cx
052c: e2d6           loop 0x504
052e: 5f             pop di
052f: 5e             pop si
0530: 1f             pop ds
0531: 07             pop es
0532: c9             leave 
0533: cb             retf 
0534: b400           mov ah, 0
0536: 51             push cx
0537: b90800         mov cx, 8
053a: 8a3c           mov bh, byte ptr [si]
053c: ee             out dx, al
053d: d0e7           shl bh, 1
053f: 7306           jae 0x547
0541: 26881d         mov byte ptr es:[di], bl
0544: eb04           jmp 0x54a
0546: 90             nop 
0547: 268825         mov byte ptr es:[di], ah
054a: d0c0           rol al, 1
054c: 3c11           cmp al, 0x11
054e: 7501           jne 0x551
0550: 47             inc di
0551: e2e9           loop 0x53c
0553: 2e83061a0050   add word ptr cs:[0x1a], 0x50
0559: 2e8b3e1a00     mov di, word ptr cs:[0x1a]
055e: 2ea02c00       mov al, byte ptr cs:[0x2c]
0562: 46             inc si
0563: 59             pop cx
0564: e2d0           loop 0x536
0566: 5f             pop di
0567: 5e             pop si
0568: 1f             pop ds
0569: 07             pop es
056a: c9             leave 
056b: cb             retf 
056c: c8000000       enter 0, 0
0570: 06             push es
0571: 1e             push ds
0572: 56             push si
0573: 57             push di
0574: 2ea10a00       mov ax, word ptr cs:[0xa]
0578: 8ec0           mov es, ax
057a: c5760c         lds si, ptr [bp + 0xc]
057d: b85000         mov ax, 0x50
0580: 8b5e08         mov bx, word ptr [bp + 8]
0583: f7e3           mul bx
0585: 8b5e06         mov bx, word ptr [bp + 6]
0588: d1eb           shr bx, 1
058a: d1eb           shr bx, 1
058c: 03c3           add ax, bx
058e: 8bf8           mov di, ax
0590: 2ea31a00       mov word ptr cs:[0x1a], ax
0594: bac403         mov dx, 0x3c4
0597: b002           mov al, 2
0599: ee             out dx, al
059a: 42             inc dx
059b: b011           mov al, 0x11
059d: 8b4e06         mov cx, word ptr [bp + 6]
05a0: 80e103         and cl, 3
05a3: d2c0           rol al, cl
05a5: 2ea22c00       mov byte ptr cs:[0x2c], al
05a9: 8b5e0a         mov bx, word ptr [bp + 0xa]
05ac: b90800         mov cx, 8
05af: 80ff00         cmp bh, 0
05b2: 7530           jne 0x5e4
05b4: 51             push cx
05b5: b90800         mov cx, 8
05b8: 8a3c           mov bh, byte ptr [si]
05ba: ee             out dx, al
05bb: d0e7           shl bh, 1
05bd: 7303           jae 0x5c2
05bf: 26881d         mov byte ptr es:[di], bl
05c2: d0c0           rol al, 1
05c4: 3c11           cmp al, 0x11
05c6: 7501           jne 0x5c9
05c8: 47             inc di
05c9: e2ef           loop 0x5ba
05cb: 2e83061a0050   add word ptr cs:[0x1a], 0x50
05d1: 2e8b3e1a00     mov di, word ptr cs:[0x1a]
05d6: 2ea02c00       mov al, byte ptr cs:[0x2c]
05da: 46             inc si
05db: 59             pop cx
05dc: e2d6           loop 0x5b4
05de: 5f             pop di
05df: 5e             pop si
05e0: 1f             pop ds
05e1: 07             pop es
05e2: c9             leave 
05e3: cb             retf 
05e4: b400           mov ah, 0
05e6: 51             push cx
05e7: b90800         mov cx, 8
05ea: 8a3c           mov bh, byte ptr [si]
05ec: ee             out dx, al
05ed: d0e7           shl bh, 1
05ef: 7306           jae 0x5f7
05f1: 26881d         mov byte ptr es:[di], bl
05f4: eb04           jmp 0x5fa
05f6: 90             nop 
05f7: 268825         mov byte ptr es:[di], ah
05fa: d0c0           rol al, 1
05fc: 3c11           cmp al, 0x11
05fe: 7501           jne 0x601
0600: 47             inc di
0601: e2e9           loop 0x5ec
0603: 2e83061a0050   add word ptr cs:[0x1a], 0x50
0609: 2e8b3e1a00     mov di, word ptr cs:[0x1a]
060e: 2ea02c00       mov al, byte ptr cs:[0x2c]
0612: 46             inc si
0613: 59             pop cx
0614: e2d0           loop 0x5e6
0616: 5f             pop di
0617: 5e             pop si
0618: 1f             pop ds
0619: 07             pop es
061a: c9             leave 
061b: cb             retf 
061c: c8000000       enter 0, 0
0620: 06             push es
0621: 1e             push ds
0622: 56             push si
0623: 57             push di
0624: 2ea10a00       mov ax, word ptr cs:[0xa]
0628: 8ec0           mov es, ax
062a: 33ff           xor di, di
062c: c5760c         lds si, ptr [bp + 0xc]
062f: 8b04           mov ax, word ptr [si]
0631: f6760a         div byte ptr [bp + 0xa]
0634: b400           mov ah, 0
0636: 2ea31e00       mov word ptr cs:[0x1e], ax
063a: 3c00           cmp al, 0
063c: 7503           jne 0x641
063e: e9c500         jmp 0x706
0641: 8b4402         mov ax, word ptr [si + 2]
0644: f6760a         div byte ptr [bp + 0xa]
0647: b400           mov ah, 0
0649: 2ea31c00       mov word ptr cs:[0x1c], ax
064d: 3c00           cmp al, 0
064f: 7503           jne 0x654
0651: e9b200         jmp 0x706
0654: 2ea11e00       mov ax, word ptr cs:[0x1e]
0658: d1e8           shr ax, 1
065a: 8b5e06         mov bx, word ptr [bp + 6]
065d: 2bd8           sub bx, ax
065f: 2e891e2000     mov word ptr cs:[0x20], bx
0664: 2ea11c00       mov ax, word ptr cs:[0x1c]
0668: d1e8           shr ax, 1
066a: 8b5e08         mov bx, word ptr [bp + 8]
066d: 2bd8           sub bx, ax
066f: 2e891e2200     mov word ptr cs:[0x22], bx
0674: b85000         mov ax, 0x50
0677: 2e8b1e2200     mov bx, word ptr cs:[0x22]
067c: f7e3           mul bx
067e: 2e8b1e2000     mov bx, word ptr cs:[0x20]
0683: d1eb           shr bx, 1
0685: d1eb           shr bx, 1
0687: 03c3           add ax, bx
0689: 03f8           add di, ax
068b: 2e893e1a00     mov word ptr cs:[0x1a], di
0690: 8b04           mov ax, word ptr [si]
0692: f6660a         mul byte ptr [bp + 0xa]
0695: 2ea32200       mov word ptr cs:[0x22], ax
0699: c57407         lds si, ptr [si + 7]
069c: 2e89361800     mov word ptr cs:[0x18], si
06a1: bac403         mov dx, 0x3c4
06a4: b002           mov al, 2
06a6: ee             out dx, al
06a7: 42             inc dx
06a8: b011           mov al, 0x11
06aa: 2e8b0e2000     mov cx, word ptr cs:[0x20]
06af: 80e103         and cl, 3
06b2: d2c0           rol al, cl
06b4: 2ea22c00       mov byte ptr cs:[0x2c], al
06b8: b500           mov ch, 0
06ba: 8a4e0a         mov cl, byte ptr [bp + 0xa]
06bd: 2e890e2000     mov word ptr cs:[0x20], cx
06c2: 2e8b1e1c00     mov bx, word ptr cs:[0x1c]
06c7: 2e8b0e1e00     mov cx, word ptr cs:[0x1e]
06cc: 8a24           mov ah, byte ptr [si]
06ce: 80fc00         cmp ah, 0
06d1: 7404           je 0x6d7
06d3: ee             out dx, al
06d4: 268825         mov byte ptr es:[di], ah
06d7: 2e03362000     add si, word ptr cs:[0x20]
06dc: d0c0           rol al, 1
06de: 3c11           cmp al, 0x11
06e0: 7501           jne 0x6e3
06e2: 47             inc di
06e3: e2e7           loop 0x6cc
06e5: 2e83061a0050   add word ptr cs:[0x1a], 0x50
06eb: 2e8b3e1a00     mov di, word ptr cs:[0x1a]
06f0: 2e8b0e2200     mov cx, word ptr cs:[0x22]
06f5: 2e010e1800     add word ptr cs:[0x18], cx
06fa: 2e8b361800     mov si, word ptr cs:[0x18]
06ff: 2ea02c00       mov al, byte ptr cs:[0x2c]
0703: 4b             dec bx
0704: 75c1           jne 0x6c7
0706: 5f             pop di
0707: 5e             pop si
0708: 1f             pop ds
0709: 07             pop es
070a: c9             leave 
070b: cb             retf 
070c: 33c0           xor ax, ax
070e: 8ec0           mov es, ax
0710: 268b166304     mov dx, word ptr es:[0x463]
0715: 2ea10a00       mov ax, word ptr cs:[0xa]
0719: b00c           mov al, 0xc
071b: b104           mov cl, 4
071d: d2e4           shl ah, cl
071f: ef             out dx, ax
0720: cb             retf 
0721: c8000000       enter 0, 0
0725: 06             push es
0726: 1e             push ds
0727: 56             push si
0728: 57             push di
0729: bac403         mov dx, 0x3c4
072c: b002           mov al, 2
072e: b40f           mov ah, 0xf
0730: ef             out dx, ax
0731: bace03         mov dx, 0x3ce
0734: b005           mov al, 5
0736: ee             out dx, al
0737: 42             inc dx
0738: ec             in al, dx
0739: 24fc           and al, 0xfc
073b: 0c01           or al, 1
073d: ee             out dx, al
073e: 2ea10a00       mov ax, word ptr cs:[0xa]
0742: 8ec0           mov es, ax
0744: 8b460c         mov ax, word ptr [bp + 0xc]
0747: 8ed8           mov ds, ax
0749: fc             cld 
074a: b85000         mov ax, 0x50
074d: f76606         mul word ptr [bp + 6]
0750: 2ea31a00       mov word ptr cs:[0x1a], ax
0754: 8bf0           mov si, ax
0756: bb5000         mov bx, 0x50
0759: 2b5e0a         sub bx, word ptr [bp + 0xa]
075c: 03c3           add ax, bx
075e: 8bf8           mov di, ax
0760: 2ea31800       mov word ptr cs:[0x18], ax
0764: 8b5608         mov dx, word ptr [bp + 8]
0767: 8b4e0a         mov cx, word ptr [bp + 0xa]
076a: f3a4           rep movsb byte ptr es:[di], byte ptr [si]
076c: 03fb           add di, bx
076e: 03f3           add si, bx
0770: 4a             dec dx
0771: 75f4           jne 0x767
0773: 8b460c         mov ax, word ptr [bp + 0xc]
0776: 8ec0           mov es, ax
0778: 2e8b3e1a00     mov di, word ptr cs:[0x1a]
077d: 8bf7           mov si, di
077f: 03760a         add si, word ptr [bp + 0xa]
0782: b85000         mov ax, 0x50
0785: f76608         mul word ptr [bp + 8]
0788: 8bc8           mov cx, ax
078a: f3a4           rep movsb byte ptr es:[di], byte ptr [si]
078c: 2ea10a00       mov ax, word ptr cs:[0xa]
0790: 8ed8           mov ds, ax
0792: 2e8b3e1800     mov di, word ptr cs:[0x18]
0797: 8bf7           mov si, di
0799: 8b5608         mov dx, word ptr [bp + 8]
079c: 8b4e0a         mov cx, word ptr [bp + 0xa]
079f: f3a4           rep movsb byte ptr es:[di], byte ptr [si]
07a1: 03fb           add di, bx
07a3: 03f3           add si, bx
07a5: 4a             dec dx
07a6: 75f4           jne 0x79c
07a8: 5f             pop di
07a9: 5e             pop si
07aa: 1f             pop ds
07ab: 07             pop es
07ac: c9             leave 
07ad: cb             retf 
07ae: c8060000       enter 6, 0
07b2: 57             push di
07b3: 56             push si
07b4: 1e             push ds
07b5: 06             push es
07b6: c47e0e         les di, ptr [bp + 0xe]
07b9: c5760a         lds si, ptr [bp + 0xa]
07bc: 8b04           mov ax, word ptr [si]
07be: 2ea31e00       mov word ptr cs:[0x1e], ax
07c2: 8b4402         mov ax, word ptr [si + 2]
07c5: 2ea31c00       mov word ptr cs:[0x1c], ax
07c9: c746fe0000     mov word ptr [bp - 2], 0
07ce: c746fc0000     mov word ptr [bp - 4], 0
07d3: c57407         lds si, ptr [si + 7]
07d6: 2e89361800     mov word ptr cs:[0x18], si
07db: 8b4e08         mov cx, word ptr [bp + 8]
07de: 51             push cx
07df: 8b4e06         mov cx, word ptr [bp + 6]
07e2: 2e8b361800     mov si, word ptr cs:[0x18]
07e7: 2ea11c00       mov ax, word ptr cs:[0x1c]
07eb: f766fc         mul word ptr [bp - 4]
07ee: f77608         div word ptr [bp + 8]
07f1: 2ef7261e00     mul word ptr cs:[0x1e]
07f6: 03f0           add si, ax
07f8: 2ea11e00       mov ax, word ptr cs:[0x1e]
07fc: f766fe         mul word ptr [bp - 2]
07ff: f77606         div word ptr [bp + 6]
0802: 03f0           add si, ax
0804: 8a04           mov al, byte ptr [si]
0806: 268805         mov byte ptr es:[di], al
0809: 47             inc di
080a: ff46fe         inc word ptr [bp - 2]
080d: e2d3           loop 0x7e2
080f: ff46fc         inc word ptr [bp - 4]
0812: c746fe0000     mov word ptr [bp - 2], 0
0817: 59             pop cx
0818: e2c4           loop 0x7de
081a: 07             pop es
081b: 1f             pop ds
081c: 5e             pop si
081d: 5f             pop di
081e: c9             leave 
081f: cb             retf 
