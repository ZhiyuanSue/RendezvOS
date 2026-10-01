#include <common/types.h>
#include <modules/dtb/dev_tree.h>
#include <modules/dtb/of.h>

struct device_node *of_find_matching_node(struct device_node *from,
                                          const struct of_device_id *matches,
                                          const struct of_device_id **match_out)
{
        const struct of_device_id *id;
        struct device_node *node;

        if (!matches)
                return NULL;

        for (id = matches; id->compatible; id++) {
                node = dev_node_find_by_compatible(from,
                                                   (char *)id->compatible);
                if (node) {
                        if (match_out)
                                *match_out = id;
                        return node;
                }
        }
        return NULL;
}
