/*
    here we think x0-x3 as the input parameter, and x0 as output
    so we just need one smc or hvc
*/
    .global psci_smc
psci_smc:
    smc #0
    ret

    .global psci_hvc
psci_hvc:
    hvc #0
    ret