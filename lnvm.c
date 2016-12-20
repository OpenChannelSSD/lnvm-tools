#include "lnvm.h"
#include <omp.h>
#include <sys/time.h>

struct for_each_conf {
	int max_ch;
	int max_lun;
	int max_blk;
	int skip_blk;
	int op;
	int show_time;
	int flag;
	int dry_run;
	void *data;
	void *meta;
};

static int rw_blk(struct nvm_dev *dev, const struct nvm_geo *geo, int op, int ch, int lun, int blk, int show_time, void *data, void *meta, int flag)
{
	int r = 0;
	int total = 0;
	struct timeval t1, t2;
	double time = 0.0;

	if (show_time)
		gettimeofday(&t1, NULL);

	for (int pg = 0; pg < geo->npages; pg++) {
		struct nvm_addr addr[geo->nplanes * geo->nsectors];
		struct nvm_ret ret;

		for (int i = 0; i < geo->nplanes * geo->nsectors; i++) {
			addr[i].g.ch = ch;
			addr[i].g.lun = lun;
			addr[i].g.pg = pg;
			addr[i].g.blk = blk;

			addr[i].g.sec = i % geo->nsectors;
			addr[i].g.pl = i / geo->nsectors;
		}

		if (op == 0) {
			r = nvm_addr_read(dev, addr, geo->nplanes * geo->nsectors, data, meta, flag, &ret);
		} else {
			char *vd = data;
			vd[0] = 0x3;
			vd[1] = 0x1;
			vd[2] = 0x3;
			vd[3] = 0x3;
			vd[4] = 0x7;
			r = nvm_addr_write(dev, addr, geo->nplanes * geo->nsectors, data, meta, flag, &ret);
		}

		if (r) {
			if (ret.result != 0x700)
				total++;
		}
/*			perror("write failed");
			nvm_addr_pr(addr[0]);
			//break;*/
	}

	if (show_time) {
		gettimeofday(&t2, NULL);

		time = (t2.tv_sec - t1.tv_sec) * 1000.0;
		time += ((t2.tv_usec - t1.tv_usec)) / 1000.0;
		printf("(%02u,%02u,%03u): avg.time: %f ms (total: %f ms)\n", ch, lun, blk,
				(time / (double)(geo->npages * geo->nplanes * geo->nsectors)) * (geo->nplanes * geo->nsectors), time);
	}
	return total;
}

static int erase_blk(struct nvm_dev *dev, const struct nvm_geo *geo, int ch, int lun, int blk, int show_time, int flag)
{
	struct nvm_addr addr[geo->nplanes];
	struct nvm_ret ret;
	struct timeval t1, t2;
	double time = 0.0;
	int r;

	for (int pl = 0; pl < geo->nplanes; pl++) {
		addr[pl].g.ch = ch;
		addr[pl].g.lun = lun;
		addr[pl].g.blk = blk;
		addr[pl].g.pl = pl;
	}

	if (show_time)
		gettimeofday(&t1, NULL);

	r = nvm_addr_erase(dev, addr, geo->nplanes, flag, &ret);
	if (r) {
/*		perror("erase failed");
		nvm_ret_pr(&ret);
		nvm_addr_pr(addr[0]);*/
	}

	if (show_time) {
		gettimeofday(&t2, NULL);

		time = (t2.tv_sec - t1.tv_sec) * 1000.0;
		time = (t2.tv_usec - t1.tv_usec) / 1000.0;
		printf("(%02u,%02u,%03u): avg.time: %f ms\n", ch, lun, blk, time);
	}

	return r;
}

static int for_each_blk(struct nvm_dev *dev, const struct nvm_geo *geo, struct for_each_conf *fec, int report[geo->nchannels][geo->nluns][geo->nblocks])
{
	int ret;

#pragma omp parallel for collapse (2) schedule (static)
	for (int ch = 0; ch < fec->max_ch; ch++) {
		for (int lun = 0; lun < fec->max_lun; lun++) {
			struct nvm_bbt* bbt;
			struct nvm_ret bbt_ret;
			struct nvm_addr bbt_addr;

			bbt_addr.ppa = 0;
			bbt_addr.g.ch = ch;
			bbt_addr.g.lun = lun;

			bbt = nvm_bbt_get(dev, bbt_addr, &bbt_ret);
			if (!bbt) {
				perror("Could not retrieve bad block table");
				nvm_ret_pr(&bbt_ret);
				continue;
			}

			for (int blk = fec->skip_blk; blk < fec->max_blk; blk++) {
				int skip = 0;
				/* bad block check */
				for (int pl = 0; pl < geo->nplanes; pl++) {
					if (bbt->blks[(blk * geo->nplanes) + pl]) {
						printf("(%02u,%02u,%03u): skip\n", ch, lun, blk);
						skip = 1;
						report[ch][lun][blk] = 0x100000;
						break;
					}
				}

				if (skip)
					continue;

				switch (fec->op) {
				case 0:
					ret = rw_blk(dev, geo, fec->op, ch, lun, blk, fec->show_time, fec->data, fec->meta, fec->flag);
					if (ret)
						report[ch][lun][blk] += ret;
					break;
				case 1:
					ret = rw_blk(dev, geo, fec->op, ch, lun, blk, fec->show_time, fec->data, fec->meta, fec->flag);
					if (ret)
						report[ch][lun][blk] = 0x1000;
					break;
				case 2:
					ret = erase_blk(dev, geo, ch, lun, blk, fec->show_time, fec->flag);
					if (ret)
						report[ch][lun][blk] = 0x10000;
					break;
				}

				if (report[ch][lun][blk]) {
					struct nvm_addr addr[geo->nplanes];

					for (int pl = 0; pl < geo->nplanes; pl++) {
						addr[pl].ppa = 0;
						addr[pl].g.ch = ch;
						addr[pl].g.lun = lun;
						addr[pl].g.blk = blk;
						addr[pl].g.pl = pl;
					}
					if (fec->dry_run) {
						printf("(%02u,%02u,%03u): marked bad (dry_run)\n", ch, lun, blk);
					}
					else {
						nvm_bbt_mark(dev, addr, geo->nplanes, 0x2, NULL);
						printf("(%02u,%02u,%03u): marked bad\n", ch, lun, blk);
					}
					report[ch][lun][blk] = 0x1000000;
				}

			}
			free(bbt->blks);
			free(bbt);
		}
	}

	return 0;
}

static void print_statistics(const struct nvm_geo *geo, int max_ch, int max_lun, int max_blk, int skip_blk, int report[geo->nchannels][geo->nluns][geo->nblocks])
{
	/* Statistics */
	printf("Begin block notications\n");
	printf("[CH,LN,BLK]: E W RDS\n");
	for (int ch = 0; ch < max_ch; ch++) {
		for (int lun = 0; lun < max_lun; lun++) {
			for (int blk = 0; blk < max_blk; blk++) {
				if (!report[ch][lun][blk])
					continue;
				printf("[%02u,%02u,%03u]: %u %u %u\n", ch, lun, blk, (report[ch][lun][blk] & 0x10000) >> 16, (report[ch][lun][blk] & 0x1000) >> 12, report[ch][lun][blk] & 0xfff);
			}
		}
	}
	printf("End block notifications\n");

	printf("\nStatistics:\n");
	printf("-----------\n");

	int erase_failures = 0;
	int write_failures = 0;
	int read_failures = 0;
	int skips = 0;
	int markedbad = 0;
	int failures = 0;

	for (int ch = 0; ch < max_ch; ch++) {
		for (int lun = 0; lun < max_lun; lun++) {
			for (int blk = 0; blk < max_blk; blk++) {
				if (report[ch][lun][blk] & 0x1000000)
					markedbad++;
				if (report[ch][lun][blk] & 0x100000)
					skips++;
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
	float total = 100 * (1 / ((max - skip_blk) / (float)failures));

	printf("Erase failures     : %04u\n", erase_failures);
	printf("Write failures     : %04u\n", write_failures);
	printf("Read blk failures  : %04u\n", read_failures);
	printf("Skipped            : %04u\n", skips);
	printf("Marked bad         : %04u\n", markedbad);

	printf("Total capacity     : %05u/%05u %02.02f%%\n", (max_ch * max_lun * max_blk) - skip_blk, failures, total);
}

static int dev_verify(struct arguments *args)
{
	struct nvm_dev *dev;
	const struct nvm_geo *geo;
	struct for_each_conf fec;
	int max_ch, max_lun, max_blk, skip_blk;
	void *buf;

	dev = nvm_dev_open(args->devname);
	if (!dev) {
		printf("Could not open device.\n");
		return -EINVAL;
	}
	geo = nvm_dev_attr_geo(dev);

	nvm_dev_pr(dev);
	nvm_geo_pr(geo);

	skip_blk = 0;
	max_ch = geo->nchannels;
	max_lun = geo->nluns;
	max_blk = geo->nblocks;

	if (args->max_ch_set)
		max_ch = args->max_ch + 1;
	if (args->max_lun_set)
		max_lun = args->max_lun + 1;
	if (args->max_blk_set)
		max_blk = args->max_blk + 1;
	if (args->skip_blk)
		skip_blk = args->skip_blk;

	int report[geo->nchannels][geo->nluns][geo->nblocks];
	memset(&report, 0, sizeof(report));

	/* Parameters end */
	buf = nvm_buf_alloc(geo, geo->vpg_nbytes);

	fec.max_ch = max_ch;
	fec.max_lun = max_lun;
	fec.max_blk = max_blk;
	fec.skip_blk = skip_blk;
	fec.data = buf;
	fec.meta = buf;
	fec.flag = geo->nplanes >> 1;
	fec.show_time = args->show_time;
	fec.dry_run = args->dry_run;

	if (args->plane_hint) {
		if (geo->nplanes < args->plane_hint) {
			printf("Plane hint not supported. Will use: %x\n", fec.flag);
		} else {
			fec.flag = args->plane_hint >> 1;
			printf("Setting plane hint: %x\n", fec.flag);
		}
	}

	if (args->do_erase) {
		fec.op = 2;
		printf("Performing erases\n");
		for_each_blk(dev, geo, &fec, report);
	}

	if (args->do_write) {
		fec.op = 1;
		printf("Performing writes\n");
		for_each_blk(dev, geo, &fec, report);
	}

	if (args->do_read) {
		fec.op = 0;
		printf("Performing reads\n");
		for_each_blk(dev, geo, &fec, report);
	}

	print_statistics(geo, fec.max_ch, fec.max_lun, fec.max_blk, fec.skip_blk, report);

	free(buf);
	return 0;
}

static struct argp_option opt_dev_verify[] =
{
	{"device", 'd', "DEVICE", 0, "e.g. /dev/nvme0n1"},
	{"dryrun", 'n', 0, 0, "Do a dryrun by not updating bad blocks when bad blocks are found."},
	{"reads", 'r', 0, 0, "Do read test"},
	{"writes", 'w', 0, 0, "Do write test"},
	{"erases", 'e', 0, 0, "Do erase test"},
	{"timings", 't', 0, 0, "Show timings per block"},
	{"maxch", 'c', "max_ch", 0, "Limit channels to 0..X"},
	{"maxlun", 'l', "max_lun", 0, "Limit LUNs to 0..Y"},
	{"maxblk", 'b', "max_blk", 0, "Limit Blocks to 0..Z"},
	{"skipblk", 's', "skip_blk", 0, "Skip first blocks to X..BLKS"},
	{"planehint", 'p', "plane_hint", 0, "1 Single plane, 2 dual plane, 4 quad plane"},
	{0}
};

static char doc_dev_verify[] =
		"\n\vExamples:\n"
		" Verify disk by writing to all good blocks and read them back. Marks blocks bad if found.\n"
		"  lnvm verify /dev/nvme0n1\n"
		" Verify disk with a dry-run. Only overwrite disk and read back data and report state. Do not mark blocks.\n"
		"  lnvm verify -d /dev/nvme0n1\n";

static error_t parse_dev_verify_opt(int key, char *arg, struct argp_state *state)
{
	struct arguments *args = state->input;

	switch (key) {
	case 'd':
		if (!arg || args->devname)
			argp_usage(state);
		if (strlen(arg) > DISK_NAME_LEN) {
			printf("Argument too long\n");
			argp_usage(state);
		}
		args->devname = arg;
		args->arg_num++;
		break;
	case 'n':
		if (args->dry_run)
			argp_usage(state);
		args->dry_run = 1;
		args->arg_num++;
		break;
	case 'r':
		if (args->do_read)
			argp_usage(state);
		args->do_read = 1;
		args->rw_defined = 1;
		args->arg_num++;
		break;
	case 'w':
		if (args->do_write)
			argp_usage(state);
		args->do_write = 1;
		args->rw_defined = 1;
		args->arg_num++;
		break;
	case 'e':
		if (args->do_erase)
			argp_usage(state);
		args->do_erase = 1;
		args->rw_defined = 1;
		args->arg_num++;
		break;
	case 't':
		if (args->show_time)
			argp_usage(state);
		args->show_time = 1;
		args->arg_num++;
		break;
	case 'c':
		if (!arg || args->max_ch_set)
			argp_usage(state);
		args->max_ch_set = 1;
		args->max_ch = atoi(arg);
		args->arg_num++;
		break;
	case 'l':
		if (!arg || args->max_lun_set)
			argp_usage(state);
		args->max_lun_set = 1;
		args->max_lun = atoi(arg);
		args->arg_num++;
		break;
	case 'b':
		if (!arg || args->max_blk_set)
			argp_usage(state);
		args->max_blk_set = 1;
		args->max_blk = atoi(arg);
		args->arg_num++;
		break;
	case 's':
		if (!arg || args->skip_blk)
			argp_usage(state);
		args->skip_blk = atoi(arg);
		args->arg_num++;
		break;
	case 'p':
		if (!arg || args->plane_hint)
			argp_usage(state);
		args->plane_hint = atoi(arg);
		args->arg_num++;
		break;
	case ARGP_KEY_ARG:
		if (args->arg_num > 9)
			argp_usage(state);
		break;
	case ARGP_KEY_END:
		if (!args->devname)
			argp_usage(state);
		if (args->arg_num < 1)
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

	if (!args->rw_defined) {
		args->do_read = 1;
		args->do_write = 1;
		args->do_erase = 1;
	}

	free(argv[0]);
	argv[0] = argv0;
	state->next += argc - 1;
}

void test_plane(struct nvm_dev *dev, const struct nvm_geo *geo, struct for_each_conf *fec, int report[geo->nchannels][geo->nluns][geo->nblocks],
				int rflag, int wflag, int eflag)
{
	fec->flag = eflag;
	fec->op = 2;
	printf("Performing erases\n");
	for_each_blk(dev, geo, fec, report);

	fec->flag = wflag;
	fec->op = 1;
	printf("Performing writes\n");
	for_each_blk(dev, geo, fec, report);

	fec->flag = rflag;
	fec->op = 0;

	for (int i = 0; i < 5; i++) {
		printf("Performing reads [%u/10]\n", i);
		for_each_blk(dev, geo, fec, report);
	}

	print_statistics(geo, fec->max_ch, fec->max_lun, fec->max_blk, fec->skip_blk, report);
}

static int dev_plane(struct arguments *args)
{
	struct nvm_dev *dev;
	const struct nvm_geo *geo;
	struct for_each_conf fec;
	int max_ch, max_lun, max_blk, skip_blk;
	void *buf;

	dev = nvm_dev_open(args->devname);
	if (!dev) {
		printf("Could not open device.\n");
		return -EINVAL;
	}
	geo = nvm_dev_attr_geo(dev);

	nvm_dev_pr(dev);
	nvm_geo_pr(geo);

	skip_blk = 0;
	max_ch = geo->nchannels;
	max_lun = geo->nluns;
	max_blk = geo->nblocks;

	if (args->max_ch_set)
		max_ch = args->max_ch +1;
	if (args->max_lun_set)
		max_lun = args->max_lun + 1;
	if (args->max_blk_set)
		max_blk = args->max_blk + 1;
	if (args->skip_blk)
		skip_blk = args->skip_blk;

	int report[geo->nchannels][geo->nluns][geo->nblocks];
	memset(&report, 0, sizeof(report));

	/* Parameters end */
	buf = nvm_buf_alloc(geo, geo->vpg_nbytes);

	fec.max_ch = max_ch;
	fec.max_lun = max_lun;
	fec.max_blk = max_blk;
	fec.skip_blk = skip_blk;
	fec.data = buf;
	fec.meta = buf;
	fec.flag = geo->nplanes >> 1;
	fec.show_time = args->show_time;

	/* Test 1 Simple */
	printf("1. Single Erase, Write, Read Test\n");
	printf("---------------------------------\n");

	test_plane(dev, geo, &fec, report, 0x0, 0x0, 0x0);
	memset(&report, 0, sizeof(report));

	printf("1. Dual Erase, Write, Read Test\n");
	printf("---------------------------------\n");
	test_plane(dev, geo, &fec, report, 0x1, 0x1, 0x1);
	memset(&report, 0, sizeof(report));

	if (geo->nplanes == 4) {
		printf("1. Quad Erase, Write, Read Test\n");
		printf("---------------------------------\n");
		test_plane(dev, geo, &fec, report, 0x2, 0x2, 0x2);
	}
	memset(&report, 0, sizeof(report));

	/* Test 2 Single write/erase, quad read */
	printf("2. Single Erase, Write, Read Test\n");
	printf("---------------------------------\n");

	test_plane(dev, geo, &fec, report, 0x0, 0x0, 0x0);
	memset(&report, 0, sizeof(report));


	printf("2. Single Erase, Write. Dual Read Test\n");
	printf("---------------------------------\n");
	test_plane(dev, geo, &fec, report, 0x0, 0x0, 0x1);
	memset(&report, 0, sizeof(report));

	if (geo->nplanes == 4) {
		printf("2. Single Erase, Write. Quad Read Test\n");
		printf("---------------------------------\n");
		test_plane(dev, geo, &fec, report, 0x0, 0x0, 0x2);
		memset(&report, 0, sizeof(report));
	}

	/* Test 3 Single write/erase, quad read */
	printf("3. Single Erase, Write, Read Test\n");
	printf("---------------------------------\n");

	test_plane(dev, geo, &fec, report, 0x0, 0x0, 0x0);
	memset(&report, 0, sizeof(report));

	printf("3. Dual Erase, Write. Single Read Test\n");
	printf("---------------------------------\n");
	test_plane(dev, geo, &fec, report, 0x1, 0x1, 0x0);
	memset(&report, 0, sizeof(report));

	if (geo->nplanes == 4) {
		printf("3. Quad Erase, Write. Single Read Test\n");
		printf("---------------------------------\n");
		test_plane(dev, geo, &fec, report, 0x2, 0x2, 0x0);
		memset(&report, 0, sizeof(report));
	}

	free(buf);

	return 0;
}

static struct argp_option opt_dev_plane[] =
{
	{"device", 'd', "DEVICE", 0, "e.g. /dev/nvme0n1"},
	{"reads", 'r', 0, 0, "Do read test"},
	{"writes", 'w', 0, 0, "Do write test"},
	{"erases", 'e', 0, 0, "Do erase test"},
	{"timings", 't', 0, 0, "Show timings per block"},
	{"maxch", 'c', "max_ch", 0, "Limit channels to 0..X"},
	{"maxlun", 'l', "max_lun", 0, "Limit LUNs to 0..Y"},
	{"maxblk", 'b', "max_blk", 0, "Limit Blocks to 0..Z"},
	{"skipblk", 's', "skip_blk", 0, "Skip first blocks to X..BLKS"},
	{0}
};

static char doc_dev_plane[] =
		"\n\vExamples:\n"
		" Verify read consistency over different types of plane hint option.\n"
		"  lnvm plane /dev/nvme0n1\n";

static struct argp argp_dev_plane = {opt_dev_plane, parse_dev_verify_opt,
							0, doc_dev_plane};

static void cmd_dev_plane(struct argp_state *state, struct arguments *args)
{
	int argc = state->argc - state->next + 1;
	char** argv = &state->argv[state->next - 1];
	char* argv0 = argv[0];

	argv[0] = malloc(strlen(state->name) + strlen(" plane") + 1);
	if(!argv[0])
		argp_failure(state, 1, ENOMEM, 0);

	sprintf(argv[0], "%s plane", state->name);

	argp_parse(&argp_dev_plane, argc, argv, ARGP_IN_ORDER, &argc, args);

	if (!args->rw_defined) {
		args->do_read = 1;
		args->do_write = 1;
		args->do_erase = 1;
	}

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
		if (strcmp(arg, "plane") == 0) {
			args->cmdtype = LIGHTNVM_DEV_PLANE;
			cmd_dev_plane(state, args);
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
	case LIGHTNVM_DEV_PLANE:
		dev_plane(&args);
		break;
	default:
		printf("No valid command given.\n");
	}

	return ret;
}
