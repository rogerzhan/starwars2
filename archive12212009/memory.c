#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "memory.h"

extern FILE *logFile;

void
logMemoryStatus(void)
{
            int chunk = 1024 * 1024 * 80;
            double dchunk;
            int x;
            char *p;
            MEMORYSTATUS stat;
            
            GlobalMemoryStatus (&stat);
            
            fprintf(logFile,"\n\n%ld percent of memory is in use.\n", stat.dwMemoryLoad);
            fprintf(logFile,"There are %8.1f total megabytes of physical memory.\n", ((double) stat.dwTotalPhys)/DIVBY);
            fprintf(logFile,"There are %8.1f free megabytes of physical memory.\n", ((double) stat.dwAvailPhys)/DIVBY);
            fprintf(logFile,"There are %8.1f total megabytes of paging file.\n", ((double) stat.dwTotalPageFile)/DIVBY);
            fprintf(logFile,"There are %8.1f free megabytes of paging file.\n", ((double) stat.dwAvailPageFile)/DIVBY);
            fprintf(logFile,"There are %8.1f total megabytes of virtual memory.\n", ((double) stat.dwTotalVirtual)/DIVBY);
            fprintf(logFile,"There are %8.1f free megabytes of virtual memory.\n", ((double) stat.dwAvailVirtual)/DIVBY);

            for(x = 0; x <= 3; x++) {
                        dchunk = (double) chunk;
                        p = calloc(1, chunk);
                        if(p)
                                    fprintf(logFile,"allocated %8.2f megabytes of memory.\n", dchunk / (1024.0 * 1024.0));
                        else {
                                    fprintf(logFile,"couldn't allocate %8.2f megabytes of memory.\n", dchunk / (1024.0 * 1024.0));
                                    return;
                        }
                        free(p);
                        fprintf(logFile,"freed %8.2f megabytes of memory.\n", dchunk / (1024.0 * 1024.0));
                        chunk += chunk;
            }
}
