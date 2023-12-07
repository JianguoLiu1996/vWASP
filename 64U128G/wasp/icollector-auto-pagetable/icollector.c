/*
Copyright (C) 2013  
Fabien Gaud <fgaud@sfu.ca>, Baptiste Lepers <baptiste.lepers@inria.fr>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
version 2, as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "icollector.h"
#include <gsl/gsl_statistics.h>
#include <sys/sysinfo.h>
#include <stdlib.h>
#include <stdbool.h>

static FILE *opt_file_out = NULL;
FILE * fp;
static int sleep_time = 10*TIME_SECOND;     /* Profile by sleep_time useconds chunks */

// #define ClosePTSRonFixNode

#define ACTIVE_PERCENTAGE       0.01


#define MIN_ACTIVE_PERCENTAGE       15

/* Only triggers carrefour if the rate of memory accesses is above the threshold and the IPC is below the other one */
#define MAPTU_MIN                   50

/* Replication thresholds */
#define MEMORY_USAGE_MAX            25 // The global memory usage must be under XX% to enable replication
#define USE_MRR                     0  // Use MRR or DCMR

#if USE_MRR
#define MRR_MIN                     90 // Enable replication if the memory read ratio is above the threshold
#else
#define DCRM_MAX                    5  // Enable replication if the data cache modified ratio is below X%
#endif

/* Interleaving thresholds */
#define MIN_IMBALANCE               35 /* Deviation in % */
#define MAX_LOCALITY                100 /* In % - We don't want to strongly decrease the locality */

/* Migration threshold */
#define MAX_LOCALITY_MIGRATION      80 /* In % */
/***/

#define ENABLE_MULTIPLEXING_CHECKS  0
#define VERBOSE                     1

/** IPC is now disabled by default **/
#define ENABLE_IPC                  0
#define IPC_MAX                     0.9

/** Internal **/
#define MAX_FEEDBACK_LENGTH         256


#if !VERBOSE
#define printf(args...) do {} while(0)
#endif

static void sig_handler(int signal);
static long sys_perf_counter_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags);
static long sys_set_ptsr_enable(void);
static long sys_set_ptsr_disable(void);
static long sys_set_ptsr_on_fixNode(struct bitmask *bmp);
static long sys_set_latency_map(int * nodeArray);
static long kvm_sys_set_latency_map(int * nodeArray);
static long kvm_sys_get_latency_map_from_hypercall(void);
/*
 * Events :
 * - PERF_TYPE_RAW: raw counters. The value must be 0xz0040yyzz.
 *      For 'z-zz' values, see AMD reference manual (eg. 076h = CPU_CLK_UNHALTED).
 *      'yy' is the Unitmask.
 *      The '4' is 0100b = event is enabled (can also be enable/disabled via ioctl).
 *      The '0' before yy indicate which level to monitor (User or OS).
 *              It is modified by the event_attr when .exclude_[user/kernel] == 0.
 *              When it is the case the bits 16 or 17 of .config are set to 1 (== monitor user/kernel).
 *              If this is set to anything else than '0', it can be confusing since the kernel does not modify it when .exclude_xxx is set.
 *
 * - PERF_TYPE_HARDWARE: predefined values of HW counters in Linux (eg PERF_COUNT_HW_CPU_CYCLES = CPU_CLK_UNHALTED).
 *
 * - leader = -1 : the event is a group leader
 *   leader = x!=-1 : the event is only scheduled when its group leader is scheduled
 */
static event_t default_events[] = {
   {
      .name    = "L1d_TLB",
      .type    = 0x8,
      .config  = 0x25,
      .leader  = -1,
      .cpuid   = -1,
   },
   {
      .name    = "dtlb_walk",
      .type    = 0x8,
      .config  = 0x34,
      .leader  = 0,
      .cpuid   = -1,
   },
   {
      .name    = "L1i_TLB",
      .type    = 0x8,
      .config  = 0x26,
      .leader  = -1,
      .cpuid   = -1,
   },
   {
      .name    = "itlb_walk",
      .type    = 0x8,
      .config  = 0x35,
      .leader  = 2,
      .cpuid   = -1,
   },
   { //ddrc
      .name    = "hisi_sccl1_ddrc0/flux_wr/",
      .type    = 0x2d,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   },
   {
      .name    = "hisi_sccl1_ddrc0/flux_rd/",
      .type    = 0x2d,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 0,
   },
   { 
      .name    = "hisi_sccl1_ddrc1/flux_wr/",
      .type    = 0x2e,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   },
   {
      .name    = "hisi_sccl1_ddrc1/flux_rd/",
      .type    = 0x2e,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 0,
   },
   { 
      .name    = "hisi_sccl1_ddrc2/flux_wr/",
      .type    = 0x2f,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   },
   {
      .name    = "hisi_sccl1_ddrc2/flux_rd/",
      .type    = 0x2f,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 0,
   },
   { 
      .name    = "hisi_sccl1_ddrc3/flux_wr/",
      .type    = 0x30,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   },
   {
      .name    = "hisi_sccl1_ddrc3/flux_rd/",
      .type    = 0x30,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 0,
   },
   {
      .name    = "hisi_sccl3_ddrc0/flux_wr/",
      .type    = 0x29,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_ddrc0/flux_rd/",
      .type    = 0x29,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_ddrc1/flux_wr/",
      .type    = 0x2a,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_ddrc1/flux_rd/",
      .type    = 0x2a,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_ddrc2/flux_wr/",
      .type    = 0x2b,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_ddrc2/flux_rd/",
      .type    = 0x2b,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_ddrc3/flux_wr/",
      .type    = 0x2c,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_ddrc3/flux_rd/",
      .type    = 0x2c,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl5_ddrc0/flux_wr/",
      .type    = 0x35,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_ddrc0/flux_rd/",
      .type    = 0x35,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_ddrc1/flux_wr/",
      .type    = 0x36,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_ddrc1/flux_rd/",
      .type    = 0x36,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_ddrc2/flux_wr/",
      .type    = 0x37,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_ddrc2/flux_rd/",
      .type    = 0x37,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_ddrc3/flux_wr/",
      .type    = 0x38,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_ddrc3/flux_rd/",
      .type    = 0x38,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl7_ddrc0/flux_wr/",
      .type    = 0x31,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_ddrc0/flux_rd/",
      .type    = 0x31,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_ddrc1/flux_wr/",
      .type    = 0x32,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_ddrc1/flux_rd/",
      .type    = 0x32,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_ddrc2/flux_wr/",
      .type    = 0x33,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_ddrc2/flux_rd/",
      .type    = 0x33,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_ddrc3/flux_wr/",
      .type    = 0x34,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_ddrc3/flux_rd/",
      .type    = 0x34,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 72,
   },
   {// hha
      .name    = "hisi_sccl1_hha2/rx_ops_num/",       
      .type    = 0x23,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   },
   {
      .name    = "hisi_sccl1_hha2/rx_outer/",
      .type    = 0x23,
      .config  = 0x1,
      .leader  = 36,
      .cpuid   = 0,
   },
   {
      .name    = "hisi_sccl1_hha2/rx_sccl/",
      .type    = 0x23,
      .config  = 0x2,
      .leader  = 36,
      .cpuid   = 0,
   },
   {
      .name    = "hisi_sccl1_hha3/rx_ops_num/",       
      .type    = 0x24,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   },
   {
      .name    = "hisi_sccl1_hha3/rx_outer/",
      .type    = 0x24,
      .config  = 0x1,
      .leader  = 39,
      .cpuid   = 0,
   },
   {
      .name    = "hisi_sccl1_hha3/rx_sccl/",
      .type    = 0x24,
      .config  = 0x2,
      .leader  = 39,
      .cpuid   = 0,
   },
   {
      .name    = "hisi_sccl3_hha0/rx_ops_num/",
      .type    = 0x21,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_hha0/rx_outer/",
      .type    = 0x21,
      .config  = 0x1,
      .leader  = 42,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_hha0/rx_sccl/",
      .type    = 0x21,
      .config  = 0x2,
      .leader  = 42,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_hha1/rx_ops_num/",
      .type    = 0x22,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_hha1/rx_outer/",
      .type    = 0x22,
      .config  = 0x1,
      .leader  = 45,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl3_hha1/rx_sccl/",
      .type    = 0x22,
      .config  = 0x2,
      .leader  = 45,
      .cpuid   = 24,
   },
   {
      .name    = "hisi_sccl5_hha6/rx_ops_num/",
      .type    = 0x27,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_hha6/rx_outer/",
      .type    = 0x27,
      .config  = 0x1,
      .leader  = 48,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_hha6/rx_sccl/",
      .type    = 0x27,
      .config  = 0x2,
      .leader  = 48,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_hha7/rx_ops_num/",
      .type    = 0x28,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_hha7/rx_outer/",
      .type    = 0x28,
      .config  = 0x1,
      .leader  = 51,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl5_hha7/rx_sccl/",
      .type    = 0x28,
      .config  = 0x2,
      .leader  = 51,
      .cpuid   = 48,
   },
   {
      .name    = "hisi_sccl7_hha4/rx_ops_num/",
      .type    = 0x25,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_hha4/rx_outer/",
      .type    = 0x25,
      .config  = 0x1,
      .leader  = 54,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_hha4/rx_sccl/",
      .type    = 0x25,
      .config  = 0x2,
      .leader  = 54,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_hha5/rx_ops_num/",
      .type    = 0x26,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_hha5/rx_outer/",
      .type    = 0x26,
      .config  = 0x1,
      .leader  = 57,
      .cpuid   = 72,
   },
   {
      .name    = "hisi_sccl7_hha5/rx_sccl/",
      .type    = 0x26,
      .config  = 0x2,
      .leader  = 57,
      .cpuid   = 72,
   },
   {
      .name    = "cpu_cycles",
      .type    = 0x8,
      .config  = 0x11,
      .leader  = -1,
      .cpuid   = -1,
   },
   {
      .name    = "mem_access",
      .type    = 0x8,
      .config  = 0x13,
      .leader  = 60,
      .cpuid   = -1,
   },
   {
      .name    = "remote_access",
      .type    = 0x8,
      .config  = 0x31,
      .leader  = 60,
      .cpuid   = -1,
   },
   {
      .name    = "ll_cache_miss",
      .type    = 0x8,
      .config  = 0x33,
      .leader  = 60,
      .cpuid   = -1,
   },
   {
      .name    = "bus_access",
      .type    = 0x8,
      .config  = 0x19,
      .leader  = 60,
      .cpuid   = -1,
   },
   {
      .name    = "inst_retired",
      .type    = 0x8,
      .config  = 0x8,
      .leader  = 60,
      .cpuid   = -1,
   },
   // {
   //    .name    = "ITLB_access",
   //    .type    = PERF_TYPE_HW_CACHE,
   //    .config  = 0x00000004,
   //    .leader  = -1,
   // },
   // {
   //    .name    = "ITLB_miss",
   //    .type    = PERF_TYPE_HW_CACHE,
   //    .config  = 0x00010004,
   //    .leader  = 0,
   // },
   // {
   //    .name    = "DTLB_access",
   //    .type    = PERF_TYPE_HW_CACHE,
   //    .config  = 0x00000003,
   //    .leader  = -1,
   // },
   // {
   //    .name    = "DTLB_miss",
   //    .type    = PERF_TYPE_HW_CACHE,
   //    .config  = 0x00010003,
   //    .leader  = 2,
   // },
// #if USE_MRR
//    /** MRR **/
//    {
//       .name    = "MRR_READ",
//       .type    = PERF_TYPE_RAW,
//       .config  = 0x1004062F0,
//       .leader  = -1,
//    },
//    {
//       .name    = "MRR_READ_WRITE",
//       .type    = PERF_TYPE_RAW,
//       .config  = 0x100407BF0,
//       .leader  = 0
//    },
// #else
//    /** DCMR */
//    {
//       .name    = "DCR_ALL",
//       .type    = PERF_TYPE_RAW,
//       .config  = 0x000401F43,
//       .leader  = -1,
//    },
//    {
//       .name    = "DCR_MODIFIED",
//       .type    = PERF_TYPE_RAW,
//       .config  = 0x000401043,
//       .leader  = 0,
//    },
// #endif

//    /** LAR & DRAM imbalance **/
//    {
//       .name    = "CPU_DRAM_NODE0",
//       .type    = PERF_TYPE_RAW,
//       .config  = 0x1004001E0,
//       .leader  = -1
//    },
//    {
//       .name    = "CPU_DRAM_NODE1",
//       .type    = PERF_TYPE_RAW,
//       .config  = 0x1004002E0,
//       .leader  = -1
//       //.leader  = 2
//    },
//    {
//       .name    = "CPU_DRAM_NODE2",
//       .type    = PERF_TYPE_RAW,
//       .config  = 0x1004004E0,
//       .leader  = -1
//       //.leader  = 2
//    },
//    {
//       .name    = "CPU_DRAM_NODE3",
//       .type    = PERF_TYPE_RAW,
//       .config  = 0x1004008E0,
//       .leader  = -1
//       //.leader  = 2
//    },

// #if ENABLE_IPC
//    /** IPC **/
//    {
//       .name    = "CPU_CLK_UNHALTED",
//       .type    = PERF_TYPE_RAW,
//       .config  = 0x00400076,
//       .leader  = -1
//    },
//    {
//       .name    = "RETIRED_INSTRUCTIONS",
//       .type    = PERF_TYPE_RAW,
//       .config  = 0x004000C0,
//       .leader  = 6
//    },
// #endif
};

static int nb_events = sizeof(default_events)/sizeof(*default_events);
static event_t *events = default_events;

static int nb_nodes;
static int numa_nodes;
static int obj_pid;
static int all_open;
static int sample_interval;
static int enable_PTSR; // 软件控制页表复制的功能使能 可选，默认不使能
static int shut_down;
static int cpu_num;
static int cpu_num2;
static int cpu_num3;
static int cpu_num4;
static int cpu_num_per_node;
static int sampling_times = 1;
static int check_pass;
static int average_check_pass;
static bool repl_pgd_enabled;
static bool disable_PTSR;
int node_Latency_last[8];
static bool already_record;
static uint64_t get_cpu_freq(void) {
   FILE *fd;
   uint64_t freq = 0;
   float freqf = 0;
   char *line = NULL;
   size_t len = 0;

   fd = fopen("/proc/cpuinfo", "r");
   if (!fd) {
      fprintf(stderr, "failed to get cpu frequency\n");
      perror(NULL);
      return freq;
   }

   while (getline(&line, &len, fd) != EOF) {
      if (sscanf(line, "cpu MHz\t: %f", &freqf) == 1) {
         freqf = freqf * 1000000UL;
         freq = (uint64_t) freqf;
         break;
      }
   }

   fclose(fd);
   return freq;
}

static int cpu_of_node(int node) {
  struct bitmask *bmp;
  int ncpus, cpu;

  ncpus = numa_num_configured_cpus();
  bmp = numa_bitmask_alloc(ncpus);
  numa_node_to_cpus(node, bmp);
  for(cpu = 0; cpu < ncpus; cpu++) {
     if (numa_bitmask_isbitset(bmp, cpu)){
        numa_bitmask_free(bmp);
        return cpu;
     }
  }
  numa_bitmask_free(bmp);
  return 0;
}

static inline void change_carrefour_state_str(char * str) {
   if(str) {
      FILE *ibs_ctl = fopen("/proc/inter_cntl", "w");
      if(ibs_ctl) {
         int len = strlen(str);
         fwrite(str, 1, len, ibs_ctl);
         // That's not safe. Todo check for errors
         fclose(ibs_ctl);
      }
      else {
         fprintf(stderr, "Cannot open the carrefour file. Is carrefour loaded?\n");
      }
   }
}

static inline void change_carrefour_state(char c) {
   FILE *ibs_ctl = fopen("/proc/inter_cntl", "w");
   if(ibs_ctl) {
      fputc(c, ibs_ctl);
      fclose(ibs_ctl);
   }
   else {
      fprintf(stderr, "Cannot open the carrefour file. Is carrefour loaded?\n");
   }
}

static long percent_running(struct perf_read_ev *last, struct perf_read_ev *prev) {
   long percent_running = (last->time_enabled-prev->time_enabled)?100*(last->time_running-prev->time_running)/(last->time_enabled-prev->time_enabled):0;
   return percent_running;
}

double  aver(double x[ ], int n)
{	int i;
    double avg=0.0;
	for (i=0; i<n; i++) 
		avg += x[i];
	return avg /= n;
}

double  standard_deviation(double x[ ], int n)
{	int i;
    double avg=0.0, sum=0.0;
	for (i=0; i<n; i++) 
		avg += x[i];
	avg /= n;
	for (i=0; i<n; i++) 
		sum += (x[i]-avg)* (x[i]-avg);
	return sqrt(sum/n);
}

void testArrPass(int *arrpass)
{
   for (int i = 0; i < numa_nodes; i++)
   {
      // printf("arr%d = %d \n",i,arrpass[i]);
      fprintf(opt_file_out, "\n arr%d = %d \n",i,arrpass[i]);
   }
}

//此函数用于物理机测量PTL
void check_node_latency(void)
{
   int node_Latency[numa_nodes][numa_nodes];
   int node_Latency_least[numa_nodes];

   char *line=(char *)malloc(256*sizeof(char));
// char line[100];
        FILE * fp1 = fopen("/home/jianguoliu/vWASP/arm/wasp/bin/nodes.csv", "r");//打开输入文件
   if (fp1==NULL) {//若打开文件失败则退出
      fprintf(opt_file_out, "\n can not open latency file at /home/jianguoliu/vWASP/arm/wasp/bin/nodes.csv !\n"); 
      free(line);    
      return;
    }
   while( fgets(line, 100, fp1) != NULL ) {
        printf("%s", line);
        char substr[10];
         // strcpy(substr, &line[5]);
         strncpy(substr, &line[5], 7);
         // printf("sub string: %s and legth = %d \n", substr,strlen(substr));
         char substr3 = line[0];
         char substr4 = line[3];
         int latency = atoi(substr);
         int fromNode = (int)substr3 -'0';
         int toNode = (int)substr4 -'0';
         // printf("latency : %d \n", latency);
         // printf("sub3 string: %d \n", fromNode);
         // printf("sub4 string: %d \n", toNode);
         node_Latency[fromNode][toNode] = latency;
    }
   fclose(fp1);//关闭输入文件   
   free(line);    

   int i,j ,needUpdate ;
   int least_latency;
   for (i = 0; i < numa_nodes; i++)
   {
      least_latency = 100000000;
      for (j = 0; j < numa_nodes; j++)
      {
         if (node_Latency[i][j]<=0)
         {
            continue;
         }
         
         if (node_Latency[i][j]<least_latency)
         {
            least_latency = node_Latency[i][j];
            node_Latency_least[i] = j;
         }
         fprintf(opt_file_out, "\n from node%d to node%d = %d \n",i ,j,node_Latency[i][j]);
         // printf("from node%d to node%d = %d\n",i ,j,node_Latency[i][j]);
      }
   }

      needUpdate = 0;
      if (already_record) //如果不是第一次
      {
         for (i = 0; i < numa_nodes; i++)
         {
            // printf("node %i's least latency node = %d \n",i,node_Latency_least[i]);
            if (node_Latency_last[i] != node_Latency_least[i])
            {
               needUpdate = 1;
            } 
            node_Latency_last[i] = node_Latency_least[i];  //记录这一次的结果
         }
      }else //如果是第一次
      {
        //检查这次的结果中， 每个节点是不是本地延迟最低， 如果不是就需要更新。
         for (i = 0; i < numa_nodes; i++)
         {
            // printf("node %i's least latency node = %d \n",i,node_Latency_least[i]);
            if (i != node_Latency_least[i])
            {
               needUpdate = 1;
            } 
            node_Latency_last[i] = node_Latency_least[i];  //记录这一次的结果
            already_record = true;
         }
      }
// testArrPass(node_Latency_least);
    if (needUpdate&&enable_PTSR)
    {
      sys_set_latency_map(node_Latency_least);
    }else
    {
          fprintf(opt_file_out, "\n do not need update!");
      //  printf("do not need update!\n");
    }
   

   

  #ifdef ClosePTSRonFixNode
   struct bitmask *bmp;
  bmp = numa_bitmask_alloc(numa_nodes);
   printf("bmp->size = %d !!",bmp->size);
printf("original !!");
numa_bitmask_setall(bmp);
   // for (size_t j = 0; j < numa_nodes; j++)
   // {
   //    printf("node i = %d ,and bitmap set = %d !!",j,numa_bitmask_isbitset(bmp, j));
   // }
   
printf("after fix !!");
for (i = 0; i < numa_nodes; i++)
   {
      printf("node %i's least latency node = %d \n",i,node_Latency_least[i]);
      if (i!=node_Latency_least[i])
      {
         numa_bitmask_clearbit(bmp, i);
      }
   }
   
    for (size_t h = 0; h < numa_nodes; h++)
   {
      printf("node i = %d ,and bitmap set = %d !!",h,numa_bitmask_isbitset(bmp, h));
   }

  sys_set_ptsr_on_fixNode(bmp);

  numa_bitmask_free(bmp);
  #endif

}

//此函数用于物理机测量PTL 给KVM中ept使用。  无需pid.
void vWASP_check_node_latency(void)
{
   int node_Latency[numa_nodes][numa_nodes];
   int node_Latency_least[numa_nodes];

   char *line=(char *)malloc(256*sizeof(char));
// char line[100];
        FILE * fp1 = fopen("/home/jianguoliu/vWASP/arm/wasp/bin/nodes.csv", "r");//打开输入文件
   if (fp1==NULL) {//若打开文件失败则退出
      fprintf(opt_file_out, "\n can not open latency file at /home/jianguoliu/vWASP/arm/wasp/bin/nodes.csv !\n"); 
      free(line);    
      return;
    }
   while( fgets(line, 100, fp1) != NULL ) {
        printf("%s", line);
        char substr[10];
         // strcpy(substr, &line[5]);
         strncpy(substr, &line[5], 7);
         // printf("sub string: %s and legth = %d \n", substr,strlen(substr));
         char substr3 = line[0];
         char substr4 = line[3];
         int latency = atoi(substr);
         int fromNode = (int)substr3 -'0';
         int toNode = (int)substr4 -'0';
         // printf("latency : %d \n", latency);
         // printf("sub3 string: %d \n", fromNode);
         // printf("sub4 string: %d \n", toNode);
         node_Latency[fromNode][toNode] = latency;
    }
   fclose(fp1);//关闭输入文件   
   free(line);    

   int i,j ,needUpdate ;
   int least_latency;
   for (i = 0; i < numa_nodes; i++)
   {
      least_latency = 100000000;
      for (j = 0; j < numa_nodes; j++)
      {
         if (node_Latency[i][j]<=0)
         {
            continue;
         }
         
         if (node_Latency[i][j]<least_latency)
         {
            least_latency = node_Latency[i][j];
            node_Latency_least[i] = j;
         }
         fprintf(opt_file_out, "\n from node%d to node%d = %d \n",i ,j,node_Latency[i][j]);
         // printf("from node%d to node%d = %d\n",i ,j,node_Latency[i][j]);
      }
   }

      needUpdate = 0;
      if (already_record) //如果不是第一次
      {
         for (i = 0; i < numa_nodes; i++)
         {
            // printf("node %i's least latency node = %d \n",i,node_Latency_least[i]);
            if (node_Latency_last[i] != node_Latency_least[i])
            {
               needUpdate = 1;
            } 
            node_Latency_last[i] = node_Latency_least[i];  //记录这一次的结果
         }
      }else //如果是第一次
      {
        //检查这次的结果中， 每个节点是不是本地延迟最低， 如果不是就需要更新。
         for (i = 0; i < numa_nodes; i++)
         {
            // printf("node %i's least latency node = %d \n",i,node_Latency_least[i]);
            if (i != node_Latency_least[i])
            {
               needUpdate = 1;
            } 
            node_Latency_last[i] = node_Latency_least[i];  //记录这一次的结果
            already_record = true;
         }
      }
testArrPass(node_Latency_least);
    if (needUpdate)
    {
      kvm_sys_set_latency_map(node_Latency_least);
    }else
    {
       printf("do not need update!\n");
    }
   

   

//   #ifdef ClosePTSRonFixNode
//    struct bitmask *bmp;
//   bmp = numa_bitmask_alloc(numa_nodes);
//    printf("bmp->size = %d !!",bmp->size);
// printf("original !!");
// numa_bitmask_setall(bmp);
//    // for (size_t j = 0; j < numa_nodes; j++)
//    // {
//    //    printf("node i = %d ,and bitmap set = %d !!",j,numa_bitmask_isbitset(bmp, j));
//    // }
   
// printf("after fix !!");
// for (i = 0; i < numa_nodes; i++)
//    {
//       printf("node %i's least latency node = %d \n",i,node_Latency_least[i]);
//       if (i!=node_Latency_least[i])
//       {
//          numa_bitmask_clearbit(bmp, i);
//       }
//    }
   
//     for (size_t h = 0; h < numa_nodes; h++)
//    {
//       printf("node i = %d ,and bitmap set = %d !!",h,numa_bitmask_isbitset(bmp, h));
//    }

//   sys_set_ptsr_on_fixNode(bmp);

//   numa_bitmask_free(bmp);
//   #endif

}


static void TLB_Miss_last_10s(struct perf_read_ev *last, struct perf_read_ev *prev, double * rr_global, double * rr_nodes) {
   int node = 0;
   int i;
   // unsigned long all_global = 0;
   // unsigned long modified_global = 0;

   // for(node = 0; node < nb_nodes; node++) {
      long all_idx = node*nb_events;

      //printf("Read = %lu , RW = %lu\n", last[all_idx].value - prev[all_idx].value, last[all_idx + 1].value - prev[all_idx + 1].value);
      unsigned long dTlb_all = last[all_idx].value - prev[all_idx].value;
      unsigned long dTlb_walk = last[all_idx + 1].value - prev[all_idx + 1].value;

      unsigned long iTlb_all = last[all_idx + 2].value - prev[all_idx + 2].value;
      unsigned long iTlb_walk = last[all_idx + 3].value - prev[all_idx + 3].value;
      // ddrc  node 0
      unsigned long hisi_sccl1_ddrc0_flux_wr = last[all_idx + 4].value - prev[all_idx + 4].value;
      unsigned long hisi_sccl1_ddrc0_flux_rd = last[all_idx + 5].value - prev[all_idx + 5].value;
      unsigned long hisi_sccl1_ddrc1_flux_wr = last[all_idx + 6].value - prev[all_idx + 6].value;
      unsigned long hisi_sccl1_ddrc1_flux_rd = last[all_idx + 7].value - prev[all_idx + 7].value;
      unsigned long hisi_sccl1_ddrc2_flux_wr = last[all_idx + 8].value - prev[all_idx + 8].value;
      unsigned long hisi_sccl1_ddrc2_flux_rd = last[all_idx + 9].value - prev[all_idx + 9].value;
      unsigned long hisi_sccl1_ddrc3_flux_wr = last[all_idx + 10].value - prev[all_idx + 10].value;
      unsigned long hisi_sccl1_ddrc3_flux_rd = last[all_idx + 11].value - prev[all_idx + 11].value;
      // ddrc  node 1
      unsigned long hisi_sccl3_ddrc0_flux_wr = last[all_idx + 12].value - prev[all_idx + 12].value;
      unsigned long hisi_sccl3_ddrc0_flux_rd = last[all_idx + 13].value - prev[all_idx + 13].value;
      unsigned long hisi_sccl3_ddrc1_flux_wr = last[all_idx + 14].value - prev[all_idx + 14].value;
      unsigned long hisi_sccl3_ddrc1_flux_rd = last[all_idx + 15].value - prev[all_idx + 15].value;
      unsigned long hisi_sccl3_ddrc2_flux_wr = last[all_idx + 16].value - prev[all_idx + 16].value;
      unsigned long hisi_sccl3_ddrc2_flux_rd = last[all_idx + 17].value - prev[all_idx + 17].value;
      unsigned long hisi_sccl3_ddrc3_flux_wr = last[all_idx + 18].value - prev[all_idx + 18].value;
      unsigned long hisi_sccl3_ddrc3_flux_rd = last[all_idx + 19].value - prev[all_idx + 19].value;
      // ddrc  node 2
      unsigned long hisi_sccl5_ddrc0_flux_wr = last[all_idx + 20].value - prev[all_idx + 20].value;
      unsigned long hisi_sccl5_ddrc0_flux_rd = last[all_idx + 21].value - prev[all_idx + 21].value;
      unsigned long hisi_sccl5_ddrc1_flux_wr = last[all_idx + 22].value - prev[all_idx + 22].value;
      unsigned long hisi_sccl5_ddrc1_flux_rd = last[all_idx + 23].value - prev[all_idx + 23].value;
      unsigned long hisi_sccl5_ddrc2_flux_wr = last[all_idx + 24].value - prev[all_idx + 24].value;
      unsigned long hisi_sccl5_ddrc2_flux_rd = last[all_idx + 25].value - prev[all_idx + 25].value;
      unsigned long hisi_sccl5_ddrc3_flux_wr = last[all_idx + 26].value - prev[all_idx + 26].value;
      unsigned long hisi_sccl5_ddrc3_flux_rd = last[all_idx + 27].value - prev[all_idx + 27].value;
      // ddrc  node 3
      unsigned long hisi_sccl7_ddrc0_flux_wr = last[all_idx + 28].value - prev[all_idx + 28].value;
      unsigned long hisi_sccl7_ddrc0_flux_rd = last[all_idx + 29].value - prev[all_idx + 29].value;
      unsigned long hisi_sccl7_ddrc1_flux_wr = last[all_idx + 30].value - prev[all_idx + 30].value;
      unsigned long hisi_sccl7_ddrc1_flux_rd = last[all_idx + 31].value - prev[all_idx + 31].value;
      unsigned long hisi_sccl7_ddrc2_flux_wr = last[all_idx + 32].value - prev[all_idx + 32].value;
      unsigned long hisi_sccl7_ddrc2_flux_rd = last[all_idx + 33].value - prev[all_idx + 33].value;
      unsigned long hisi_sccl7_ddrc3_flux_wr = last[all_idx + 34].value - prev[all_idx + 34].value;
      unsigned long hisi_sccl7_ddrc3_flux_rd = last[all_idx + 35].value - prev[all_idx + 35].value;
      // hha  node 0
      unsigned long hisi_sccl1_hha2_rx_ops_num = last[all_idx + 36].value - prev[all_idx + 36].value;
      unsigned long hisi_sccl1_hha2_rx_outer = last[all_idx + 37].value - prev[all_idx + 37].value;
      unsigned long hisi_sccl1_hha2_rx_sccl = last[all_idx + 38].value - prev[all_idx + 38].value;
      unsigned long hisi_sccl1_hha3_rx_ops_num = last[all_idx + 39].value - prev[all_idx + 39].value;
      unsigned long hisi_sccl1_hha3_rx_outer = last[all_idx + 40].value - prev[all_idx + 40].value;
      unsigned long hisi_sccl1_hha3_rx_sccl = last[all_idx + 41].value - prev[all_idx + 41].value;
      // hha  node 1
      unsigned long hisi_sccl3_hha0_rx_ops_num = last[all_idx + 42].value - prev[all_idx + 42].value;
      unsigned long hisi_sccl3_hha0_rx_outer = last[all_idx + 43].value - prev[all_idx + 43].value;
      unsigned long hisi_sccl3_hha0_rx_sccl = last[all_idx + 44].value - prev[all_idx + 44].value;
      unsigned long hisi_sccl3_hha1_rx_ops_num = last[all_idx + 45].value - prev[all_idx + 45].value;
      unsigned long hisi_sccl3_hha1_rx_outer = last[all_idx + 46].value - prev[all_idx + 46].value;
      unsigned long hisi_sccl3_hha1_rx_sccl = last[all_idx + 47].value - prev[all_idx + 47].value;
      // hha  node 2
      unsigned long hisi_sccl5_hha6_rx_ops_num = last[all_idx + 48].value - prev[all_idx + 48].value;
      unsigned long hisi_sccl5_hha6_rx_outer = last[all_idx + 49].value - prev[all_idx + 49].value;
      unsigned long hisi_sccl5_hha6_rx_sccl = last[all_idx + 50].value - prev[all_idx + 50].value;
      unsigned long hisi_sccl5_hha7_rx_ops_num = last[all_idx + 51].value - prev[all_idx + 51].value;
      unsigned long hisi_sccl5_hha7_rx_outer = last[all_idx + 52].value - prev[all_idx + 52].value;
      unsigned long hisi_sccl5_hha7_rx_sccl = last[all_idx + 53].value - prev[all_idx + 53].value;
      // hha  node 3
      unsigned long hisi_sccl7_hha4_rx_ops_num = last[all_idx + 54].value - prev[all_idx + 54].value;
      unsigned long hisi_sccl7_hha4_rx_outer = last[all_idx + 55].value - prev[all_idx + 55].value;
      unsigned long hisi_sccl7_hha4_rx_sccl = last[all_idx + 56].value - prev[all_idx + 56].value;
      unsigned long hisi_sccl7_hha5_rx_ops_num = last[all_idx + 57].value - prev[all_idx + 57].value;
      unsigned long hisi_sccl7_hha5_rx_outer = last[all_idx + 58].value - prev[all_idx + 58].value;
      unsigned long hisi_sccl7_hha5_rx_sccl = last[all_idx + 59].value - prev[all_idx + 59].value;

      unsigned long cpu_cycles = last[all_idx + 60].value - prev[all_idx + 60].value;
      unsigned long mem_access = last[all_idx + 61].value - prev[all_idx + 61].value;
      unsigned long remote_access = last[all_idx + 62].value - prev[all_idx + 62].value;
      unsigned long ll_cache_miss = last[all_idx + 63].value - prev[all_idx + 63].value;
      unsigned long bus_access = last[all_idx + 64].value - prev[all_idx + 64].value;
      unsigned long inst_retired = last[all_idx + 65].value - prev[all_idx + 65].value;

      unsigned long mem_access_all = last[all_idx + 61].value;


      // unsigned long l1dTlb_access = last[all_idx + 4].value - prev[all_idx + 4].value;
      // unsigned long l1dTlb_access_refill = last[all_idx + 5].value - prev[all_idx + 5].value;

      // unsigned long l1iTlb_access = last[all_idx + 6].value - prev[all_idx + 6].value;
      // unsigned long l1iTlb_access_refill = last[all_idx + 7].value - prev[all_idx + 7].value;



      // if(dTlb_all)
      //  {

         // for(i = 4; i < 24; i++)
         // {
         //    if (!last[all_idx + i].value)
         //    {
         //       shut_down =1;
         //    }  
         // }
         // rr_nodes[node] = (1. - (double) modified / (double) all) * 100.;
         // printf("\niTlb : %lu - %lu.  percentage =  %lf ======= dTlb : %lu - %lu.  percentage =  %lf\n", iTlb_walk, iTlb_all,(double) iTlb_walk / (double) iTlb_all, dTlb_walk, dTlb_all,(double) dTlb_walk / (double) dTlb_all);
         fprintf(opt_file_out, "last %d s :\n",(sample_interval==0)?10:sample_interval);

         // fprintf(opt_file_out, "node0 :hisi_sccl1_ddrc2_flux_wr = %d , hisi_sccl1_ddrc2_flux_rd = %d \n",hisi_sccl1_ddrc2_flux_wr,hisi_sccl1_ddrc2_flux_rd);
         // fprintf(opt_file_out, "node1 :hisi_sccl3_ddrc3_flux_wr = %d , hisi_sccl3_ddrc3_flux_rd = %d \n",hisi_sccl3_ddrc3_flux_wr,hisi_sccl3_ddrc3_flux_rd);
         // fprintf(opt_file_out, "node2 :hisi_sccl5_ddrc2_flux_wr = %d , hisi_sccl5_ddrc2_flux_rd = %d \n",hisi_sccl5_ddrc2_flux_wr,hisi_sccl5_ddrc2_flux_rd);
         // fprintf(opt_file_out, "node3 :hisi_sccl7_ddrc3_flux_wr = %d , hisi_sccl7_ddrc3_flux_rd = %d \n",hisi_sccl7_ddrc3_flux_wr,hisi_sccl7_ddrc3_flux_rd);

         // fprintf(opt_file_out, "\nnode0 :hisi_sccl1_hha3_rx_ops_num = %d , hisi_sccl1_hha3_rx_outer = %d , hisi_sccl1_hha3_rx_sccl = %d\n",hisi_sccl1_hha3_rx_ops_num,hisi_sccl1_hha3_rx_outer,hisi_sccl1_hha3_rx_sccl);
         // fprintf(opt_file_out, "node1 :hisi_sccl3_hha1_rx_ops_num = %d , hisi_sccl3_hha1_rx_outer = %d , hisi_sccl3_hha1_rx_sccl = %d\n",hisi_sccl3_hha1_rx_ops_num,hisi_sccl3_hha1_rx_outer,hisi_sccl3_hha1_rx_sccl);
         // fprintf(opt_file_out, "node2 :hisi_sccl5_hha7_rx_ops_num = %d , hisi_sccl5_hha7_rx_outer = %d , hisi_sccl5_hha7_rx_sccl = %d\n",hisi_sccl5_hha7_rx_ops_num,hisi_sccl5_hha7_rx_outer,hisi_sccl5_hha7_rx_sccl);
         // fprintf(opt_file_out, "node3 :hisi_sccl7_hha5_rx_ops_num = %d , hisi_sccl7_hha5_rx_outer = %d , hisi_sccl7_hha5_rx_sccl = %d\n",hisi_sccl7_hha5_rx_ops_num,hisi_sccl7_hha5_rx_outer,hisi_sccl7_hha5_rx_sccl);
         // printf("\niTlb_access : %lu - %lu.  percentage =  %lf ======= dTlb_access : %lu - %lu.  percentage =  %lf\n", l1iTlb_access_refill, l1iTlb_access,(double) l1iTlb_access_refill / (double) l1iTlb_access, l1dTlb_access_refill, l1dTlb_access,(double) l1dTlb_access_refill / (double) l1dTlb_access);
         *rr_global = (double) dTlb_walk / (double) dTlb_all;
         float dTlb_miss_rate = (double) dTlb_walk / (double) dTlb_all;
         float iTlb_miss_rate = (double) iTlb_walk / (double) iTlb_all;
         
         
         fprintf(opt_file_out, "\n\n\ndTlb_miss_rate = %lf    : %lu - %lu\n",dTlb_miss_rate, dTlb_walk, dTlb_all);
         fprintf(opt_file_out, "\niTlb_miss_rate = %lf    : %lu - %lu\n",iTlb_miss_rate, iTlb_walk, iTlb_all);
         

         
        //mem-imb
         unsigned long sccl1_mc = hisi_sccl1_ddrc0_flux_wr + hisi_sccl1_ddrc0_flux_rd + hisi_sccl1_ddrc1_flux_wr + hisi_sccl1_ddrc1_flux_rd+ hisi_sccl1_ddrc2_flux_wr + hisi_sccl1_ddrc2_flux_rd+ hisi_sccl1_ddrc3_flux_wr + hisi_sccl1_ddrc3_flux_rd;
         unsigned long sccl3_mc = hisi_sccl3_ddrc0_flux_wr + hisi_sccl3_ddrc0_flux_rd + hisi_sccl3_ddrc1_flux_wr + hisi_sccl3_ddrc1_flux_rd + hisi_sccl3_ddrc2_flux_wr + hisi_sccl3_ddrc2_flux_rd + hisi_sccl3_ddrc3_flux_wr + hisi_sccl3_ddrc3_flux_rd;
         unsigned long sccl5_mc = hisi_sccl5_ddrc0_flux_wr + hisi_sccl5_ddrc0_flux_rd + hisi_sccl5_ddrc1_flux_wr + hisi_sccl5_ddrc1_flux_rd + hisi_sccl5_ddrc2_flux_wr + hisi_sccl5_ddrc2_flux_rd + hisi_sccl5_ddrc3_flux_wr + hisi_sccl5_ddrc3_flux_rd;
         unsigned long sccl7_mc = hisi_sccl7_ddrc0_flux_wr + hisi_sccl7_ddrc0_flux_rd + hisi_sccl7_ddrc1_flux_wr + hisi_sccl7_ddrc1_flux_rd + hisi_sccl7_ddrc2_flux_wr + hisi_sccl7_ddrc2_flux_rd + hisi_sccl7_ddrc3_flux_wr + hisi_sccl7_ddrc3_flux_rd;

         double arr[4] ={sccl1_mc,sccl3_mc,sccl5_mc,sccl7_mc};

         double mem_imb = standard_deviation(arr, 4) / aver(arr, 4);

         fprintf(opt_file_out, "\nMemory_controller_imbalance = %lf \n",mem_imb);
         // printf("mem_imb = %lf      :sccl1_mc = %d , sccl3_mc = %d , sccl5_mc = %d, sccl7_mc = %d,aver = %lf, standard_deviation = %lf \n",mem_imb,sccl1_mc,sccl3_mc,sccl5_mc,sccl7_mc,aver(arr, 4),standard_deviation(arr, 4));
         unsigned long  node0_local_access = hisi_sccl1_hha2_rx_ops_num - hisi_sccl1_hha2_rx_outer - hisi_sccl1_hha2_rx_sccl + hisi_sccl1_hha3_rx_ops_num - hisi_sccl1_hha3_rx_outer - hisi_sccl1_hha3_rx_sccl;
         unsigned long  node1_local_access = hisi_sccl3_hha0_rx_ops_num - hisi_sccl3_hha0_rx_outer - hisi_sccl3_hha0_rx_sccl + hisi_sccl3_hha1_rx_ops_num - hisi_sccl3_hha1_rx_outer - hisi_sccl3_hha1_rx_sccl;
         unsigned long  node2_local_access = hisi_sccl5_hha6_rx_ops_num - hisi_sccl5_hha6_rx_outer - hisi_sccl5_hha6_rx_sccl + hisi_sccl5_hha7_rx_ops_num - hisi_sccl5_hha7_rx_outer - hisi_sccl5_hha7_rx_sccl;
         unsigned long  node3_local_access = hisi_sccl7_hha4_rx_ops_num - hisi_sccl7_hha4_rx_outer - hisi_sccl7_hha4_rx_sccl + hisi_sccl7_hha5_rx_ops_num - hisi_sccl7_hha5_rx_outer - hisi_sccl7_hha5_rx_sccl;
         double LRR = (double) (node0_local_access + node1_local_access + node2_local_access + node3_local_access)/(double) (hisi_sccl1_hha2_rx_ops_num +hisi_sccl1_hha3_rx_ops_num + hisi_sccl3_hha0_rx_ops_num + hisi_sccl3_hha1_rx_ops_num + hisi_sccl5_hha6_rx_ops_num + hisi_sccl5_hha7_rx_ops_num + hisi_sccl7_hha4_rx_ops_num + hisi_sccl7_hha5_rx_ops_num);
         fprintf(opt_file_out, "\nLRR = %lf \n",LRR);

         double arr2[8] ={hisi_sccl1_hha2_rx_outer + hisi_sccl1_hha3_rx_outer,hisi_sccl1_hha2_rx_sccl + hisi_sccl1_hha3_rx_sccl,hisi_sccl3_hha0_rx_outer + hisi_sccl3_hha1_rx_outer,hisi_sccl3_hha0_rx_sccl + hisi_sccl3_hha1_rx_sccl,hisi_sccl5_hha6_rx_outer + hisi_sccl5_hha7_rx_outer,hisi_sccl5_hha6_rx_sccl + hisi_sccl5_hha7_rx_sccl,hisi_sccl7_hha4_rx_outer + hisi_sccl7_hha5_rx_outer,hisi_sccl7_hha4_rx_sccl + hisi_sccl7_hha5_rx_sccl};
         double i_imb = standard_deviation(arr2, 8) / aver(arr2, 8);

         fprintf(opt_file_out, "\ninterconnect_imbalance = %lf \n",i_imb);

         fprintf(opt_file_out, "\nlast->time_enabled-prev->time_enabled = %lf \n",last->time_enabled-prev->time_enabled);
         // double maptu = (double) mem_access / (double) (1000000*((sample_interval==0)?10:sample_interval));
         double maptu = (double) mem_access / (double) (last->time_enabled-prev->time_enabled);
         
         fprintf(opt_file_out, "\nMAPTU = %lf \n",maptu);

         double mem_access_latency = (double) cpu_cycles / (double) mem_access;
         fprintf(opt_file_out, "\nmem_access_latency = %lf \n",mem_access_latency);

         double l3MPKI = (double) (ll_cache_miss*1000) / (double) inst_retired;
         fprintf(opt_file_out, "\nl3MPKI = %lf \n",l3MPKI);

         double remote_access_rate = (double) remote_access / (double) mem_access;
         fprintf(opt_file_out, "\nremote_access_rate = %lf \n",remote_access_rate);

         fprintf(opt_file_out, "\nbus_access = %lf \n",(double)bus_access);
         fprintf(opt_file_out, "\nmem_access = %ld \n",mem_access);
         fprintf(opt_file_out, "\nmem_access = %ld \n",mem_access_all);
         double maptu_mc = (sccl1_mc+sccl3_mc+sccl5_mc+sccl7_mc) / (double) (1000000*((sample_interval==0)?10:sample_interval));
         fprintf(opt_file_out, "\nMAPTU_mc = %lf \n",maptu_mc);
         // fprintf(opt_file_out, "\nnode0 mem_access = %ld \n",sccl1_mc);
         // fprintf(opt_file_out, "\nnode1 mem_access = %ld \n",sccl3_mc);
         // fprintf(opt_file_out, "\nnode2 mem_access = %ld \n",sccl5_mc);
         // fprintf(opt_file_out, "\nnode3 mem_access = %ld \n",sccl7_mc);

         // unsigned long sccl1_mc0 = hisi_sccl1_ddrc0_flux_wr + hisi_sccl1_ddrc0_flux_rd;
         // unsigned long sccl1_mc1 = hisi_sccl1_ddrc1_flux_wr + hisi_sccl1_ddrc1_flux_rd;
         // unsigned long sccl1_mc2 = hisi_sccl1_ddrc2_flux_wr + hisi_sccl1_ddrc2_flux_rd;
         // unsigned long sccl1_mc3 = hisi_sccl1_ddrc3_flux_wr + hisi_sccl1_ddrc3_flux_rd;
         // fprintf(opt_file_out, "\nnode0 mc0 = %ld \n",sccl1_mc0);
         // fprintf(opt_file_out, "\nnode0 mc1 = %ld \n",sccl1_mc1);
         // fprintf(opt_file_out, "\nnode0 mc2 = %ld \n",sccl1_mc2);
         // fprintf(opt_file_out, "\nnode0 mc3 = %ld \n",sccl1_mc3);
         // unsigned long sccl3_mc0 = hisi_sccl3_ddrc0_flux_wr + hisi_sccl3_ddrc0_flux_rd;
         // unsigned long sccl3_mc1 = hisi_sccl3_ddrc1_flux_wr + hisi_sccl3_ddrc1_flux_rd;
         // unsigned long sccl3_mc2 = hisi_sccl3_ddrc2_flux_wr + hisi_sccl3_ddrc2_flux_rd;
         // unsigned long sccl3_mc3 = hisi_sccl3_ddrc3_flux_wr + hisi_sccl3_ddrc3_flux_rd;
         // fprintf(opt_file_out, "\nnode1 mc0 = %ld \n",sccl3_mc0);
         // fprintf(opt_file_out, "\nnode1 mc1 = %ld \n",sccl3_mc1);
         // fprintf(opt_file_out, "\nnode1 mc2 = %ld \n",sccl3_mc2);
         // fprintf(opt_file_out, "\nnode1 mc3 = %ld \n",sccl3_mc3);
         // unsigned long sccl5_mc0 = hisi_sccl5_ddrc0_flux_wr + hisi_sccl5_ddrc0_flux_rd;
         // unsigned long sccl5_mc1 = hisi_sccl5_ddrc1_flux_wr + hisi_sccl5_ddrc1_flux_rd;
         // unsigned long sccl5_mc2 = hisi_sccl5_ddrc2_flux_wr + hisi_sccl5_ddrc2_flux_rd;
         // unsigned long sccl5_mc3 = hisi_sccl5_ddrc3_flux_wr + hisi_sccl5_ddrc3_flux_rd;
         // fprintf(opt_file_out, "\nnode2 mc0 = %ld \n",sccl5_mc0);
         // fprintf(opt_file_out, "\nnode2 mc1 = %ld \n",sccl5_mc1);
         // fprintf(opt_file_out, "\nnode2 mc2 = %ld \n",sccl5_mc2);
         // fprintf(opt_file_out, "\nnode2 mc3 = %ld \n",sccl5_mc3);
         // unsigned long sccl7_mc0 = hisi_sccl7_ddrc0_flux_wr + hisi_sccl7_ddrc0_flux_rd;
         // unsigned long sccl7_mc1 = hisi_sccl7_ddrc1_flux_wr + hisi_sccl7_ddrc1_flux_rd;
         // unsigned long sccl7_mc2 = hisi_sccl7_ddrc2_flux_wr + hisi_sccl7_ddrc2_flux_rd;
         // unsigned long sccl7_mc3 = hisi_sccl7_ddrc3_flux_wr + hisi_sccl7_ddrc3_flux_rd;
         // fprintf(opt_file_out, "\nnode3 mc0 = %ld \n",sccl7_mc0);
         // fprintf(opt_file_out, "\nnode3 mc1 = %ld \n",sccl7_mc1);
         // fprintf(opt_file_out, "\nnode3 mc2 = %ld \n",sccl7_mc2);
         // fprintf(opt_file_out, "\nnode3 mc3 = %ld \n",sccl7_mc3);

         // unsigned long node0_recieve_from_other_socket = hisi_sccl1_hha2_rx_outer + hisi_sccl1_hha3_rx_outer;
         // unsigned long node0_recieve_from_node1 = hisi_sccl1_hha2_rx_sccl + hisi_sccl1_hha3_rx_sccl;
         // unsigned long node1_recieve_from_other_socket = hisi_sccl3_hha0_rx_outer + hisi_sccl3_hha1_rx_outer;
         // unsigned long node1_recieve_from_node0 = hisi_sccl3_hha0_rx_sccl + hisi_sccl3_hha1_rx_sccl;
         // unsigned long node2_recieve_from_other_socket = hisi_sccl5_hha6_rx_outer + hisi_sccl5_hha7_rx_outer;
         // unsigned long node2_recieve_from_node3 = hisi_sccl5_hha6_rx_sccl + hisi_sccl5_hha7_rx_sccl;
         // unsigned long node3_recieve_from_other_socket = hisi_sccl7_hha4_rx_outer + hisi_sccl7_hha5_rx_outer;
         // unsigned long node3_recieve_from_node2 = hisi_sccl7_hha4_rx_sccl + hisi_sccl7_hha5_rx_sccl;
         // fprintf(opt_file_out, "\n node0_recieve_from_other_socket = %ld \n",node0_recieve_from_other_socket);
         // fprintf(opt_file_out, "\n node0_recieve_from_node1 = %ld \n",node0_recieve_from_node1);
         // fprintf(opt_file_out, "\n node1_recieve_from_other_socket = %ld \n",node1_recieve_from_other_socket);
         // fprintf(opt_file_out, "\n node1_recieve_from_node0 = %ld \n",node1_recieve_from_node0);
         // fprintf(opt_file_out, "\n node2_recieve_from_other_socket = %ld \n",node2_recieve_from_other_socket);
         // fprintf(opt_file_out, "\n node2_recieve_from_node3 = %ld \n",node2_recieve_from_node3);
         // fprintf(opt_file_out, "\n node3_recieve_from_other_socket = %ld \n",node3_recieve_from_other_socket);
         // fprintf(opt_file_out, "\n node3_recieve_from_node2 = %ld \n",node3_recieve_from_node2);

         
      // }
      // if (!all_open)
      // {
      //    fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
      // }
      
   // }
   

   sampling_times += 1;
   // sys_set_ptsr_enable();
   // check_node_latency();
//    vWASP_check_node_latency();
kvm_sys_get_latency_map_from_hypercall();


   if (maptu > 0.1)
   {
      if (dTlb_miss_rate>0.001)
      {
      //    if (remote_access_rate > 0.01)
         {
            // if (mem_imb > 0.1)
            {
               
               fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
               fprintf(opt_file_out, "\n This duration matchs the condition of PageTable replication !");
               fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n"); 
               
               // sys_set_ptsr_enable();
               // check_node_latency();
               check_pass += 1;
               fflush(opt_file_out);
               return;
            }
         }  

      }
   }
   //  check_node_latency();  
      
   //  sys_set_ptsr_disable();  
      

   fflush(opt_file_out);
// printf("all_global %d : %lu - %lu\n", node, modified_global, all_global);
   // *rr_global = 100;
   // if(all_global) {
   //    *rr_global = (1 - (double) modified_global / (double) all_global) * 100.;
   // }
}

static void TLB_Miss_all(struct perf_read_ev *last, struct perf_read_ev *prev, double * rr_global, double * rr_nodes) {
   int node = 0;
   // unsigned long all_global = 0;
   // unsigned long modified_global = 0;

   // for(node = 0; node < nb_nodes; node++) {
      long all_idx = node*nb_events;

      //printf("Read = %lu , RW = %lu\n", last[all_idx].value - prev[all_idx].value, last[all_idx + 1].value - prev[all_idx + 1].value);
      unsigned long dTlb_all = last[all_idx].value ;
      unsigned long dTlb_walk = last[all_idx + 1].value ;

      unsigned long iTlb_all = last[all_idx + 2].value ;
      unsigned long iTlb_walk = last[all_idx + 3].value ;
      // ddrc  node 0
      unsigned long hisi_sccl1_ddrc0_flux_wr = last[all_idx + 4].value;
      unsigned long hisi_sccl1_ddrc0_flux_rd = last[all_idx + 5].value;
      unsigned long hisi_sccl1_ddrc1_flux_wr = last[all_idx + 6].value;
      unsigned long hisi_sccl1_ddrc1_flux_rd = last[all_idx + 7].value;
      unsigned long hisi_sccl1_ddrc2_flux_wr = last[all_idx + 8].value;
      unsigned long hisi_sccl1_ddrc2_flux_rd = last[all_idx + 9].value;
      unsigned long hisi_sccl1_ddrc3_flux_wr = last[all_idx + 10].value;
      unsigned long hisi_sccl1_ddrc3_flux_rd = last[all_idx + 11].value;
      // ddrc  node 1
      unsigned long hisi_sccl3_ddrc0_flux_wr = last[all_idx + 12].value;
      unsigned long hisi_sccl3_ddrc0_flux_rd = last[all_idx + 13].value;
      unsigned long hisi_sccl3_ddrc1_flux_wr = last[all_idx + 14].value;
      unsigned long hisi_sccl3_ddrc1_flux_rd = last[all_idx + 15].value;
      unsigned long hisi_sccl3_ddrc2_flux_wr = last[all_idx + 16].value;
      unsigned long hisi_sccl3_ddrc2_flux_rd = last[all_idx + 17].value;
      unsigned long hisi_sccl3_ddrc3_flux_wr = last[all_idx + 18].value;
      unsigned long hisi_sccl3_ddrc3_flux_rd = last[all_idx + 19].value;
      // ddrc  node 2
      unsigned long hisi_sccl5_ddrc0_flux_wr = last[all_idx + 20].value;
      unsigned long hisi_sccl5_ddrc0_flux_rd = last[all_idx + 21].value;
      unsigned long hisi_sccl5_ddrc1_flux_wr = last[all_idx + 22].value;
      unsigned long hisi_sccl5_ddrc1_flux_rd = last[all_idx + 23].value;
      unsigned long hisi_sccl5_ddrc2_flux_wr = last[all_idx + 24].value;
      unsigned long hisi_sccl5_ddrc2_flux_rd = last[all_idx + 25].value;
      unsigned long hisi_sccl5_ddrc3_flux_wr = last[all_idx + 26].value;
      unsigned long hisi_sccl5_ddrc3_flux_rd = last[all_idx + 27].value;
      // ddrc  node 3
      unsigned long hisi_sccl7_ddrc0_flux_wr = last[all_idx + 28].value;
      unsigned long hisi_sccl7_ddrc0_flux_rd = last[all_idx + 29].value;
      unsigned long hisi_sccl7_ddrc1_flux_wr = last[all_idx + 30].value;
      unsigned long hisi_sccl7_ddrc1_flux_rd = last[all_idx + 31].value;
      unsigned long hisi_sccl7_ddrc2_flux_wr = last[all_idx + 32].value;
      unsigned long hisi_sccl7_ddrc2_flux_rd = last[all_idx + 33].value;
      unsigned long hisi_sccl7_ddrc3_flux_wr = last[all_idx + 34].value;
      unsigned long hisi_sccl7_ddrc3_flux_rd = last[all_idx + 35].value;
      // hha  node 0
      unsigned long hisi_sccl1_hha2_rx_ops_num = last[all_idx + 36].value;
      unsigned long hisi_sccl1_hha2_rx_outer = last[all_idx + 37].value;
      unsigned long hisi_sccl1_hha2_rx_sccl = last[all_idx + 38].value;
      unsigned long hisi_sccl1_hha3_rx_ops_num = last[all_idx + 39].value;
      unsigned long hisi_sccl1_hha3_rx_outer = last[all_idx + 40].value;
      unsigned long hisi_sccl1_hha3_rx_sccl = last[all_idx + 41].value;
      // hha  node 1
      unsigned long hisi_sccl3_hha0_rx_ops_num = last[all_idx + 42].value;
      unsigned long hisi_sccl3_hha0_rx_outer = last[all_idx + 43].value;
      unsigned long hisi_sccl3_hha0_rx_sccl = last[all_idx + 44].value;
      unsigned long hisi_sccl3_hha1_rx_ops_num = last[all_idx + 45].value;
      unsigned long hisi_sccl3_hha1_rx_outer = last[all_idx + 46].value;
      unsigned long hisi_sccl3_hha1_rx_sccl = last[all_idx + 47].value;
      // hha  node 2
      unsigned long hisi_sccl5_hha6_rx_ops_num = last[all_idx + 48].value;
      unsigned long hisi_sccl5_hha6_rx_outer = last[all_idx + 49].value;
      unsigned long hisi_sccl5_hha6_rx_sccl = last[all_idx + 50].value;
      unsigned long hisi_sccl5_hha7_rx_ops_num = last[all_idx + 51].value;
      unsigned long hisi_sccl5_hha7_rx_outer = last[all_idx + 52].value;
      unsigned long hisi_sccl5_hha7_rx_sccl = last[all_idx + 53].value;
      // hha  node 3
      unsigned long hisi_sccl7_hha4_rx_ops_num = last[all_idx + 54].value;
      unsigned long hisi_sccl7_hha4_rx_outer = last[all_idx + 55].value;
      unsigned long hisi_sccl7_hha4_rx_sccl = last[all_idx + 56].value;
      unsigned long hisi_sccl7_hha5_rx_ops_num = last[all_idx + 57].value;
      unsigned long hisi_sccl7_hha5_rx_outer = last[all_idx + 58].value;
      unsigned long hisi_sccl7_hha5_rx_sccl = last[all_idx + 59].value;


      // unsigned long l1dTlb_access = last[all_idx + 4].value - prev[all_idx + 4].value;
      // unsigned long l1dTlb_access_refill = last[all_idx + 5].value - prev[all_idx + 5].value;

      // unsigned long l1iTlb_access = last[all_idx + 6].value - prev[all_idx + 6].value;
      // unsigned long l1iTlb_access_refill = last[all_idx + 7].value - prev[all_idx + 7].value;



      // if(dTlb_all) 
      // {
         // rr_nodes[node] = (1. - (double) modified / (double) all) * 100.;
         // printf("\niTlb : %lu - %lu.  percentage =  %lf ======= dTlb : %lu - %lu.  percentage =  %lf\n", iTlb_walk, iTlb_all,(double) iTlb_walk / (double) iTlb_all, dTlb_walk, dTlb_all,(double) dTlb_walk / (double) dTlb_all);
         // fprintf(opt_file_out, "all :\n");

         // fprintf(opt_file_out, "node0 :hisi_sccl1_ddrc2_flux_wr = %d , hisi_sccl1_ddrc2_flux_rd = %d \n",hisi_sccl1_ddrc2_flux_wr,hisi_sccl1_ddrc2_flux_rd);
         // fprintf(opt_file_out, "node1 :hisi_sccl3_ddrc3_flux_wr = %d , hisi_sccl3_ddrc3_flux_rd = %d \n",hisi_sccl3_ddrc3_flux_wr,hisi_sccl3_ddrc3_flux_rd);
         // fprintf(opt_file_out, "node2 :hisi_sccl5_ddrc2_flux_wr = %d , hisi_sccl5_ddrc2_flux_rd = %d \n",hisi_sccl5_ddrc2_flux_wr,hisi_sccl5_ddrc2_flux_rd);
         // fprintf(opt_file_out, "node3 :hisi_sccl7_ddrc3_flux_wr = %d , hisi_sccl7_ddrc3_flux_rd = %d \n",hisi_sccl7_ddrc3_flux_wr,hisi_sccl7_ddrc3_flux_rd);

         // fprintf(opt_file_out, "\nnode0 :hisi_sccl1_hha3_rx_ops_num = %d , hisi_sccl1_hha3_rx_outer = %d , hisi_sccl1_hha3_rx_sccl = %d\n",hisi_sccl1_hha3_rx_ops_num,hisi_sccl1_hha3_rx_outer,hisi_sccl1_hha3_rx_sccl);
         // fprintf(opt_file_out, "node1 :hisi_sccl3_hha1_rx_ops_num = %d , hisi_sccl3_hha1_rx_outer = %d , hisi_sccl3_hha1_rx_sccl = %d\n",hisi_sccl3_hha1_rx_ops_num,hisi_sccl3_hha1_rx_outer,hisi_sccl3_hha1_rx_sccl);
         // fprintf(opt_file_out, "node2 :hisi_sccl5_hha7_rx_ops_num = %d , hisi_sccl5_hha7_rx_outer = %d , hisi_sccl5_hha7_rx_sccl = %d\n",hisi_sccl5_hha7_rx_ops_num,hisi_sccl5_hha7_rx_outer,hisi_sccl5_hha7_rx_sccl);
         // fprintf(opt_file_out, "node3 :hisi_sccl7_hha5_rx_ops_num = %d , hisi_sccl7_hha5_rx_outer = %d , hisi_sccl7_hha5_rx_sccl = %d\n",hisi_sccl7_hha5_rx_ops_num,hisi_sccl7_hha5_rx_outer,hisi_sccl7_hha5_rx_sccl);
         // printf("\niTlb_access : %lu - %lu.  percentage =  %lf ======= dTlb_access : %lu - %lu.  percentage =  %lf\n", l1iTlb_access_refill, l1iTlb_access,(double) l1iTlb_access_refill / (double) l1iTlb_access, l1dTlb_access_refill, l1dTlb_access,(double) l1dTlb_access_refill / (double) l1dTlb_access);
         *rr_global = (double) dTlb_walk / (double) dTlb_all;
         float dTlb_miss_rate = (double) dTlb_walk / (double) dTlb_all;
         float iTlb_miss_rate = (double) iTlb_walk / (double) iTlb_all;
         
        //mem-imb
         unsigned long sccl1_mc = hisi_sccl1_ddrc0_flux_wr + hisi_sccl1_ddrc0_flux_rd + hisi_sccl1_ddrc1_flux_wr + hisi_sccl1_ddrc1_flux_rd+ hisi_sccl1_ddrc2_flux_wr + hisi_sccl1_ddrc2_flux_rd+ hisi_sccl1_ddrc3_flux_wr + hisi_sccl1_ddrc3_flux_rd;
         unsigned long sccl3_mc = hisi_sccl3_ddrc0_flux_wr + hisi_sccl3_ddrc0_flux_rd + hisi_sccl3_ddrc1_flux_wr + hisi_sccl3_ddrc1_flux_rd + hisi_sccl3_ddrc2_flux_wr + hisi_sccl3_ddrc2_flux_rd + hisi_sccl3_ddrc3_flux_wr + hisi_sccl3_ddrc3_flux_rd;
         unsigned long sccl5_mc = hisi_sccl5_ddrc0_flux_wr + hisi_sccl5_ddrc0_flux_rd + hisi_sccl5_ddrc1_flux_wr + hisi_sccl5_ddrc1_flux_rd + hisi_sccl5_ddrc2_flux_wr + hisi_sccl5_ddrc2_flux_rd + hisi_sccl5_ddrc3_flux_wr + hisi_sccl5_ddrc3_flux_rd;
         unsigned long sccl7_mc = hisi_sccl7_ddrc0_flux_wr + hisi_sccl7_ddrc0_flux_rd + hisi_sccl7_ddrc1_flux_wr + hisi_sccl7_ddrc1_flux_rd + hisi_sccl7_ddrc2_flux_wr + hisi_sccl7_ddrc2_flux_rd + hisi_sccl7_ddrc3_flux_wr + hisi_sccl7_ddrc3_flux_rd;

         double arr[4] ={sccl1_mc,sccl3_mc,sccl5_mc,sccl7_mc};

         double mem_imb = standard_deviation(arr, 4) / aver(arr, 4);

         // fprintf(opt_file_out, "\nMemory_controller_imbalance = %lf \n",mem_imb);
         // printf("mem_imb = %lf      :sccl1_mc = %d , sccl3_mc = %d , sccl5_mc = %d, sccl7_mc = %d,aver = %lf, standard_deviation = %lf \n",mem_imb,sccl1_mc,sccl3_mc,sccl5_mc,sccl7_mc,aver(arr, 4),standard_deviation(arr, 4));
         unsigned long  node0_local_access = hisi_sccl1_hha2_rx_ops_num - hisi_sccl1_hha2_rx_outer - hisi_sccl1_hha2_rx_sccl + hisi_sccl1_hha3_rx_ops_num - hisi_sccl1_hha3_rx_outer - hisi_sccl1_hha3_rx_sccl;
         unsigned long  node1_local_access = hisi_sccl3_hha0_rx_ops_num - hisi_sccl3_hha0_rx_outer - hisi_sccl3_hha0_rx_sccl + hisi_sccl3_hha1_rx_ops_num - hisi_sccl3_hha1_rx_outer - hisi_sccl3_hha1_rx_sccl;
         unsigned long  node2_local_access = hisi_sccl5_hha6_rx_ops_num - hisi_sccl5_hha6_rx_outer - hisi_sccl5_hha6_rx_sccl + hisi_sccl5_hha7_rx_ops_num - hisi_sccl5_hha7_rx_outer - hisi_sccl5_hha7_rx_sccl;
         unsigned long  node3_local_access = hisi_sccl7_hha4_rx_ops_num - hisi_sccl7_hha4_rx_outer - hisi_sccl7_hha4_rx_sccl + hisi_sccl7_hha5_rx_ops_num - hisi_sccl7_hha5_rx_outer - hisi_sccl7_hha5_rx_sccl;
         double LRR = (double) (node0_local_access + node1_local_access + node2_local_access + node3_local_access)/(double) (hisi_sccl1_hha2_rx_ops_num +hisi_sccl1_hha3_rx_ops_num + hisi_sccl3_hha0_rx_ops_num + hisi_sccl3_hha1_rx_ops_num + hisi_sccl5_hha6_rx_ops_num + hisi_sccl5_hha7_rx_ops_num + hisi_sccl7_hha4_rx_ops_num + hisi_sccl7_hha5_rx_ops_num);
         // fprintf(opt_file_out, "\nLRR = %lf \n",LRR);

         double arr2[8] ={hisi_sccl1_hha2_rx_outer + hisi_sccl1_hha3_rx_outer,hisi_sccl1_hha2_rx_sccl + hisi_sccl1_hha3_rx_sccl,hisi_sccl3_hha0_rx_outer + hisi_sccl3_hha1_rx_outer,hisi_sccl3_hha0_rx_sccl + hisi_sccl3_hha1_rx_sccl,hisi_sccl5_hha6_rx_outer + hisi_sccl5_hha7_rx_outer,hisi_sccl5_hha6_rx_sccl + hisi_sccl5_hha7_rx_sccl,hisi_sccl7_hha4_rx_outer + hisi_sccl7_hha5_rx_outer,hisi_sccl7_hha4_rx_sccl + hisi_sccl7_hha5_rx_sccl};
         double i_imb = standard_deviation(arr2, 8) / aver(arr2, 8);

         // fprintf(opt_file_out, "\ninterconnect_imbalance = %lf \n",i_imb);
      // }
      // fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
      // fflush(opt_file_out);
   // }
   if (all_open)
      {
         fprintf(opt_file_out, "all :\n");
         fprintf(opt_file_out, "\n\n\ndTlb_miss_rate = %lf    : %lu - %lu\n",dTlb_miss_rate, dTlb_walk, dTlb_all);
         fprintf(opt_file_out, "\niTlb_miss_rate = %lf    : %lu - %lu\n",iTlb_miss_rate, iTlb_walk, iTlb_all);
         fprintf(opt_file_out, "\nMemory_controller_imbalance = %lf \n",mem_imb);
         fprintf(opt_file_out, "\nLRR = %lf \n",LRR);
         fprintf(opt_file_out, "\ninterconnect_imbalance = %lf \n",i_imb);  
             
      }

      if (dTlb_miss_rate>0.001 && LRR < 0.6 && mem_imb > 0.1)
      {
         average_check_pass +=1;
         fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
         fprintf(opt_file_out, "\n Average matchs the condition of PageTable replication !");
         fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
      }
// printf("all_global %d : %lu - %lu\n", node, modified_global, all_global);
   // *rr_global = 100;
   // if(all_global) {
   //    *rr_global = (1 - (double) modified_global / (double) all_global) * 100.;
   // }
}

static void TLB_Miss_last_10s_systemWide(struct perf_read_ev *last, struct perf_read_ev *prev, double * rr_global, double * rr_nodes) {
   int node = 0;
   int i;
   unsigned long dTlb_all = 0;
   unsigned long dTlb_walk = 0;
   unsigned long iTlb_all = 0;
   unsigned long iTlb_walk = 0;
   // unsigned long all_global = 0;
   // unsigned long modified_global = 0;

   // for(node = 0; node < nb_nodes; node++) {
      long all_idx = node*nb_events;
      for ( i = 0; i < cpu_num; i++)
      {
         dTlb_all = dTlb_all + last[all_idx+i].value - prev[all_idx+i].value;
      }
      for (i = cpu_num; i < cpu_num2; i++)
      {
         dTlb_walk = dTlb_walk + last[all_idx+i].value - prev[all_idx+i].value;
      }
      for (i = cpu_num2; i < cpu_num3; i++)
      {
         iTlb_all = iTlb_all + last[all_idx+i].value - prev[all_idx+i].value;
      }
      for (i = cpu_num3; i < cpu_num4; i++)
      {
         iTlb_walk = iTlb_walk + last[all_idx+i].value - prev[all_idx+i].value;
      }
      
      //printf("Read = %lu , RW = %lu\n", last[all_idx].value - prev[all_idx].value, last[all_idx + 1].value - prev[all_idx + 1].value);
      // unsigned long dTlb_all = last[all_idx].value - prev[all_idx].value;
      // unsigned long dTlb_walk = last[all_idx + 1].value - prev[all_idx + 1].value;

      // unsigned long iTlb_all = last[all_idx + 2].value - prev[all_idx + 2].value;
      // unsigned long iTlb_walk = last[all_idx + 3].value - prev[all_idx + 3].value;
      // ddrc  node 0
      unsigned long hisi_sccl1_ddrc0_flux_wr = last[all_idx + cpu_num4].value - prev[all_idx + cpu_num4].value;
      unsigned long hisi_sccl1_ddrc0_flux_rd = last[all_idx + cpu_num4+1].value - prev[all_idx + cpu_num4+1].value;
      unsigned long hisi_sccl1_ddrc1_flux_wr = last[all_idx + cpu_num4+2].value - prev[all_idx + cpu_num4+2].value;
      unsigned long hisi_sccl1_ddrc1_flux_rd = last[all_idx + cpu_num4+3].value - prev[all_idx + cpu_num4+3].value;
      unsigned long hisi_sccl1_ddrc2_flux_wr = last[all_idx + cpu_num4+4].value - prev[all_idx + cpu_num4+4].value;
      unsigned long hisi_sccl1_ddrc2_flux_rd = last[all_idx + cpu_num4+5].value - prev[all_idx + cpu_num4+5].value;
      unsigned long hisi_sccl1_ddrc3_flux_wr = last[all_idx + cpu_num4+6].value - prev[all_idx + cpu_num4+6].value;
      unsigned long hisi_sccl1_ddrc3_flux_rd = last[all_idx + cpu_num4+7].value - prev[all_idx + cpu_num4+7].value;
      // ddrc  node 1
      unsigned long hisi_sccl3_ddrc0_flux_wr = last[all_idx + cpu_num4+8].value - prev[all_idx + cpu_num4+8].value;
      unsigned long hisi_sccl3_ddrc0_flux_rd = last[all_idx + cpu_num4+9].value - prev[all_idx + cpu_num4+9].value;
      unsigned long hisi_sccl3_ddrc1_flux_wr = last[all_idx + cpu_num4+10].value - prev[all_idx + cpu_num4+10].value;
      unsigned long hisi_sccl3_ddrc1_flux_rd = last[all_idx + cpu_num4+11].value - prev[all_idx + cpu_num4+11].value;
      unsigned long hisi_sccl3_ddrc2_flux_wr = last[all_idx + cpu_num4+12].value - prev[all_idx + cpu_num4+12].value;
      unsigned long hisi_sccl3_ddrc2_flux_rd = last[all_idx + cpu_num4+13].value - prev[all_idx + cpu_num4+13].value;
      unsigned long hisi_sccl3_ddrc3_flux_wr = last[all_idx + cpu_num4+14].value - prev[all_idx + cpu_num4+14].value;
      unsigned long hisi_sccl3_ddrc3_flux_rd = last[all_idx + cpu_num4+15].value - prev[all_idx + cpu_num4+15].value;
      // ddrc  node 2
      unsigned long hisi_sccl5_ddrc0_flux_wr = last[all_idx + cpu_num4+16].value - prev[all_idx + cpu_num4+16].value;
      unsigned long hisi_sccl5_ddrc0_flux_rd = last[all_idx + cpu_num4+17].value - prev[all_idx + cpu_num4+17].value;
      unsigned long hisi_sccl5_ddrc1_flux_wr = last[all_idx + cpu_num4+18].value - prev[all_idx + cpu_num4+18].value;
      unsigned long hisi_sccl5_ddrc1_flux_rd = last[all_idx + cpu_num4+19].value - prev[all_idx + cpu_num4+19].value;
      unsigned long hisi_sccl5_ddrc2_flux_wr = last[all_idx + cpu_num4+20].value - prev[all_idx + cpu_num4+20].value;
      unsigned long hisi_sccl5_ddrc2_flux_rd = last[all_idx + cpu_num4+21].value - prev[all_idx + cpu_num4+21].value;
      unsigned long hisi_sccl5_ddrc3_flux_wr = last[all_idx + cpu_num4+22].value - prev[all_idx + cpu_num4+22].value;
      unsigned long hisi_sccl5_ddrc3_flux_rd = last[all_idx + cpu_num4+23].value - prev[all_idx + cpu_num4+23].value;
      // ddrc  node 3
      unsigned long hisi_sccl7_ddrc0_flux_wr = last[all_idx + cpu_num4+24].value - prev[all_idx + cpu_num4+24].value;
      unsigned long hisi_sccl7_ddrc0_flux_rd = last[all_idx + cpu_num4+25].value - prev[all_idx + cpu_num4+25].value;
      unsigned long hisi_sccl7_ddrc1_flux_wr = last[all_idx + cpu_num4+26].value - prev[all_idx + cpu_num4+26].value;
      unsigned long hisi_sccl7_ddrc1_flux_rd = last[all_idx + cpu_num4+27].value - prev[all_idx + cpu_num4+27].value;
      unsigned long hisi_sccl7_ddrc2_flux_wr = last[all_idx + cpu_num4+28].value - prev[all_idx + cpu_num4+28].value;
      unsigned long hisi_sccl7_ddrc2_flux_rd = last[all_idx + cpu_num4+29].value - prev[all_idx + cpu_num4+29].value;
      unsigned long hisi_sccl7_ddrc3_flux_wr = last[all_idx + cpu_num4+30].value - prev[all_idx + cpu_num4+30].value;
      unsigned long hisi_sccl7_ddrc3_flux_rd = last[all_idx + cpu_num4+31].value - prev[all_idx + cpu_num4+31].value;
      // hha  node 0
      unsigned long hisi_sccl1_hha2_rx_ops_num = last[all_idx + cpu_num4+32].value - prev[all_idx + cpu_num4+32].value;
      unsigned long hisi_sccl1_hha2_rx_outer = last[all_idx + cpu_num4+33].value - prev[all_idx + cpu_num4+33].value;
      unsigned long hisi_sccl1_hha2_rx_sccl = last[all_idx + cpu_num4+34].value - prev[all_idx + cpu_num4+34].value;
      unsigned long hisi_sccl1_hha3_rx_ops_num = last[all_idx + cpu_num4+35].value - prev[all_idx + cpu_num4+35].value;
      unsigned long hisi_sccl1_hha3_rx_outer = last[all_idx + cpu_num4+36].value - prev[all_idx + cpu_num4+36].value;
      unsigned long hisi_sccl1_hha3_rx_sccl = last[all_idx + cpu_num4+37].value - prev[all_idx + cpu_num4+37].value;
      // hha  node 1
      unsigned long hisi_sccl3_hha0_rx_ops_num = last[all_idx + cpu_num4+38].value - prev[all_idx + cpu_num4+38].value;
      unsigned long hisi_sccl3_hha0_rx_outer = last[all_idx + cpu_num4+39].value - prev[all_idx + cpu_num4+39].value;
      unsigned long hisi_sccl3_hha0_rx_sccl = last[all_idx + cpu_num4+40].value - prev[all_idx + cpu_num4+40].value;
      unsigned long hisi_sccl3_hha1_rx_ops_num = last[all_idx + cpu_num4+41].value - prev[all_idx + cpu_num4+41].value;
      unsigned long hisi_sccl3_hha1_rx_outer = last[all_idx + cpu_num4+42].value - prev[all_idx + cpu_num4+42].value;
      unsigned long hisi_sccl3_hha1_rx_sccl = last[all_idx + cpu_num4+43].value - prev[all_idx + cpu_num4+43].value;
      // hha  node 2
      unsigned long hisi_sccl5_hha6_rx_ops_num = last[all_idx + cpu_num4+44].value - prev[all_idx + cpu_num4+44].value;
      unsigned long hisi_sccl5_hha6_rx_outer = last[all_idx + cpu_num4+45].value - prev[all_idx + cpu_num4+45].value;
      unsigned long hisi_sccl5_hha6_rx_sccl = last[all_idx + cpu_num4+46].value - prev[all_idx + cpu_num4+46].value;
      unsigned long hisi_sccl5_hha7_rx_ops_num = last[all_idx + cpu_num4+47].value - prev[all_idx + cpu_num4+47].value;
      unsigned long hisi_sccl5_hha7_rx_outer = last[all_idx + cpu_num4+48].value - prev[all_idx + cpu_num4+48].value;
      unsigned long hisi_sccl5_hha7_rx_sccl = last[all_idx + cpu_num4+49].value - prev[all_idx + cpu_num4+49].value;
      // hha  node 3
      unsigned long hisi_sccl7_hha4_rx_ops_num = last[all_idx + cpu_num4+50].value - prev[all_idx + cpu_num4+50].value;
      unsigned long hisi_sccl7_hha4_rx_outer = last[all_idx + cpu_num4+51].value - prev[all_idx + cpu_num4+51].value;
      unsigned long hisi_sccl7_hha4_rx_sccl = last[all_idx + cpu_num4+52].value - prev[all_idx + cpu_num4+52].value;
      unsigned long hisi_sccl7_hha5_rx_ops_num = last[all_idx + cpu_num4+53].value - prev[all_idx + cpu_num4+53].value;
      unsigned long hisi_sccl7_hha5_rx_outer = last[all_idx + cpu_num4+54].value - prev[all_idx + cpu_num4+54].value;
      unsigned long hisi_sccl7_hha5_rx_sccl = last[all_idx + cpu_num4+55].value - prev[all_idx + cpu_num4+55].value;


      // unsigned long l1dTlb_access = last[all_idx + 4].value - prev[all_idx + 4].value;
      // unsigned long l1dTlb_access_refill = last[all_idx + 5].value - prev[all_idx + 5].value;

      // unsigned long l1iTlb_access = last[all_idx + 6].value - prev[all_idx + 6].value;
      // unsigned long l1iTlb_access_refill = last[all_idx + 7].value - prev[all_idx + 7].value;



      // if(dTlb_all) 
      // {

         // for(i = cpu_num4; i < cpu_num4+20; i++)
         // {
         //    if (!last[all_idx + i].value)
         //    {
         //       shut_down =1;
         //    }  
         // }
         // rr_nodes[node] = (1. - (double) modified / (double) all) * 100.;
         // printf("\niTlb : %lu - %lu.  percentage =  %lf ======= dTlb : %lu - %lu.  percentage =  %lf\n", iTlb_walk, iTlb_all,(double) iTlb_walk / (double) iTlb_all, dTlb_walk, dTlb_all,(double) dTlb_walk / (double) dTlb_all);
         fprintf(opt_file_out, "last %d s :\n",(sample_interval==0)?10:sample_interval);

         // fprintf(opt_file_out, "node0 :hisi_sccl1_ddrc2_flux_wr = %d , hisi_sccl1_ddrc2_flux_rd = %d \n",hisi_sccl1_ddrc2_flux_wr,hisi_sccl1_ddrc2_flux_rd);
         // fprintf(opt_file_out, "node1 :hisi_sccl3_ddrc3_flux_wr = %d , hisi_sccl3_ddrc3_flux_rd = %d \n",hisi_sccl3_ddrc3_flux_wr,hisi_sccl3_ddrc3_flux_rd);
         // fprintf(opt_file_out, "node2 :hisi_sccl5_ddrc2_flux_wr = %d , hisi_sccl5_ddrc2_flux_rd = %d \n",hisi_sccl5_ddrc2_flux_wr,hisi_sccl5_ddrc2_flux_rd);
         // fprintf(opt_file_out, "node3 :hisi_sccl7_ddrc3_flux_wr = %d , hisi_sccl7_ddrc3_flux_rd = %d \n",hisi_sccl7_ddrc3_flux_wr,hisi_sccl7_ddrc3_flux_rd);

         // fprintf(opt_file_out, "\nnode0 :hisi_sccl1_hha3_rx_ops_num = %d , hisi_sccl1_hha3_rx_outer = %d , hisi_sccl1_hha3_rx_sccl = %d\n",hisi_sccl1_hha3_rx_ops_num,hisi_sccl1_hha3_rx_outer,hisi_sccl1_hha3_rx_sccl);
         // fprintf(opt_file_out, "node1 :hisi_sccl3_hha1_rx_ops_num = %d , hisi_sccl3_hha1_rx_outer = %d , hisi_sccl3_hha1_rx_sccl = %d\n",hisi_sccl3_hha1_rx_ops_num,hisi_sccl3_hha1_rx_outer,hisi_sccl3_hha1_rx_sccl);
         // fprintf(opt_file_out, "node2 :hisi_sccl5_hha7_rx_ops_num = %d , hisi_sccl5_hha7_rx_outer = %d , hisi_sccl5_hha7_rx_sccl = %d\n",hisi_sccl5_hha7_rx_ops_num,hisi_sccl5_hha7_rx_outer,hisi_sccl5_hha7_rx_sccl);
         // fprintf(opt_file_out, "node3 :hisi_sccl7_hha5_rx_ops_num = %d , hisi_sccl7_hha5_rx_outer = %d , hisi_sccl7_hha5_rx_sccl = %d\n",hisi_sccl7_hha5_rx_ops_num,hisi_sccl7_hha5_rx_outer,hisi_sccl7_hha5_rx_sccl);
         // printf("\niTlb_access : %lu - %lu.  percentage =  %lf ======= dTlb_access : %lu - %lu.  percentage =  %lf\n", l1iTlb_access_refill, l1iTlb_access,(double) l1iTlb_access_refill / (double) l1iTlb_access, l1dTlb_access_refill, l1dTlb_access,(double) l1dTlb_access_refill / (double) l1dTlb_access);
         *rr_global = (double) dTlb_walk / (double) dTlb_all;

         float dTlb_miss_rate = (double) dTlb_walk / (double) dTlb_all;
         float iTlb_miss_rate = (double) iTlb_walk / (double) iTlb_all;
         
         
         fprintf(opt_file_out, "\n\n\ndTlb_miss_rate = %lf    : %lu - %lu\n",dTlb_miss_rate, dTlb_walk, dTlb_all);
         fprintf(opt_file_out, "\niTlb_miss_rate = %lf    : %lu - %lu\n",iTlb_miss_rate, iTlb_walk, iTlb_all);

         
        //mem-imb
         unsigned long sccl1_mc = hisi_sccl1_ddrc0_flux_wr + hisi_sccl1_ddrc0_flux_rd + hisi_sccl1_ddrc1_flux_wr + hisi_sccl1_ddrc1_flux_rd+ hisi_sccl1_ddrc2_flux_wr + hisi_sccl1_ddrc2_flux_rd+ hisi_sccl1_ddrc3_flux_wr + hisi_sccl1_ddrc3_flux_rd;
         unsigned long sccl3_mc = hisi_sccl3_ddrc0_flux_wr + hisi_sccl3_ddrc0_flux_rd + hisi_sccl3_ddrc1_flux_wr + hisi_sccl3_ddrc1_flux_rd + hisi_sccl3_ddrc2_flux_wr + hisi_sccl3_ddrc2_flux_rd + hisi_sccl3_ddrc3_flux_wr + hisi_sccl3_ddrc3_flux_rd;
         unsigned long sccl5_mc = hisi_sccl5_ddrc0_flux_wr + hisi_sccl5_ddrc0_flux_rd + hisi_sccl5_ddrc1_flux_wr + hisi_sccl5_ddrc1_flux_rd + hisi_sccl5_ddrc2_flux_wr + hisi_sccl5_ddrc2_flux_rd + hisi_sccl5_ddrc3_flux_wr + hisi_sccl5_ddrc3_flux_rd;
         unsigned long sccl7_mc = hisi_sccl7_ddrc0_flux_wr + hisi_sccl7_ddrc0_flux_rd + hisi_sccl7_ddrc1_flux_wr + hisi_sccl7_ddrc1_flux_rd + hisi_sccl7_ddrc2_flux_wr + hisi_sccl7_ddrc2_flux_rd + hisi_sccl7_ddrc3_flux_wr + hisi_sccl7_ddrc3_flux_rd;

         double arr[4] ={sccl1_mc,sccl3_mc,sccl5_mc,sccl7_mc};

         double mem_imb = standard_deviation(arr, 4) / aver(arr, 4);

         fprintf(opt_file_out, "\nMemory_controller_imbalance = %lf \n",mem_imb);
         // printf("mem_imb = %lf      :sccl1_mc = %d , sccl3_mc = %d , sccl5_mc = %d, sccl7_mc = %d,aver = %lf, standard_deviation = %lf \n",mem_imb,sccl1_mc,sccl3_mc,sccl5_mc,sccl7_mc,aver(arr, 4),standard_deviation(arr, 4));
         unsigned long  node0_local_access = hisi_sccl1_hha2_rx_ops_num - hisi_sccl1_hha2_rx_outer - hisi_sccl1_hha2_rx_sccl + hisi_sccl1_hha3_rx_ops_num - hisi_sccl1_hha3_rx_outer - hisi_sccl1_hha3_rx_sccl;
         unsigned long  node1_local_access = hisi_sccl3_hha0_rx_ops_num - hisi_sccl3_hha0_rx_outer - hisi_sccl3_hha0_rx_sccl + hisi_sccl3_hha1_rx_ops_num - hisi_sccl3_hha1_rx_outer - hisi_sccl3_hha1_rx_sccl;
         unsigned long  node2_local_access = hisi_sccl5_hha6_rx_ops_num - hisi_sccl5_hha6_rx_outer - hisi_sccl5_hha6_rx_sccl + hisi_sccl5_hha7_rx_ops_num - hisi_sccl5_hha7_rx_outer - hisi_sccl5_hha7_rx_sccl;
         unsigned long  node3_local_access = hisi_sccl7_hha4_rx_ops_num - hisi_sccl7_hha4_rx_outer - hisi_sccl7_hha4_rx_sccl + hisi_sccl7_hha5_rx_ops_num - hisi_sccl7_hha5_rx_outer - hisi_sccl7_hha5_rx_sccl;
         double LRR = (double) (node0_local_access + node1_local_access + node2_local_access + node3_local_access)/(double) (hisi_sccl1_hha2_rx_ops_num +hisi_sccl1_hha3_rx_ops_num + hisi_sccl3_hha0_rx_ops_num + hisi_sccl3_hha1_rx_ops_num + hisi_sccl5_hha6_rx_ops_num + hisi_sccl5_hha7_rx_ops_num + hisi_sccl7_hha4_rx_ops_num + hisi_sccl7_hha5_rx_ops_num);
         fprintf(opt_file_out, "\nLRR = %lf \n",LRR);

         double arr2[8] ={hisi_sccl1_hha2_rx_outer + hisi_sccl1_hha3_rx_outer,hisi_sccl1_hha2_rx_sccl + hisi_sccl1_hha3_rx_sccl,hisi_sccl3_hha0_rx_outer + hisi_sccl3_hha1_rx_outer,hisi_sccl3_hha0_rx_sccl + hisi_sccl3_hha1_rx_sccl,hisi_sccl5_hha6_rx_outer + hisi_sccl5_hha7_rx_outer,hisi_sccl5_hha6_rx_sccl + hisi_sccl5_hha7_rx_sccl,hisi_sccl7_hha4_rx_outer + hisi_sccl7_hha5_rx_outer,hisi_sccl7_hha4_rx_sccl + hisi_sccl7_hha5_rx_sccl};
         double i_imb = standard_deviation(arr2, 8) / aver(arr2, 8);

         fprintf(opt_file_out, "\ninterconnect_imbalance = %lf \n",i_imb);

         
      // }
      sampling_times += 1;
      
      
      if (dTlb_miss_rate>0.001 && LRR < 0.6 && mem_imb > 0.1)
      {
         check_pass += 1;
         fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
         fprintf(opt_file_out, "\n This duration matchs the condition of PageTable replication !");
         fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
      }
      


      // if (!all_open)
      // {
      //    fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
      // }


      fflush(opt_file_out); 
   // }
// printf("all_global %d : %lu - %lu\n", node, modified_global, all_global);
   // *rr_global = 100;
   // if(all_global) {
   //    *rr_global = (1 - (double) modified_global / (double) all_global) * 100.;
   // }
}

static void TLB_Miss_all_systemWide(struct perf_read_ev *last, struct perf_read_ev *prev, double * rr_global, double * rr_nodes) {
   int node = 0;
   int  i;
   // unsigned long all_global = 0;
   // unsigned long modified_global = 0;
   unsigned long dTlb_all = 0;
   unsigned long dTlb_walk = 0;
   unsigned long iTlb_all = 0;
   unsigned long iTlb_walk = 0;

   // for(node = 0; node < nb_nodes; node++) {
      long all_idx = node*nb_events;

      for ( i = 0; i < cpu_num; i++)
      {
         dTlb_all = dTlb_all + last[all_idx+i].value;
      }
      for (i = cpu_num; i < cpu_num2; i++)
      {
         dTlb_walk = dTlb_walk + last[all_idx+i].value;
      }
      for (i = cpu_num2; i < cpu_num3; i++)
      {
         iTlb_all = iTlb_all + last[all_idx+i].value;
      }
      for (i = cpu_num3; i < cpu_num4; i++)
      {
         iTlb_walk = iTlb_walk + last[all_idx+i].value;
      }
      
      //printf("Read = %lu , RW = %lu\n", last[all_idx].value - prev[all_idx].value, last[all_idx + 1].value - prev[all_idx + 1].value);
      // unsigned long dTlb_all = last[all_idx].value - prev[all_idx].value;
      // unsigned long dTlb_walk = last[all_idx + 1].value - prev[all_idx + 1].value;

      // unsigned long iTlb_all = last[all_idx + 2].value - prev[all_idx + 2].value;
      // unsigned long iTlb_walk = last[all_idx + 3].value - prev[all_idx + 3].value;
      // ddrc  node 0
      unsigned long hisi_sccl1_ddrc0_flux_wr = last[all_idx + cpu_num4].value;
      unsigned long hisi_sccl1_ddrc0_flux_rd = last[all_idx + cpu_num4+1].value;
      unsigned long hisi_sccl1_ddrc1_flux_wr = last[all_idx + cpu_num4+2].value;
      unsigned long hisi_sccl1_ddrc1_flux_rd = last[all_idx + cpu_num4+3].value;
      unsigned long hisi_sccl1_ddrc2_flux_wr = last[all_idx + cpu_num4+4].value;
      unsigned long hisi_sccl1_ddrc2_flux_rd = last[all_idx + cpu_num4+5].value;
      unsigned long hisi_sccl1_ddrc3_flux_wr = last[all_idx + cpu_num4+6].value;
      unsigned long hisi_sccl1_ddrc3_flux_rd = last[all_idx + cpu_num4+7].value;
      // ddrc  node 1
      unsigned long hisi_sccl3_ddrc0_flux_wr = last[all_idx + cpu_num4+8].value;
      unsigned long hisi_sccl3_ddrc0_flux_rd = last[all_idx + cpu_num4+9].value;
      unsigned long hisi_sccl3_ddrc1_flux_wr = last[all_idx + cpu_num4+10].value;
      unsigned long hisi_sccl3_ddrc1_flux_rd = last[all_idx + cpu_num4+11].value;
      unsigned long hisi_sccl3_ddrc2_flux_wr = last[all_idx + cpu_num4+12].value;
      unsigned long hisi_sccl3_ddrc2_flux_rd = last[all_idx + cpu_num4+13].value;
      unsigned long hisi_sccl3_ddrc3_flux_wr = last[all_idx + cpu_num4+14].value;
      unsigned long hisi_sccl3_ddrc3_flux_rd = last[all_idx + cpu_num4+15].value;
      // ddrc  node 2
      unsigned long hisi_sccl5_ddrc0_flux_wr = last[all_idx + cpu_num4+16].value;
      unsigned long hisi_sccl5_ddrc0_flux_rd = last[all_idx + cpu_num4+17].value;
      unsigned long hisi_sccl5_ddrc1_flux_wr = last[all_idx + cpu_num4+18].value;
      unsigned long hisi_sccl5_ddrc1_flux_rd = last[all_idx + cpu_num4+19].value;
      unsigned long hisi_sccl5_ddrc2_flux_wr = last[all_idx + cpu_num4+20].value;
      unsigned long hisi_sccl5_ddrc2_flux_rd = last[all_idx + cpu_num4+21].value;
      unsigned long hisi_sccl5_ddrc3_flux_wr = last[all_idx + cpu_num4+22].value;
      unsigned long hisi_sccl5_ddrc3_flux_rd = last[all_idx + cpu_num4+23].value;
      // ddrc  node 3
      unsigned long hisi_sccl7_ddrc0_flux_wr = last[all_idx + cpu_num4+24].value;
      unsigned long hisi_sccl7_ddrc0_flux_rd = last[all_idx + cpu_num4+25].value;
      unsigned long hisi_sccl7_ddrc1_flux_wr = last[all_idx + cpu_num4+26].value;
      unsigned long hisi_sccl7_ddrc1_flux_rd = last[all_idx + cpu_num4+27].value;
      unsigned long hisi_sccl7_ddrc2_flux_wr = last[all_idx + cpu_num4+28].value;
      unsigned long hisi_sccl7_ddrc2_flux_rd = last[all_idx + cpu_num4+29].value;
      unsigned long hisi_sccl7_ddrc3_flux_wr = last[all_idx + cpu_num4+30].value;
      unsigned long hisi_sccl7_ddrc3_flux_rd = last[all_idx + cpu_num4+31].value;
      // hha  node 0
      unsigned long hisi_sccl1_hha2_rx_ops_num = last[all_idx + cpu_num4+32].value;
      unsigned long hisi_sccl1_hha2_rx_outer = last[all_idx + cpu_num4+33].value;
      unsigned long hisi_sccl1_hha2_rx_sccl = last[all_idx + cpu_num4+34].value;
      unsigned long hisi_sccl1_hha3_rx_ops_num = last[all_idx + cpu_num4+35].value;
      unsigned long hisi_sccl1_hha3_rx_outer = last[all_idx + cpu_num4+36].value;
      unsigned long hisi_sccl1_hha3_rx_sccl = last[all_idx + cpu_num4+37].value;
      // hha  node 1
      unsigned long hisi_sccl3_hha0_rx_ops_num = last[all_idx + cpu_num4+38].value;
      unsigned long hisi_sccl3_hha0_rx_outer = last[all_idx + cpu_num4+39].value;
      unsigned long hisi_sccl3_hha0_rx_sccl = last[all_idx + cpu_num4+40].value;
      unsigned long hisi_sccl3_hha1_rx_ops_num = last[all_idx + cpu_num4+41].value;
      unsigned long hisi_sccl3_hha1_rx_outer = last[all_idx + cpu_num4+42].value;
      unsigned long hisi_sccl3_hha1_rx_sccl = last[all_idx + cpu_num4+43].value;
      // hha  node 2
      unsigned long hisi_sccl5_hha6_rx_ops_num = last[all_idx + cpu_num4+44].value;
      unsigned long hisi_sccl5_hha6_rx_outer = last[all_idx + cpu_num4+45].value;
      unsigned long hisi_sccl5_hha6_rx_sccl = last[all_idx + cpu_num4+46].value;
      unsigned long hisi_sccl5_hha7_rx_ops_num = last[all_idx + cpu_num4+47].value;
      unsigned long hisi_sccl5_hha7_rx_outer = last[all_idx + cpu_num4+48].value;
      unsigned long hisi_sccl5_hha7_rx_sccl = last[all_idx + cpu_num4+49].value;
      // hha  node 3
      unsigned long hisi_sccl7_hha4_rx_ops_num = last[all_idx + cpu_num4+50].value;
      unsigned long hisi_sccl7_hha4_rx_outer = last[all_idx + cpu_num4+51].value;
      unsigned long hisi_sccl7_hha4_rx_sccl = last[all_idx + cpu_num4+52].value;
      unsigned long hisi_sccl7_hha5_rx_ops_num = last[all_idx + cpu_num4+53].value;
      unsigned long hisi_sccl7_hha5_rx_outer = last[all_idx + cpu_num4+54].value;
      unsigned long hisi_sccl7_hha5_rx_sccl = last[all_idx + cpu_num4+55].value;



      //printf("Read = %lu , RW = %lu\n", last[all_idx].value - prev[all_idx].value, last[all_idx + 1].value - prev[all_idx + 1].value);
      // unsigned long dTlb_all = last[all_idx].value;
      // unsigned long dTlb_walk = last[all_idx + 1].value;

      // unsigned long iTlb_all = last[all_idx + 2].value;
      // unsigned long iTlb_walk = last[all_idx + 3].value;
      // // ddrc  node 0
      // unsigned long hisi_sccl1_ddrc2_flux_wr = last[all_idx + 4].value;
      // unsigned long hisi_sccl1_ddrc2_flux_rd = last[all_idx + 5].value;
      // // ddrc  node 1
      // unsigned long hisi_sccl3_ddrc3_flux_wr = last[all_idx + 6].value;
      // unsigned long hisi_sccl3_ddrc3_flux_rd = last[all_idx + 7].value;
      // // ddrc  node 2
      // unsigned long hisi_sccl5_ddrc2_flux_wr = last[all_idx + 8].value;
      // unsigned long hisi_sccl5_ddrc2_flux_rd = last[all_idx + 9].value;
      // // ddrc  node 3
      // unsigned long hisi_sccl7_ddrc3_flux_wr = last[all_idx + 10].value ;
      // unsigned long hisi_sccl7_ddrc3_flux_rd = last[all_idx + 11].value ;
      // // hha  node 0
      // unsigned long hisi_sccl1_hha3_rx_ops_num = last[all_idx + 12].value ;
      // unsigned long hisi_sccl1_hha3_rx_outer = last[all_idx + 13].value ;
      // unsigned long hisi_sccl1_hha3_rx_sccl = last[all_idx + 14].value ;
      // // hha  node 1
      // unsigned long hisi_sccl3_hha1_rx_ops_num = last[all_idx + 15].value ;
      // unsigned long hisi_sccl3_hha1_rx_outer = last[all_idx + 16].value ;
      // unsigned long hisi_sccl3_hha1_rx_sccl = last[all_idx + 17].value ;
      // // hha  node 2
      // unsigned long hisi_sccl5_hha7_rx_ops_num = last[all_idx + 18].value ;
      // unsigned long hisi_sccl5_hha7_rx_outer = last[all_idx + 19].value ;
      // unsigned long hisi_sccl5_hha7_rx_sccl = last[all_idx + 20].value ;
      // // hha  node 3
      // unsigned long hisi_sccl7_hha5_rx_ops_num = last[all_idx + 21].value ;
      // unsigned long hisi_sccl7_hha5_rx_outer = last[all_idx + 22].value ;
      // unsigned long hisi_sccl7_hha5_rx_sccl = last[all_idx + 23].value ;


      // unsigned long l1dTlb_access = last[all_idx + 4].value - prev[all_idx + 4].value;
      // unsigned long l1dTlb_access_refill = last[all_idx + 5].value - prev[all_idx + 5].value;

      // unsigned long l1iTlb_access = last[all_idx + 6].value - prev[all_idx + 6].value;
      // unsigned long l1iTlb_access_refill = last[all_idx + 7].value - prev[all_idx + 7].value;



      // if(dTlb_all)
         // rr_nodes[node] = (1. - (double) modified / (double) all) * 100.;
         // printf("\niTlb : %lu - %lu.  percentage =  %lf ======= dTlb : %lu - %lu.  percentage =  %lf\n", iTlb_walk, iTlb_all,(double) iTlb_walk / (double) iTlb_all, dTlb_walk, dTlb_all,(double) dTlb_walk / (double) dTlb_all);
         
         

         // fprintf(opt_file_out, "node0 :hisi_sccl1_ddrc2_flux_wr = %d , hisi_sccl1_ddrc2_flux_rd = %d \n",hisi_sccl1_ddrc2_flux_wr,hisi_sccl1_ddrc2_flux_rd);
         // fprintf(opt_file_out, "node1 :hisi_sccl3_ddrc3_flux_wr = %d , hisi_sccl3_ddrc3_flux_rd = %d \n",hisi_sccl3_ddrc3_flux_wr,hisi_sccl3_ddrc3_flux_rd);
         // fprintf(opt_file_out, "node2 :hisi_sccl5_ddrc2_flux_wr = %d , hisi_sccl5_ddrc2_flux_rd = %d \n",hisi_sccl5_ddrc2_flux_wr,hisi_sccl5_ddrc2_flux_rd);
         // fprintf(opt_file_out, "node3 :hisi_sccl7_ddrc3_flux_wr = %d , hisi_sccl7_ddrc3_flux_rd = %d \n",hisi_sccl7_ddrc3_flux_wr,hisi_sccl7_ddrc3_flux_rd);

         // fprintf(opt_file_out, "\nnode0 :hisi_sccl1_hha3_rx_ops_num = %d , hisi_sccl1_hha3_rx_outer = %d , hisi_sccl1_hha3_rx_sccl = %d\n",hisi_sccl1_hha3_rx_ops_num,hisi_sccl1_hha3_rx_outer,hisi_sccl1_hha3_rx_sccl);
         // fprintf(opt_file_out, "node1 :hisi_sccl3_hha1_rx_ops_num = %d , hisi_sccl3_hha1_rx_outer = %d , hisi_sccl3_hha1_rx_sccl = %d\n",hisi_sccl3_hha1_rx_ops_num,hisi_sccl3_hha1_rx_outer,hisi_sccl3_hha1_rx_sccl);
         // fprintf(opt_file_out, "node2 :hisi_sccl5_hha7_rx_ops_num = %d , hisi_sccl5_hha7_rx_outer = %d , hisi_sccl5_hha7_rx_sccl = %d\n",hisi_sccl5_hha7_rx_ops_num,hisi_sccl5_hha7_rx_outer,hisi_sccl5_hha7_rx_sccl);
         // fprintf(opt_file_out, "node3 :hisi_sccl7_hha5_rx_ops_num = %d , hisi_sccl7_hha5_rx_outer = %d , hisi_sccl7_hha5_rx_sccl = %d\n",hisi_sccl7_hha5_rx_ops_num,hisi_sccl7_hha5_rx_outer,hisi_sccl7_hha5_rx_sccl);
         // printf("\niTlb_access : %lu - %lu.  percentage =  %lf ======= dTlb_access : %lu - %lu.  percentage =  %lf\n", l1iTlb_access_refill, l1iTlb_access,(double) l1iTlb_access_refill / (double) l1iTlb_access, l1dTlb_access_refill, l1dTlb_access,(double) l1dTlb_access_refill / (double) l1dTlb_access);
         *rr_global = (double) dTlb_walk / (double) dTlb_all;
         float dTlb_miss_rate = (double) dTlb_walk / (double) dTlb_all;
         float iTlb_miss_rate = (double) iTlb_walk / (double) iTlb_all;
         
         
         

         
        //mem-imb
         unsigned long sccl1_mc = hisi_sccl1_ddrc0_flux_wr + hisi_sccl1_ddrc0_flux_rd + hisi_sccl1_ddrc1_flux_wr + hisi_sccl1_ddrc1_flux_rd+ hisi_sccl1_ddrc2_flux_wr + hisi_sccl1_ddrc2_flux_rd+ hisi_sccl1_ddrc3_flux_wr + hisi_sccl1_ddrc3_flux_rd;
         unsigned long sccl3_mc = hisi_sccl3_ddrc0_flux_wr + hisi_sccl3_ddrc0_flux_rd + hisi_sccl3_ddrc1_flux_wr + hisi_sccl3_ddrc1_flux_rd + hisi_sccl3_ddrc2_flux_wr + hisi_sccl3_ddrc2_flux_rd + hisi_sccl3_ddrc3_flux_wr + hisi_sccl3_ddrc3_flux_rd;
         unsigned long sccl5_mc = hisi_sccl5_ddrc0_flux_wr + hisi_sccl5_ddrc0_flux_rd + hisi_sccl5_ddrc1_flux_wr + hisi_sccl5_ddrc1_flux_rd + hisi_sccl5_ddrc2_flux_wr + hisi_sccl5_ddrc2_flux_rd + hisi_sccl5_ddrc3_flux_wr + hisi_sccl5_ddrc3_flux_rd;
         unsigned long sccl7_mc = hisi_sccl7_ddrc0_flux_wr + hisi_sccl7_ddrc0_flux_rd + hisi_sccl7_ddrc1_flux_wr + hisi_sccl7_ddrc1_flux_rd + hisi_sccl7_ddrc2_flux_wr + hisi_sccl7_ddrc2_flux_rd + hisi_sccl7_ddrc3_flux_wr + hisi_sccl7_ddrc3_flux_rd;

         double arr[4] ={sccl1_mc,sccl3_mc,sccl5_mc,sccl7_mc};

         double mem_imb = standard_deviation(arr, 4) / aver(arr, 4);

         // fprintf(opt_file_out, "\nMemory_controller_imbalance = %lf \n",mem_imb);
         // printf("mem_imb = %lf      :sccl1_mc = %d , sccl3_mc = %d , sccl5_mc = %d, sccl7_mc = %d,aver = %lf, standard_deviation = %lf \n",mem_imb,sccl1_mc,sccl3_mc,sccl5_mc,sccl7_mc,aver(arr, 4),standard_deviation(arr, 4));
         unsigned long  node0_local_access = hisi_sccl1_hha2_rx_ops_num - hisi_sccl1_hha2_rx_outer - hisi_sccl1_hha2_rx_sccl + hisi_sccl1_hha3_rx_ops_num - hisi_sccl1_hha3_rx_outer - hisi_sccl1_hha3_rx_sccl;
         unsigned long  node1_local_access = hisi_sccl3_hha0_rx_ops_num - hisi_sccl3_hha0_rx_outer - hisi_sccl3_hha0_rx_sccl + hisi_sccl3_hha1_rx_ops_num - hisi_sccl3_hha1_rx_outer - hisi_sccl3_hha1_rx_sccl;
         unsigned long  node2_local_access = hisi_sccl5_hha6_rx_ops_num - hisi_sccl5_hha6_rx_outer - hisi_sccl5_hha6_rx_sccl + hisi_sccl5_hha7_rx_ops_num - hisi_sccl5_hha7_rx_outer - hisi_sccl5_hha7_rx_sccl;
         unsigned long  node3_local_access = hisi_sccl7_hha4_rx_ops_num - hisi_sccl7_hha4_rx_outer - hisi_sccl7_hha4_rx_sccl + hisi_sccl7_hha5_rx_ops_num - hisi_sccl7_hha5_rx_outer - hisi_sccl7_hha5_rx_sccl;
         double LRR = (double) (node0_local_access + node1_local_access + node2_local_access + node3_local_access)/(double) (hisi_sccl1_hha2_rx_ops_num +hisi_sccl1_hha3_rx_ops_num + hisi_sccl3_hha0_rx_ops_num + hisi_sccl3_hha1_rx_ops_num + hisi_sccl5_hha6_rx_ops_num + hisi_sccl5_hha7_rx_ops_num + hisi_sccl7_hha4_rx_ops_num + hisi_sccl7_hha5_rx_ops_num);
         // fprintf(opt_file_out, "\nLRR = %lf \n",LRR);

         double arr2[8] ={hisi_sccl1_hha2_rx_outer + hisi_sccl1_hha3_rx_outer,hisi_sccl1_hha2_rx_sccl + hisi_sccl1_hha3_rx_sccl,hisi_sccl3_hha0_rx_outer + hisi_sccl3_hha1_rx_outer,hisi_sccl3_hha0_rx_sccl + hisi_sccl3_hha1_rx_sccl,hisi_sccl5_hha6_rx_outer + hisi_sccl5_hha7_rx_outer,hisi_sccl5_hha6_rx_sccl + hisi_sccl5_hha7_rx_sccl,hisi_sccl7_hha4_rx_outer + hisi_sccl7_hha5_rx_outer,hisi_sccl7_hha4_rx_sccl + hisi_sccl7_hha5_rx_sccl};
         double i_imb = standard_deviation(arr2, 8) / aver(arr2, 8);

         
      if (all_open)
      {
         fprintf(opt_file_out, "all :\n");
         fprintf(opt_file_out, "\n\n\ndTlb_miss_rate = %lf    : %lu - %lu\n",dTlb_miss_rate, dTlb_walk, dTlb_all);
         fprintf(opt_file_out, "\niTlb_miss_rate = %lf    : %lu - %lu\n",iTlb_miss_rate, iTlb_walk, iTlb_all);
         fprintf(opt_file_out, "\nMemory_controller_imbalance = %lf \n",mem_imb);
         fprintf(opt_file_out, "\nLRR = %lf \n",LRR);
         fprintf(opt_file_out, "\ninterconnect_imbalance = %lf \n",i_imb);  
             
      }

      if (dTlb_miss_rate>0.001 && LRR < 0.6 && mem_imb > 0.1)
      {
         average_check_pass +=1;
         fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
         fprintf(opt_file_out, "\n Average matchs the condition of PageTable replication !");
         fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
      }

   // }
// printf("all_global %d : %lu - %lu\n", node, modified_global, all_global);
   // *rr_global = 100;
   // if(all_global) {
   //    *rr_global = (1 - (double) modified_global / (double) all_global) * 100.;
   // }
}
#if USE_MRR
static void mrr(struct perf_read_ev *last, struct perf_read_ev *prev, double * rr_global, double * maptu_global, double * rr_nodes, double * maptu_nodes) {
   int node;
   unsigned long read_global = 0;
   unsigned long rw_global = 0;

   //unsigned long time_enabled = last->time_enabled-prev->time_enabled;
   unsigned long time_enabled = last->time_running-prev->time_running;

   for(node = 0; node < nb_nodes; node++) {
      long read_idx = node*nb_events;

#if ENABLE_MULTIPLEXING_CHECKS
      long percent_running_read = percent_running(&last[read_idx], &prev[read_idx]);
      long percent_running_rw = percent_running(&last[read_idx + 1], &prev[read_idx + 1]);

      if(percent_running_read < MIN_ACTIVE_PERCENTAGE) {
         printf("WARNING: %ld %%\n", percent_running_read);
      }

      if(percent_running_rw > percent_running_read+1 || percent_running_rw < percent_running_read-1) { //Allow 1% difference
         printf("WARNING: %% read = %ld , %% rw = %ld\n", percent_running_read, percent_running_rw);
      }
#endif

      //printf("Read = %lu , RW = %lu\n", last[read_idx].value - prev[read_idx].value, last[read_idx + 1].value - prev[read_idx + 1].value);
      unsigned long read = last[read_idx].value - prev[read_idx].value;
      unsigned long rw   = last[read_idx + 1].value - prev[read_idx + 1].value;

      read_global += read;
      rw_global += rw;

      rr_nodes[node] = 1;
      maptu_nodes[node] = 0;

      if(rw) {
         rr_nodes[node] = ((double) read) / ((double) rw) * 100.;
      }

      //printf("%lu - %lu - %lu\n", read, rw, time_enabled);
      if(time_enabled) {
         maptu_nodes[node] = (double) rw / (double) time_enabled;
      }
   }

   *rr_global = 1;
   *maptu_global = 0;

   if(rw_global) {
      *rr_global = ((double) read_global) / ((double) rw_global) * 100.;
   }

   //printf("%lu - %lu - %lu\n", read, rw, time_enabled);
   if(time_enabled) {
      *maptu_global = (double) rw_global / (double) time_enabled;
   }
}

#else
static void dcmr(struct perf_read_ev *last, struct perf_read_ev *prev, double * rr_global, double * rr_nodes) {
   int node;
   unsigned long all_global = 0;
   unsigned long modified_global = 0;

   for(node = 0; node < nb_nodes; node++) {
      long all_idx = node*nb_events;

#if ENABLE_MULTIPLEXING_CHECKS
      long percent_running_all = percent_running(&last[all_idx], &prev[all_idx]);
      long percent_running_modified = percent_running(&last[all_idx + 1], &prev[all_idx + 1]);

      if(percent_running_all < MIN_ACTIVE_PERCENTAGE) {
         printf("WARNING: %ld %%\n", percent_running_read);
      }

      if(percent_running_all > percent_running_modified+1 || percent_running_all < percent_running_modified-1) { //Allow 1% difference
         printf("WARNING: %% all = %ld , %% modified = %ld\n", percent_running_all, percent_running_modified);
      }
#endif

      //printf("Read = %lu , RW = %lu\n", last[all_idx].value - prev[all_idx].value, last[all_idx + 1].value - prev[all_idx + 1].value);
      unsigned long all = last[all_idx].value - prev[all_idx].value;
      unsigned long modified = last[all_idx + 1].value - prev[all_idx + 1].value;

      all_global += all;
      modified_global += modified;

      rr_nodes[node] = 100;
      if(all) {
         //printf("%d : %lu - %lu\n", node, modified, all);
         rr_nodes[node] = (1. - (double) modified / (double) all) * 100.;
      }
   }

   *rr_global = 100;
   if(all_global) {
      *rr_global = (1 - (double) modified_global / (double) all_global) * 100.;
   }
}
#endif

static void dram_accesses(struct perf_read_ev *last, struct perf_read_ev *prev, double * lar, double * load_imbalance, double * aggregate_dram_accesses_to_node, double * lar_node, double * maptu_global, double * maptu_nodes) {
   int node;
   unsigned long la_global = 0;
   unsigned long ta_global = 0;

   for(node = 0; node < nb_nodes; node++) {
      long node0_idx = node*nb_events + 2; // The first two events are used to compute the mrr

      int to_node = 0;
      unsigned long ta = 0;
      unsigned long la = 0;

#if ENABLE_MULTIPLEXING_CHECKS
      long percent_running_n0 = percent_running(&last[node0_idx], &prev[node0_idx]);
#endif

      for(to_node = 0; to_node < nb_nodes; to_node++) { //Hard coded for now. Todo.
         long percent_running_node = percent_running(&last[node0_idx + to_node], &prev[node0_idx + to_node]);

#if ENABLE_MULTIPLEXING_CHECKS
         if(percent_running_node< MIN_ACTIVE_PERCENTAGE) {
            printf("WARNING: %ld %%\n", percent_running_node);
         }

         if(percent_running_node > percent_running_n0+1 || percent_running_node < percent_running_n0-1) { //Allow 1% difference
            printf("WARNING: %% node %d = %ld , %% n0 = %ld\n", to_node, percent_running_node, percent_running_n0);
         }
#endif

         unsigned long da = last[node0_idx + to_node].value - prev[node0_idx + to_node].value;
         if(percent_running_node) {
            da = (da * 100) / percent_running_node; // Try to fix perf mutliplexing issues
         }
         else {
            da = 0;
         }

         //printf("Node %d to node %d : da = %lu, %% running = %ld\n", node, to_node, da, percent_running_node);

         if(node == to_node) {
            la_global += da;
            la += da;
         }

         ta_global += da;
         ta += da;

         aggregate_dram_accesses_to_node[to_node] += da;
      }


      if(ta) {
         lar_node[node] = (double) la / (double) ta;
      }
   }

   for(node = 0; node < nb_nodes; node++) {
      if(maptu_nodes) {
         maptu_nodes[node] = 0;
         if(last->time_enabled-prev->time_enabled) {
            maptu_nodes[node] = (double) aggregate_dram_accesses_to_node[node] / (double) (last->time_enabled-prev->time_enabled) ;
         }
      }
   }

   if(ta_global) {
      *lar = (double) la_global / (double) ta_global;
   }
   else {
      *lar = 0;
   }

   if(maptu_global) {
      *maptu_global = 0;
      if(last->time_enabled-prev->time_enabled) {
         *maptu_global = (double) ta_global / (double) (last->time_enabled-prev->time_enabled) ;
      }
   }


   double mean_da = gsl_stats_mean(aggregate_dram_accesses_to_node, 1, nb_nodes);
   double stddev_da = gsl_stats_sd_m(aggregate_dram_accesses_to_node, 1, nb_nodes, mean_da);

   if(mean_da) {
      *load_imbalance = stddev_da / mean_da;
   }
   else {
      *load_imbalance = 0;
   }
}

#if ENABLE_IPC
static void ipc(struct perf_read_ev *last, struct perf_read_ev *prev, double * ipc_global, double * ipc_node) {
   int node;
   unsigned long clk_global = 0;
   unsigned long inst_global = 0;

   for(node = 0; node < nb_nodes; node++) {
      long cpuclock_idx = node*nb_events + 6; // The first two events are used to compute the mrr, the next 4 to compute the LAR

#if ENABLE_MULTIPLEXING_CHECKS
      long percent_running_clock = percent_running(&last[cpuclock_idx], &prev[cpuclock_idx]);
      long percent_running_instructions = percent_running(&last[cpuclock_idx + 1], &prev[cpuclock_idx + 1]);

      if(percent_running_clock < MIN_ACTIVE_PERCENTAGE) {
         printf("WARNING: %ld %%\n", percent_running_clock);
      }

      if(percent_running_clock > percent_running_instructions+1 || percent_running_clock < percent_running_instructions-1) { //Allow 1% difference
         printf("WARNING: %% clock = %ld , %% instructions = %ld\n", percent_running_clock, percent_running_instructions);
      }
#endif

      unsigned long clk = last[cpuclock_idx].value - prev[cpuclock_idx].value;
      unsigned long inst = last[cpuclock_idx + 1].value - prev[cpuclock_idx + 1].value;

      clk_global += clk;
      inst_global += inst;

      if(clk) {
         ipc_node[node] = (double) inst / (double) clk;
      }
      else {
        printf("WARNING: Clk = 0 !!!\n");
      }
   }

   if(clk_global) {
      *ipc_global = (double) inst_global / (double) clk_global;
   }
   else {
      printf("WARNING: clk_global = 0 !!!\n");
   }
}
#endif

/** For now we take decision with a global overview... */
static int carrefour_replication_enabled    = 1; // It is enabled by default
static int carrefour_interleaving_enabled   = 1; // It is enabled by default
static int carrefour_migration_enabled      = 1; // It is enabled by default

#if USE_MRR
static const int rr_min = MRR_MIN;
#else
static const int rr_min = 100 - DCRM_MAX;
#endif

static inline void carrefour(double rr, double maptu, double lar, double imbalance, double *aggregate_dram_accesses_to_node, double ipc, double global_mem_usage) {
   int carrefour_enabled = 0;

#if ENABLE_IPC
   if(maptu >= MAPTU_MIN && ipc <= IPC_MAX) {
      carrefour_enabled = 1;
   }
#else
   if(maptu >= MAPTU_MIN) {
      carrefour_enabled = 1;
   }
#endif

   if(carrefour_enabled) {
      /** Check for replication thresholds **/
      int er = (global_mem_usage <= MEMORY_USAGE_MAX) && (rr >= rr_min);

      if(er && !carrefour_replication_enabled) {
         change_carrefour_state('R');
         carrefour_replication_enabled = 1;
      }
      else if (!er && carrefour_replication_enabled) {
         change_carrefour_state('r');
         carrefour_replication_enabled = 0;
      }

      /** Check for interleaving threasholds **/
      int ei = lar < MAX_LOCALITY && imbalance > MIN_IMBALANCE;

      if(ei && ! carrefour_interleaving_enabled) {
         change_carrefour_state('I');
         carrefour_interleaving_enabled = 1;
      }
      else if(!ei && carrefour_interleaving_enabled) {
         //printf("GLOBAL: disable interleaving (lar = %.1f, imbalance = %.1f)\n", lar, imbalance);
         change_carrefour_state('i');
         carrefour_interleaving_enabled = 0;
      }

      /** Check for migration threasholds **/
      if(lar < MAX_LOCALITY_MIGRATION && ! carrefour_migration_enabled) {
         change_carrefour_state('M');
         carrefour_migration_enabled = 1;
      }
      else if (lar >= MAX_LOCALITY_MIGRATION && carrefour_migration_enabled) {
         change_carrefour_state('m');
         carrefour_migration_enabled = 0;
      }

      /** Interleaving needs more feedback **/
      if(carrefour_interleaving_enabled) {
         char feedback[MAX_FEEDBACK_LENGTH];
         int node, written;
         memset(feedback, 0, MAX_FEEDBACK_LENGTH*sizeof(char));

         for(node = 0; node < nb_nodes; node++) {
            if(node == 0) {
               written = snprintf(feedback, MAX_FEEDBACK_LENGTH, "T%lu", (unsigned long) aggregate_dram_accesses_to_node[node]);
            }
            else {
               written += snprintf(feedback+written, MAX_FEEDBACK_LENGTH - written, ",%lu", (unsigned long) aggregate_dram_accesses_to_node[node]);
            }
         }

         if(written < MAX_FEEDBACK_LENGTH) {
            change_carrefour_state_str(feedback);
         }
         else {
            printf("WARNING: You MUST increase MAX_FEEDBACK_LENGTH!\n");
         }
      }

      /** Update state **/
      if(!carrefour_replication_enabled && !carrefour_interleaving_enabled && !carrefour_migration_enabled) {
         carrefour_enabled = 0;
      }
   }

   printf("[DECISION] Carrefour %s, migration %s, interleaving %s, replication %s\n\n",
         carrefour_enabled ? "Enabled" : "Disabled",
         carrefour_migration_enabled ? "Enabled" : "Disabled",
         carrefour_interleaving_enabled ? "Enabled" : "Disabled",
         carrefour_replication_enabled ? "Enabled" : "Disabled");

   if(carrefour_enabled) {
      change_carrefour_state('e'); // End profiling + lauches carrefour
      change_carrefour_state('b'); // Start the profiling again
   }
   else {
      change_carrefour_state('x');
   }
}

static void thread_loop() {
   int i, j, cpuid;
   int *fd = calloc(nb_events * sizeof(*fd) * nb_nodes, 1);
   struct perf_event_attr *events_attr = calloc(nb_events * sizeof(*events_attr) * nb_nodes, 1);
   assert(events_attr != NULL);
   assert(fd);
   for(i = 0; i < nb_nodes; i++) {
      int core = cpu_of_node(i);
      for (j = 0; j < nb_events; j++) {
         //printf("Registering event %d on node %d\n", j, i);
         events_attr[i*nb_events + j].size = sizeof(struct perf_event_attr);
         events_attr[i*nb_events + j].type = events[j].type;
         events_attr[i*nb_events + j].config = events[j].config;
         events_attr[i*nb_events + j].exclude_kernel = events[j].exclude_kernel;
         events_attr[i*nb_events + j].exclude_user = events[j].exclude_user;
         events_attr[i*nb_events + j].read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
         fd[i*nb_events + j] = sys_perf_counter_open(&events_attr[i*nb_events + j], (j<4||j>59)?obj_pid:-1, events[j].cpuid, (events[j].leader==-1)?-1:fd[i*nb_events + events[j].leader], 0);
         // printf("j = %d , fd[j] = %d",j , fd[i*nb_events + j]);
         if (fd[i*nb_events + j] < 0) {
            fprintf(stdout, "#[%d] sys_perf_counter_open failed: %s\n", core, strerror(errno));
            return;
         }
      }
   }
	
   struct perf_read_ev single_count;
   struct perf_read_ev *last_counts = calloc(nb_nodes*nb_events, sizeof(*last_counts));
   struct perf_read_ev *last_counts_prev = calloc(nb_nodes*nb_events, sizeof(*last_counts_prev));

   double *rr_nodes, *maptu_nodes;
   double *aggregate_dram_accesses_to_node, *lar_node;
   double * ipc_node;

   rr_nodes = (double *) malloc(nb_nodes*sizeof(double));
   maptu_nodes =  (double *) malloc(nb_nodes*sizeof(double));
   aggregate_dram_accesses_to_node = (double *) malloc(nb_nodes*sizeof(double));
   lar_node = (double *) malloc(nb_nodes*sizeof(double));
   ipc_node = (double *) malloc(nb_nodes*sizeof(double));

   // change_carrefour_state('b'); // Make sure that the profiling is started

   while (1) {
       /* check if the pid still exists before collecting dump.
         * This is racy as a new process may acquire the same pid
         * but the chances of that happenning for us is really really low .
         */
        if (kill(obj_pid, 0) && errno == ESRCH)
            break;

      usleep(sleep_time);
      for(i = 0; i < nb_nodes; i++) {
         for (j = 0; j < nb_events; j++) {
            assert(read(fd[i*nb_events + j], &single_count, sizeof(single_count)) == sizeof(single_count));
/*            printf("[%d,%d] %ld enabled %ld running %ld%%\n", i, j,
                  single_count.time_enabled - last_counts[i*nb_events + j].time_enabled,
                  single_count.time_running - last_counts[i*nb_events + j].time_running,
                  (single_count.time_enabled-last_counts[i*nb_events + j].time_enabled)?100*(single_count.time_running-last_counts[i*nb_events + j].time_running)/(single_count.time_enabled-last_counts[i*nb_events + j].time_enabled):0); */
            last_counts[i*nb_events + j] = single_count;
         }
      }

      double rr_global = 0, maptu_global = 0;
      double lar = 0, load_imbalance = 0;
      double ipc_global = 0;

      memset(rr_nodes, 0, nb_nodes*sizeof(double));
      memset(maptu_nodes, 0, nb_nodes*sizeof(double));
      memset(aggregate_dram_accesses_to_node, 0, nb_nodes*sizeof(double));
      memset(lar_node, 0, nb_nodes*sizeof(double));
      memset(ipc_node, 0, nb_nodes*sizeof(double));

      fprintf(opt_file_out, "\n\n第 %d 次 采样 \n",sampling_times);

      TLB_Miss_last_10s(last_counts, last_counts_prev, &rr_global, rr_nodes);
      //  TLB_Miss_all(last_counts, last_counts_prev, &rr_global, rr_nodes);
       fprintf(opt_file_out, "\n满足开启页表复制的单次采样 %d 次； 占 总采样次数的百分比 ： %3f %%\n",check_pass,(double)check_pass/(double)sampling_times *100);
      // fprintf(opt_file_out, "\n满足开启页表复制的平均采样 %d 次； 占 总采样次数的百分比 ： %3f %%\n",average_check_pass,(double)average_check_pass/(double)sampling_times *100);
      fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
      fflush(opt_file_out);


      if (shut_down)
      {
         printf("error: Topology is not the same as defult\n");
         fprintf(opt_file_out, "error: Topology is not the same as defult\n");
         break;
      }
      
      
      // 
// #if USE_MRR
//       mrr(last_counts, last_counts_prev, &rr_global, &maptu_global, rr_nodes, maptu_nodes);
//       dram_accesses(last_counts, last_counts_prev, &lar, &load_imbalance, aggregate_dram_accesses_to_node, lar_node, NULL, NULL);
// #else
//       dcmr(last_counts, last_counts_prev, &rr_global, rr_nodes);
//       dram_accesses(last_counts, last_counts_prev, &lar, &load_imbalance, aggregate_dram_accesses_to_node, lar_node, &maptu_global, maptu_nodes);
// #endif


// #if ENABLE_IPC
//       ipc(last_counts, last_counts_prev, &ipc_global, ipc_node);
// #endif

      struct sysinfo info;
      double global_mem_usage = 0;
      if (sysinfo(&info) != 0) {
         printf("sysinfo: error reading system statistics");
         global_mem_usage = 0;
      }
      else {
         global_mem_usage =  (double) (info.totalram-info.freeram) / (double) info.totalram * 100.;
      }

      if (rr_global > ACTIVE_PERCENTAGE)
      {
         // printf("\n we should start pgtrpl !!");
         // sys_set_ptsr_enable(obj_pid);
         // for(i = 0; i < nb_nodes; i++) {
         //   for (j = 0; j < nb_events; j++) {
         //      close(fd[i*nb_events + j]); 
         //   }  
         // }
         // free(rr_nodes);
         // free(maptu_nodes);
         // free(aggregate_dram_accesses_to_node);
         // free(lar_node);
         // free(ipc_node);
         // break;
      }
      
      

      // for(i = 0; i < nb_nodes; i++) {
      //    printf("[ Node %d ] %.1f %% read accesses - MAPTU = %.1f - # of accesses = %.1f - LAR = %.1f - IPC = %.2f\n",
      //             i, rr_nodes[i], maptu_nodes[i] * 1000., aggregate_dram_accesses_to_node[i], lar_node[i] * 100., ipc_node[i]);
      // }
      // printf("[ GLOBAL ] %.1f %% read accesses - MAPTU = %.1f - LAR = %.1f - Imbalance = %.1f %% - IPC = %.2f - Mem usage = %.1f %%\n",
      //             rr_global, maptu_global * 1000., lar * 100., load_imbalance * 100., ipc_global, global_mem_usage);

      // carrefour(rr_global, maptu_global * 1000., lar * 100., load_imbalance * 100., aggregate_dram_accesses_to_node, ipc_global, global_mem_usage);

      for(i = 0; i < nb_nodes; i++) {
         for (j = 0; j < nb_events; j++) {
            last_counts_prev[i*nb_events + j] = last_counts[i*nb_events + j];
         }
      }
      // collect_pagetable(obj_pid, opt_file_out);

   }

   free(rr_nodes);
   free(maptu_nodes);
   free(aggregate_dram_accesses_to_node);
   free(lar_node);
   free(ipc_node);

   return;
}

static void thread_loop_systemWide() {

   int i, j, cpuid;
   nb_events = cpu_num*4 + 56;


   event_t systemWide_events[nb_events];
   for (i = 0; i < cpu_num; i++)
   {
      systemWide_events[i] = (event_t){
      .name    = "L1d_TLB",
      .type    = 0x8,
      .config  = 0x25,
      .leader  = -1,
      .cpuid   = i,
      };
   }
   
   for (i = cpu_num; i < cpu_num2; i++)
   {
      systemWide_events[i] = (event_t){
      .name    = "dtlb_walk",
      .type    = 0x8,
      .config  = 0x34,
      .leader  = -1,
      .cpuid   = i-cpu_num,
      };
   }
   for (i = cpu_num2; i < cpu_num3; i++)
   {
      systemWide_events[i] = (event_t){
      .name    = "L1i_TLB",
      .type    = 0x8,
      .config  = 0x26,
      .leader  = -1,
      .cpuid   = i-cpu_num2,
   };
   }
   for (i = cpu_num3; i < cpu_num4; i++)
   {
      systemWide_events[i] = (event_t){
      .name    = "itlb_walk",
      .type    = 0x8,
      .config  = 0x35,
      .leader  = -1,
      .cpuid   = i-cpu_num3,
   };
   }

   //node0  ddrc
   systemWide_events[cpu_num4] = (event_t){ //ddrc
      .name    = "hisi_sccl1_ddrc0/flux_wr/",
      .type    = 0x2d,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+1] = (event_t){
      .name    = "hisi_sccl1_ddrc0/flux_rd/",
      .type    = 0x2d,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+2] = (event_t){ //ddrc
      .name    = "hisi_sccl1_ddrc1/flux_wr/",
      .type    = 0x2e,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+3] = (event_t){
      .name    = "hisi_sccl1_ddrc1/flux_rd/",
      .type    = 0x2e,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+4] = (event_t){ //ddrc
      .name    = "hisi_sccl1_ddrc2/flux_wr/",
      .type    = 0x2f,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+5] = (event_t){
      .name    = "hisi_sccl1_ddrc2/flux_rd/",
      .type    = 0x2f,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+6] = (event_t){ //ddrc
      .name    = "hisi_sccl1_ddrc3/flux_wr/",
      .type    = 0x30,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+7] = (event_t){
      .name    = "hisi_sccl1_ddrc3/flux_rd/",
      .type    = 0x30,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = 0,
   };

   //node1  ddrc
   systemWide_events[cpu_num4+8] = (event_t)
   {
      .name    = "hisi_sccl3_ddrc0/flux_wr/",
      .type    = 0x29,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+9] = (event_t){
      .name    = "hisi_sccl3_ddrc0/flux_rd/",
      .type    = 0x29,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+10] = (event_t)
   {
      .name    = "hisi_sccl3_ddrc1/flux_wr/",
      .type    = 0x2a,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+11] = (event_t){
      .name    = "hisi_sccl3_ddrc1/flux_rd/",
      .type    = 0x2a,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+12] = (event_t)
   {
      .name    = "hisi_sccl3_ddrc2/flux_wr/",
      .type    = 0x2b,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+13] = (event_t){
      .name    = "hisi_sccl3_ddrc2/flux_rd/",
      .type    = 0x2b,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+14] = (event_t)
   {
      .name    = "hisi_sccl3_ddrc3/flux_wr/",
      .type    = 0x2c,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+15] = (event_t){
      .name    = "hisi_sccl3_ddrc3/flux_rd/",
      .type    = 0x2c,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node,
   };

//node2  ddrc
   systemWide_events[cpu_num4+16] = (event_t){
      .name    = "hisi_sccl5_ddrc0/flux_wr/",
      .type    = 0x35,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+17] = (event_t){
      .name    = "hisi_sccl5_ddrc0/flux_rd/",
      .type    = 0x35,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+18] = (event_t){
      .name    = "hisi_sccl5_ddrc1/flux_wr/",
      .type    = 0x36,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+19] = (event_t){
      .name    = "hisi_sccl5_ddrc1/flux_rd/",
      .type    = 0x36,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+20] = (event_t){
      .name    = "hisi_sccl5_ddrc2/flux_wr/",
      .type    = 0x37,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+21] = (event_t){
      .name    = "hisi_sccl5_ddrc2/flux_rd/",
      .type    = 0x37,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+22] = (event_t){
      .name    = "hisi_sccl5_ddrc3/flux_wr/",
      .type    = 0x38,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+23] = (event_t){
      .name    = "hisi_sccl5_ddrc3/flux_rd/",
      .type    = 0x38,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*2,
   };

//node3  ddrc
   systemWide_events[cpu_num4+24] = (event_t){
      .name    = "hisi_sccl7_ddrc0/flux_wr/",
      .type    = 0x31,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+25] = (event_t){
      .name    = "hisi_sccl7_ddrc0/flux_rd/",
      .type    = 0x31,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+26] = (event_t){
      .name    = "hisi_sccl7_ddrc1/flux_wr/",
      .type    = 0x32,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+27] = (event_t){
      .name    = "hisi_sccl7_ddrc1/flux_rd/",
      .type    = 0x32,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+28] = (event_t){
      .name    = "hisi_sccl7_ddrc2/flux_wr/",
      .type    = 0x33,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+29] = (event_t){
      .name    = "hisi_sccl7_ddrc2/flux_rd/",
      .type    = 0x33,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+30] = (event_t){
      .name    = "hisi_sccl7_ddrc3/flux_wr/",
      .type    = 0x34,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+31] = (event_t){
      .name    = "hisi_sccl7_ddrc3/flux_rd/",
      .type    = 0x34,
      .config  = 0x1,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*3,
   };


// node0  hha  
   systemWide_events[cpu_num4+32] = (event_t){
      .name    = "hisi_sccl1_hha2/rx_ops_num/",       
      .type    = 0x23,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+33] = (event_t){
      .name    = "hisi_sccl1_hha2/rx_outer/",
      .type    = 0x23,
      .config  = 0x1,
      .leader  = cpu_num4+32,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+34] = (event_t){
      .name    = "hisi_sccl1_hha2/rx_sccl/",
      .type    = 0x23,
      .config  = 0x2,
      .leader  = cpu_num4+32,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+35] = (event_t){
      .name    = "hisi_sccl1_hha3/rx_ops_num/",       
      .type    = 0x24,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+36] = (event_t){
      .name    = "hisi_sccl1_hha3/rx_outer/",
      .type    = 0x24,
      .config  = 0x1,
      .leader  = cpu_num4+35,
      .cpuid   = 0,
   };
   systemWide_events[cpu_num4+37] = (event_t){
      .name    = "hisi_sccl1_hha3/rx_sccl/",
      .type    = 0x24,
      .config  = 0x2,
      .leader  = cpu_num4+35,
      .cpuid   = 0,
   };

// node1  hha  

   systemWide_events[cpu_num4+38] = (event_t){
      .name    = "hisi_sccl3_hha0/rx_ops_num/",
      .type    = 0x21,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+39] = (event_t){
      .name    = "hisi_sccl3_hha0/rx_outer/",
      .type    = 0x21,
      .config  = 0x1,
      .leader  = cpu_num4+38,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+40] = (event_t){
      .name    = "hisi_sccl3_hha0/rx_sccl/",
      .type    = 0x21,
      .config  = 0x2,
      .leader  = cpu_num4+38,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+41] = (event_t){
      .name    = "hisi_sccl3_hha1/rx_ops_num/",
      .type    = 0x22,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+42] = (event_t){
      .name    = "hisi_sccl3_hha1/rx_outer/",
      .type    = 0x22,
      .config  = 0x1,
      .leader  = cpu_num4+41,
      .cpuid   = cpu_num_per_node,
   };
   systemWide_events[cpu_num4+43] = (event_t){
      .name    = "hisi_sccl3_hha1/rx_sccl/",
      .type    = 0x22,
      .config  = 0x2,
      .leader  = cpu_num4+41,
      .cpuid   = cpu_num_per_node,
   };

// node2  hha 

   systemWide_events[cpu_num4+44] = (event_t){
      .name    = "hisi_sccl5_hha6/rx_ops_num/",
      .type    = 0x27,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+45] = (event_t) {
      .name    = "hisi_sccl5_hha6/rx_outer/",
      .type    = 0x27,
      .config  = 0x1,
      .leader  = cpu_num4+44,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+46] = (event_t){
      .name    = "hisi_sccl5_hha6/rx_sccl/",
      .type    = 0x27,
      .config  = 0x2,
      .leader  = cpu_num4+44,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+47] = (event_t){
      .name    = "hisi_sccl5_hha7/rx_ops_num/",
      .type    = 0x28,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+48] = (event_t) {
      .name    = "hisi_sccl5_hha7/rx_outer/",
      .type    = 0x28,
      .config  = 0x1,
      .leader  = cpu_num4+47,
      .cpuid   = cpu_num_per_node*2,
   };
   systemWide_events[cpu_num4+49] = (event_t){
      .name    = "hisi_sccl5_hha7/rx_sccl/",
      .type    = 0x28,
      .config  = 0x2,
      .leader  = cpu_num4+47,
      .cpuid   = cpu_num_per_node*2,
   };


// node3  hha

   systemWide_events[cpu_num4+50] = (event_t){
      .name    = "hisi_sccl7_hha4/rx_ops_num/",
      .type    = 0x25,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+51] = (event_t){
      .name    = "hisi_sccl7_hha4/rx_outer/",
      .type    = 0x25,
      .config  = 0x1,
      .leader  = cpu_num4+50,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+52] = (event_t){
      .name    = "hisi_sccl7_hha4/rx_sccl/",
      .type    = 0x25,
      .config  = 0x2,
      .leader  = cpu_num4+50,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+53] = (event_t){
      .name    = "hisi_sccl7_hha5/rx_ops_num/",
      .type    = 0x26,
      .config  = 0x0,
      .leader  = -1,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+54] = (event_t){
      .name    = "hisi_sccl7_hha5/rx_outer/",
      .type    = 0x26,
      .config  = 0x1,
      .leader  = cpu_num4+53,
      .cpuid   = cpu_num_per_node*3,
   };
   systemWide_events[cpu_num4+55] = (event_t){
      .name    = "hisi_sccl7_hha5/rx_sccl/",
      .type    = 0x26,
      .config  = 0x2,
      .leader  = cpu_num4+53,
      .cpuid   = cpu_num_per_node*3,
   };
   

   events = systemWide_events;

   // for (i = 0; i < nb_events; i++)
   // {
   //    printf("\n event name = %s, event cpuid = %d",events[i].name,events[i].cpuid);
   // }


   // return;


   
   int *fd = calloc(nb_events * sizeof(*fd) * nb_nodes, 1);
   struct perf_event_attr *events_attr = calloc(nb_events * sizeof(*events_attr) * nb_nodes, 1);
   assert(events_attr != NULL);
   assert(fd);
   for(i = 0; i < nb_nodes; i++) {
      int core = cpu_of_node(i);
      for (j = 0; j < nb_events; j++) {
         //printf("Registering event %d on node %d\n", j, i);
         events_attr[i*nb_events + j].size = sizeof(struct perf_event_attr);
         events_attr[i*nb_events + j].type = events[j].type;
         events_attr[i*nb_events + j].config = events[j].config;
         events_attr[i*nb_events + j].exclude_kernel = events[j].exclude_kernel;
         events_attr[i*nb_events + j].exclude_user = events[j].exclude_user;
         events_attr[i*nb_events + j].read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
         fd[i*nb_events + j] = sys_perf_counter_open(&events_attr[i*nb_events + j], obj_pid, events[j].cpuid, (events[j].leader==-1)?-1:fd[i*nb_events + events[j].leader], 0);
         // printf("j = %d , fd[j] = %d",j , fd[i*nb_events + j]);
         if (fd[i*nb_events + j] < 0) {
            fprintf(stdout, "#[%d] sys_perf_counter_open failed: %s\n", core, strerror(errno));
            return;
         }
      }
   }
	
   struct perf_read_ev single_count;
   struct perf_read_ev *last_counts = calloc(nb_nodes*nb_events, sizeof(*last_counts));
   struct perf_read_ev *last_counts_prev = calloc(nb_nodes*nb_events, sizeof(*last_counts_prev));

   double *rr_nodes, *maptu_nodes;
   double *aggregate_dram_accesses_to_node, *lar_node;
   double * ipc_node;

   rr_nodes = (double *) malloc(nb_nodes*sizeof(double));
   maptu_nodes =  (double *) malloc(nb_nodes*sizeof(double));
   aggregate_dram_accesses_to_node = (double *) malloc(nb_nodes*sizeof(double));
   lar_node = (double *) malloc(nb_nodes*sizeof(double));
   ipc_node = (double *) malloc(nb_nodes*sizeof(double));

   // change_carrefour_state('b'); // Make sure that the profiling is started

   while (1) {
       /* check if the pid still exists before collecting dump.
         * This is racy as a new process may acquire the same pid
         * but the chances of that happenning for us is really really low .
         */
      //   if (kill(obj_pid, 0) && errno == ESRCH)
      //       break;

      usleep(sleep_time);
      for(i = 0; i < nb_nodes; i++) {
         for (j = 0; j < nb_events; j++) {
            assert(read(fd[i*nb_events + j], &single_count, sizeof(single_count)) == sizeof(single_count));
/*            printf("[%d,%d] %ld enabled %ld running %ld%%\n", i, j,
                  single_count.time_enabled - last_counts[i*nb_events + j].time_enabled,
                  single_count.time_running - last_counts[i*nb_events + j].time_running,
                  (single_count.time_enabled-last_counts[i*nb_events + j].time_enabled)?100*(single_count.time_running-last_counts[i*nb_events + j].time_running)/(single_count.time_enabled-last_counts[i*nb_events + j].time_enabled):0); */
            last_counts[i*nb_events + j] = single_count;
         }
      }

      double rr_global = 0, maptu_global = 0;
      double lar = 0, load_imbalance = 0;
      double ipc_global = 0;

      memset(rr_nodes, 0, nb_nodes*sizeof(double));
      memset(maptu_nodes, 0, nb_nodes*sizeof(double));
      memset(aggregate_dram_accesses_to_node, 0, nb_nodes*sizeof(double));
      memset(lar_node, 0, nb_nodes*sizeof(double));
      memset(ipc_node, 0, nb_nodes*sizeof(double));

      fprintf(opt_file_out, "\n\n第 %d 次 采样 \n",sampling_times);

      TLB_Miss_last_10s_systemWide(last_counts, last_counts_prev, &rr_global, rr_nodes);
      TLB_Miss_all_systemWide(last_counts, last_counts_prev, &rr_global, rr_nodes);

      fprintf(opt_file_out, "\n满足开启页表复制的单次采样 %d 次； 占 总采样次数的百分比 ： %3f %%\n",check_pass,(double)check_pass/(double)sampling_times *100);
      // fprintf(opt_file_out, "\n满足开启页表复制的平均采样 %d 次； 占 总采样次数的百分比 ： %3f %%\n",average_check_pass,(double)average_check_pass/(double)sampling_times *100);
      fprintf(opt_file_out, "\n+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
      fflush(opt_file_out);

      if (shut_down)
      {
         printf("error: Topology is not the same as defult\n");
         fprintf(opt_file_out, "error: Topology is not the same as defult\n");
         break;
      }
      
      
      // 
// #if USE_MRR
//       mrr(last_counts, last_counts_prev, &rr_global, &maptu_global, rr_nodes, maptu_nodes);
//       dram_accesses(last_counts, last_counts_prev, &lar, &load_imbalance, aggregate_dram_accesses_to_node, lar_node, NULL, NULL);
// #else
//       dcmr(last_counts, last_counts_prev, &rr_global, rr_nodes);
//       dram_accesses(last_counts, last_counts_prev, &lar, &load_imbalance, aggregate_dram_accesses_to_node, lar_node, &maptu_global, maptu_nodes);
// #endif


// #if ENABLE_IPC
//       ipc(last_counts, last_counts_prev, &ipc_global, ipc_node);
// #endif

      struct sysinfo info;
      double global_mem_usage = 0;
      if (sysinfo(&info) != 0) {
         printf("sysinfo: error reading system statistics");
         global_mem_usage = 0;
      }
      else {
         global_mem_usage =  (double) (info.totalram-info.freeram) / (double) info.totalram * 100.;
      }

      if (rr_global > ACTIVE_PERCENTAGE)
      {
         // printf("\n we should start pgtrpl !!");
         // sys_set_ptsr_enable(obj_pid);
         // for(i = 0; i < nb_nodes; i++) {
         //   for (j = 0; j < nb_events; j++) {
         //      close(fd[i*nb_events + j]); 
         //   }  
         // }
         // free(rr_nodes);
         // free(maptu_nodes);
         // free(aggregate_dram_accesses_to_node);
         // free(lar_node);
         // free(ipc_node);
         // break;
      }
      
      

      // for(i = 0; i < nb_nodes; i++) {
      //    printf("[ Node %d ] %.1f %% read accesses - MAPTU = %.1f - # of accesses = %.1f - LAR = %.1f - IPC = %.2f\n",
      //             i, rr_nodes[i], maptu_nodes[i] * 1000., aggregate_dram_accesses_to_node[i], lar_node[i] * 100., ipc_node[i]);
      // }
      // printf("[ GLOBAL ] %.1f %% read accesses - MAPTU = %.1f - LAR = %.1f - Imbalance = %.1f %% - IPC = %.2f - Mem usage = %.1f %%\n",
      //             rr_global, maptu_global * 1000., lar * 100., load_imbalance * 100., ipc_global, global_mem_usage);

      // carrefour(rr_global, maptu_global * 1000., lar * 100., load_imbalance * 100., aggregate_dram_accesses_to_node, ipc_global, global_mem_usage);

      for(i = 0; i < nb_nodes; i++) {
         for (j = 0; j < nb_events; j++) {
            last_counts_prev[i*nb_events + j] = last_counts[i*nb_events + j];
         }
      }

      collect_pagetable(obj_pid, opt_file_out);

   }

   free(rr_nodes);
   free(maptu_nodes);
   free(aggregate_dram_accesses_to_node);
   free(lar_node);
   free(ipc_node);

   return;
}

static void thread_loop_only_PTL() {

   while (1) {

      usleep(sleep_time);

      //开启页表复制的方法， 目前测试在脚本中已经默认开启了。
   // sys_set_ptsr_enable();


// 宿主机测量PTL给ept使用时，调用这个。无需pid.
   //vWASP_check_node_latency();

   //虚拟机测量PTL给gpt使用时，调用这个。 需要传入PID
    //check_node_latency();
   
   //虚拟机测量PTL给gpt使用时，如果想直接用宿主机测好的PTL，调用这个。需要传入PID。    
   kvm_sys_get_latency_map_from_hypercall();

      fprintf(opt_file_out, "\n\n第 %d 次 采样 \n",sampling_times);
      sampling_times += 1;

      fflush(opt_file_out);


      if (shut_down)
      {
         printf("error: Topology is not the same as defult\n");
         fprintf(opt_file_out, "error: Topology is not the same as defult\n");
         break;
      }

   }

   return;
}

static long sys_set_ptsr_enable(void) {
   int ret;
   if (enable_PTSR)//软件开启了控制页表复制的功能
   {
      if (!repl_pgd_enabled) //如果是第一次，还没有开启页表复制功能，则开启
      {
             fprintf(opt_file_out, "\n first, set ptsr start !!!");
      //    printf("\n first, set ptsr start !!!\n");
         int ret = syscall(441, obj_pid, 1, NULL, 0, NULL);
         #  if defined(__x86_64__) || defined(__i386__)
            if (ret < 0 && ret > -4096) {
               errno = -ret;
               ret = -1;
            }
         #  endif
         repl_pgd_enabled = true;
      }else if (disable_PTSR) //如果已经开启页表复制功能，后面又关闭了，可以重新打开
      {
             fprintf(opt_file_out, "\n set ptsr re enable !!!");
      //    printf("\n  set ptsr enable !!!\n");
         ret = syscall(441, obj_pid, 2, NULL, 0, NULL);
         #  if defined(__x86_64__) || defined(__i386__)
            if (ret < 0 && ret > -4096) {
               errno = -ret;
               ret = -1;
            }
         #  endif
         disable_PTSR = false;
      }else
      {
            fprintf(opt_file_out, "\n ptsr already start !!!");
      //    printf("\n ptsr already start !!!\n");
      }
   }
   return ret;
}

static long sys_set_ptsr_disable(void) {
   int ret;
   if (enable_PTSR)//软件开启了控制页表复制的功能
   {
      if (repl_pgd_enabled && !disable_PTSR)
      {
         printf("\n we got sys_set_ptsr_disable !!!\n");
         ret = syscall(441, obj_pid, 0, NULL, 0, NULL);
         #  if defined(__x86_64__) || defined(__i386__)
            if (ret < 0 && ret > -4096) {
               errno = -ret;
               ret = -1;
            }
         #  endif
         disable_PTSR = true;
      }else
      {
         printf("\n no good for ptsr !!!\n");
      }
   }
   return ret;
}


static long sys_set_ptsr_on_fixNode(struct bitmask *bmp) {
   int ret;
   if (enable_PTSR)//软件开启了控制页表复制的功能
   {
      if (repl_pgd_enabled && !disable_PTSR)
      {
         printf("\n  sys_set_ptsr_on_fixNode !!!\n");
         ret = syscall(441, obj_pid, 3, bmp->maskp, bmp->size + 1, NULL);
         #  if defined(__x86_64__) || defined(__i386__)
            if (ret < 0 && ret > -4096) {
               errno = -ret;
               ret = -1;
            }
         #  endif
      }else
      {
         printf("\n no good for ptsr !!!\n");
      }
   }
   return ret;
}

static long kvm_sys_set_latency_map(int * nodeArray) {
   int ret;
   testArrPass(nodeArray);
   // if (enable_PTSR)//软件开启了控制页表复制的功能
   {
      // if (repl_pgd_enabled && !disable_PTSR)
      {
         printf("\n  kvm_sys_set_latency_map !!!\n");
         ret = syscall(441, NULL, 9, NULL, 0, nodeArray);
         #  if defined(__x86_64__) || defined(__i386__)
            if (ret < 0 && ret > -4096) {
               errno = -ret;
               ret = -1;
            }
         #  endif
      }
      // else
      // {
      //    printf("\n no good for ptsr !!!\n");
      // }
   }
   return ret;
}

//虚拟机中如果想直接用宿主机测好的PTL 可以调用这个接口
static long kvm_sys_get_latency_map_from_hypercall(void) {
   int ret;
//    testArrPass(nodeArray);
   // if (enable_PTSR)//软件开启了控制页表复制的功能
   {
      // if (repl_pgd_enabled && !disable_PTSR)
      {
         printf("\n  kvm_sys_get_latency_map_from_hypercall !!!\n");
         ret = syscall(441, NULL, 10, NULL, 0, NULL);
         #  if defined(__x86_64__) || defined(__i386__)
            if (ret < 0 && ret > -4096) {
               errno = -ret;
               ret = -1;
            }
         #  endif
      }
      // else
      // {
      //    printf("\n no good for ptsr !!!\n");
      // }
   }
   return ret;
}


static long sys_set_latency_map(int * nodeArray) {
   int ret;
   testArrPass(nodeArray);
   // if (enable_PTSR)//软件开启了控制页表复制的功能
   {
      // if (repl_pgd_enabled && !disable_PTSR)
      {
            fprintf(opt_file_out, "\n sys_set_latency_map!");
      //    printf("\n  sys_set_latency_map !!!\n");
         ret = syscall(441, obj_pid, 4, NULL, 0, nodeArray);
         #  if defined(__x86_64__) || defined(__i386__)
            if (ret < 0 && ret > -4096) {
               errno = -ret;
               ret = -1;
            }
         #  endif
      }
      // else
      // {
      //    printf("\n no good for ptsr !!!\n");
      // }
   }
   return ret;
}

static long sys_perf_counter_open(struct perf_event_attr *hw_event, pid_t pid, int cpu, int group_fd, unsigned long flags) {
   int ret = syscall(__NR_perf_counter_open, hw_event, pid, cpu, group_fd, flags);
#  if defined(__x86_64__) || defined(__i386__)
   if (ret < 0 && ret > -4096) {
      errno = -ret;
      ret = -1;
   }
#  endif
   return ret;
}

static void sig_handler(int signal) {
   printf("#signal caught: %d\n", signal);
   // change_carrefour_state('k');
   fflush(NULL);
   exit(0);
}


#include <sched.h>
#include <linux/unistd.h>
#include <sys/mman.h>
// static pid_t gettid(void) {
//       return syscall(__NR_gettid);
// }

// void set_affinity(int cpu_id) {
//    int tid = gettid();
//    cpu_set_t mask;
//    CPU_ZERO(&mask);
//    CPU_SET(cpu_id, &mask);
//    printf("Setting tid %d on core %d\n", tid, cpu_id);
//    int r = sched_setaffinity(tid, sizeof(mask), &mask);
//    if (r < 0) {
//       fprintf(stderr, "couldn't set affinity for %d\n", cpu_id);
//       exit(1);
//    }
// }

void *example_thread(void *arg) {

printf("This is an example thread\n");
//测试节点延迟
         
        char shbuffer[80];
        memset(shbuffer, 0, sizeof(shbuffer));
        fp=popen("/home/jianguoliu/vWASP/arm/wasp/mytest.sh","r");
      //   fgets(shbuffer,sizeof(shbuffer),fp);
      //   printf("result = ");
      //   for (i = 0; i < 100; i++)
      //   {
      //       printf("%s",shbuffer[i]);
      //   }

      //   while(NULL != fgets(shbuffer,sizeof(shbuffer),fp)) 
      // {  
      //       printf("%s",shbuffer);    
      // } 

return NULL;

}


int main(int argc, char**argv) {
   signal(SIGPIPE, sig_handler);
   signal(SIGTERM, sig_handler);
   signal(SIGINT, sig_handler);

   for (int i = 0; i < argc; i++) {
        printf("%s ", argv[i]);
    }
   //  if (argc < 2) {
   //      printf("Usage: icollector <pid> [outfile] <all_open> <sample_interval(s)> <enable_PTSR>\n");
   //      return -1;
   //  }

   obj_pid = 0;
   if (argc >= 2) {
       obj_pid = atoi(argv[1]);  
    }
    
   printf("\n pid  = %d \n",obj_pid);

   cpu_num = numa_num_configured_cpus();
   cpu_num2 = 2*cpu_num;
   cpu_num3 = 3*cpu_num;
   cpu_num4 = 4*cpu_num;
   nb_nodes = numa_num_configured_nodes();
   numa_nodes = nb_nodes;
   printf("\tnuma nodes = %d \n", nb_nodes);
   cpu_num_per_node = cpu_num/nb_nodes;
   printf("\n cpus = %d \n",cpu_num);
   nb_nodes = 1;
   
   
   
   // FILE *opt_file_out = NULL;
    if (argc >= 3) {
       if (atoi(argv[2])!=-1)
       {
          opt_file_out = fopen(argv[2], "w");//a 追加， w 覆盖
       }   
    }

    if (opt_file_out == NULL) {
        opt_file_out = stdout;
    }

    fprintf(opt_file_out, "<version> 0.0.3 <version>\n"); // 全拓扑uncore事件版本
    fprintf(opt_file_out, "pid = %d\n",obj_pid);

   all_open = 0;
    if (argc >= 4) {
        all_open = atoi(argv[3]);
    }

    if (argc >= 5) {
        sample_interval = atoi(argv[4]);
        sleep_time = sample_interval*TIME_SECOND;
    }

   enable_PTSR = 0;
    if (argc >= 6) {
        enable_PTSR = atoi(argv[5]);
    }
   

   int i;
   uint64_t clk_speed = get_cpu_freq();

   printf("#Clock speed: %llu\n", (long long unsigned)clk_speed);
   for(i = 0; i< nb_events; i++) {
      printf("#Event %d: %s (%llx) (Exclude Kernel: %s; Exclude User: %s)\n", i, events[i].name, (long long unsigned)events[i].config, (events[i].exclude_kernel)?"yes":"no", (events[i].exclude_user)?"yes":"no");
   }

   printf("Parameters :\n");
//    printf("\tMIN_ACTIVE_PERCENTAGE = %d\n", MIN_ACTIVE_PERCENTAGE);
//    printf("\tMAPTU_MIN = %d accesses / usec\n", MAPTU_MIN);
//    printf("\tMEMORY_USAGE_MAX = %d %%\n", MEMORY_USAGE_MAX);
// #if USE_MRR
//    printf("\tMRR_MIN = %d %%\n", MRR_MIN);
// #else
//    printf("\tDCRM_MAX = %d %%\n", DCRM_MAX);
// #endif
//    printf("\tMIN_IMBALANCE = %d %%\n", MIN_IMBALANCE);
//    printf("\tMAX_LOCALITY = %d %%\n", MAX_LOCALITY);
//    printf("\tMAX_LOCALITY_MIGRATION = %d %%\n", MAX_LOCALITY_MIGRATION);
// #if ENABLE_IPC
//    printf("\tIPC_MAX = %f\n", IPC_MAX);
// #endif

   
   
   printf("test code v6  2023.11.14!!"); 

 //开启新线程测PTL
   pthread_t thread_id;
   pthread_create(&thread_id, NULL, example_thread, NULL);
   pthread_join(thread_id, NULL);

   // 测perf 性能事件
   thread_loop_only_PTL();

   pclose(fp);

   // FILE * fp1 = fopen("/home/huawei/wasp/bin/nodes.csv", "r");//打开输入文件
   // if (fp1==NULL) {//若打开文件失败则退出
   //    fprintf(opt_file_out, "\n can not open latency file at ~/wasp/bin/nodes.csv !\n"); 
   //    // free(line);    
   //    return;
   //  }

   //  return 0;

   // enable_PTSR = 1;
   // sys_set_ptsr_enable();



//测试关闭ptsr 和 指定节点复制页表
   // enable_PTSR = true;
//    sys_set_ptsr_enable();

// struct bitmask *bmp;
//   bmp = numa_bitmask_alloc(numa_nodes);
//    printf("bmp->size = %d !!",bmp->size);
// printf("original !!");
// numa_bitmask_setall(bmp);
//    for (size_t j = 0; j < numa_nodes; j++)
//    {
//       printf("node i = %d ,and bitmap set = %d !!",j,numa_bitmask_isbitset(bmp, j));
//    }
   
// printf("after fix !!");
//    numa_bitmask_clearbit(bmp, 1);

//     for (size_t h = 0; h < numa_nodes; h++)
//    {
//       printf("node i = %d ,and bitmap set = %d !!",h,numa_bitmask_isbitset(bmp, h));
//    }

//   sys_set_ptsr_on_fixNode(bmp);
   // sys_set_ptsr_enable();
   // sys_set_ptsr_disable();
   // sys_set_ptsr_enable();
   // sys_set_ptsr_enable();
   // sys_set_ptsr_disable();
   // sys_set_ptsr_enable();

//    if (obj_pid == -1)
//    {
//       thread_loop_systemWide();
//    }else
//    {
      //  thread_loop();
//    }

    

   return 0;
}

