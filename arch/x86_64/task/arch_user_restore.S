    .section .text
    .global arch_user_restore
arch_user_restore:
    /*TODO:add the sysret codes*/
    cli
    
    swapgs

    sysretq