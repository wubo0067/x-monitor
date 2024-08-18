/*
 * @Author: CALM.WU
 * @Date: 2022-01-18 11:38:31
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2022-01-24 17:26:50
 */

#include "common.h"
#include "compiler.h"
#include "os.h"
#include "log.h"
#include "procfile.h"
#include "files.h"
#include "strings.h"
#include "consts.h"

uint32_t cpu_cores_num = 1;
uint32_t system_hz = 0;

static const char *__def_ipaddr = "0.0.0.0";
static const char *__def_macaddr = "00:00:00:00:00:00";
static const char *__def_hostname = "unknown";
static __thread char __hostname[HOST_NAME_MAX] = { 0 };
static const char __no_user[] = "";

const char *get_hostname()
{
	if (unlikely(0 == __hostname[0])) {
		if (unlikely(0 != gethostname(__hostname, HOST_NAME_MAX))) {
			strlcpy(__hostname, __def_hostname, HOST_NAME_MAX);
			__hostname[7] = '\0';
		}
	}
	return __hostname;
}

const char *get_ipaddr_by_iface(const char *iface, char *ip_buf,
				size_t ip_buf_size)
{
	if (unlikely(NULL == iface)) {
		return __def_ipaddr;
	}

	struct ifreq ifr;
	ifr.ifr_addr.sa_family = AF_INET;

	int32_t fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (unlikely(fd < 0)) {
		return __def_ipaddr;
	}

	strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
	ioctl(fd, SIOCGIFADDR, &ifr);

	close(fd);

	char *ip_addr =
		inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
	strlcpy(ip_buf, ip_addr, ip_buf_size);
	// ip_buf[ip_buf_size - 1] = '\0';

	return ip_buf;
}

const char *get_macaddr_by_iface(const char *iface, char *mac_buf,
				 size_t mac_buf_size)
{
	if (unlikely(NULL == iface)) {
		return __def_ipaddr;
	}

	struct ifreq ifr;
	ifr.ifr_addr.sa_family = AF_INET;

	int32_t fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (unlikely(fd < 0)) {
		return __def_macaddr;
	}

	strlcpy(ifr.ifr_name, iface, IFNAMSIZ);
	ioctl(fd, SIOCGIFHWADDR, &ifr);

	close(fd);

	uint8_t *mac = (uint8_t *)ifr.ifr_hwaddr.sa_data;

	snprintf(mac_buf, mac_buf_size, "%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n",
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	mac_buf[mac_buf_size - 1] = '\0';

	return mac_buf;
}

// 锁定内存限制 BCC 已经无条件地将此限制设置为无穷大，但 libbpf
// 不会自动执行此操作（按照设计）。
int32_t bump_memlock_rlimit(void)
{
	struct rlimit rlim_new = {
		.rlim_cur = RLIM_INFINITY,
		.rlim_max = RLIM_INFINITY,
	};

	return setrlimit(RLIMIT_MEMLOCK, &rlim_new);
}

const char *get_username(uid_t uid)
{
	struct passwd *pwd = getpwuid(uid);
	if (pwd == NULL) {
		return __no_user;
	}
	return pwd->pw_name;
}

uint32_t get_cpu_cores_num()
{
	// return sysconf(_SC_NPROCESSORS_ONLN); 加强移植性
	if (cpu_cores_num > 1)
		return cpu_cores_num;

	struct proc_file *pf_stat =
		procfile_open("/proc/stat", NULL, PROCFILE_FLAG_DEFAULT);
	if (unlikely(!pf_stat)) {
		error("Cannot open /proc/stat. Assuming system has %d processors. "
		      "error: %s",
		      cpu_cores_num, strerror(errno));
		return cpu_cores_num;
	}

	pf_stat = procfile_readall(pf_stat);
	if (unlikely(!pf_stat)) {
		error("Cannot read /proc/stat. Assuming system has %d __processors.",
		      cpu_cores_num);
		return cpu_cores_num;
	}

	cpu_cores_num = 0;
	for (size_t index = 0; index < procfile_lines(pf_stat); index++) {
		if (!procfile_linewords(pf_stat, index)) {
			continue;
		}
		if (strncmp(procfile_lineword(pf_stat, index, 0), "cpu", 3) ==
		    0) {
			cpu_cores_num++;
		}
	}

	cpu_cores_num--;
	if (cpu_cores_num < 1) {
		cpu_cores_num = 1;
	}

	procfile_close(pf_stat);

	debug("System has %d __processors.", cpu_cores_num);

	return cpu_cores_num;
}

int32_t read_tcp_mem(uint64_t *low, uint64_t *pressure, uint64_t *high)
{
	int32_t ret = 0;
	char rd_buffer[1024] = { 0 };
	char *start = NULL, *end = NULL;

	ret = read_file("/proc/sys/net/ipv4/tcp_mem", rd_buffer, 1023);
	if (unlikely(ret < 0)) {
		return ret;
	}

	start = rd_buffer;
	// *解析 C-stringstr 将其内容解释为指定内容的整数 base，它以 type 的值形式返回 unsigned
	// long long
	// *int。如果 endptr 不是空指针，该函数还会设置 endptr 指向数字后的第一个字符。
	*low = strtoull(start, &end, 10);

	start = end;
	*pressure = strtoull(start, &end, 10);

	start = end;
	*high = strtoull(start, &end, 10);

	debug("TCP MEM low = %lu, pressure = %lu, high = %lu", *low, *pressure,
	      *high);

	return 0;
}

__always_inline int32_t read_tcp_max_orphans(uint64_t *tcp_max_orphans)
{
	if (unlikely(read_file_to_uint64("/proc/sys/net/ipv4/tcp_max_orphans",
					 tcp_max_orphans) < 0)) {
		return -1;
	}

	debug("TCP max orphans = %lu", *tcp_max_orphans);
	return 0;
}

static __thread char __proc_pid_cmdline_file[XM_PROC_FILENAME_MAX] = { 0 };

int32_t read_proc_pid_cmdline(pid_t pid, char *cmdline, size_t size)
{
	int32_t fd;
	int32_t rc = 0;
	ssize_t bytes = 0;
	static const char *unknown_cmdline = "<unknown>";

	snprintf(__proc_pid_cmdline_file, XM_PROC_FILENAME_MAX - 1,
		 "/proc/%d/cmdline", pid);

	fd = open(__proc_pid_cmdline_file, O_RDONLY | O_NOFOLLOW, 0666);
	if (unlikely(fd == -1)) {
		rc = -1;
		goto __error;
	}

	bytes = read(fd, cmdline, size);
	close(fd);

	if (unlikely(bytes < 0)) {
		rc = -2;
		goto __error;
	}

	// ** 要特殊处理
	cmdline[bytes] = '\0';
	for (ssize_t i = 0; i < bytes; i++) {
		if (unlikely(!cmdline[i]))
			cmdline[i] = ' ';
	}

__error:
	if (rc != 0) {
		/*
         * The process went away before we could read its process name. Try
         * to give the user "<unknown>" here, but otherwise they get to look
         * at a blank.
         */
		if (strlcpy(cmdline, unknown_cmdline, size) >= size) {
			rc = -3;
		}
	}

	return rc;
}

/**
 * Get the number of ticks per second from the system
 */
void get_system_hz()
{
	long ticks;

	if ((ticks = sysconf(_SC_CLK_TCK)) < 0) {
		error("sysconf get _SC_CLK_TCK error: %s", strerror(errno));
		exit(-1);
	}
	system_hz = (uint32_t)ticks;
}

/**
 * It reads the first line of /proc/uptime, and returns the first number in that
 * line, multiplied by 100
 * /proc/uptime的进度是百分之一秒，统一规范为百分之一秒为单位
 * @return The uptime of the system in centiseconds.
 */
uint64_t get_uptime()
{
	FILE *fp = NULL;
	char line[XM_PROC_LINE_SIZE] = { 0 };
	uint32_t up_sec = 0, up_cent = 0;
	uint64_t uptime = 0;

	if (likely((fp = fopen("/proc/uptime", "r")) != NULL)) {
		if (likely(fgets(line, XM_PROC_LINE_SIZE, fp) != NULL)) {
			if (likely(sscanf(line, "%u.%u", &up_sec, &up_cent) ==
				   2)) {
				uptime = (uint64_t)up_sec * 100 + up_cent;
			}
		}
	}

	if (NULL != fp) {
		fclose(fp);
	}
	return uptime;
}

pid_t system_pid_max = 32768;
/**
 * Read the system's maximum pid value from /proc/sys/kernel/pid_max and return
 * it.
 *
 * @return The maximum number of processes that can be created on the system.
 */
pid_t get_system_pid_max()
{
	static uint8_t read = 0;

	if (likely(read)) {
		return system_pid_max;
	}
	read = 1;

	uint64_t max_pid = 0;
	if (unlikely(0 != read_file_to_uint64("/proc/sys/kernel/pid_max",
					      &max_pid))) {
		error("Cannot open file '/proc/sys/kernel/pid_max'. Assuming system "
		      "supports %d pids",
		      system_pid_max);
		return system_pid_max;
	}

	if (unlikely(!max_pid)) {
		error("Cannot parse file '/proc/sys/kernel/pid_max'. Assuming system "
		      "supports %d pids.",
		      system_pid_max);
		return system_pid_max;
	}

	system_pid_max = (pid_t)max_pid;
	return system_pid_max;
}

enum smaps_line_type {
	LINE_IS_NORMAL,
	LINE_IS_RSS,
	LINE_IS_PSS,
	// LINE_IS_PSS_ANON,
	// LINE_IS_PSS_FILE,
	// LINE_IS_PSS_SHMEM,
	LINE_IS_USS,
};

static __always_inline bool __check_line_is_rss(const char *line)
{
	if (line[0] == 'R' && line[1] == 's' && line[2] == 's' &&
	    line[3] == ':')
		return true;
	return false;
}

static __always_inline bool __check_line_is_pss(const char *line)
{
	if (line[0] == 'P' && line[1] == 's' && line[2] == 's') {
		if (line[3] == ':')
			return true;
		if (line[3] == '_' && line[4] == 'A' && line[5] == 'n' &&
		    line[6] == 'o' && line[7] == 'n' && line[8] == ':')
			return true;
		if (line[3] == '_' && line[4] == 'F' && line[5] == 'i' &&
		    line[6] == 'l' && line[7] == 'e' && line[8] == ':')
			return true;
		if (line[3] == '_' && line[4] == 'S' && line[5] == 'h' &&
		    line[6] == 'm' && line[7] == 'e' && line[8] == 'm' &&
		    line[9] == ':')
			return true;
	}
	return false;
}

static __always_inline bool __check_line_is_uss(const char *line)
{
	if (line[0] == 'P' && line[1] == 'r' && line[2] == 'i' &&
	    line[3] == 'v' && line[4] == 'a' && line[5] == 't' &&
	    line[6] == 'e' && line[7] == '_') {
		if (line[8] == 'C' || line[8] == 'D') {
			return true;
		}
	}
	return false;
}

static __always_inline uint64_t __get_mss_val(char *line)
{
	uint64_t val = 0;

	while (*line) {
		if (*line >= '0' && *line <= '9') {
			val *= 10;
			val += *line - '0';
		} else if (val > 0) {
			return val;
		}
		line++;
	}
	return val;
}

struct SmapsLineTagInfo {
	enum smaps_line_type type;
	bool (*check_line)(const char *line);
};

static struct SmapsLineTagInfo __smaps_line_tags[] = {
	{
		LINE_IS_RSS,
		__check_line_is_rss,
	},
	{
		LINE_IS_PSS,
		__check_line_is_pss,
	},
	// {
	//     LINE_IS_PSS_ANON,
	//     __check_line_is_pss,
	// },
	// {
	//     LINE_IS_PSS_FILE,
	//     __check_line_is_pss,
	// },
	// {
	//     LINE_IS_PSS_SHMEM,
	//     __check_line_is_pss,
	// },
	{
		LINE_IS_USS,
		__check_line_is_uss,
	},
};

// https://www.jianshu.com/p/8203457a11cc
// https://www.kernel.org/doc/Documentation/ABI/testing/procfs-smaps_rollup

// TODO: read /proc/self/smaps_rollup

static const char *__proc_pid_smaps_fmt = "/proc/%d/smaps";
static const char *__proc_pid_smaps_rollup_fmt = "/proc/%d/smaps_rollup";

int32_t get_mss_from_smaps(pid_t pid, struct smaps_info *info)
{
	if (info->smaps_fd <= 0) {
		char f_smaps_path[XM_PROC_FILENAME_MAX] = { 0 };
		char f_smaps_rollup_path[XM_PROC_FILENAME_MAX] = { 0 };

		snprintf(f_smaps_path, XM_PROC_FILENAME_MAX,
			 __proc_pid_smaps_fmt, pid);
		snprintf(f_smaps_rollup_path, XM_PROC_FILENAME_MAX,
			 __proc_pid_smaps_rollup_fmt, pid);

		__builtin_memset(info, 0, sizeof(struct smaps_info));

		// 判断文件是否存在
		if (file_exists(f_smaps_rollup_path)) {
			info->smaps_fd = open(f_smaps_rollup_path, O_RDONLY);

			if (unlikely(-1 == info->smaps_fd)) {
				error("open smaps '%s' failed",
				      f_smaps_rollup_path);
				return -1;
			}

		} else {
			info->smaps_fd = open(f_smaps_path, O_RDONLY);

			if (unlikely(-1 == info->smaps_fd)) {
				error("open smaps '%s' failed", f_smaps_path);
				return -1;
			}
		}
	} else {
		lseek(info->smaps_fd, 0, SEEK_SET);
	}

	char block_buf[XM_PROC_CONTENT_BUF_SIZE];
	ssize_t rest_bytes = 0, read_bytes = 0, line_size = 0;
	char *cursor = NULL, *line_end = NULL;
	uint8_t *match_tags = (uint8_t *)calloc(ARRAY_SIZE(__smaps_line_tags),
						sizeof(uint8_t));

#define __CLEAN_TAGS(tags)                                                     \
	do {                                                                   \
		for (uint32_t i = 0; i < ARRAY_SIZE(__smaps_line_tags); ++i) { \
			tags[i] = 0;                                           \
		}                                                              \
	} while (0)

	__CLEAN_TAGS(match_tags);

	// block read
	while ((read_bytes = read(info->smaps_fd, block_buf + rest_bytes,
				  XM_PROC_CONTENT_BUF_SIZE - rest_bytes - 1)) >
	       0) {
		rest_bytes += read_bytes;
		block_buf[rest_bytes] = '\0';
		cursor = block_buf;

		while ((line_end = strchr(cursor, '\n')) != NULL) {
			line_size = line_end - cursor + 1;
			*line_end = '\0';

			// parse line
			for (size_t i = 0; i < ARRAY_SIZE(__smaps_line_tags);
			     i++) {
				if (0 == match_tags[i] &&
				    __smaps_line_tags[i].check_line(cursor)) {
					switch (__smaps_line_tags[i].type) {
					case LINE_IS_RSS:
						info->rss +=
							__get_mss_val(cursor);
						match_tags[i] = 1;
						break;
					case LINE_IS_PSS:
						info->pss +=
							__get_mss_val(cursor);
						match_tags[i] = 1;
						break;
					// case LINE_IS_PSS_ANON:
					//     info->pss_anon += __get_mss_val(cursor);
					//     match_tags[i] = 1;
					//     break;
					// case LINE_IS_PSS_FILE:
					//     info->pss_file += __get_mss_val(cursor);
					//     match_tags[i] = 1;
					//     break;
					// case LINE_IS_PSS_SHMEM:
					//     info->pss_shmem += __get_mss_val(cursor);
					//     match_tags[i] = 1;
					//     break;
					case LINE_IS_USS:
						info->uss +=
							__get_mss_val(cursor);
						match_tags[i] = 1;
						break;
					}
					break;
				}
			}
			__CLEAN_TAGS(match_tags);

			rest_bytes -= line_size;
			if (rest_bytes > 0) {
				cursor = line_end + 1;
			} else {
				break;
			}
		}

		if (rest_bytes > 0) {
			memmove(block_buf, cursor, rest_bytes);
		}
	}

	free(match_tags);

	if (read_bytes < 0) {
		error("read smaps failed");
		close(info->smaps_fd);
		info->smaps_fd = 0;
		return -1;
	}

	return 0;
}

static void __get_all_childpids(pid_t ppid,
				struct process_descendant_pids *pd_pids)
{
	int32_t read_size = 0;
	int32_t child_pids_size = 0;
	char proc_children_file[XM_PROC_CHILDREN_FILENAME_SIZE] = { 0 };
	char proc_children_line[XM_PROC_CHILDREN_LINE_SIZE] = { 0 };
	uint64_t children_pids[XM_CHILDPID_COUNT_MAX] = { 0 };

	// ** /proc/pid/task/tid/children读取该文件，分解为pid列表
	snprintf(proc_children_file, XM_PROC_CHILDREN_FILENAME_SIZE,
		 "/proc/%d/task/%d/children", ppid, ppid);

	read_size = read_file(proc_children_file, proc_children_line,
			      XM_PROC_CHILDREN_LINE_SIZE - 1);
	if (likely(read_size > 0)) {
		child_pids_size =
			strsplit_to_nums(proc_children_line, " ", children_pids,
					 XM_CHILDPID_COUNT_MAX);

		if (likely(child_pids_size > 0)) {
			pd_pids->pids = (pid_t *)realloc(
				pd_pids->pids,
				sizeof(pid_t) *
					(pd_pids->pids_size + child_pids_size));
			if (unlikely(!pd_pids->pids)) {
				error("realloc child_pids failed");
				return;
			}
			// child level
			for (int32_t i = 0; i < child_pids_size; i++) {
				pd_pids->pids[pd_pids->pids_size] =
					(pid_t)children_pids[i];
				pd_pids->pids_size++;
			}

			// grandson level
			for (int32_t i = 0; i < child_pids_size; i++) {
				__get_all_childpids(children_pids[i], pd_pids);
			}
		}
	}
}

int32_t get_process_descendant_pids(pid_t pid,
				    struct process_descendant_pids *pd_pids)
{
	if (unlikely(NULL == pd_pids || pid < 0)) {
		return -1;
	}

	__get_all_childpids(pid, pd_pids);

	return pd_pids->pids_size;
}

int32_t get_block_device_sector_size(const char *disk)
{
	char *block_size_full_path = NULL;

	int32_t ret = asprintf(&block_size_full_path,
			       "/sys/block/%s/queue/logical_block_size", disk);
	if (likely(-1 != ret)) {
		// 判断文件是否存在
		if (likely(file_exists(block_size_full_path))) {
			// 读取文件
			int64_t sector_size = 0;
			if (likely(0 == read_file_to_int64(block_size_full_path,
							   &sector_size))) {
				ret = (int32_t)sector_size;
			}
		} else {
			ret = -ENOENT;
			error("file %s not exists", block_size_full_path);
		}
		free(block_size_full_path);
	}
	return ret;
}

/**
 * Gets the default gateway and interface for the current network.
 *
 * @param addr The address of the default gateway.
 * @param iface The interface of the default gateway.
 *
 * @returns 0 on success, -1 on failure.
 */
int32_t get_default_gateway_and_iface(in_addr_t *addr, char *iface)
{
	uint32_t destination = 0;
	int32_t nread = 0, gateway = 0;

	FILE *fp = fopen("/proc/net/route", "r");
	if (unlikely(!fp)) {
		error("open /proc/net/route failed, reason: %s",
		      strerror(errno));
		return -1;
	}

	// skip the header line
	if (fscanf(fp, "%*[^\n]\n") < 0) {
		fclose(fp);
		return -1;
	}

	for (;;) {
		nread = fscanf(fp, "%s%X%X\n", iface, &destination, &gateway);
		if (nread != 3) {
			break;
		}

		if (0 == destination) {
			*addr = gateway;
			fclose(fp);
			return 0;
		}
	}

	if (fp) {
		// default route not found
		fclose(fp);
	}
	return -1;
}
