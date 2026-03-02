#include "modem.h"

struct stat_modem* stat_modem_init()
{
	struct stat_modem* status_modem;
	status_modem =  malloc(sizeof(struct stat_modem));
	return status_modem;
}
