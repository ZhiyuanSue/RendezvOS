# 1/before we use the file system to get user test case
# we might need to use 'incbin' to include the user case
# 2/if we use file system, we also need to use musl cc to complie it
# and put it into a image, so we actually need a user test case 

# this user dir should be put under modules
# for it can be ignored instead of a git submodule

# and it might be complied directly with another way

# the user repo is also can be configured

import sys
import os
import json
import shutil

target_dir = "modules/user"
target_config_arch_list=[
	'aarch64',
	'longarch',
	'riscv64',
	'x86_64'
]

if __name__ =='__main__':
	arch=sys.argv[1]
	root_dir=sys.argv[2]
	user_config_file=sys.argv[3]
	print(arch)
	print(root_dir)
	print(user_config_file)
    
	if os.path.isfile(user_config_file)==False:
		print("ERROR:no such an user config file")
		exit(2)
	if arch not in target_config_arch_list:
		print("ERROR:no such an arch")
		exit(1)
	user_dir=os.path.join(root_dir,target_dir)
	with open(user_config_file,'r') as json_file:
		user_json=json.load(json_file)
		if os.path.isdir(user_dir) == False:
			git_repo_link = user_json['git']
			git_repo_clone_cmd = f'git clone {git_repo_link} {user_dir}'
			status = os.system(git_repo_clone_cmd)
			if status != 0:
				print("ERROR:git clone repo "+git_repo_link+" fail")
				exit(2)
		using_file_system = user_json['filesystem']
		if using_file_system == False:
			# using incbin
			pass
		else:
			# using file system to test
			pass
	