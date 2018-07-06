#include <windows.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sendParentMessage.h"
extern FILE *logFile;

void
sendParentMessage(char *message_key, char *message)
{
	int ret;
	FILE *fp;
	//fprintf(logFile,"%s\n", "we are here");

	fp = fopen(message_key,"w");
	if(fp) {
		//char *message = "roger harryafa";
		//ret = fwrite(message, 1, strlen(message), fp);
		fprintf(fp,"%s\n", message);
		fprintf(logFile,"%s\n", message);
		//if(ret != strlen(message)) {
		//	fprintf(logFile,"fwrite()=%d: errno=%d\n%s\n", ret, errno, strerror(errno));
		//	exit(1);
		//}
		fflush(fp);
		fclose(fp);
	}
	else {
		fprintf(logFile,"%s Line %d: can't open %s. errno = %d\n%s\n", __FILE__,__LINE__,message_key, errno, strerror(errno));
		exit(1);
	}
	//ret = _unlink(message_key);
	//if(ret != 0) {
	//	fprintf(logFile,"%s Line %d: _unlink() failed. errno = %d\n%s\n", __FILE__,__LINE__,errno, strerror(errno));
	//	exit(1);
	//}
}
