#ifndef _RENDEZVOS_ID_H_
#define _RENDEZVOS_ID_H_

#include <common/types.h>
#include <rendezvos/sync/spin_lock.h>
#include <rendezvos/smp/percpu.h>
#include <common/limits.h>

typedef u64 id_t;
typedef id_t pid_t;
typedef id_t tid_t;

#define INVALID_ID U64_MAX

/**
 * @brief id allocator protected by MCS.
 */
typedef struct {
        id_t id; /* Next id to allocate. */
        spin_lock spin_ptr; /* MCS lock head for @c get_new_id. */
} Id_Manager;

/**
 * @brief Zero @p idmng->id and clear @p idmng->spin_ptr (no-op if NULL).
 * @note Does not take a lock; @c get_new_id performs MCS locking.
 */
void init_id_manager(Id_Manager* idmng);

/**
 * @brief Allocate the next id under MCS.
 * @param idmng Manager
 * @return Previous @c idmng->id, then increment; @c INVALID_ID if @p idmng is
 *         NULL or the counter already goto @c INVALID_ID.
 */
id_t get_new_id(Id_Manager* idmng);

/**
 * @brief Initialize @c tid_manager for early boot.
 */
void init_core_id_system(void);

/**
 * @brief Global tid allocator singleton.
 */
extern Id_Manager tid_manager;

#endif
