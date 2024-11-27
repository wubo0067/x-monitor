import time
import os
import math

cpu_stats = ['yld_count',
             'sched_count', 'sched_goidle',
             'ttwu_count', 'ttwu_local',
             'rq_cpu_time',
             'rq_sched_info.run_delay', 'rq_sched_info.pcount']
cpu_idle_types = ['CPU_IDLE', 'CPU_NOT_IDLE', 'CPU_NEWLY_IDLE']
domain_idle_stats = ['lb_count',
                     'lb_balanced',
                     'lb_failed',
                     'lb_imbalance',
                     'lb_gained',
                     'lb_hot_gained',
                     'lb_nobusyq',
                     'lb_nobusyg']
domain_stats = ['alb_count', 'alb_failed', 'alb_pushed',
                'sbe_count', 'sbe_balanced', 'sbe_pushed',
                'sbf_count', 'sbf_balanced', 'sbf_pushed',
                'ttwu_wake_remote', 'ttwu_move_affine',
                'ttwu_move_balance']

SCHEDSTAT = '/proc/schedstat'

# Terminal handling is taken from http://blog.taz.net.au/2012/04/09/getting-the-terminal-size-in-python/
def get_terminal_width(fd = 1):
    if not os.isatty(fd):
        return 999

    try:
        import fcntl, termios, struct
        hw = struct.unpack('hh', fcntl.ioctl(fd, termios.TIOCGWINSZ, '1234'))
        return hw[1]
    except:
        return os.environ.get('COLUMNS', 80)
    return 80

# Parses list of integer values from incoming strings in `values`
def parse_stats(stats, values, params):
    for param in params:
        value = int(values.pop(0))
        stats[param] = value

# Reads /proc/schedstat once and builds hierarchial dict from
# incoming values with values. Returns that dictionary
# TODO: filtering on per-cpu, per-domain, per-idle-class and per-parameter bsis
def read_schedstat():
    cpu = None
    stats = {}

    with open(SCHEDSTAT) as inf:
        for line in inf:
            values = line.strip().split()
            valgroup = values.pop(0)

            if valgroup == 'version':
                version = int(values.pop(0))
                if version != 15:
                    raise ValueError('Cannot parse schedstat version %d' % version)
            elif valgroup.startswith('cpu'):
                cpu = valgroup

                stats[cpu] = {}
                stats[cpu]['stats'] = cpustats = {}

                parse_stats(cpustats, values, cpu_stats)
            elif valgroup.startswith('domain'):
                stats[cpu][valgroup] = domstats = {}

                values.pop(0)        # cpumask

                for idletype in cpu_idle_types:
                    domstats[idletype] = idlestats = {}
                    parse_stats(idlestats, values, domain_idle_stats)
                parse_stats(domstats, values, domain_stats)

    return stats

# Recursively substitute stat2 - stat1 values for hierarchial dicts
# coming from read_schedstat(). If values wasn't changed, ignore them
def diff_stats(stat1, stat2, prefix = ''):
    diff = {}
    for k in stat1.keys():
        v1 = stat1[k]
        v2 = stat2[k]
        kprefix = prefix + '.' + k

        if isinstance(v1, dict) and isinstance(v2, dict):
            diff.update(diff_stats(v1, v2, kprefix))
        elif v1 != v2:
            diff[kprefix] = v2 - v1
    return diff

# Substitute current and baseline values based on per-cpu basis
# if no new data available for CPU, ignore its data
def diff_sched_stats(baseline, current):
    scheddiff = {}
    diffparams = set()

    for cpu in baseline:
        # Check if cpu has been offlined and not shown in current
        if cpu not in current:
            continue

        diff = diff_stats(baseline[cpu], current[cpu])

        if diff:
            scheddiff[cpu] = diff
            diffparams = diffparams.union(diff.keys())

    return scheddiff, list(sorted(diffparams))

# Main loop
baseline = read_schedstat()

while True:
    # Sleep till new data will become available after 1 second
    # TODO: customizable intervals and count like *stat do
    now = time.time()
    time.sleep(math.ceil(now) - now)

    current = read_schedstat()

    # Calculate
    scheddiff, diffparams = diff_sched_stats(baseline, current)

    # Generate format string and sort cpu list
    cpus = sorted(scheddiff.keys(),
                  key = lambda cpuname: int(cpuname[3:]))
    paramlen = max(map(len, diffparams))

    maxstatlen = get_terminal_width() - paramlen
    cpus_per_line = maxstatlen / 8
    cpulines = int((len(cpus) * 1.5) / cpus_per_line)

    # Chunck cpus into blocks according to terminal length
    cpublocks = []
    for firstcpu in xrange(0, len(cpus), cpus_per_line):
        lastcpu = firstcpu + cpus_per_line
        cpublocks.append(cpus[firstcpu:lastcpu])

    print

    for cpus in cpublocks:
        fmtstr = '{:%d}' % paramlen + ' {:^7}' * len(cpus)

        # Print header
        print fmtstr.format(*([time.ctime()] + cpus))

        for param in diffparams:
            values = [scheddiff[cpu].get(param, '')
                      for cpu in cpus]

            # If any of values was changed, print them
            if any(values):
                print fmtstr.format(*([param] + values))

    baseline = current