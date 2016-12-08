#ifndef LNVM_H_
#define LNVM_H_

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <argp.h>

#include "linux/lightnvm.h"
#include <liblightnvm.h>

enum cmdtypes {
	LIGHTNVM_DEV_VERIFY = 1,
	LIGHTNVM_DEV_PLANE = 2,

};

struct arguments
{
	char *devname;

	int cmdtype;
	int arg_num; /* state->arg_num doesn't increase with subcommands */
	char *dev;

	/* verify */
	int dry_run;

	int do_read;
	int do_write;
	int do_erase;

	int rw_defined;

	int show_time;

	int max_ch;
	int max_lun;
	int max_blk;
	int skip_blk;

	int plane_hint;
};


#endif
