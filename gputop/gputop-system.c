#include <unistd.h>

/* For the sake of consistently running gputop with respect to a test
 * application; this is essentially a test application that does
 * nothing interesting itself that can be used while investigating
 * system-wide metrics.
 *
 * Note that the ui will be initialized/run via a library constructor
 * (gputop_ui_init)
 */

int
main(int argc, char **argv)
{
    while (1)
	sleep(60);

    return 0;
}
