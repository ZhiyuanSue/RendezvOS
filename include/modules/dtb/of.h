#ifndef _RENDEZVOS_OF_H_
#define _RENDEZVOS_OF_H_
#include <common/types.h>

struct device_node;

/**
 * Linux-style OF match entry (compatible string + optional driver data).
 * Tables end with a zeroed sentinel (compatible == NULL).
 */
struct of_device_id {
        const char *compatible;
        const void *data;
};

/**
 * @brief Walk @p matches; return first node whose compatible list hits.
 * @param from Start node (NULL search from device_root)
 * @param match_out Optional: set to the matching table entry
 */
struct device_node *of_find_matching_node(struct device_node *from,
                                          const struct of_device_id *matches,
                                          const struct of_device_id **match_out);

#endif
