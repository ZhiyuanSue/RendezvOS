import sys
import os
import json

target_config_file_name="Makefile.env"
target_config_arch_list=[
	'aarch64',
	'longarch',
	'riscv64',
	'x86_64'
]
usable_module_list={}
module_features=[]
kernel_features=[]

def configure_kernel(kernel_config,root_dir):
	# ==== ==== ==== ==== ==== ==== ==== ==== #
	# deal with the kernel configs
	
	kernel_config_str=""

	# the kernel config must be used
	if 'use' not in kernel_config.keys():
		print("Error:the kernel configs must have 'use' attribute and be set to True")
		exit(1)
	use_kernel_config=kernel_config['use']
	if use_kernel_config==False:
		print("Error:the kernel configs must be used")
		exit(1)
	# ARCH
	if 'ARCH' not in kernel_config.keys():
		print("Error:the kernel configs must have 'ARCH' attribute")
		exit(1)
	ARCH_kernel_config=kernel_config['ARCH']
	if ARCH_kernel_config not in target_config_arch_list:
		print("Error:the ARCH must be one of aarch64,longarch,riscv64,x86_64")
		exit(1)
	kernel_config_str = kernel_config_str + "ARCH\t:=\t" + ARCH_kernel_config + "\n"
		
	# CROSS COMPLIER
	if 'CROSS_COMPLIER' not in kernel_config.keys():
		print("Error:the kernel configs must have 'CROSS_COMPLIER' attribute")
		exit(1)
	CROSS_COMPLIER_kernel_config=kernel_config['CROSS_COMPLIER']
	kernel_config_str = kernel_config_str + "CROSS_COMPLIER\t:=\t" + CROSS_COMPLIER_kernel_config + "\n"
		
	# DBG
	DGB_kernel_config="false"
	if 'DBG' in kernel_config.keys():
		DGB_kernel_config=kernel_config['DBG']
	elif 'DEBUG' in kernel_config.keys():
		DGB_kernel_config=kernel_config['DEBUG']
	if DGB_kernel_config==True:
		DGB_kernel_config="true"
	elif DGB_kernel_config==False:
		DGB_kernel_config="false"
	kernel_config_str = kernel_config_str + "DBG\t:=\t" + DGB_kernel_config  + "\n"
		
	# CFLAGS
	if 'CFLAGS' in kernel_config.keys():
		CFLAGS_kernel_config=kernel_config['CFLAGS']
		kernel_config_str = kernel_config_str + "CFLAGS\t:=\t"
		for CFLAGS in CFLAGS_kernel_config:
			kernel_config_str = kernel_config_str + CFLAGS + ' '
		kernel_config_str = kernel_config_str + '\n'
		
	
	# LDFLAGS
	if 'LDFLAGS' in kernel_config.keys():
		LDFLAGS_kernel_config=kernel_config['LDFLAGS']
		kernel_config_str = kernel_config_str + "LDFLAGS\t:=\t"
		for LDFLAGS in LDFLAGS_kernel_config:
			kernel_config_str = kernel_config_str + LDFLAGS + ' '
		kernel_config_str = kernel_config_str + '\n'
	# ==== ==== ==== ==== ==== ==== ==== ==== #
	# the previous keys is used for all kind of archs
	# but if the arch need some special keys, add it here
	if ARCH_kernel_config=="aarch64":
		pass
	elif ARCH_kernel_config=="longarch":
		pass
	elif ARCH_kernel_config=="riscv64":
		pass
	elif ARCH_kernel_config=="x86_64":
		pass
	# ==== ==== ==== ==== ==== ==== ==== ==== #
	# write the configs into makefile.env
	target_config_file_path=os.path.join(root_dir,target_config_file_name)
	target_config_file=open(target_config_file_path,'w')
	target_config_file.write(kernel_config_str)
	target_config_file.close()

	# FEATURES
	if 'features' in kernel_config.keys() :
		features_kernel_config=kernel_config['features']
		for feature in features_kernel_config:
			kernel_features.append(feature)

def configure_kernel_feature(kernel_config,root_dir):
	# FEATURES
	kernel_config_str = ""
	if 'features' in kernel_config.keys() :
		features_kernel_config=kernel_config['features']
		kernel_config_str = kernel_config_str + "CFLAGS\t+=\t"
		for feature in features_kernel_config:
			kernel_config_str = kernel_config_str + ' -D ' + feature

		for feature in module_features:
			kernel_config_str = kernel_config_str + ' -D ' + feature
		kernel_config_str = kernel_config_str + '\n'
	# ==== ==== ==== ==== ==== ==== ==== ==== #
	# write the configs into kernel/makefile.env
	target_config_file_path=os.path.join(root_dir,"kernel",target_config_file_name)
	target_config_file=open(target_config_file_path,'w')
	target_config_file.write(kernel_config_str)
	target_config_file.close()
	# ==== ==== ==== ==== ==== ==== ==== ==== #
	# write the configs into arch/makefile.env
	target_config_file_path=os.path.join(root_dir,"arch",target_config_file_name)
	target_config_file=open(target_config_file_path,'w')
	target_config_file.write(kernel_config_str)
	target_config_file.close()

def configure_module(module_name,module_config,root_dir):
	if 'use' not in module_config.keys():
		print("Error: the ",module_name," module must have a 'use' attribute")
		exit(1)
	use_module_config=module_config['use']
	if use_module_config==False:
		return
	usable_module_list[module_name]=[]
	# test weather the target module dir exist
	if 'path' not in module_config.keys():
		print("Error: the ",module_name," module must have a 'path' attribute")
		exit(1)
	target_module_dir=module_config['path']
	target_module_dir=os.path.join(root_dir,target_module_dir)
	if os.path.isdir(target_module_dir)==False:
		print("ERROR:no such a module dir exist:",target_module_dir)
		exit(2)
	# add the module config file
	target_config_file_path=os.path.join(target_module_dir,target_config_file_name)

	module_config_str=""
		
	# FEATURES
	if 'features' in module_config.keys():
		feature_module_config=module_config['features']
		module_config_str = module_config_str + "CFLAGS\t+=\t"
		for feature in feature_module_config:
			module_config_str =module_config_str + ' -D ' + feature
			usable_module_list[module_name].append(feature)
			module_features.append(feature)
		module_config_str = module_config_str + '\n'
		
	# check the depend modules and add the configs of depend modules into this module
	if 'depend_module' in module_config.keys():
		depend_module_config=module_config['depend_module']
		module_config_str = module_config_str + "CFLAGS\t+=\t"
		for deps in depend_module_config:
			if deps not in usable_module_list.keys():
				print("the module ",module_name, " depends on the ",deps,",but this module not exist or not usable,please check" )
				exit(1)
			for deps_feature in usable_module_list[deps]:
				module_config_str =module_config_str + ' -D ' + deps_feature
				usable_module_list[module_name].append(deps_feature)
		for feature in kernel_features:
			module_config_str = module_config_str + ' -D '+ feature
		module_config_str = module_config_str + '\n'
			
	# write the configs into makefile.env
	target_config_file_path=os.path.join(target_module_dir,target_config_file_name)
	target_config_file=open(target_config_file_path,'w')
	target_config_file.write(module_config_str)
	target_config_file.close()

def configure_modules(module_configs,root_dir):
	# we have to add a makefile.env under modules dir
	modules_dir=os.path.join(root_dir,"modules")
	if os.path.isdir(modules_dir)==False:
		print("Warning:no 'modules' dir")
		return
	kernel_config_str="modules\t:=\t"
	modules_header_file_str=""
	for module_name,module_config in module_configs.items():
		if 'use' not in module_config.keys():
			print("Error: the ",module_name," module must have a 'use' attribute")
			exit(1)
		use_module_config=module_config['use']
		if use_module_config==False:
			continue
		kernel_config_str=kernel_config_str+module_name+' '
		modules_header_file_str=modules_header_file_str+"#include \""+module_name+"/"+module_name+".h\"\n"
		
	target_config_file_path=os.path.join(modules_dir,target_config_file_name)
	target_config_file=open(target_config_file_path,'w')
	target_config_file.write(kernel_config_str)
	target_config_file.close()
	
	modules_header_file_path=os.path.join(root_dir,"include","modules","modules.h")
	modules_header_file=open(modules_header_file_path,'w')
	modules_header_file.write(modules_header_file_str)
	modules_header_file.close()

def configure(config_file):
	script_config_dir=sys.argv[2]
	root_dir=sys.argv[1]
	config_file_path=os.path.join(script_config_dir,config_file)
	if os.path.isfile(config_file_path)==False:
		print("Error:No such a config file,please check")
		exit(2)
	with open(config_file_path,'r') as json_file:
		config_json=json.load(json_file)
		# starting config the kernel
		if 'kernel' not in config_json.keys():
			print("Error:the kernel configs must exist")
			exit(1)
		kernel_config=config_json['kernel']
		configure_kernel(kernel_config,root_dir)
			
		# starting config the modules
		if 'modules' not in config_json.keys():
			# the config file have no modules is reasonable
			print("Warning:no modules in the config file")
			return
		module_configs=config_json['modules']
		configure_modules(module_configs,root_dir)
		for module_name,module_config in module_configs.items():
			# print(module_name,module_config)
			configure_module(module_name,module_config,root_dir)
		#config kernel feature
		configure_kernel_feature(kernel_config,root_dir)
				
					

if __name__ =='__main__':
	if(len(sys.argv)<=3):
		print("Error:Need more parameters,please give the config file path")
		exit(2)
	config_file=sys.argv[3]
	configure(config_file)
