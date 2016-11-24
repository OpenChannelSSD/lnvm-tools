#include "lnvm.h"

static int dev_verify(int fd, struct arguments *args)
{
	NVM_DEV dev;
	NVM_GEO geo;

	dev = nvm_dev_open("nvme0n1");
	if (!dev) {
		printf("My life is not worth living anymore\n");
		return -EINVAL;
	}
	geo = nvm_dev_attr_geo(dev);
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

	free(argv[0]);
	argv[0] = argv0;
	state->next += argc - 1;
}

const char *argp_program_version = "1.0";
const char *argp_program_bug_address = "Matias Bjørling <mb@lightnvm.io>";
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
	char dev[FILENAME_MAX] = "/dev/lightnvm/control";
	int fd, ret = 0;
	struct arguments args = { 0 };

	argp_parse(&argp, argc, argv, ARGP_IN_ORDER, NULL, &args);

	fd = open(dev, O_WRONLY);
	if (fd < 0) {
		printf("Failed to open LightNVM mgmt %s. Error: %d\n",
								dev, fd);
		return -EINVAL;
	}

	switch (args.cmdtype) {
	case LIGHTNVM_DEV_VERIFY:
		dev_verify(fd, &args);
		break;
	default:
		printf("No valid command given.\n");
	}

	close(fd);

	return ret;
}
