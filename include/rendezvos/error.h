#ifndef _RENDEZVOS_ERROR_H_
#define _RENDEZVOS_ERROR_H_

/**
 * @brief Core error identifiers (positive enum values).
 *
 * Callers return @c REND_SUCCESS (0) or @c -E_* (negated enum). Enum starts
 * at 1024 so Linux-style small errno values remain free for compat layers —
 * this header does **not** redefine Linux errno.
 */
enum Error_t {
        REND_SUCCESS = 0,
        E_RENDEZVOS = 1024, /* Generic failure */
        E_IN_PARAM, /*Invalid argument*/
        E_REND_TEST, /*Test framework failure*/
        E_REND_IPC, /*IPC failure*/
        E_REND_AGAIN, /* Retryable (non-blocking IPC, buddy reclaim exhausted, …) */
        E_REND_ABANDON, /*abandon*/
        E_REND_NO_MSG, /*Empty message queue / no message available*/
        E_REND_RC_UNEQUAL, /*Refcount / ownership mismatch*/
        E_REND_NOFOUND, /*Lookup miss*/
        E_REND_OVERFLOW, /*Bounds exceeded*/
        E_REND_NO_MEM, /*Allocation failure*/
        E_REND_PORT_CLOSED, /*Port closed / ops denied*/
};

#endif
