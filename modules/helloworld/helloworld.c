#include <modules/log/log.h>

void hello_world()
{
#ifdef HELLO
	pr_info("Shampoos");
#endif
}