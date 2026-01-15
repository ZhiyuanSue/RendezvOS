#ifndef _RENDEZVOS_ERROR_H_
#define _RENDEZVOS_ERROR_H_

/*in order to be compatiable with the linux, I choose this number*/
enum Error_t {
        REND_SUCCESS = 0,
        E_RENDEZVOS = 1024,
        E_IN_PARAM,
        E_REND_TEST,
        E_REND_IPC,
};

#endif
