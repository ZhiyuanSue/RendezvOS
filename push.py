import os
import shutil
import sys
import subprocess

# This file is used for sub module push
# as it have been splited the include dir
# the push must rebuild the sub module and then push it
# you can use it like this
# python3 push.py modules/submodule_name "commit message"

if __name__ =='__main__':
	if len(sys.argv) !=3:
		print("Error:error input argc")
		exit(2)
	subrepo_path = sys.argv[1]
    
	commit_message = sys.argv[2]
    
	current_dir = os.getcwd()
	target_subrepo_path = os.path.join(current_dir,subrepo_path)
	
	if os.path.isdir(target_subrepo_path)==False:
		print("Error:no such a subrepo")
		exit(2)
	target_subrepo_git_path = os.path.join(target_subrepo_path,".git")
	if os.path.isdir(target_subrepo_git_path)==False:
		print("Error:target is not a git repo")
		exit(2)
    
	# copy the include files to the module repo
	src_subrepo_include = os.path.join(current_dir,"include",subrepo_path)
	dst_subrepo_include = os.path.join(current_dir,subrepo_path,"include")
	shutil.copytree(src_subrepo_include, dst_subrepo_include, dirs_exist_ok=True, copy_function=shutil.copy2)
    
	dst_git_ignore = os.path.join(dst_subrepo_include,".gitignore")
	os.remove(dst_git_ignore)
    
	# try to push

	subprocess.run(["git", "add", "."], cwd=target_subrepo_path, check=True,
					capture_output=True, text=True)
 
	subprocess.run(["git", "commit", "-m", commit_message],
					cwd=target_subrepo_path, check=True, capture_output=True, text=True)

	subprocess.run(["git", "push"], cwd=target_subrepo_path, check=True,
					capture_output=True, text=True)
    