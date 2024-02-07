# ShamPoOS

The project was originally named shanpu os in honor of my brother Shanpu Xu, but later changed its name to ShamPoOS due to the similar pronunciation of the word to shampoos
# Introduction

# About Shanpu Xu
Shanpu Xu is my senior brother, a 2020 master's student in the Department of Computer Science and Technology at Tsinghua University. He has an incredible talent for mathematics and computer science, as well as an extremely hard and diligent attitude to study and work, which is my role model in life

# Start
## run
First you need to config it,choose one config file under script/config, e.g. 
```
make config CONFIG=config_x86_64.json
```
then 
```
make run
```
no more other steps

## config
The config file, which is read by configure.py, is the config file for the kernel

if you want to change it ,just see what happened in the configure.py or you can try to build one for yourself.

unlike the toml file in rust, all the configure of shampoos is set in config file ,and no more work for the kernel developer.