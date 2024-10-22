/*
 * @Author: CALM.WU
 * @Date: 2023-03-25 12:43:28
 * @Last Modified by: CALM.WU
 * @Last Modified time: 2023-12-15 16:17:35
 */

package collector

import (
	"bytes"
	"encoding/binary"
	"strconv"
	"sync"
	"time"

	"github.com/cilium/ebpf"
	"github.com/cilium/ebpf/ringbuf"
	"github.com/emirpasic/gods/sets/hashset"
	"github.com/golang/glog"
	"github.com/mitchellh/mapstructure"
	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/sanity-io/litter"
	calmutils "github.com/wubo0067/calmwu-go/utils"
	"xmonitor.calmwu/plugin_ebpf/exporter/collector/bpfmodule"
	"xmonitor.calmwu/plugin_ebpf/exporter/config"
	"xmonitor.calmwu/plugin_ebpf/exporter/internal/bpfutil"
	"xmonitor.calmwu/plugin_ebpf/exporter/internal/eventcenter"
	"xmonitor.calmwu/plugin_ebpf/exporter/internal/utils"
)

type cpuSchedEBpfProgArgs struct {
	config.EBpfProgBaseArgs
	OffCPUMinDurationMillsecs int `mapstructure:"offcpu_min_millsecs"` // offcpu 状态的持续时间阈值，毫秒
	OffCPUMaxDurationMillsecs int `mapstructure:"offcpu_max_millsecs"` // offcpu 状态的持续时间阈值，毫秒
	OffCPUTaskType            int `mapstructure:"offcpu_task_type"`    // offcpu 状态的任务类型
}

type cpuSchedPrivateArgs struct {
	GatherInterval time.Duration `mapstructure:"gather_interval"`
}

type cpuSchedProgram struct {
	scEvtRD             *ringbuf.Reader
	cpuSchedEvtDataChan *bpfmodule.CpuSchedEvtDataChannel
	metricsUpdateFilter *hashset.Set
	runQueueLatencyDesc *prometheus.Desc
	offCPUDurationDesc  *prometheus.Desc
	hungTaskDesc        *prometheus.Desc
	*eBPFBaseProgram
	objs               *bpfmodule.XMCpuScheduleObjects
	runQLatencyBuckets map[int]uint64
	sampleCount        uint64
	sampleSum          float64
	guard              sync.Mutex
}

const (
	defaultCpuSchedEvtChanSize       = (1 << 8)
	defaultRunQueueLatMapKey   int32 = -1
)

var (
	__cpuSchedEBpfArgs    cpuSchedEBpfProgArgs
	__cpuSchedPrivateArgs cpuSchedPrivateArgs
)

func init() {
	registerEBPFProgram(cpuSchedProgName, newCpuSchedProgram)
}

func loadToRunCPUSchedEProg(name string, prog *cpuSchedProgram) error {
	var err error

	prog.objs = new(bpfmodule.XMCpuScheduleObjects)
	prog.links, err = bpfutil.AttachToRun(name, prog.objs, bpfmodule.LoadXMCpuSchedule, func(spec *ebpf.CollectionSpec) error {
		err = spec.RewriteConstants(map[string]interface{}{
			"__filter_scope_type":            int32(__cpuSchedEBpfArgs.FilterScopeType),
			"__filter_scope_value":           int64(__cpuSchedEBpfArgs.FilterScopeValue),
			"__offcpu_min_duration_nanosecs": int64(time.Duration(__cpuSchedEBpfArgs.OffCPUMinDurationMillsecs) * time.Millisecond),
			"__offcpu_max_duration_nanosecs": int64(time.Duration(__cpuSchedEBpfArgs.OffCPUMaxDurationMillsecs) * time.Millisecond),
			"__offcpu_task_type":             int8(__cpuSchedEBpfArgs.OffCPUTaskType),
		})

		if err != nil {
			return errors.Wrap(err, "RewriteConstants failed.")
		}
		return err
	})

	if err != nil {
		prog.objs.Close()
		prog.objs = nil
		return err
	}

	prog.scEvtRD, err = ringbuf.NewReader(prog.objs.XmCsEventRingbufMap)
	if err != nil {
		for _, link := range prog.links {
			link.Close()
		}
		prog.links = nil
		prog.objs.Close()
		prog.objs = nil
	}
	return err
}

func newCpuSchedProgram(name string) (eBPFProgram, error) {
	mapstructure.Decode(config.ProgramConfigByName(name).Args.EBpfProg, &__cpuSchedEBpfArgs)
	mapstructure.Decode(config.ProgramConfigByName(name).Args.Private, &__cpuSchedPrivateArgs)
	__cpuSchedPrivateArgs.GatherInterval = __cpuSchedPrivateArgs.GatherInterval * time.Second

	glog.Infof("eBPFProgram:'%s' EbpfArgs:%s, PrivateArgs:%s", name, litter.Sdump(__cpuSchedEBpfArgs),
		litter.Sdump(__cpuSchedPrivateArgs))

	csProg := &cpuSchedProgram{
		eBPFBaseProgram: &eBPFBaseProgram{
			name:        name,
			stopChan:    make(chan struct{}),
			gatherTimer: calmutils.NewTimer(),
		},
		metricsUpdateFilter: hashset.New(),
		runQLatencyBuckets:  make(map[int]uint64, len(__powerOfTwo)),
		cpuSchedEvtDataChan: bpfmodule.NewCpuSchedEvtDataChannel(defaultCpuSchedEvtChanSize),
	}

	for _, metricDesc := range config.ProgramConfigByName(name).Args.Metrics {
		switch metricDesc {
		case "runq_latency":
			csProg.runQueueLatencyDesc = prometheus.NewDesc(
				prometheus.BuildFQName("cpu", "schedule", metricDesc),
				"This measures the time a task spends waiting on a run queue for a turn on-CPU, and shows this time as a histogram. in microseconds.",
				[]string{"res_type", "res_value"}, prometheus.Labels{"from": "xm_ebpf"},
			)
		case "offcpu_duration":
			csProg.offCPUDurationDesc = prometheus.NewDesc(
				prometheus.BuildFQName("cpu", "schedule", metricDesc),
				"The duration of the Task's off-CPU state, in milliseconds.",
				[]string{"tid", "pid", "comm", "res_type", "res_value"}, prometheus.Labels{"from": "xm_ebpf"},
			)
		case "hung_task":
			csProg.hungTaskDesc = prometheus.NewDesc(
				prometheus.BuildFQName("cpu", "schedule", metricDesc),
				"pid of the process that is hung",
				[]string{"pid", "comm", "res_type", "res_value"}, prometheus.Labels{"from": "xm_ebpf"},
			)
		}
	}

	if err := loadToRunCPUSchedEProg(name, csProg); err != nil {
		err = errors.Wrapf(err, "eBPFProgram:'%s' runCpuSchedProgram failed.", name)
		glog.Error(err)
		return nil, err
	}

	csProg.wg.Go(csProg.tracingeBPFEvent)
	csProg.wg.Go(csProg.handlingeBPFData)

	return csProg, nil
}

func (csp *cpuSchedProgram) Update(ch chan<- prometheus.Metric) error {
	glog.Infof("eBPFProgram:'%s' update start...", csp.name)
	defer glog.Infof("eBPFProgram:'%s' update done.", csp.name)

	csp.guard.Lock()
	// avoid data race
	buckets := make(map[float64]uint64)
	for k, v := range csp.runQLatencyBuckets {
		buckets[float64(k)] = v
	}

	ch <- prometheus.MustNewConstHistogram(csp.runQueueLatencyDesc,
		csp.sampleCount, csp.sampleSum, buckets,
		func() string {
			return __cpuSchedEBpfArgs.FilterScopeType.String()
		}(),
		func() string {
			return strconv.Itoa(__cpuSchedEBpfArgs.FilterScopeValue)
		}(),
	)
	csp.guard.Unlock()

	// 将 cpu schedule event 数据转换为 Prometheus 指标
L:
	for {
		select {
		case cpuSchedEvtData, ok := <-csp.cpuSchedEvtDataChan.C:
			if ok {
				comm := utils.CommToString(cpuSchedEvtData.Comm[:])
				//glog.Infof("eBPFProgram:'%s' cpuSchedEvtData:%s", csp.name, litter.Sdump(cpuSchedEvtData))
				if cpuSchedEvtData.EvtType == bpfmodule.XMCpuScheduleXmCpuSchedEvtTypeXM_CS_EVT_TYPE_OFFCPU {
					// 输出进程 offcpu 的时间
					if config.ProgramCommFilter(csp.name, comm) {
						if !csp.metricsUpdateFilter.Contains(cpuSchedEvtData.Pid) {
							ch <- prometheus.MustNewConstMetric(csp.offCPUDurationDesc,
								prometheus.GaugeValue, float64(cpuSchedEvtData.OffcpuDurationMillsecs),
								strconv.FormatInt(int64(cpuSchedEvtData.Tid), 10),
								strconv.FormatInt(int64(cpuSchedEvtData.Pid), 10),
								comm,
								__cpuSchedEBpfArgs.FilterScopeType.String(),
								strconv.Itoa(__cpuSchedEBpfArgs.FilterScopeValue))
							csp.metricsUpdateFilter.Add(cpuSchedEvtData.Pid)
						}
					}
				} else if cpuSchedEvtData.EvtType == bpfmodule.XMCpuScheduleXmCpuSchedEvtTypeXM_CS_EVT_TYPE_HUNG_TASK {
					// 输出 hung 的进程 pid
					ch <- prometheus.MustNewConstMetric(csp.hungTaskDesc,
						prometheus.GaugeValue, float64(cpuSchedEvtData.Pid),
						strconv.FormatInt(int64(cpuSchedEvtData.Pid), 10),
						comm,
						__cpuSchedEBpfArgs.FilterScopeType.String(),
						strconv.Itoa(__cpuSchedEBpfArgs.FilterScopeValue))

					glog.Infof("eBPFProgram:'%s' process pid:%d, comm:'%s' is hung", csp.name, cpuSchedEvtData.Pid, comm)

				} else if cpuSchedEvtData.EvtType == bpfmodule.XMCpuScheduleXmCpuSchedEvtTypeXM_CS_EVT_TYPE_PROCESS_EXIT {
					evtInfo := new(eventcenter.EBPFEventInfo)
					evtInfo.EvtType = eventcenter.EBPF_EVENT_PROCESS_EXIT
					evtInfo.EvtData = &eventcenter.EBPFEventProcessExit{
						Tid:  cpuSchedEvtData.Tid,
						Pid:  cpuSchedEvtData.Pid,
						Comm: utils.CommToString(cpuSchedEvtData.Comm[:]),
					}
					eventcenter.DefInstance.Publish(csp.name, evtInfo)
				}
			}
		default:
			break L
		}
	}

	csp.metricsUpdateFilter.Clear()
	return nil
}

func (csp *cpuSchedProgram) Stop() {
	if csp.scEvtRD != nil {
		// 关闭 ringbuf
		csp.scEvtRD.Close()
	}

	csp.finalizer()

	if csp.objs != nil {
		csp.objs.Close()
		csp.objs = nil
	}
	csp.cpuSchedEvtDataChan.SafeClose()
	glog.Infof("eBPFProgram:'%s' stopped.", csp.name)
}

func (csp *cpuSchedProgram) Reload() error {
	return nil
}

func (csp *cpuSchedProgram) handlingeBPFData() {
	glog.Infof("eBPFProgram:'%s' start handling eBPF Data...", csp.name)
	csp.gatherTimer.Reset(__cpuSchedPrivateArgs.GatherInterval)

	var histogram bpfmodule.XMCpuScheduleXmRunqlatHist

loop:
	for {
		select {
		case <-csp.stopChan:
			glog.Warningf("eBPFProgram:'%s' tracing runQueueLatency receive stop notify", csp.name)
			break loop
		case <-csp.gatherTimer.Chan():
			// gather eBPF data
			if err := csp.objs.XmCsRunqlatHistsMap.Lookup(defaultRunQueueLatMapKey, &histogram); err != nil {
				if !errors.Is(err, ebpf.ErrKeyNotExist) {
					glog.Errorf("eBPFProgram:'%s' Lookup error. err:%s", csp.name, err.Error())
				}
			} else {
				//glog.Infof("eBPFProgram:'%s' runqLat histogram:%#+v", csp.name, histogram)

				if err := csp.objs.XmCsRunqlatHistsMap.Delete(defaultRunQueueLatMapKey); err != nil {
					glog.Errorf("eBPFProgram:'%s' Delete error. err:%s", csp.name, err.Error())
				}

				// 统计
				csp.guard.Lock()
				csp.sampleCount = 0
				csp.sampleSum = 0.0
				//glog.Infof("eBPFProgram:'%s' tracing runQueueLatency ===>", csp.name)
				for i, slot := range histogram.Slots {
					bucket := __powerOfTwo[i]                              // 桶的上限
					csp.sampleCount += uint64(slot)                        // 统计本周期的样本总数
					csp.sampleSum += float64(slot) * float64(bucket) * 0.6 // 估算样本的总和
					csp.runQLatencyBuckets[bucket] = csp.sampleCount       // 每个桶的样本数，下层包括上层统计数量
					// glog.Infof("\tusecs(%d -> %d) count: %d", func() int {
					// 	if i == 0 {
					// 		return 0
					// 	} else {
					// 		return int(__powerOfTwo[i-1]) + 1
					// 	}
					// }(), int(bucket), slot)
				}
				csp.guard.Unlock()
				glog.Infof("eBPFProgram:'%s' tracing runQueueLatency <===", csp.name)
			}

			csp.gatherTimer.Reset(__cpuSchedPrivateArgs.GatherInterval)
		}
	}
}

func (csp *cpuSchedProgram) tracingeBPFEvent() {
	glog.Infof("eBPFProgram:'%s' start tracing eBPF Event...", csp.name)

loop:
	for {
		record, err := csp.scEvtRD.Read()
		if err != nil {
			if errors.Is(err, ringbuf.ErrClosed) {
				glog.Warningf("eBPFProgram:'%s' tracing eBPF Event goroutine receive stop notify", csp.name)
				break loop
			}
			glog.Errorf("eBPFProgram:'%s' Read error. err:%s", csp.name, err.Error())
			continue
		}

		// 解析数据
		scEvtData := new(bpfmodule.XMCpuScheduleXmCpuSchedEvtData)
		if err := binary.Read(bytes.NewBuffer(record.RawSample), binary.LittleEndian, scEvtData); err != nil {
			glog.Errorf("failed to parse XMCpuScheduleXmCpuSchedEvtData, err: %v", err)
			continue
		}

		csp.cpuSchedEvtDataChan.SafeSend(scEvtData, false)
	}
}
