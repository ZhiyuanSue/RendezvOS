import sys
import os

def configure():
    print(len(sys.argv))
    print(sys.argv[0])
    print(sys.argv[1])
    pass

if __name__ =='__main__':
    print(len(sys.argv))
    if(len(sys.argv)<=2):
        print("Need more parameters,please give the config file path")
        exit(2)
    config_file=sys.argv[2]
    configure(config_file)
