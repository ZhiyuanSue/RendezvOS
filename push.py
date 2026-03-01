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
	
	if not os.path.isdir(target_subrepo_path):
		print("Error:no such a subrepo")
		exit(2)
	target_subrepo_git_path = os.path.join(target_subrepo_path,".git")
	if not os.path.isdir(target_subrepo_git_path):
		print("Error:target is not a git repo")
		exit(2)
    
	# copy the include files to the module repo
	dst_subrepo_include = os.path.join(current_dir,subrepo_path,"include")
    
	dst_git_ignore = os.path.join(dst_subrepo_include,".gitignore")
	if os.path.exists(dst_git_ignore):
		os.remove(dst_git_ignore)
    
	# try to push

	try:
		subprocess.run(["git", "add", "."], cwd=target_subrepo_path, check=True,
						capture_output=True, text=True)
 
		subprocess.run(["git", "commit", "-m", commit_message],
						cwd=target_subrepo_path, check=True, capture_output=True, text=True)

		subprocess.run(["git", "push"], cwd=target_subrepo_path, check=True,
						capture_output=True, text=True)

	except Exception as e:
		print(f"Git operation failed:{e}",file = sys.stderr)
		raise

	finally:
		with open(dst_git_ignore, "w", encoding="utf-8") as f:
			f.write("*\n")