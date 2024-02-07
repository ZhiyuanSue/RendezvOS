import sys
import os
import json

target_config_file_name="Makefile.env"

def configure_kernel(kernel_config,root_dir):
    # ==== ==== ==== ==== ==== ==== ==== ==== #
    # deal with the kernel configs
    # the kernel config must be used
    if 'use' not in kernel_config.keys():
        print("Error:the kernel configs must have 'use' attribute and be set to True")
        exit(1)
    use_kernel_config=kernel_config['use']
    if use_kernel_config==False:
        print("Error:the kernel configs must be used")
        exit(1)
    kernel_config_str=""
    
    if 'ARCH' not in kernel_config.keys():
        print("Error:the kernel configs must have 'ARCH' attribute")
        exit(1)
    
    # ==== ==== ==== ==== ==== ==== ==== ==== #
    # the previous keys is used for all kind of archs
    # but if the arch need some special keys, add it here
    
    # ==== ==== ==== ==== ==== ==== ==== ==== #
    # write the configs into makefile.env
    target_config_file_path=os.path.join(root_dir,target_config_file_name)
    if os.path.isfile(target_config_file_path):
        pass
    else:
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
    # test weather the target module dir exist
    if 'path' not in module_config.keys():
        print("Error: the ",module_name," module must have a 'path' attribute")
        exit(1)
    target_module_dir=module_config['path']
    target_module_dir=os.path.join(root_dir,target_module_dir)
    if os.path.isdir(target_module_dir)==False:
        print("ERROR:no such a module dir exist:",target_module_dir)
        exit(2)
    # target_config_file_path=os.path.join()

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
        for module_name,module_config in module_configs.items():
            # print(module_name,module_config)
            configure_module(module_name,module_config,root_dir)
            
            

if __name__ =='__main__':
    if(len(sys.argv)<=3):
        print("Error:Need more parameters,please give the config file path")
        exit(2)
    config_file=sys.argv[3]
    configure(config_file)
