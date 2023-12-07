/*
    Copyright (C) 2018-2019 VMware, Inc.
    SPDX-License-Identifier: GPL-2.0
    Linux kernel module to dump process page-tables.
    The kernel-module is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; version 2.
    The kernel-module  is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.
    You should find a copy of v2 of the GNU General Public License somewhere
    on your Linux system; if not, write to the Free Software Foundation,
    Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

// #include <stdio.h>
// #include <fcntl.h>      /* open */
// #include <unistd.h>     /* exit */
// #include <sys/ioctl.h>  /* ioctl */
// #include <sys/mman.h>  /* mlock */
// #include <stdlib.h>
// #include <limits.h>
// #include <numa.h>
// #include <signal.h>
// #include <errno.h>
// #include <sys/time.h>

#include "dodump.h"

//pt = page table   dt = data
struct numa_node_count_info
{
    int pt_pointer_away; 
    int pt_pointer_all;
    int pagetable_all;
    int dt_pointer_away;
    int dt_pointer_all; 
};

struct time_varying_map
{
    struct numa_node_count_info node[64];
};

#define BUF_SIZE_BITS 24
#define PARAM(ptr, sz) ((unsigned long) sz << 48 | (unsigned long)ptr)
#define PARAM_GET_PTR(p) (void *)(p & 0xffffffffffff)
#define PARAM_GET_BITS(p) (p >> 48)

#define PTABLE_BASE_MASK(x) ((x) & 0xfffffffff000UL)

struct time_varying_map tvmapArray[14400]; //5 days  if interval is 30s.

void dump_numa_info(struct nodemap *map, FILE *opt_file_out)
{
    int i;

    fprintf(opt_file_out, "<numamap>\n");
    for (i = 0; i < map->nr_nodes; i++) {
        // fprintf(opt_file_out, "%d %ld %ld\n", map->node[i].id,
        //     map->node[i].node_start_pfn, map->node[i].node_end_pfn);
        fprintf(opt_file_out, "%d %lx %lx\n", map->node[i].id,
            map->node[i].node_start_pfn >>12, map->node[i].node_end_pfn >>12);
    }
    fprintf(opt_file_out, "</numamap>\n");
}

int pt_numa_Count(struct nodemap *map, unsigned long base, FILE *opt_file_out)
{
    int i,j=0;
    for (i = 0; i < map->nr_nodes; i++) {
        if ((base>=(map->node[i].node_start_pfn >>12))&&(base<(map->node[i].node_end_pfn >>12)))
        {
            j=i;
            break;
        }
    }
    if (j!=i)
    {
        fprintf(opt_file_out, "</error  addr=%lx not match numa node>\n",base);
    }
    return j;
}

/*
 * Three types of page-tables can be dumped:
 * 0: Regular host page tables
 * 1: KVM VM's extended page tables
 * 2: KVM VM's shadow page tables
*/
// int main(int argc, char *argv[])
int collect_pagetable(long pid, FILE *opt_file_out)
{
    long c; 
    int nodenum = 0;
    int distribution_nodenum = 0;
    struct timeval tv;

    // if (argc < 4) {
    //     printf("Usage: dodump <pid> <0(do not repeat)|n(repeat every n s)> [outfile]\n");
    //     return -1;
    // }
    
    // long pid = strtol(argv[1], NULL, 10);

    if (pid == 0) {
        pid = getpid();
    }

    long pgtables_type = 0x0;
    // long pgtables_type = strtol(argv[2], NULL, 10);
    if (!(pgtables_type == PTDUMP_REGULAR || pgtables_type == PTDUMP_ePT)) {
        printf("Please enter a valid ptables identifier (argument #2). Valid values:\n");
        printf("0\tHOST_PTABLES\n1\tEPT_PTABLES\n");
        exit(0);
    }

    // int repeat_time = strtol(argv[2], NULL, 10);
    // repeat_time *= 1000000;

    // FILE *opt_file_out = NULL;
    // if (argc == 4) {
    //     opt_file_out = fopen(argv[3], "w");//a 追加， w 覆盖
    // }

    if (opt_file_out == NULL) {
        opt_file_out = stdout;
    }

    int f = open("/proc/ptdump", 0);
    if (f < 0) {
        printf ("Can't open device file: %s\n", "/proc/ptdump");
        return -1;
    }

    //printf("step 1\n");

    c = ioctl(f, PTDUMP_IOCTL_PGTABLES_TYPE, pgtables_type);
    if (c < 0) {
        printf("Error while setting pgtables_type\n");
        return -1;

    }

    struct nodemap *numa_map = calloc(1, sizeof(*numa_map));
    if (!numa_map)
        return -ENOMEM;

    c = ioctl(f, PTDUMP_IOCTL_MKCMD(PTDUMP_IOCTL_NUMA_NODEMAP, 0, 512),
            PTDUMP_IOCTL_MKARGBUF(numa_map, 0));
    if (c < 0) {
        printf("Error while fetching numa node map\n");
        free(numa_map);
        return -1;
    }
    // fprintf(opt_file_out, "<version>0.9.7");
    // fprintf(opt_file_out, "<version>\n");
    dump_numa_info(numa_map, opt_file_out);

    // fprintf(opt_file_out, "pid = %d \n",pid);
    // printf("we got here  !!!!!!!!!!!!!!!!!!!!!!!!!\n");
    // return 0;


    //printf("step 2\n");
    // free(numa_map);
    int pte_distribution[numa_map->nr_nodes][4][numa_map->nr_nodes];
    int ptmap[numa_map->nr_nodes][4]; 
    
    int repeat_count = 0;

    struct ptdump *result = calloc(1, sizeof(*result));
    if (!result) {
        return -1;
    }
    //printf("step 3\n");
    mlockall(MCL_CURRENT | MCL_FUTURE | MCL_ONFAULT);
    //printf("step 4\n");
    // while(1) {
        /* check if the pid still exists before collecting dump.
         * This is racy as a new process may acquire the same pid
         * but the chances of that happenning for us is really really low .
         */
        // if (kill(pid, 0) && errno == ESRCH)
        //     break;
        //不要让结果累加   
        memset(pte_distribution,0,sizeof(pte_distribution));
        memset(ptmap,0,sizeof(ptmap));
        //记录时变信息

        //时间戳
        gettimeofday(&tv, NULL);
        fprintf(opt_file_out,"<---------------- timestap second=\"%ld\" ---------------->\n", tv.tv_sec);

        result->processid = pid;

        c = ioctl(f, PTDUMP_IOCTL_MKCMD(PTDUMP_IOCTL_CMD_DUMP, 0, 512),
                     PTDUMP_IOCTL_MKARGBUF(result, 0));
        if (c < 0) {
            fprintf(opt_file_out,"<ptdump process=\"%ld\" error=\"%ld\"></ptdump>\n", pid, c);
            free(numa_map);
            free(result);
            close(f);
            munlockall();
            return 0;
        }
        fprintf(opt_file_out,"<ptdump process=\"%ld\" count=\"%zu\">\n", pid, result->num_tables);
        fprintf(opt_file_out,"<numamigrations>%zu</numamigrations>\n", result->num_migrations);
        for (int level = 4; level > 0; level--) {
            for (unsigned long i = 0; i < result->num_tables; i++) {
                if (PTDUMP_TABLE_EXLEVEL(result->table[i].base) != level) {
                    continue;
                }
                // fprintf(opt_file_out, "<level%d b=\"%lx\">", level, PTABLE_BASE_MASK(result->table[i].base) >> 12);
                nodenum = pt_numa_Count(numa_map,PTABLE_BASE_MASK(result->table[i].base) >> 12,opt_file_out);
                ptmap[nodenum][level-1]+=1; 
                for (int j = 0; j < 512; j++) //512
                // for (int j = 0; j < 0; j++) //512
                {
                    if (!(result->table[i].entries[j] & 0x1)) {
                                continue;
                    }
                    distribution_nodenum = pt_numa_Count(numa_map,PTABLE_BASE_MASK(result->table[i].entries[j]) >> 12,opt_file_out);
                    pte_distribution[nodenum][level-1][distribution_nodenum]+=1;
                    // char *prefix = "";

                    /* check if the entry is valid */
                    // if (!(result->table[i].entries[j] & 0x1)) {
                    //     /* entry is not valid, check if the global bit is set */
                    //     // if (!(result->table[i].entries[j] & (0x1 << 8))) {
                    //     //     /* global bit is not set, continue */
                    //     //     fprintf(opt_file_out, "0 ");
                    //     //     continue;
                    //     // }
                    //     /* set the prefix to a NUMA entry */
                    //     prefix = "n";
                    // }

                    /* case distinction on the level */
                    // switch(level) {
                    //     case 1:
                    //         if (!(result->table[i].entries[j] & 0x1)) {
                    //             fprintf(opt_file_out, "0 ");
                    //             continue;
                    //         }
                    //         // if (!(result->table[i].entries[j] & (0x1UL << 63))) {
                    //         //     fprintf(opt_file_out, "%sx%lx ", prefix, PTABLE_BASE_MASK(result->table[i].entries[j]) >> 12);
                    //         // } else {
                    //             fprintf(opt_file_out, "page:%s%lx ", prefix, PTABLE_BASE_MASK(result->table[i].entries[j]) >> 12);
                    //         // }
                    //         break;
                    //     case 2:
                    //        if (!(result->table[i].entries[j] & 0x1)) {
                    //             fprintf(opt_file_out, "0 ");
                    //             continue;
                    //         }
                    //         // if (!(result->table[i].entries[j] & (0x1 << 7))) {
                    //         //     fprintf(opt_file_out, "%sp%lx ", prefix, PTABLE_BASE_MASK(result->table[i].entries[j]) >> 12);
                    //         // } else if (!(result->table[i].entries[j] & (0x1UL << 63))) {
                    //         //     fprintf(opt_file_out, "%sx%lx ", prefix, PTABLE_BASE_MASK(result->table[i].entries[j]) >> 21);
                    //         // } else {
                    //             fprintf(opt_file_out, "pte:%s%lx ", prefix, PTABLE_BASE_MASK(result->table[i].entries[j]) >> 21);
                    //         // }

                    //         break;
                    //     case 3:
                    //         if (!(result->table[i].entries[j] & 0x1)) {
                    //             fprintf(opt_file_out, "0 ");
                    //             continue;
                    //         }
                    //         /* we're not using 1G pages, just print the  */
                    //         fprintf(opt_file_out, "pmd:%lx ", PTABLE_BASE_MASK(result->table[i].entries[j]) >> 12);
                    //         break;
                    //     case 4:
                    //         if (!(result->table[i].entries[j] & 0x1)) {
                    //             fprintf(opt_file_out, "0 ");
                    //             continue;
                    //         }
                    //         //if (i < 256) 
                    //         {
                    //             /* just print out the entry, if it belongs to the user space */
                    //             fprintf(opt_file_out, "pud:%lx ", PTABLE_BASE_MASK(result->table[i].entries[j]) >> 12);
                    //         }

                    //         break;
                    //     default:
                    //         continue;
                    // }
                }
                // fprintf(opt_file_out, "</level%d>\n", level);
            }
        }
        int all_pt_per_level = 0;
        int away_pt_per_level = 0;
        int all_pt_per_node = 0;
        int away_pt_per_node = 0;
        int all_dt_per_node = 0;
        int away_dt_per_node = 0;
        fprintf(opt_file_out, "<ptmap>\n");
        fprintf(opt_file_out,"example: node0 level4 1 [ 0 3 0 0 ] 100%%\n level4后面的1 是页表数, []内的四个数字的位置表示四个numa节点，数值表示当前页表中指向的下一级页表在4个节点上的数量，最后的百分比表示远端页表所占比例\n\n\n");
        for (nodenum = 0; nodenum < numa_map->nr_nodes; nodenum++)
        {
            all_pt_per_node = 0;
            away_pt_per_node = 0;
            all_dt_per_node = 0;
            away_dt_per_node = 0;
            fprintf(opt_file_out, "<node %d\n> :",nodenum);
            for (int ptlevel = 4; ptlevel > 0; ptlevel--)
            {
                all_pt_per_level = 0;
                away_pt_per_level = 0;
                fprintf(opt_file_out, "level%d  %d [",ptlevel,ptmap[nodenum][ptlevel-1]);
                tvmapArray[repeat_count].node[nodenum].pagetable_all+=ptmap[nodenum][ptlevel-1];
                for (int distr_node = 0; distr_node < numa_map->nr_nodes; distr_node++)
                {
                    fprintf(opt_file_out, " %d",pte_distribution[nodenum][ptlevel-1][distr_node]);
                    all_pt_per_level+=pte_distribution[nodenum][ptlevel-1][distr_node];
                    if (distr_node!=nodenum)
                    {
                        away_pt_per_level+=pte_distribution[nodenum][ptlevel-1][distr_node];
                    } 
                }
                //pte 指针指向的是数据，其他级的页表指向的是下一级页表
                if (ptlevel==1)
                {
                    fprintf(opt_file_out, "] (next_level_data_all_this_level = %d ,next_level_data_away_this_level = %d,  %.1f%%) \n",all_pt_per_level,away_pt_per_level,all_pt_per_level==0?0:(away_pt_per_level)*1.0/all_pt_per_level*100);
                    all_dt_per_node=all_pt_per_level;
                    away_dt_per_node=away_pt_per_level;
                    tvmapArray[repeat_count].node[nodenum].dt_pointer_away=away_dt_per_node;
                    tvmapArray[repeat_count].node[nodenum].dt_pointer_all=all_dt_per_node;
                }else
                {
                    fprintf(opt_file_out, "] (next_level_pt_all_this_level = %d ,next_level_pt_away_this_level = %d,  %.1f%%) \n",all_pt_per_level,away_pt_per_level,all_pt_per_level==0?0:(away_pt_per_level)*1.0/all_pt_per_level*100);
                    all_pt_per_node+=all_pt_per_level;
                    away_pt_per_node+=away_pt_per_level;
                    tvmapArray[repeat_count].node[nodenum].pt_pointer_away=away_pt_per_node;
                    tvmapArray[repeat_count].node[nodenum].pt_pointer_all=all_pt_per_node;
                }   
            }
            fprintf(opt_file_out, " {next_level_pt_all_this_node = %d ,next_level_pt_away_this_node = %d,  %.1f%% } \n",all_pt_per_node,away_pt_per_node,all_pt_per_node==0?0:(away_pt_per_node)*1.0/all_pt_per_node*100);
            fprintf(opt_file_out, " {next_level_data_all_this_node = %d ,next_level_data_away_this_node = %d,  %.1f%% } \n",all_dt_per_node,away_dt_per_node,all_dt_per_node==0?0:(away_dt_per_node)*1.0/all_dt_per_node*100);
            fprintf(opt_file_out, "</node %d>\n\n",nodenum);
            fprintf(opt_file_out, "\nTV count : 1 %d  2 %d 3 %d 4 %d 5 %d\n",tvmapArray[repeat_count].node[nodenum].pagetable_all,tvmapArray[repeat_count].node[nodenum].pt_pointer_away,tvmapArray[repeat_count].node[nodenum].pt_pointer_all,tvmapArray[repeat_count].node[nodenum].dt_pointer_away,tvmapArray[repeat_count].node[nodenum].dt_pointer_all);
        }
        fprintf(opt_file_out, "</ptmap>\n");
        fprintf(opt_file_out,"</ptdump>\n");
        fflush(opt_file_out);
        repeat_count++;
        // wait_and_dump_next:
        // if (repeat_time)
        // {
        //     usleep(repeat_time);
        // }else{
        //     break;
        // } 
	//exit(0);
	// break;
    // }
    fprintf(opt_file_out,"</all over>\n");
//printf("step 5\n");
//生成时变数据csv
/*
    char buf[180]; 
    char *p;  
    p = getcwd(buf,sizeof(buf)); 
    if(p == NULL)
    {
        printf("ERROR get cwd\n");
        return 1;
    }  
    printf("current working directory: %s\n", buf); 
    strcat(buf, "/TV.csv");
    printf("now working directory: %s\n", buf); 
    FILE *tvcsv = fopen(buf, "w");//a 追加， w 覆盖
    if(!tvcsv)
    {
        printf("ERROR creating %s\n",buf);
        return 1;
    }

    fprintf(tvcsv, "times\t");
    for (nodenum = 0; nodenum < numa_map->nr_nodes; nodenum++)
    {
        fprintf(tvcsv, "away_pt_pointer_node%d\t",nodenum);
    }
    for (nodenum = 0; nodenum < numa_map->nr_nodes; nodenum++)
    {
        fprintf(tvcsv, "all_pt_pointer_node%d\t",nodenum);

    }
    for (nodenum = 0; nodenum < numa_map->nr_nodes; nodenum++)
    {
        fprintf(tvcsv, "all_pgtable_node%d\t",nodenum);

    }
    for (nodenum = 0; nodenum < numa_map->nr_nodes; nodenum++)
    {
        fprintf(tvcsv, "away_dt_pointer_node%d\t",nodenum);

    }
    for (nodenum = 0; nodenum < numa_map->nr_nodes; nodenum++)
    {
        fprintf(tvcsv, "all_dt_pointer_node%d\t",nodenum);

    }
    fprintf(tvcsv, "\n");
    for (int i = 0; i < repeat_count; i++)
    {
        fprintf(tvcsv, "%d\t",i);
        for (nodenum = 0; nodenum < numa_map->nr_nodes; nodenum++)
        {
            fprintf(tvcsv, "%d\t",tvmapArray[i].node[nodenum].pt_pointer_away);
        }
        for (nodenum = 0; nodenum < numa_map->nr_nodes; nodenum++)
        {
            fprintf(tvcsv, "%d\t",tvmapArray[i].node[nodenum].pt_pointer_all);

        }
        for (nodenum = 0; nodenum < numa_map->nr_nodes; nodenum++)
        {
            fprintf(tvcsv, "%d\t",tvmapArray[i].node[nodenum].pagetable_all);

        }
        for (nodenum = 0; nodenum < numa_map->nr_nodes; nodenum++)
        {
            fprintf(tvcsv, "%d\t",tvmapArray[i].node[nodenum].dt_pointer_away);
        }
        for (nodenum = 0; nodenum < numa_map->nr_nodes; nodenum++)
        {
            fprintf(tvcsv, "%d\t",tvmapArray[i].node[nodenum].dt_pointer_all);
        }
        fprintf(tvcsv, "\n");
    }
    fflush(tvcsv);
    */
//printf("step 6\n");
    free(numa_map);
    free(result);
    close(f);
    munlockall();
    // #define CONFIG_SHM_FILE_NAME "/tmp/ptdump-bench"
    // FILE *fd = fopen(CONFIG_SHM_FILE_NAME ".done", "w");
    // if (!fd) {
	// fprintf (stderr, "ERROR: ptdump could not create the shared file descriptor\n");
    // }
    return 0;
}
