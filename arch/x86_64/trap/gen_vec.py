
vec_len=256
vec_file_name="./trap_vec.s"

def gen_vec():
	string=""
	string+="\t.section\t.trap.vec\n"
	for index in range(0,vec_len):
		string+="\t.global\ttrap_"+str(index)+"\n"
		string+="trap_"+str(index)+":\n"
		string+="\tjmp\ttrap_entry\n"
	string+="\n"
	string+="\t.section\t.data.trap.vec\n"
	string+="\t.global\ttrap_vec\n"
	string+="trap_vec:\n"
	for index in range(0,vec_len):
		string+="\t.quad\ttrap_"+str(index)+"\n"
	string+="\n"
	string+="\t.section\t.data.trap.idt\n"
	string+="\t.global\tidt\n"
	string+="\t.align\t0x10\n"
	string+="idt:\n"
	string+="\t.zero\t16*256\n"
	with open(vec_file_name,"w") as file:
		file.write(string)

	pass
if __name__ == '__main__':
	gen_vec()