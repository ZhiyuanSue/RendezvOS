import sys
import os
import json

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
        kernel_config=config_json['kernel']
        use_kernel_config=kernel_config['use']
        if use_kernel_config==False:
            print("Error:the kernel configs must be used")
            exit(1)
        
        # starting config the modules
        module_configs=config_json['modules']
        for module_config in module_configs:
            print(module_config)

if __name__ =='__main__':
    if(len(sys.argv)<=3):
        print("Error:Need more parameters,please give the config file path")
        exit(2)
    config_file=sys.argv[3]
    configure(config_file)
