#include "datetime.h"

typedef struct cjoinresult {
	int leg1OrgAptID;
	int leg1DestAptID;
	char leg1Flight_no[8];
	char leg1Flight_carrier[4];
	DateTime leg1DptTime;
	DateTime leg1ArrTime;
	int leg2OrgAptID;
	int leg2DestAptID;
	char leg2Flight_no[8];
	char leg2Flight_carrier[4];
	DateTime leg2DptTime;
	DateTime leg2ArrTime;
} CjoinResult;

int cjoin(MY_CONNECTION *myconn, BINTREENODE **result, char *oag1sql, char *oag2sql, int MIN_Layover, int MAX_Layover);

