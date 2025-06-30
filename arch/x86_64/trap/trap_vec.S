.macro trap_entry trap_id error_code_num
	sub		$(\error_code_num * 8),	%rsp
	sub		$(16*8),%rsp

	/*TODO: check the trap from kernel or user, if user, swapgs*/
	mov		%r15,	8(%rsp)
	mov		%r14,	16(%rsp)
	mov		%r13,	24(%rsp)
	mov		%r12,	32(%rsp)
	mov		%rbp,	40(%rsp)
	mov		%rbx,	48(%rsp)
	mov		%r11,	56(%rsp)
	mov		%r10,	64(%rsp)
	mov		%r9,	72(%rsp)
	mov		%r8,	80(%rsp)
	mov		%rax,	88(%rsp)
	mov		%rcx,	96(%rsp)
	mov		%rdx,	104(%rsp)
	mov		%rsi,	112(%rsp)
	mov		%rdi,	120(%rsp)

	mov		$(\trap_id),	%rax
	mov		18*8(%rsp),		%rbx
	and		$(0x3),		%rbx
	cmp		$0,		%rbx
	je		trap_enter_from_kernel\@
	/*the following is something that trap from user need to do*/
	swapgs
trap_enter_from_kernel\@:
	mov		%rax,	(%rsp)
	
	mov		%rsp,%rdi	/*use the rsp as the trap frame pointer*/
.endm

.macro trap_exit
	mov		18*8(%rsp),		%rbx
	and		$(0x3),		%rbx
	cmp		$0,		%rbx
	je		trap_exit_from_kernel\@
	/*the following is something that trap from user need to do*/
	swapgs
trap_exit_from_kernel\@:
	mov		120(%rsp),	%rdi
	mov		112(%rsp),	%rsi
	mov		104(%rsp),	%rdx
	mov		96(%rsp),	%rcx
	mov		88(%rsp),	%rax
	mov		80(%rsp),	%r8
	mov		72(%rsp),	%r9
	mov		64(%rsp),	%r10
	mov		56(%rsp),	%r11
	mov		48(%rsp),	%rbx
	mov		40(%rsp),	%rbp
	mov		32(%rsp),	%r12
	mov		24(%rsp),	%r13
	mov		16(%rsp),	%r14
	mov		8(%rsp),		%r15

	add		$(17*8),%rsp		//also pop error code
	iretq
.endm

	.section	.trap.vec
	.global	trap_0
trap_0:
	trap_entry 0 1
	call trap_handler
	trap_exit
	.global	trap_1
trap_1:
	trap_entry 1 1
	call trap_handler
	trap_exit
	.global	trap_2
trap_2:
	trap_entry 2 1
	call trap_handler
	trap_exit
	.global	trap_3
trap_3:
	trap_entry 3 1
	call trap_handler
	trap_exit
	.global	trap_4
trap_4:
	trap_entry 4 1
	call trap_handler
	trap_exit
	.global	trap_5
trap_5:
	trap_entry 5 1
	call trap_handler
	trap_exit
	.global	trap_6
trap_6:
	trap_entry 6 1
	call trap_handler
	trap_exit
	.global	trap_7
trap_7:
	trap_entry 7 1
	call trap_handler
	trap_exit
	.global	trap_8
trap_8:
	trap_entry 8 0
	call trap_handler
	trap_exit
	.global	trap_9
trap_9:
	trap_entry 9 1
	call trap_handler
	trap_exit
	.global	trap_10
trap_10:
	trap_entry 10 0
	call trap_handler
	trap_exit
	.global	trap_11
trap_11:
	trap_entry 11 0
	call trap_handler
	trap_exit
	.global	trap_12
trap_12:
	trap_entry 12 0
	call trap_handler
	trap_exit
	.global	trap_13
trap_13:
	trap_entry 13 0
	call trap_handler
	trap_exit
	.global	trap_14
trap_14:
	trap_entry 14 0
	call trap_handler
	trap_exit
	.global	trap_15
trap_15:
	trap_entry 15 1
	call trap_handler
	trap_exit
	.global	trap_16
trap_16:
	trap_entry 16 1
	call trap_handler
	trap_exit
	.global	trap_17
trap_17:
	trap_entry 17 0
	call trap_handler
	trap_exit
	.global	trap_18
trap_18:
	trap_entry 18 1
	call trap_handler
	trap_exit
	.global	trap_19
trap_19:
	trap_entry 19 1
	call trap_handler
	trap_exit
	.global	trap_20
trap_20:
	trap_entry 20 1
	call trap_handler
	trap_exit
	.global	trap_21
trap_21:
	trap_entry 21 1
	call trap_handler
	trap_exit
	.global	trap_22
trap_22:
	trap_entry 22 1
	call trap_handler
	trap_exit
	.global	trap_23
trap_23:
	trap_entry 23 1
	call trap_handler
	trap_exit
	.global	trap_24
trap_24:
	trap_entry 24 1
	call trap_handler
	trap_exit
	.global	trap_25
trap_25:
	trap_entry 25 1
	call trap_handler
	trap_exit
	.global	trap_26
trap_26:
	trap_entry 26 1
	call trap_handler
	trap_exit
	.global	trap_27
trap_27:
	trap_entry 27 1
	call trap_handler
	trap_exit
	.global	trap_28
trap_28:
	trap_entry 28 1
	call trap_handler
	trap_exit
	.global	trap_29
trap_29:
	trap_entry 29 1
	call trap_handler
	trap_exit
	.global	trap_30
trap_30:
	trap_entry 30 1
	call trap_handler
	trap_exit
	.global	trap_31
trap_31:
	trap_entry 31 1
	call trap_handler
	trap_exit
	.global	trap_32
trap_32:
	trap_entry 32 1
	call trap_handler
	trap_exit
	.global	trap_33
trap_33:
	trap_entry 33 1
	call trap_handler
	trap_exit
	.global	trap_34
trap_34:
	trap_entry 34 1
	call trap_handler
	trap_exit
	.global	trap_35
trap_35:
	trap_entry 35 1
	call trap_handler
	trap_exit
	.global	trap_36
trap_36:
	trap_entry 36 1
	call trap_handler
	trap_exit
	.global	trap_37
trap_37:
	trap_entry 37 1
	call trap_handler
	trap_exit
	.global	trap_38
trap_38:
	trap_entry 38 1
	call trap_handler
	trap_exit
	.global	trap_39
trap_39:
	trap_entry 39 1
	call trap_handler
	trap_exit
	.global	trap_40
trap_40:
	trap_entry 40 1
	call trap_handler
	trap_exit
	.global	trap_41
trap_41:
	trap_entry 41 1
	call trap_handler
	trap_exit
	.global	trap_42
trap_42:
	trap_entry 42 1
	call trap_handler
	trap_exit
	.global	trap_43
trap_43:
	trap_entry 43 1
	call trap_handler
	trap_exit
	.global	trap_44
trap_44:
	trap_entry 44 1
	call trap_handler
	trap_exit
	.global	trap_45
trap_45:
	trap_entry 45 1
	call trap_handler
	trap_exit
	.global	trap_46
trap_46:
	trap_entry 46 1
	call trap_handler
	trap_exit
	.global	trap_47
trap_47:
	trap_entry 47 1
	call trap_handler
	trap_exit
	.global	trap_48
trap_48:
	trap_entry 48 1
	call trap_handler
	trap_exit
	.global	trap_49
trap_49:
	trap_entry 49 1
	call trap_handler
	trap_exit
	.global	trap_50
trap_50:
	trap_entry 50 1
	call trap_handler
	trap_exit
	.global	trap_51
trap_51:
	trap_entry 51 1
	call trap_handler
	trap_exit
	.global	trap_52
trap_52:
	trap_entry 52 1
	call trap_handler
	trap_exit
	.global	trap_53
trap_53:
	trap_entry 53 1
	call trap_handler
	trap_exit
	.global	trap_54
trap_54:
	trap_entry 54 1
	call trap_handler
	trap_exit
	.global	trap_55
trap_55:
	trap_entry 55 1
	call trap_handler
	trap_exit
	.global	trap_56
trap_56:
	trap_entry 56 1
	call trap_handler
	trap_exit
	.global	trap_57
trap_57:
	trap_entry 57 1
	call trap_handler
	trap_exit
	.global	trap_58
trap_58:
	trap_entry 58 1
	call trap_handler
	trap_exit
	.global	trap_59
trap_59:
	trap_entry 59 1
	call trap_handler
	trap_exit
	.global	trap_60
trap_60:
	trap_entry 60 1
	call trap_handler
	trap_exit
	.global	trap_61
trap_61:
	trap_entry 61 1
	call trap_handler
	trap_exit
	.global	trap_62
trap_62:
	trap_entry 62 1
	call trap_handler
	trap_exit
	.global	trap_63
trap_63:
	trap_entry 63 1
	call trap_handler
	trap_exit
	.global	trap_64
trap_64:
	trap_entry 64 1
	call trap_handler
	trap_exit
	.global	trap_65
trap_65:
	trap_entry 65 1
	call trap_handler
	trap_exit
	.global	trap_66
trap_66:
	trap_entry 66 1
	call trap_handler
	trap_exit
	.global	trap_67
trap_67:
	trap_entry 67 1
	call trap_handler
	trap_exit
	.global	trap_68
trap_68:
	trap_entry 68 1
	call trap_handler
	trap_exit
	.global	trap_69
trap_69:
	trap_entry 69 1
	call trap_handler
	trap_exit
	.global	trap_70
trap_70:
	trap_entry 70 1
	call trap_handler
	trap_exit
	.global	trap_71
trap_71:
	trap_entry 71 1
	call trap_handler
	trap_exit
	.global	trap_72
trap_72:
	trap_entry 72 1
	call trap_handler
	trap_exit
	.global	trap_73
trap_73:
	trap_entry 73 1
	call trap_handler
	trap_exit
	.global	trap_74
trap_74:
	trap_entry 74 1
	call trap_handler
	trap_exit
	.global	trap_75
trap_75:
	trap_entry 75 1
	call trap_handler
	trap_exit
	.global	trap_76
trap_76:
	trap_entry 76 1
	call trap_handler
	trap_exit
	.global	trap_77
trap_77:
	trap_entry 77 1
	call trap_handler
	trap_exit
	.global	trap_78
trap_78:
	trap_entry 78 1
	call trap_handler
	trap_exit
	.global	trap_79
trap_79:
	trap_entry 79 1
	call trap_handler
	trap_exit
	.global	trap_80
trap_80:
	trap_entry 80 1
	call trap_handler
	trap_exit
	.global	trap_81
trap_81:
	trap_entry 81 1
	call trap_handler
	trap_exit
	.global	trap_82
trap_82:
	trap_entry 82 1
	call trap_handler
	trap_exit
	.global	trap_83
trap_83:
	trap_entry 83 1
	call trap_handler
	trap_exit
	.global	trap_84
trap_84:
	trap_entry 84 1
	call trap_handler
	trap_exit
	.global	trap_85
trap_85:
	trap_entry 85 1
	call trap_handler
	trap_exit
	.global	trap_86
trap_86:
	trap_entry 86 1
	call trap_handler
	trap_exit
	.global	trap_87
trap_87:
	trap_entry 87 1
	call trap_handler
	trap_exit
	.global	trap_88
trap_88:
	trap_entry 88 1
	call trap_handler
	trap_exit
	.global	trap_89
trap_89:
	trap_entry 89 1
	call trap_handler
	trap_exit
	.global	trap_90
trap_90:
	trap_entry 90 1
	call trap_handler
	trap_exit
	.global	trap_91
trap_91:
	trap_entry 91 1
	call trap_handler
	trap_exit
	.global	trap_92
trap_92:
	trap_entry 92 1
	call trap_handler
	trap_exit
	.global	trap_93
trap_93:
	trap_entry 93 1
	call trap_handler
	trap_exit
	.global	trap_94
trap_94:
	trap_entry 94 1
	call trap_handler
	trap_exit
	.global	trap_95
trap_95:
	trap_entry 95 1
	call trap_handler
	trap_exit
	.global	trap_96
trap_96:
	trap_entry 96 1
	call trap_handler
	trap_exit
	.global	trap_97
trap_97:
	trap_entry 97 1
	call trap_handler
	trap_exit
	.global	trap_98
trap_98:
	trap_entry 98 1
	call trap_handler
	trap_exit
	.global	trap_99
trap_99:
	trap_entry 99 1
	call trap_handler
	trap_exit
	.global	trap_100
trap_100:
	trap_entry 100 1
	call trap_handler
	trap_exit
	.global	trap_101
trap_101:
	trap_entry 101 1
	call trap_handler
	trap_exit
	.global	trap_102
trap_102:
	trap_entry 102 1
	call trap_handler
	trap_exit
	.global	trap_103
trap_103:
	trap_entry 103 1
	call trap_handler
	trap_exit
	.global	trap_104
trap_104:
	trap_entry 104 1
	call trap_handler
	trap_exit
	.global	trap_105
trap_105:
	trap_entry 105 1
	call trap_handler
	trap_exit
	.global	trap_106
trap_106:
	trap_entry 106 1
	call trap_handler
	trap_exit
	.global	trap_107
trap_107:
	trap_entry 107 1
	call trap_handler
	trap_exit
	.global	trap_108
trap_108:
	trap_entry 108 1
	call trap_handler
	trap_exit
	.global	trap_109
trap_109:
	trap_entry 109 1
	call trap_handler
	trap_exit
	.global	trap_110
trap_110:
	trap_entry 110 1
	call trap_handler
	trap_exit
	.global	trap_111
trap_111:
	trap_entry 111 1
	call trap_handler
	trap_exit
	.global	trap_112
trap_112:
	trap_entry 112 1
	call trap_handler
	trap_exit
	.global	trap_113
trap_113:
	trap_entry 113 1
	call trap_handler
	trap_exit
	.global	trap_114
trap_114:
	trap_entry 114 1
	call trap_handler
	trap_exit
	.global	trap_115
trap_115:
	trap_entry 115 1
	call trap_handler
	trap_exit
	.global	trap_116
trap_116:
	trap_entry 116 1
	call trap_handler
	trap_exit
	.global	trap_117
trap_117:
	trap_entry 117 1
	call trap_handler
	trap_exit
	.global	trap_118
trap_118:
	trap_entry 118 1
	call trap_handler
	trap_exit
	.global	trap_119
trap_119:
	trap_entry 119 1
	call trap_handler
	trap_exit
	.global	trap_120
trap_120:
	trap_entry 120 1
	call trap_handler
	trap_exit
	.global	trap_121
trap_121:
	trap_entry 121 1
	call trap_handler
	trap_exit
	.global	trap_122
trap_122:
	trap_entry 122 1
	call trap_handler
	trap_exit
	.global	trap_123
trap_123:
	trap_entry 123 1
	call trap_handler
	trap_exit
	.global	trap_124
trap_124:
	trap_entry 124 1
	call trap_handler
	trap_exit
	.global	trap_125
trap_125:
	trap_entry 125 1
	call trap_handler
	trap_exit
	.global	trap_126
trap_126:
	trap_entry 126 1
	call trap_handler
	trap_exit
	.global	trap_127
trap_127:
	trap_entry 127 1
	call trap_handler
	trap_exit
	.global	trap_128
trap_128:
	trap_entry 128 1
	call trap_handler
	trap_exit
	.global	trap_129
trap_129:
	trap_entry 129 1
	call trap_handler
	trap_exit
	.global	trap_130
trap_130:
	trap_entry 130 1
	call trap_handler
	trap_exit
	.global	trap_131
trap_131:
	trap_entry 131 1
	call trap_handler
	trap_exit
	.global	trap_132
trap_132:
	trap_entry 132 1
	call trap_handler
	trap_exit
	.global	trap_133
trap_133:
	trap_entry 133 1
	call trap_handler
	trap_exit
	.global	trap_134
trap_134:
	trap_entry 134 1
	call trap_handler
	trap_exit
	.global	trap_135
trap_135:
	trap_entry 135 1
	call trap_handler
	trap_exit
	.global	trap_136
trap_136:
	trap_entry 136 1
	call trap_handler
	trap_exit
	.global	trap_137
trap_137:
	trap_entry 137 1
	call trap_handler
	trap_exit
	.global	trap_138
trap_138:
	trap_entry 138 1
	call trap_handler
	trap_exit
	.global	trap_139
trap_139:
	trap_entry 139 1
	call trap_handler
	trap_exit
	.global	trap_140
trap_140:
	trap_entry 140 1
	call trap_handler
	trap_exit
	.global	trap_141
trap_141:
	trap_entry 141 1
	call trap_handler
	trap_exit
	.global	trap_142
trap_142:
	trap_entry 142 1
	call trap_handler
	trap_exit
	.global	trap_143
trap_143:
	trap_entry 143 1
	call trap_handler
	trap_exit
	.global	trap_144
trap_144:
	trap_entry 144 1
	call trap_handler
	trap_exit
	.global	trap_145
trap_145:
	trap_entry 145 1
	call trap_handler
	trap_exit
	.global	trap_146
trap_146:
	trap_entry 146 1
	call trap_handler
	trap_exit
	.global	trap_147
trap_147:
	trap_entry 147 1
	call trap_handler
	trap_exit
	.global	trap_148
trap_148:
	trap_entry 148 1
	call trap_handler
	trap_exit
	.global	trap_149
trap_149:
	trap_entry 149 1
	call trap_handler
	trap_exit
	.global	trap_150
trap_150:
	trap_entry 150 1
	call trap_handler
	trap_exit
	.global	trap_151
trap_151:
	trap_entry 151 1
	call trap_handler
	trap_exit
	.global	trap_152
trap_152:
	trap_entry 152 1
	call trap_handler
	trap_exit
	.global	trap_153
trap_153:
	trap_entry 153 1
	call trap_handler
	trap_exit
	.global	trap_154
trap_154:
	trap_entry 154 1
	call trap_handler
	trap_exit
	.global	trap_155
trap_155:
	trap_entry 155 1
	call trap_handler
	trap_exit
	.global	trap_156
trap_156:
	trap_entry 156 1
	call trap_handler
	trap_exit
	.global	trap_157
trap_157:
	trap_entry 157 1
	call trap_handler
	trap_exit
	.global	trap_158
trap_158:
	trap_entry 158 1
	call trap_handler
	trap_exit
	.global	trap_159
trap_159:
	trap_entry 159 1
	call trap_handler
	trap_exit
	.global	trap_160
trap_160:
	trap_entry 160 1
	call trap_handler
	trap_exit
	.global	trap_161
trap_161:
	trap_entry 161 1
	call trap_handler
	trap_exit
	.global	trap_162
trap_162:
	trap_entry 162 1
	call trap_handler
	trap_exit
	.global	trap_163
trap_163:
	trap_entry 163 1
	call trap_handler
	trap_exit
	.global	trap_164
trap_164:
	trap_entry 164 1
	call trap_handler
	trap_exit
	.global	trap_165
trap_165:
	trap_entry 165 1
	call trap_handler
	trap_exit
	.global	trap_166
trap_166:
	trap_entry 166 1
	call trap_handler
	trap_exit
	.global	trap_167
trap_167:
	trap_entry 167 1
	call trap_handler
	trap_exit
	.global	trap_168
trap_168:
	trap_entry 168 1
	call trap_handler
	trap_exit
	.global	trap_169
trap_169:
	trap_entry 169 1
	call trap_handler
	trap_exit
	.global	trap_170
trap_170:
	trap_entry 170 1
	call trap_handler
	trap_exit
	.global	trap_171
trap_171:
	trap_entry 171 1
	call trap_handler
	trap_exit
	.global	trap_172
trap_172:
	trap_entry 172 1
	call trap_handler
	trap_exit
	.global	trap_173
trap_173:
	trap_entry 173 1
	call trap_handler
	trap_exit
	.global	trap_174
trap_174:
	trap_entry 174 1
	call trap_handler
	trap_exit
	.global	trap_175
trap_175:
	trap_entry 175 1
	call trap_handler
	trap_exit
	.global	trap_176
trap_176:
	trap_entry 176 1
	call trap_handler
	trap_exit
	.global	trap_177
trap_177:
	trap_entry 177 1
	call trap_handler
	trap_exit
	.global	trap_178
trap_178:
	trap_entry 178 1
	call trap_handler
	trap_exit
	.global	trap_179
trap_179:
	trap_entry 179 1
	call trap_handler
	trap_exit
	.global	trap_180
trap_180:
	trap_entry 180 1
	call trap_handler
	trap_exit
	.global	trap_181
trap_181:
	trap_entry 181 1
	call trap_handler
	trap_exit
	.global	trap_182
trap_182:
	trap_entry 182 1
	call trap_handler
	trap_exit
	.global	trap_183
trap_183:
	trap_entry 183 1
	call trap_handler
	trap_exit
	.global	trap_184
trap_184:
	trap_entry 184 1
	call trap_handler
	trap_exit
	.global	trap_185
trap_185:
	trap_entry 185 1
	call trap_handler
	trap_exit
	.global	trap_186
trap_186:
	trap_entry 186 1
	call trap_handler
	trap_exit
	.global	trap_187
trap_187:
	trap_entry 187 1
	call trap_handler
	trap_exit
	.global	trap_188
trap_188:
	trap_entry 188 1
	call trap_handler
	trap_exit
	.global	trap_189
trap_189:
	trap_entry 189 1
	call trap_handler
	trap_exit
	.global	trap_190
trap_190:
	trap_entry 190 1
	call trap_handler
	trap_exit
	.global	trap_191
trap_191:
	trap_entry 191 1
	call trap_handler
	trap_exit
	.global	trap_192
trap_192:
	trap_entry 192 1
	call trap_handler
	trap_exit
	.global	trap_193
trap_193:
	trap_entry 193 1
	call trap_handler
	trap_exit
	.global	trap_194
trap_194:
	trap_entry 194 1
	call trap_handler
	trap_exit
	.global	trap_195
trap_195:
	trap_entry 195 1
	call trap_handler
	trap_exit
	.global	trap_196
trap_196:
	trap_entry 196 1
	call trap_handler
	trap_exit
	.global	trap_197
trap_197:
	trap_entry 197 1
	call trap_handler
	trap_exit
	.global	trap_198
trap_198:
	trap_entry 198 1
	call trap_handler
	trap_exit
	.global	trap_199
trap_199:
	trap_entry 199 1
	call trap_handler
	trap_exit
	.global	trap_200
trap_200:
	trap_entry 200 1
	call trap_handler
	trap_exit
	.global	trap_201
trap_201:
	trap_entry 201 1
	call trap_handler
	trap_exit
	.global	trap_202
trap_202:
	trap_entry 202 1
	call trap_handler
	trap_exit
	.global	trap_203
trap_203:
	trap_entry 203 1
	call trap_handler
	trap_exit
	.global	trap_204
trap_204:
	trap_entry 204 1
	call trap_handler
	trap_exit
	.global	trap_205
trap_205:
	trap_entry 205 1
	call trap_handler
	trap_exit
	.global	trap_206
trap_206:
	trap_entry 206 1
	call trap_handler
	trap_exit
	.global	trap_207
trap_207:
	trap_entry 207 1
	call trap_handler
	trap_exit
	.global	trap_208
trap_208:
	trap_entry 208 1
	call trap_handler
	trap_exit
	.global	trap_209
trap_209:
	trap_entry 209 1
	call trap_handler
	trap_exit
	.global	trap_210
trap_210:
	trap_entry 210 1
	call trap_handler
	trap_exit
	.global	trap_211
trap_211:
	trap_entry 211 1
	call trap_handler
	trap_exit
	.global	trap_212
trap_212:
	trap_entry 212 1
	call trap_handler
	trap_exit
	.global	trap_213
trap_213:
	trap_entry 213 1
	call trap_handler
	trap_exit
	.global	trap_214
trap_214:
	trap_entry 214 1
	call trap_handler
	trap_exit
	.global	trap_215
trap_215:
	trap_entry 215 1
	call trap_handler
	trap_exit
	.global	trap_216
trap_216:
	trap_entry 216 1
	call trap_handler
	trap_exit
	.global	trap_217
trap_217:
	trap_entry 217 1
	call trap_handler
	trap_exit
	.global	trap_218
trap_218:
	trap_entry 218 1
	call trap_handler
	trap_exit
	.global	trap_219
trap_219:
	trap_entry 219 1
	call trap_handler
	trap_exit
	.global	trap_220
trap_220:
	trap_entry 220 1
	call trap_handler
	trap_exit
	.global	trap_221
trap_221:
	trap_entry 221 1
	call trap_handler
	trap_exit
	.global	trap_222
trap_222:
	trap_entry 222 1
	call trap_handler
	trap_exit
	.global	trap_223
trap_223:
	trap_entry 223 1
	call trap_handler
	trap_exit
	.global	trap_224
trap_224:
	trap_entry 224 1
	call trap_handler
	trap_exit
	.global	trap_225
trap_225:
	trap_entry 225 1
	call trap_handler
	trap_exit
	.global	trap_226
trap_226:
	trap_entry 226 1
	call trap_handler
	trap_exit
	.global	trap_227
trap_227:
	trap_entry 227 1
	call trap_handler
	trap_exit
	.global	trap_228
trap_228:
	trap_entry 228 1
	call trap_handler
	trap_exit
	.global	trap_229
trap_229:
	trap_entry 229 1
	call trap_handler
	trap_exit
	.global	trap_230
trap_230:
	trap_entry 230 1
	call trap_handler
	trap_exit
	.global	trap_231
trap_231:
	trap_entry 231 1
	call trap_handler
	trap_exit
	.global	trap_232
trap_232:
	trap_entry 232 1
	call trap_handler
	trap_exit
	.global	trap_233
trap_233:
	trap_entry 233 1
	call trap_handler
	trap_exit
	.global	trap_234
trap_234:
	trap_entry 234 1
	call trap_handler
	trap_exit
	.global	trap_235
trap_235:
	trap_entry 235 1
	call trap_handler
	trap_exit
	.global	trap_236
trap_236:
	trap_entry 236 1
	call trap_handler
	trap_exit
	.global	trap_237
trap_237:
	trap_entry 237 1
	call trap_handler
	trap_exit
	.global	trap_238
trap_238:
	trap_entry 238 1
	call trap_handler
	trap_exit
	.global	trap_239
trap_239:
	trap_entry 239 1
	call trap_handler
	trap_exit
	.global	trap_240
trap_240:
	trap_entry 240 1
	call trap_handler
	trap_exit
	.global	trap_241
trap_241:
	trap_entry 241 1
	call trap_handler
	trap_exit
	.global	trap_242
trap_242:
	trap_entry 242 1
	call trap_handler
	trap_exit
	.global	trap_243
trap_243:
	trap_entry 243 1
	call trap_handler
	trap_exit
	.global	trap_244
trap_244:
	trap_entry 244 1
	call trap_handler
	trap_exit
	.global	trap_245
trap_245:
	trap_entry 245 1
	call trap_handler
	trap_exit
	.global	trap_246
trap_246:
	trap_entry 246 1
	call trap_handler
	trap_exit
	.global	trap_247
trap_247:
	trap_entry 247 1
	call trap_handler
	trap_exit
	.global	trap_248
trap_248:
	trap_entry 248 1
	call trap_handler
	trap_exit
	.global	trap_249
trap_249:
	trap_entry 249 1
	call trap_handler
	trap_exit
	.global	trap_250
trap_250:
	trap_entry 250 1
	call trap_handler
	trap_exit
	.global	trap_251
trap_251:
	trap_entry 251 1
	call trap_handler
	trap_exit
	.global	trap_252
trap_252:
	trap_entry 252 1
	call trap_handler
	trap_exit
	.global	trap_253
trap_253:
	trap_entry 253 1
	call trap_handler
	trap_exit
	.global	trap_254
trap_254:
	trap_entry 254 1
	call trap_handler
	trap_exit
	.global	trap_255
trap_255:
	trap_entry 255 1
	call trap_handler
	trap_exit

	.section	.data.trap.vec
	.global	trap_vec
	.p2align 3
trap_vec:
	.quad	trap_0
	.quad	trap_1
	.quad	trap_2
	.quad	trap_3
	.quad	trap_4
	.quad	trap_5
	.quad	trap_6
	.quad	trap_7
	.quad	trap_8
	.quad	trap_9
	.quad	trap_10
	.quad	trap_11
	.quad	trap_12
	.quad	trap_13
	.quad	trap_14
	.quad	trap_15
	.quad	trap_16
	.quad	trap_17
	.quad	trap_18
	.quad	trap_19
	.quad	trap_20
	.quad	trap_21
	.quad	trap_22
	.quad	trap_23
	.quad	trap_24
	.quad	trap_25
	.quad	trap_26
	.quad	trap_27
	.quad	trap_28
	.quad	trap_29
	.quad	trap_30
	.quad	trap_31
	.quad	trap_32
	.quad	trap_33
	.quad	trap_34
	.quad	trap_35
	.quad	trap_36
	.quad	trap_37
	.quad	trap_38
	.quad	trap_39
	.quad	trap_40
	.quad	trap_41
	.quad	trap_42
	.quad	trap_43
	.quad	trap_44
	.quad	trap_45
	.quad	trap_46
	.quad	trap_47
	.quad	trap_48
	.quad	trap_49
	.quad	trap_50
	.quad	trap_51
	.quad	trap_52
	.quad	trap_53
	.quad	trap_54
	.quad	trap_55
	.quad	trap_56
	.quad	trap_57
	.quad	trap_58
	.quad	trap_59
	.quad	trap_60
	.quad	trap_61
	.quad	trap_62
	.quad	trap_63
	.quad	trap_64
	.quad	trap_65
	.quad	trap_66
	.quad	trap_67
	.quad	trap_68
	.quad	trap_69
	.quad	trap_70
	.quad	trap_71
	.quad	trap_72
	.quad	trap_73
	.quad	trap_74
	.quad	trap_75
	.quad	trap_76
	.quad	trap_77
	.quad	trap_78
	.quad	trap_79
	.quad	trap_80
	.quad	trap_81
	.quad	trap_82
	.quad	trap_83
	.quad	trap_84
	.quad	trap_85
	.quad	trap_86
	.quad	trap_87
	.quad	trap_88
	.quad	trap_89
	.quad	trap_90
	.quad	trap_91
	.quad	trap_92
	.quad	trap_93
	.quad	trap_94
	.quad	trap_95
	.quad	trap_96
	.quad	trap_97
	.quad	trap_98
	.quad	trap_99
	.quad	trap_100
	.quad	trap_101
	.quad	trap_102
	.quad	trap_103
	.quad	trap_104
	.quad	trap_105
	.quad	trap_106
	.quad	trap_107
	.quad	trap_108
	.quad	trap_109
	.quad	trap_110
	.quad	trap_111
	.quad	trap_112
	.quad	trap_113
	.quad	trap_114
	.quad	trap_115
	.quad	trap_116
	.quad	trap_117
	.quad	trap_118
	.quad	trap_119
	.quad	trap_120
	.quad	trap_121
	.quad	trap_122
	.quad	trap_123
	.quad	trap_124
	.quad	trap_125
	.quad	trap_126
	.quad	trap_127
	.quad	trap_128
	.quad	trap_129
	.quad	trap_130
	.quad	trap_131
	.quad	trap_132
	.quad	trap_133
	.quad	trap_134
	.quad	trap_135
	.quad	trap_136
	.quad	trap_137
	.quad	trap_138
	.quad	trap_139
	.quad	trap_140
	.quad	trap_141
	.quad	trap_142
	.quad	trap_143
	.quad	trap_144
	.quad	trap_145
	.quad	trap_146
	.quad	trap_147
	.quad	trap_148
	.quad	trap_149
	.quad	trap_150
	.quad	trap_151
	.quad	trap_152
	.quad	trap_153
	.quad	trap_154
	.quad	trap_155
	.quad	trap_156
	.quad	trap_157
	.quad	trap_158
	.quad	trap_159
	.quad	trap_160
	.quad	trap_161
	.quad	trap_162
	.quad	trap_163
	.quad	trap_164
	.quad	trap_165
	.quad	trap_166
	.quad	trap_167
	.quad	trap_168
	.quad	trap_169
	.quad	trap_170
	.quad	trap_171
	.quad	trap_172
	.quad	trap_173
	.quad	trap_174
	.quad	trap_175
	.quad	trap_176
	.quad	trap_177
	.quad	trap_178
	.quad	trap_179
	.quad	trap_180
	.quad	trap_181
	.quad	trap_182
	.quad	trap_183
	.quad	trap_184
	.quad	trap_185
	.quad	trap_186
	.quad	trap_187
	.quad	trap_188
	.quad	trap_189
	.quad	trap_190
	.quad	trap_191
	.quad	trap_192
	.quad	trap_193
	.quad	trap_194
	.quad	trap_195
	.quad	trap_196
	.quad	trap_197
	.quad	trap_198
	.quad	trap_199
	.quad	trap_200
	.quad	trap_201
	.quad	trap_202
	.quad	trap_203
	.quad	trap_204
	.quad	trap_205
	.quad	trap_206
	.quad	trap_207
	.quad	trap_208
	.quad	trap_209
	.quad	trap_210
	.quad	trap_211
	.quad	trap_212
	.quad	trap_213
	.quad	trap_214
	.quad	trap_215
	.quad	trap_216
	.quad	trap_217
	.quad	trap_218
	.quad	trap_219
	.quad	trap_220
	.quad	trap_221
	.quad	trap_222
	.quad	trap_223
	.quad	trap_224
	.quad	trap_225
	.quad	trap_226
	.quad	trap_227
	.quad	trap_228
	.quad	trap_229
	.quad	trap_230
	.quad	trap_231
	.quad	trap_232
	.quad	trap_233
	.quad	trap_234
	.quad	trap_235
	.quad	trap_236
	.quad	trap_237
	.quad	trap_238
	.quad	trap_239
	.quad	trap_240
	.quad	trap_241
	.quad	trap_242
	.quad	trap_243
	.quad	trap_244
	.quad	trap_245
	.quad	trap_246
	.quad	trap_247
	.quad	trap_248
	.quad	trap_249
	.quad	trap_250
	.quad	trap_251
	.quad	trap_252
	.quad	trap_253
	.quad	trap_254
	.quad	trap_255

	.section	.data.trap.trap_vec_table
	.global	trap_vec_table
	.align	0x10
trap_vec_table:
	.zero	16*256
