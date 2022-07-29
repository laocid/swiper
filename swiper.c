/*****************************************************************
** Live wallpaper engine written in C for Linux.                **
**                                                              **
** Requirements: feh, ffmpeg, ffprobe.                          **
**                                                              **
** Description: Accepts video files, extracts its frames at     **
** custom frame rates, resolutions and formats, and dynamically **
** displays these frames with precision and optimisation.       **
**                                                              **
** Started Jun 4 2022; Finished Jun 12 2022.                    **
**                                                              **
** Written by laocid.                                           **
*****************************************************************/

/*TODO:
- Precise error messages (use errno)
- Clean termination only occurs for inside of swiper_execute_wallpaper()
	(use sighandler... but without global variables, how?) */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <dirent.h>
#include <mntent.h>
#include <regex.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include <pwd.h>
#include <errno.h>

/* SIZES */
#define FIELD_LEN 64
#define LINE_LEN 128
#define FILE_LEN 256
#define PATH_LEN 512

/* DO NOT TOUCH */
#define PROC_DIR "/proc/"
#define PROC_ARGS "/cmdline"
#define SUDO_ENV "SUDO_USER"
#define MATCH_STR "frame="
#define OPTSTR "s:cPdr:fai:w:h:p:"
#define MNT_SZ 1000000000

/* CONFIGURABLE */
#define SREGXP ".*oswip.*"
#define TFSMP "/mnt/swiper"
#define SWIPER ".swiper"
#define MDFN ".metadata"
#define NUNITS 25

/* FLAGS */
#define F_SAVE 1
#define F_RUN 2
#define F_DAEMONIZE 4
#define F_WIDTH 8
#define F_HEIGHT 16
#define F_CACHE 32
#define F_FORCE 64
#define F_RFPS 128
#define F_PNG 256
#define F_PFPS 512
#define F_INSPECT 1024

/* Video info */
struct metadata
{
	char *name;
	int width, height; // ...in pixels
	char *rfps, *pfps; // (render, playback)
	char format[4];
	double duration; // ...in seconds
};

/* Paths */
struct pathinfo
{
	char *a_path; // ...of run frames (for -c)
	char *s_path; // ...of saved frames
	char *v_path; // ...of video file
};

/* Special swiper functions */
void swiper_show_help();
void swiper_init_pre(struct metadata *, struct pathinfo *);
void swiper_init_post(short int, struct metadata *, struct pathinfo *);
short int swiper_parse_opts(int, char **, struct metadata *, struct pathinfo *);
void swiper_safety_protocol(short int, struct metadata *, struct pathinfo *);
void swiper_request_metadata(struct metadata *, char *);
char *swiper_resolve_mdfield(char *, char *);
void swiper_save_metadata(struct metadata *, struct pathinfo *);
void swiper_load_metadata(struct metadata *, short int, char *);
void swiper_print_md(struct metadata *, short int);
void swiper_render_frames(struct metadata *, struct pathinfo *);
void swiper_save_action(char *, int);
char **swiper_retrieve_image_names(int *, char *, char *);
void swiper_shave_s_path(char *, int, char *);
void swiper_execute_wallpaper(char **, int, char *, double);
void swiper_shutdown(struct metadata *, struct pathinfo *, char **, int);

/* Generic functions */
void feh_display_wallpaper(char *, char *);
int is_duplicate_proc(char *);
char *filename(char *);
void cleardir(char *);
void copydir(char *, char *);
int lateral_dir_visfile_isempty(char *);
int lateral_dir_visfile_size(char *);
int lateral_dir_visfile_count(char *);
void rolling_umount(char *);
int real_username(char **);
double frstr2double(char *);
int is_num_str(char *);
void sighandler(int);
void die(char *);
void dief(char *, ...);

int term = 0;

int main(int argc, char *argv[])
{
	struct sigaction sa;
	struct metadata md;
	struct pathinfo pi;
	char **files = NULL;
	short int flags;
	int nfiles;
	double dfps;

	if(argc == 1)
	{
		swiper_show_help();
		exit(EXIT_SUCCESS);
	}

	sa.sa_handler = sighandler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	// init1/2: allocate memory, set default values
	swiper_init_pre(&md, &pi); 

	opterr = 0;

	// primarily for setting arguments via optarg
	if((flags = swiper_parse_opts(argc, argv, &md, &pi)) < 0)
		dief("duplicate option, -%c", flags);
	else if(!flags)
		die("unrecognised option or missing argument.");
	
	// check directories exist, check options and args are valid format
	swiper_safety_protocol(flags, &md, &pi);

	// init2/2: flag and optarg reliant variables
	swiper_init_post(flags, &md, &pi);

	// override and negate, -s, -a
	if(flags & F_INSPECT)
	{
		swiper_request_metadata(&md, pi.v_path); // ...original metadata
		printf("metadata:\n\tname: %s\n", md.name);
		swiper_print_md(&md, flags);
	}
	else // allow both -s, -a
	{
		if(flags & F_SAVE)
		{
			swiper_request_metadata(&md, pi.v_path); // ...custom metadata
			cleardir(pi.s_path);
			swiper_save_metadata(&md, &pi); // -a mode needs to know
			printf("saving %s as:\n", md.name); 
			swiper_print_md(&md, flags);
			printf("this might take a while...\n");
			swiper_render_frames(&md, &pi);
		}
		if(flags & F_RUN)
		{
			if(flags & F_CACHE) 
			{
				rolling_umount(TFSMP); // protect against duplicate mounts

				// change mount size by changing size=???, and value of MNT_SZ
				if(mount("tmpfs", TFSMP, "tmpfs", 0, "size=1G,mode=0777") == -1)
				{
					printf("errno: %d\n", errno);
					dief("failed to mount tmpfs at %s", TFSMP);
				}
				cleardir(TFSMP);
				printf("caching frames...\n");
				copydir(pi.a_path, pi.s_path);
			}
			swiper_load_metadata(&md, flags, pi.s_path); // mainly to retrieve rfps
			if((files = swiper_retrieve_image_names(&nfiles, pi.s_path, md.format)) == NULL)
				die("low memory; manage system processes.");
			dfps = frstr2double(md.pfps);
			printf("applying wallpaper at %.2lffps:\n", dfps);
			swiper_print_md(&md, flags); // <== this is why dot file stores not only rfps
			if(flags & F_DAEMONIZE)
				if(daemon(1, 0))
					die("failed to daemonize process");
			swiper_execute_wallpaper(files, nfiles, pi.a_path, dfps);
		}
	}

	swiper_shutdown(&md, &pi, files, nfiles);

	return 0;
}

/* Help menu */
void swiper_show_help()
{
    printf("usage: swiper [-i <video-file] [-s <video-file> [-r <render-fps>][-w <width>]\n\t[-h <height>][-P]] [-a [-d][-c][-p <playback-fps>]]\n");
	printf("\t-i: inspect video metadata\n");
    printf("\t-s: save live wallpaper\n");
    printf("\t-P: save as png frames; jpeg by default (with -s)\n");
    printf("\t-w: width of resolution in pixels (with -s)\n");
    printf("\t-h: height of resolution in pixels (with -s)\n");
    printf("\t-r: set render fps (with -s)\n");
    printf("\t-c: cache frames in memory (with -a)\n");
    printf("\n\t-a: apply saved wallpaper\n");
    printf("\t-d: daemonize process (with -a)\n");
    printf("\t-f: forcibly ignore duplicate processes\n");
    printf("\t-p: display at alternate playback fps (with -a)\n");
    printf("examples:\n");
    printf("\tswiper -s ~/Videos/234878.gif\n");
	printf("\tswiper -i 05-06-97.avi\n");
    printf("\tswiper -a\n");
    printf("\tswiper -s ./joyster.mov -r 29.98 -P\n");
    printf("\tswiper -s ~kruz/298983.mp4 -w 1280 -h 720\n");
    printf("\tswiper -adc\n");
    printf("\tswiper -s ../lightning.mp4 -adf\n");
    printf("\tswiper -s 90s-synth.gif -r 442/10 -P -ad -p 30\n");
	printf("\n%cWritten by laocid.\n", (unsigned char) 189);
}

/* Allows swiper_execute_wallpaper() to terminate cleanly. */
void sighandler(int sig) { term = 1; }

/* Protect against memory leaks */
void swiper_shutdown(struct metadata *md, struct pathinfo *pi, char **files, int n)
{
	if(md->name != NULL)
		free(md->name);
	if(md->rfps != NULL)
		free(md->rfps);
	if(md->pfps != NULL)
		free(md->pfps);
	if(pi->a_path != NULL)
		free(pi->a_path);
	if(pi->s_path != NULL)
		free(pi->s_path);
	if(pi->v_path != NULL)
		free(pi->v_path);
	if(files != NULL)
	{
		for(int i = 0; i < n; ++i) 
        	free(files[i]);
        free(files);
	}
}

/* Initial data initialisation */
void swiper_init_pre(struct metadata *md, struct pathinfo *pi)
{
	char *username = NULL;

	md->name = calloc(FILE_LEN+1, 1);
	md->width = -1;
	md->height = -1;
	md->rfps = calloc(FIELD_LEN+1, 1);
	md->pfps = calloc(FIELD_LEN+1, 1);
	strncpy(md->format, "jpg", 4);

	pi->a_path = calloc(PATH_LEN+1, 1);
	pi->s_path = calloc(PATH_LEN+1, 1);
	pi->v_path = calloc(PATH_LEN+1, 1);

	if(real_username(&username))
		die("failed to retrieve username");
	snprintf(pi->s_path, PATH_LEN, "/home/%s/%s", username, SWIPER);
	//free(username);
}

/* Init data with option arguments and detect duplicate options */
short int swiper_parse_opts(int argc, char **argv, struct metadata *md, struct pathinfo *pi)
{
    short int flags = 0;
    int opt;

    while((opt = getopt(argc, argv, OPTSTR)) != -1)
    {
        switch(opt)
        {
			case 'i': if(flags & F_INSPECT) return -opt;
				else { flags |= F_INSPECT; strncpy(pi->v_path, optarg, PATH_LEN); } break;
            case 's': if(flags & F_SAVE) return -opt;
				else { flags |= F_SAVE; strncpy(pi->v_path, optarg, PATH_LEN); } break;
            case 'r': if(flags & F_RFPS) return -opt; 
				else { flags |= F_RFPS; strncat(md->rfps, optarg, FIELD_LEN); } break;
            case 'w': if(flags & F_WIDTH) return -opt;
				else { flags |= F_WIDTH; md->width = atoi(optarg); } break;
            case 'h': if(flags & F_HEIGHT) return -opt;
				else { flags |= F_HEIGHT; md->height = atoi(optarg); } break;
            case 'P': if(flags & F_PNG) return -opt; 
				else { flags |= F_PNG; strncpy(md->format, "png", 4); } break;
            case 'a': if(flags & F_RUN) return -opt; else flags |= F_RUN; break;
            case 'c': if(flags & F_CACHE) return -opt; else flags |= F_CACHE; break;
            case 'd': if(flags & F_DAEMONIZE) return -opt; else flags |= F_DAEMONIZE; break;
            case 'p': if(flags & F_PFPS) return -opt; 
				else { flags |= F_PFPS; strncat(md->pfps, optarg, FIELD_LEN); } break;
            case 'f': 
				if(flags & F_FORCE) return -opt; else flags |= F_FORCE; break;
			case '?':
				return 0;
        }
    }

    return flags;
}

/* Define option precedence and perform option and argument validation as
 * a safety net for successive code. Any future functions should refer to
 * this function to keep efficiency in mind. */
void swiper_safety_protocol(short int flags, struct metadata *md, struct pathinfo *pi)
{
	struct stat sb;

	if(!(flags & F_FORCE))
		if(is_duplicate_proc(SREGXP) > 1)
        	dief("duplicate process detected (like %s)", SREGXP);

	if(!(flags & F_SAVE) && !(flags & F_RUN) & !(flags & F_INSPECT))
		die("must inspect (-i), save (-s), or apply wallpaper (-a)");
	
	if((flags & F_INSPECT) && (flags & (F_SAVE|F_RUN)))
		die("must inspect (-i) as a standalone operation\n");
	
	if(!(flags & F_SAVE) && flags & (F_CACHE|F_RFPS|F_WIDTH|F_HEIGHT|F_PNG))
	{
		if(flags & F_RFPS)
			die("incompatible option, -r, requires -s");
		if(flags & F_WIDTH)
			die("incompatible option, -w, requires -s");
		if(flags & F_HEIGHT)
			die("incompatible option, -h, requires -s");
		if(flags & F_PNG)
			die("incompatible option, -P, requires -s");
	}

	if(!(flags & F_RUN) && flags & (F_RFPS|F_WIDTH|F_HEIGHT|F_PNG))
	{
		if(flags & F_CACHE)
			die("incompatible option, -c, requires -a");
		if(flags & F_DAEMONIZE)
			die("incompatible option, -d, requires -a");
		if(flags & F_PFPS)
			die("incompatible option, -p, requires -a");
	}

	if(flags & F_INSPECT || flags & F_SAVE)
		if(stat(pi->v_path, &sb) == -1)
			dief("no such file, '%s'", pi->v_path);
		
	if(flags & F_SAVE)
	{
		if(stat(pi->s_path, &sb) == -1)
			mkdir(pi->s_path, 0700);

		if(flags & F_RFPS)
			if(!is_num_str(md->rfps))
				dief("invalid format for argument of, -%c", 'r');
	}

	if(flags & F_RUN)
	{
		if(lateral_dir_visfile_isempty(pi->s_path))
			die("no wallpaper saved, use '-s <video-file>'");

		if(flags & F_PFPS)
			if(!is_num_str(md->pfps))
				dief("invalid format for argument of, -%c", 'r');
			

		if(flags & F_CACHE)
		{
			if(getuid())
				die("must run as superuser for, -c");
			if(lateral_dir_visfile_size(pi->s_path) >= MNT_SZ)
				dief("not enough space to cache frames in %s", TFSMP);
			if(stat(TFSMP, &sb) == -1)
				mkdir(TFSMP, 0700);
		}
	}
}

/* Init data which requires swiper_parse_opts() and swiper_safety_check()
 * to run first. */
void swiper_init_post(short int flags, struct metadata *md, struct pathinfo *pi)
{
	if(flags & F_INSPECT || flags & F_SAVE)
		strncpy(md->name, filename(pi->v_path), FILE_LEN);
		
	if(flags & F_CACHE)
		strncat(pi->a_path, TFSMP, PATH_LEN);
	else
		strncat(pi->a_path, pi->s_path, PATH_LEN);
}

/* Get username corresponding to euid, unless uid is 0 - hence 
 * "real username" */
int real_username(char **username)
{
    uid_t uid;
	struct passwd *pw;
    int len = 32; 

    *username = calloc(len+1, 1); 

    // root id = 0
    if((uid = getuid()))
    {   
        if(!(pw = getpwuid(uid)))
            return 1;
        strncat(*username, pw->pw_name, len);
    }   
    else
    {   
        if(!(*username = getenv(SUDO_ENV)))
            return 1;
    }   
    return 0;
}

/* Returns number of processes with name matching regex
    e.g. ".*htop.*" */
int is_duplicate_proc(char *regexp)
{
    int n = 0;
    DIR *dir;
    struct dirent *ent;
    char *filepath, *line, *proc;
    regex_t r;

    if(regcomp(&r, regexp, 0))
        die("failed in regcomp()");

    filepath = calloc(PATH_LEN+1, 1);
    line = calloc(LINE_LEN+1, 1);

    dir = opendir(PROC_DIR);

    while((ent = readdir(dir)) != NULL)
    {
        FILE *fp;

        // skip non-numeric directories
        if(!is_num_str(ent->d_name) || ent->d_type != DT_DIR)
            continue;

        snprintf(filepath, PATH_LEN, "%s/%s/%s", PROC_DIR, ent->d_name, PROC_ARGS);

		// skip non-proc directories
        if((fp = fopen(filepath, "r")) == NULL)
			continue;

        fgets(line, LINE_LEN, fp);
        proc = calloc(LINE_LEN+1, 1);
        strncat(proc, strtok(line, " "), LINE_LEN); // get argv[0] of process name

        if(!regexec(&r, proc, 0, NULL, 0))
            n++;

        fclose(fp); free(proc);
    }

    closedir(dir); free(filepath); free(line);

    return n;
}

/* Determine whether or not a string can be cleanly converted into an
 * number e.g. fmt="%d" or "%lf", for integer and float respectively. */
int is_num_str(char *str)
{
    char period = 0;

    for(int i = 0; str[i] != '\0'; ++i)
    {
        if(str[i] >= 48 && str[i] >= 57)
            continue;
        if(str[i] == '.' && period++)
                return 0;
    }
    return 1;
}

/* Convert fractional string into double type */
double frstr2double(char *frstr)
{
    double dfps;
    char tmp[FIELD_LEN+1]; // strchr() and strtok() modify string..?

    strncpy(tmp, frstr, FIELD_LEN);
    tmp[FIELD_LEN] = '\0';
    if(strchr(tmp, '/') == NULL) return atof(frstr);

    strncpy(tmp, frstr, FIELD_LEN);
    tmp[FIELD_LEN] = '\0';
    dfps = atof(strtok(tmp, "/"));
    dfps /= atof(strtok(NULL, "/"));

    return dfps;
}

/* Get filename of filepath; accepts relative paths as well */
char *filename(char *filepath)
{
    char *fn;
    int i, n = 0;

    fn = calloc(FILE_LEN+1, 1);

    for(i = 0; i < strlen(filepath); ++i)
    {
        if(filepath[i] == '/')
            n = i+1;
    }
    if(n == i) // invalid file path, ends with /
        return NULL;
    else if(!n) // invalid file path, has no /
        return NULL;

    strncpy(fn, filepath+n, FILE_LEN);

    return fn;
}

/* Determine whether or not a directory is empty */
int lateral_dir_visfile_isempty(char *path)
{
    DIR *dir = opendir(path);
    struct dirent *ent;
    for(int i = 0;(ent = readdir(dir)) != NULL; ++i)
    {
		if(*(ent->d_name) == '.')
			i--;
        if(i > 0)
            return 0;
    }
	closedir(dir);
    return 1;
}

/* Calculate total size in bytes, including first-layer, non-hidden files */
int lateral_dir_visfile_size(char *dirpath)
{
	DIR *dir = opendir(dirpath);
    struct dirent *ent;
    struct stat sb;
    unsigned long int size;
    char *filepath;

    filepath = calloc(PATH_LEN+1, sizeof(char));
    
    while((ent = readdir(dir)) != NULL)
    {   
        snprintf(filepath, PATH_LEN, "%s/%s", dirpath, ent->d_name);
        stat(filepath, &sb);
        size += sb.st_size;
    }

    closedir(dir);
    return size;
}

/* Fill struct metadata *md using ffprobe. */
void swiper_request_metadata(struct metadata *md, char *v_path)
{
	char *value;

	value = swiper_resolve_mdfield(v_path, "duration");
	md->duration = atof(value); free(value);
	if(md->width == -1)
	{
		value = swiper_resolve_mdfield(v_path, "width");
		md->width = atoi(value); free(value);
	}
	if(md->height == -1)
	{
		value = swiper_resolve_mdfield(v_path, "height");
		md->height = atoi(value); free(value); // why is there two 'height'
	}
	if(*(md->rfps) == '\0')
	{
		value = swiper_resolve_mdfield(v_path, "avg_frame_rate");
		strncpy(md->rfps, value, FIELD_LEN);
		free(value);
	}
}

/* Get values of metadata fields requested from ffprobe. */
char *swiper_resolve_mdfield(char *v_path, char *field)
{
	FILE *fp;
	char *cmd, *value;
	int len = PATH_LEN+128;

	cmd = calloc(len+1, 1);
	value = calloc(LINE_LEN+1, 1);

	snprintf(cmd, len, "ffprobe -v 0 -of csv=p=0 -select_streams v:0 -show_entries stream=%s %s", field, v_path);

	if((fp = popen(cmd, "r")) == NULL)
		dief("failed to open pipe, '%s'", cmd);
	if(fgets(value, LINE_LEN, fp) == NULL)
		dief("extracting metadata from, '%s'", v_path);
	value[strcspn(value, "\n")] = '\0';
	pclose(fp);

	return value;
}

/* Save data in struct metadata *md at MDFN (see macros). */
void swiper_save_metadata(struct metadata *md, struct pathinfo *pi)
{
	FILE *fp;
	char *filepath;

	filepath = calloc(PATH_LEN+1, 1);
	snprintf(filepath, PATH_LEN, "%s/%s", pi->s_path, MDFN);
	if((fp = fopen(filepath, "w")) == NULL)
		dief("failed to open file, '%s'", filepath);
	fprintf(fp, "%s %s %d %d %.4lf %s", md->name, md->rfps, md->width, md->height, md->duration, md->format);
	fclose(fp); free(filepath);
}

/* Print metadata of video with units */
void swiper_print_md(struct metadata *md, short int flags)
{
		printf("\twidth: %dpx\n", md->width);
		printf("\theight: %dpx\n", md->height);
		if(flags & F_INSPECT)
			printf("\tfps: %.2lffps\n", frstr2double(md->rfps));
		else
			printf("\trender fps: %.2lffps\n", frstr2double(md->rfps));
		if(flags & F_RUN)
			printf("\tplayback fps: %.2lffps\n", frstr2double(md->pfps));
		if(!(flags & F_INSPECT))
			printf("\tformat: %s\n", md->format);
		printf("\tduration: %.2lfs\n", md->duration);
}

/* Delete all content listed in directory at dirpath */
void cleardir(char *dirpath)
{
	DIR *dir;
	struct dirent *ent;
	char *filepath;

	if((dir = opendir(dirpath)) == NULL)
		dief("failed to open directory, '%s'", dirpath);
	filepath = calloc(PATH_LEN+1, 1);
	while((ent = readdir(dir)) != NULL)
	{
		snprintf(filepath, PATH_LEN, "%s/%s", dirpath, ent->d_name);
		if(strcmp(ent->d_name, "..") && strcmp(ent->d_name, "."))
			remove(filepath);
	}
}

/* Convert video file into many image frames and store at pi->s_path */
void swiper_render_frames(struct metadata *md, struct pathinfo *pi)
{
	int nfr, len; 
	char *cmd;

	len = (PATH_LEN * 2) + 32;
	cmd = calloc(len+1, 1);

	snprintf(cmd, len, "ffmpeg -i %s -r %s -vf scale=%d:%d %s/%%04d.%s -hide_banner 2>&1", pi->v_path, md->rfps, md->width, md->height, pi->s_path, md->format);

	// truncation is trivial
	nfr = md->duration * frstr2double(md->rfps);

	swiper_save_action(cmd, nfr);
}

/* Run ffmpeg to convert video into image frames, and parse output into an
 * ASCII progress bar. */
void swiper_save_action(char *cmd, int nfr)
{
	FILE *fp;
    int fd, x, frame;
    size_t len;
    char *long_line, *bar, c[1];
    double pcnt;

    len = LINE_LEN * 2;
    long_line = malloc(len+1);

    if((fp = popen(cmd, "r")) == NULL) { die("popen()"); }
    fd = fileno(fp); // blocking for popen() only works with read()

	// skip output until got "frame = ..." line
    while(strstr(long_line, MATCH_STR) == NULL)
    {
        memset(long_line, '\0', len+1);
        *c = 1;
        for(x = 0; *c != '\n' && *c != '\r' && x < len; ++x) 
        {
            read(fd, (void *) c, 1); 
            long_line[x] = *c; 
        }
        if(x >= len)
            die("unexpected ffmpeg output");
    }   

    bar = calloc(NUNITS+1, 1); 
    do  
    {   
        strtok(long_line, "="); 
        sscanf(strtok(NULL, "="), " %d ", &frame);

		// NUNITS is mutable, but if longer than printf line, it won't overwrite progress bar
        for(int j = 0; j < NUNITS; ++j) 
        {
            if((double) frame / nfr >= (double) j / NUNITS)
                bar[j] = '#';
            else
                bar[j] = '-';
        }
        if(x >= len) // dangerous exit condition (could also signify unexpected output)
		    pcnt = 100.00;

        pcnt = ((double) frame / nfr) * 100;

		// don't make NUNITS longer than this
        printf("\r%3.2lf%% %s", (pcnt >= 100) ? 100.00 : pcnt, bar); 
        fflush(stdout);

        memset(long_line, '\0', len+1);
        *c = 1;
        for(x = 0; *c != '\r' && x < len; ++x)
        {
            read(fd, c, 1);
            long_line[x] = *c;
        }
    } while(pcnt < 100.00);
    fflush(stdin); // carriage return line is not always instantaneous
    printf("\n");
    pclose(fp);
}

/* Unmounts all mounted filesystems at some mount point, char *mp */
void rolling_umount(char *mp)
{
    struct mntent *me;
    FILE *ptr;

    ptr = setmntent("/etc/mtab", "r+");
    while((me = getmntent(ptr)) != NULL)
    {
        if(!strcmp(mp, me->mnt_dir))
            umount(mp);
    }
    endmntent(ptr);
}

/* Copy files in a directory to another directory without recursion. */
void copydir(char *dst, char *src)
{
    DIR *dir = opendir(src);
    struct dirent *ent;
    FILE *sptr, *dptr;
    char sfile[PATH_LEN+1], dfile[PATH_LEN+1];
    int c;

    while((ent = readdir(dir)) != NULL)
    {
        if(strcmp(ent->d_name, "..") && strcmp(ent->d_name, ".") && ent->d_type == DT_REG)
        {
            snprintf(sfile, PATH_LEN, "%s/%s", src, ent->d_name);
            snprintf(dfile, PATH_LEN, "%s/%s", dst, ent->d_name);
            if((sptr = fopen(sfile, "r")) == NULL)
				dief("failed to open, '%s'", sfile);
            if((dptr = fopen(dfile, "w")) == NULL)
				dief("failed to open, '%s'", dfile);
            while((c = fgetc(sptr)) != EOF)
                fputc(c, dptr);
            fclose(sptr);
            fclose(dptr);
        }
    }
}

/* Store file names of video frames in an array. */
char **swiper_retrieve_image_names(int *nfiles, char *dirpath, char *format)
{
	DIR *dir;
	struct dirent *ent;
	int len;
	char **files;

	dir = opendir(dirpath);
	*nfiles = lateral_dir_visfile_count(dirpath);
	len = (sizeof(int) * 4) + 5;

	// cap frames at 9999 (like %04d), or cause overflow later in function
	if(*nfiles > 9999) 
	{
		swiper_shave_s_path(dirpath, *nfiles, format);
		*nfiles = 9999;
	}

	files = (char **) malloc(*nfiles * sizeof(char *));
	if(files == NULL)
		return NULL;
	for(int i = 0; (ent = readdir(dir)) != NULL; ++i)
	{
		if(*(ent->d_name) != '.' && ent->d_type == DT_REG)
		{
			files[i] = calloc(len, 1);
			if(files[i] == NULL)
				return NULL;
			snprintf(files[i], len, "%04d.%s", i+1, format);
		}
		else i--;
	}
	return files;
}

/* Read file, MDFN, (see macros) into struct metadata md as defined in main() */
void swiper_load_metadata(struct metadata *md, short int flags, char *s_path)
{
	FILE *fp;
	char c;
    int i;
    char *filepath, *line;

	filepath = calloc(PATH_LEN+1, 1);
	line = calloc(LINE_LEN+1, 1);

    snprintf(filepath, PATH_LEN, "%s/%s", s_path, MDFN);

    if((fp = fopen(filepath, "r")) == NULL)
        dief("failed loading %s", MDFN);

    for(i = 0; (c = fgetc(fp)) != EOF && i < LINE_LEN; ++i)
        line[i] = c;

    strncpy(md->name, strtok(line, " "), FILE_LEN);
    strncpy(md->rfps, strtok(NULL, " "), FIELD_LEN);
	if(!(flags & F_PFPS))
    	strncpy(md->pfps, md->rfps, FIELD_LEN);
    md->width = atoi(strtok(NULL, " "));
    md->height = atoi(strtok(NULL, " "));
    md->duration = atof(strtok(NULL, " "));
    strncpy(md->format, strtok(NULL, " "), 4);

    fclose(fp);
}

/* Count lateral, visible files in a directory i.e. first-layer, non-hidden 
 * files. */
int lateral_dir_visfile_count(char *dirpath)
{
	struct dirent *ent;
    DIR *dir;
    int n;

	if((dir = opendir(dirpath)) == NULL)
		dief("failed to open directory, '%s'", dirpath);

    for(n = 0; (ent = readdir(dir)) != NULL;)
    {
        if(ent->d_type == DT_REG && *(ent->d_name) != '.')
            n++;
    }
    closedir(dir);
    return n;
}

/* Limit files from 0000.ext to 9999.ext (since "ffmpeg -i ... %04d.ext ..."),
 * and char **files are constructed no by reading a directory, but by
 * constructing a list of files like %04d.jpg, using the number of files in the
 * direcotry to determine the last file name in a lexiographic sequence. */
void swiper_shave_s_path(char *s_path, int n, char *format)
{
	char *filepath;

	filepath = calloc(PATH_LEN+9, 1);

	for(int i = 10000; i <= n; ++i)
	{
		snprintf(filepath, PATH_LEN+8, "%s/%d.%s", s_path, i, format);
		if(remove(filepath) == -1) die("fatal bug; unrecognised file path");
	}
}

/* Display image frames at md->a_path in order, on loop to create the 
 * apperance of a live wallpaper. */
void swiper_execute_wallpaper(char **files, int n, char *a_path, double dfps)
{
	clock_t start, end;
	struct timespec reg, res;
	long n_delay;

	reg.tv_sec = 0;
	n_delay = (1 / dfps) * pow(10, 9); // convert dfps into nanoseconds delay

	while(1)
	{
		if(term) break;
		for(int i = 0; i < n; ++i)
		{
			reg.tv_nsec = n_delay;
			start = clock();
			if(term) break;
			feh_display_wallpaper(files[i], a_path);
			end = clock();

			// deduct time spent in if(term) and display_wallpaper() from delay
			reg.tv_nsec -= (((double) end - start) / CLOCKS_PER_SEC) * pow(10,9);

			if(reg.tv_nsec < 0)
				continue;
			nanosleep(&reg, &res);
		}
	}
}

/* Display a single wallpaper at a_path/image */
void feh_display_wallpaper(char *image, char *a_path)
{
	int len;
	char *argv[4];

	len = strlen(a_path) + strlen(image) + 2;
	argv[0] = "feh";
    argv[1] = "--bg-scale";
    argv[2] = calloc(len+1, 1);
    snprintf(argv[2], len, "%s/%s", a_path, image);
    argv[3] = NULL;

	 // so parent won't zombify child processes
    signal(SIGCHLD, SIG_IGN);

    // many times faster than fork()
    if(!vfork())
    {
        execvp(argv[0], argv);
        exit(EXIT_SUCCESS);
    }

    free(argv[2]);
}

/* Display error message and exit program */
void die(char *err_msg)
{
    printf("Error: %s\n", err_msg);
    exit(EXIT_FAILURE);
}

/* Same as die() but 'err_msg' is can be a format string and a variable 
 * argument list is acceptable. Currently, only accept %s, %d, %c with
 * no additional values (e.g. %3d and %.2d are invalid) */
void dief(char *err_fmt, ...)
{
    va_list ap; 
    char c;

    printf("Error: ");
    va_start(ap, err_fmt);
    while ((c = *err_fmt))
    {   
        if(c == '%')
        {
            err_fmt++;
            switch (*err_fmt)
            {
                case 's':
                    printf("%s", va_arg(ap, char *));
                    break;
                case 'd':
                    printf("%d", va_arg(ap, int));
                    break;
                case 'c':
                    printf("%c", (char) va_arg(ap, int));
                    break;
                case '%':
                    printf("%%");
            }
        }
        else printf("%c", c); 
        err_fmt++;
    }   
    va_end(ap);
    printf("\n");
    exit(EXIT_FAILURE);
}

