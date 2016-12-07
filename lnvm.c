#include "lnvm.h"
#include <omp.h>

static int dev_verify(struct arguments *args)
{
	NVM_DEV dev;
	NVM_GEO geo;
	void *buf;
	int max_ch, max_lun, max_blk, skip_blk;
	int do_write, do_erase;
	int magic_flag;

	dev = nvm_dev_open("/dev/nvme1n1");
	if (!dev) {
		printf("My life is not worth living anymore\n");
		return -EINVAL;
	}
	geo = nvm_dev_attr_geo(dev);

	nvm_dev_pr(dev);
	nvm_geo_pr(geo);

	/* Parameters begin */
	skip_blk = 2;
	max_ch = geo.nchannels;
	max_lun = geo.nluns;
	max_blk = geo.nblocks;

	int report[geo.nchannels][geo.nluns][geo.nblocks];

	memset(&report, 0, sizeof(report));
	max_ch = 1;
	max_lun = 1;
	max_blk = 32;

	do_write = 1;
	do_erase = 1;

	magic_flag = NVM_MAGIC_FLAG_DUAL;

	/* Parameters end */
	buf = nvm_buf_alloc(geo, geo.vpg_nbytes);

	if (do_erase) {
	printf("Erase it all!\n");
#pragma omp parallel for collapse (2) schedule (static)
	for (int ch = 0; ch < max_ch; ch++) {
		for (int lun = 0; lun < max_lun; lun++) {
			/*printf("%u ", lun);*/
			for (int blk = skip_blk; blk < max_blk; blk++) {
				NVM_ADDR addr[geo.nplanes];
				NVM_RET ret;

				for (int pl = 0; pl < geo.nplanes; pl++) {
					addr[pl].g.ch = ch;
					addr[pl].g.lun = lun;
					addr[pl].g.blk = blk;
					addr[pl].g.pl = pl;
				}

				if (nvm_addr_erase(dev, addr, geo.nplanes, magic_flag, &ret)) {
					report[ch][lun][blk] = 0x10000;
/*					perror("erase failed");
					nvm_ret_pr(&ret);
					nvm_addr_pr(addr[0]);*/
				}
			}
		}
	}
	}

	if (do_write) {
	printf("Write it all!\n");
#pragma omp parallel for collapse (3) schedule (static)
	for (int ch = 0; ch < max_ch; ch++) {
		//printf("CH: %u\n", ch);
		for (int lun = 0; lun < max_lun; lun++) {
			//printf("LUN: %u\n", lun);
			for (int blk = skip_blk; blk < max_blk; blk++) {
				for (int pg = 0; pg < geo.npages; pg++) {
					NVM_ADDR addr[geo.nplanes * geo.nsectors];
					NVM_RET ret;

					for (int i = 0; i < geo.nplanes * geo.nsectors; i++) {
						addr[i].g.ch = ch;
						addr[i].g.lun = lun;
						addr[i].g.pg = pg;
						addr[i].g.blk = blk;

						addr[i].g.sec = i % geo.nsectors;
						addr[i].g.pl = i / geo.nsectors;
					}

					if (nvm_addr_write(dev, addr, geo.nplanes * geo.nsectors, buf, buf, magic_flag, &ret)) {
						report[ch][lun][blk] |= 0x1000;
/*						perror("write failed");
						nvm_ret_pr(&ret);
						nvm_addr_pr(addr[0]);*/
						//break;
					}
				}
			}
		}
	}
	}

	printf("Read it all!\n");
#pragma omp parallel for collapse (3) schedule (static)
	for (int ch = 0; ch < max_ch; ch++) {
		/*printf("CH: %u\n", ch);*/
		for (int lun = 0; lun < max_lun; lun++) {
			/*printf("LUN: %u\n", lun);*/
			for (int blk = skip_blk; blk < max_blk; blk++) {
				for (int pg = 0; pg < geo.npages; pg++) {
					NVM_ADDR addr[geo.nplanes * geo.nsectors];
					NVM_RET ret;

					for (int i = 0; i < geo.nplanes * geo.nsectors; i++) {
						addr[i].g.ch = ch;
						addr[i].g.lun = lun;
						addr[i].g.pg = pg;
						addr[i].g.blk = blk;

						addr[i].g.sec = i % geo.nsectors;
						addr[i].g.pl = i / geo.nsectors;
					}

					if (nvm_addr_read(dev, addr, geo.nplanes * geo.nsectors, buf, buf, magic_flag, &ret)) {
						report[ch][lun][blk]++;
/*						perror("read failed");
						nvm_ret_pr(&ret);
						nvm_addr_pr(addr[0]);*/
						//break;
					}
				}
			}
		}
	}

	free(buf);
	for (int ch = 0; ch < max_ch; ch++) {
		for (int lun = 0; lun < max_lun; lun++) {
			printf("CH: %u LUN: %u\n------------\n", ch, lun);
			for (int blk = 0; blk < max_blk; blk++) {
				if (!report[ch][lun][blk])
					continue;
				printf("%03u: %u %u %u\n", blk, (report[ch][lun][blk] & 0x10000) >> 16, (report[ch][lun][blk] & 0x1000) >> 12, report[ch][lun][blk] & 0xfff);
			}
		}
	}

	printf("\nStatistics:\n");
	printf("-----------\n");

	int erase_failures = 0;
	int write_failures = 0;
	int read_failures = 0;
	int failures = 0;

	for (int ch = 0; ch < max_ch; ch++) {
		for (int lun = 0; lun < max_lun; lun++) {
			for (int blk = 0; blk < max_blk; blk++) {
				if (report[ch][lun][blk] & 0x10000)
					erase_failures++;
				if (report[ch][lun][blk] & 0x1000)
					write_failures++;
				if (report[ch][lun][blk] & 0xfff)
					read_failures++;
				if (report[ch][lun][blk])
					failures++;
			}
		}
	}

	float max = max_ch * max_lun * max_blk;
	float total = 100 * ((max / (max - (float)failures)) - 1);

	printf("Erase failures     : %04u\n", erase_failures);
	printf("Write failures     : %04u\n", write_failures);
	printf("Read blk failures  : %04u\n", read_failures);

	printf("Total capacity     : %05u/%05u %02.02f%%\n", (max_ch * max_lun * max_blk), failures, total);


	return 0;
}

static struct argp_option opt_dev_verify[] =
{
	{"device", 'd', "DEVICE", 0, "LightNVM device e.g. nvme0n1"},
	{"dryrun", 'd', 0, 0, "Do a dryrun by not updating bad blocks when bad blocks are found."},
	{0}
};

static char doc_dev_verify[] =
		"\n\vExamples:\n"
		" Verify disk by writing to all good blocks and read them back. Marks blocks bad if found.\n"
		"  lnvm factory /dev/nvme0n1\n"
		" Verify disk with a dry-run. Only overwrite disk and read back data and report state. Do not mark blocks.\n"
		"  lnvm factory -d /dev/nvme0n1\n";

static error_t parse_dev_verify_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *args = state->input;

	switch (key) {
	case 'd':
		if (args->dry_run)
			argp_usage(state);
		args->dry_run = 1;
		args->arg_num++;
		break;
	case ARGP_KEY_ARG:
		if (args->arg_num > 2)
			argp_usage(state);
		break;
	case ARGP_KEY_END:
		if (args->arg_num < 0)
			argp_usage(state);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp argp_dev_verify = {opt_dev_verify, parse_dev_verify_opt,
							0, doc_dev_verify};

static void cmd_dev_verify(struct argp_state *state, struct arguments *args)
{
	int argc = state->argc - state->next + 1;
	char** argv = &state->argv[state->next - 1];
	char* argv0 = argv[0];

	argv[0] = malloc(strlen(state->name) + strlen(" verify") + 1);
	if(!argv[0])
		argp_failure(state, 1, ENOMEM, 0);

	sprintf(argv[0], "%s verify", state->name);

	argp_parse(&argp_dev_verify, argc, argv, ARGP_IN_ORDER, &argc, args);

	free(argv[0]);
	argv[0] = argv0;
	state->next += argc - 1;
}

const char *argp_program_version = "1.0";
const char *argp_program_bug_address = "Matias Bjørling <matias@cnexlabs.com>";
static char args_doc_global[] =
		"\nSupported commands are:\n"
		"  verify       Verify media\n";

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *args = state->input;

	switch (key) {
	case ARGP_KEY_ARG:
		if (strcmp(arg, "verify") == 0) {
			args->cmdtype = LIGHTNVM_DEV_VERIFY;
			cmd_dev_verify(state, args);
		}
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp argp = {NULL, parse_opt, "[<cmd> [CMD-OPTIONS]]",
							args_doc_global};

int main(int argc, char **argv)
{
	int ret = 0;
	struct arguments args = { 0 };

	argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &args);

	switch (args.cmdtype) {
	case LIGHTNVM_DEV_VERIFY:
		dev_verify(&args);
		break;
	default:
		printf("No valid command given.\n");
	}

	return ret;
}
