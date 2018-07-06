#include "os_config.h"
#include "datetime.h"
#include "bintree.h"
#include "split.h"
#ifdef MS_C_COMPILER
#include "config-win.h"
#endif
#include "mysql.h"
#ifndef _my_mysql_h
#include "my_mysql.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cjoin.h"

typedef struct leg1 {
	int dep_aptid;
	int arr_aptid;
	char FD_airline_desig[4];
	char FD_flight_number[8];
	DateTime UTC_departure_time;
	DateTime UTC_arrival_time;
} LEG1;

typedef enum {
    LG_dep_aptid = 0,
    LG_arr_aptid,
    LG_FD_flight_number,
    LG_FD_airline_desig,
    LG_UTC_departure_time,
    LG_UTC_arrival_time,
    LG_nbr_items
};


static BINTREENODE *leg1Root = NULL;

static int leg1Compare(void *a, void *b);
static int resultCompare(void *a, void *b);
static void showLeg1(LEG1 *leg1Ptr);

int
cjoin(MY_CONNECTION *myconn, BINTREENODE **result, char *oag1sql, char *oag2sql, int MIN_Layover, int MAX_Layover)
{
        int errNbr;

	MYSQL_RES *res;
	MYSQL_FIELD *cols;
	MYSQL_ROW row;
	my_ulonglong rows;
//	char tbuf1[32], tbuf2[32], tbuf3[32];

//	int i;
	LEG1 *leg1Ptr, leg1Buf;
	BINTREENODE *tmp;
	LookupRet lkRet;
	time_t leg1_arrival_t, leg2_depart_t;
	DateTime leg2_depart_dt;
	int rowMatches;

	char sqlStmt[2048];

	CjoinResult *resultPtr;
	*result = NULL;
	rowMatches = 0;

	

	if(!myDoQuery(myconn, oag1sql, &res, &cols)) {
		fprintf(stderr,"db errno: %d: %s\n", myconn->my_errno, myconn->my_error_msg);
		exit(1);
	}
	if(res) {
		for(rows = 0; rows < myconn->rowCount; rows++) {
			row = mysql_fetch_row(res);
			if(! row)
				break;
			// install row in binary tree
			if(!(leg1Ptr = (LEG1 *) calloc(1,sizeof(LEG1)))) {
				fprintf(stderr,"%s Line %d: Out of memory\n", __FILE__,__LINE__);
				exit(1);
			}
			leg1Ptr->dep_aptid = atoi(row[LG_dep_aptid]);
			leg1Ptr->arr_aptid = atoi(row[LG_arr_aptid]);
			strcpy(leg1Ptr->FD_airline_desig, row[LG_FD_airline_desig]);
			strcpy(leg1Ptr->FD_flight_number, row[LG_FD_flight_number]);
			if((leg1Ptr->UTC_departure_time =
			    dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[LG_UTC_departure_time], NULL, &errNbr)) == BadDateTime) {
				fprintf(stderr,"%s Line %d: Bad Date Time: %s\n", __FILE__,__LINE__,row[LG_UTC_departure_time]);
				exit(1);
			}
			if((leg1Ptr->UTC_arrival_time =
			    dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[LG_UTC_arrival_time], NULL, &errNbr)) == BadDateTime) {
				fprintf(stderr,"%s Line %d: Bad Date Time: %s\n", __FILE__,__LINE__,row[LG_UTC_arrival_time]);
				exit(1);
			}
			if(!(leg1Root = RBTreeInsert(leg1Root, leg1Ptr, leg1Compare))) {
				fprintf(stderr,"%s Line %d: TreeInsert() failed\n", __FILE__,__LINE__);
				fprintf(stderr,"SQL:\n%s\n", sqlStmt);
				showLeg1(leg1Ptr);
				exit(1);
			}	
		}
		mysql_free_result(res);

		if(!myDoQuery(myconn, oag2sql, &res, &cols)) {
			fprintf(stderr,"db errno: %d: %s\n", myconn->my_errno, myconn->my_error_msg);
			exit(1);
		}
		if(res) {
			for(rows = 0; rows < myconn->rowCount; rows++) {
				row = mysql_fetch_row(res);
				if(! row)
					break;
				leg1Buf.arr_aptid = atoi(row[LG_dep_aptid]);
				strcpy(leg1Buf.FD_airline_desig, row[LG_FD_airline_desig]);
				leg1Buf.FD_flight_number[0] = '\0';
				lkRet = Lookup(leg1Root, &leg1Buf, leg1Compare, &tmp);
				switch(lkRet) {
				case ExactMatchFound: // impossible, since we don't enter FD_flight_number
					fprintf(stderr,"%s Line %d: found null flight number.\n", __FILE__,__LINE__);
					continue;
				case NotFoundReturnedNextItem:
					for(;tmp; tmp = Successor(tmp)) {
						leg1Ptr = (LEG1 *) getTreeObject(tmp);
						if(!(strcmp(leg1Ptr->FD_airline_desig,row[LG_FD_airline_desig]) == 0 && leg1Ptr->arr_aptid == atoi(row[LG_dep_aptid])))
							break;
						leg1_arrival_t = DateTimeToTime_t(leg1Ptr->UTC_arrival_time);
						if((leg2_depart_dt =
			    			    dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d", row[LG_UTC_departure_time], NULL, &errNbr)) == BadDateTime) {
							fprintf(stderr,"%s Line %d: Bad Date Time: %s\n", __FILE__,__LINE__,row[LG_UTC_departure_time]);
							exit(1);
						}
						leg2_depart_t = DateTimeToTime_t(leg2_depart_dt);
						if((leg2_depart_t - leg1_arrival_t >= MIN_Layover) && (leg2_depart_t - leg1_arrival_t <= MAX_Layover)) {
							if(!(resultPtr = (CjoinResult *) calloc(1, sizeof(CjoinResult)))) {
								fprintf(stderr,"%s Line %d: Out of memory.\n",__FILE__,__LINE__);
								exit(1);
							}
							resultPtr->leg1OrgAptID = leg1Ptr->dep_aptid; 
							resultPtr->leg1DestAptID =  leg1Ptr->arr_aptid;
							strcpy(resultPtr->leg1Flight_no, leg1Ptr->FD_flight_number);
							strcpy(resultPtr->leg1Flight_carrier,leg1Ptr->FD_airline_desig);
							resultPtr->leg1DptTime = leg1Ptr->UTC_departure_time;
							resultPtr->leg1ArrTime = leg1Ptr->UTC_arrival_time;
							resultPtr->leg2OrgAptID = atoi(row[LG_dep_aptid]);
							resultPtr->leg2DestAptID = atoi(row[LG_arr_aptid]);
							strcpy(resultPtr->leg2Flight_no,row[LG_FD_flight_number]);
							strcpy(resultPtr->leg2Flight_carrier,row[LG_FD_airline_desig]);
							if((resultPtr->leg2DptTime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d",
								row[LG_UTC_departure_time], NULL, &errNbr)) == BadDateTime) {
								fprintf(stderr,"%s Line %d: Bad Date Time: %s\n", __FILE__,__LINE__,row[LG_UTC_departure_time]);
								exit(1);
							}
							if((resultPtr->leg2ArrTime = dt_StringToDateTime("YMDHm", "%d/%d/%d %d:%d",
								row[LG_UTC_arrival_time], NULL, &errNbr)) == BadDateTime) {
								fprintf(stderr,"%s Line %d: Bad Date Time: %s\n", __FILE__,__LINE__,row[LG_UTC_arrival_time]);
								exit(1);
							}
							if(!TreeSearch(*result,resultPtr,resultCompare))
								*result = RBTreeInsert(*result, resultPtr, resultCompare);
							else
								continue;
							rowMatches++;
						}
					}
					break;
				}
			}
			mysql_free_result(res);
		}
	}
	while(leg1Root) {
		tmp = Minimum(leg1Root);
		leg1Ptr = (LEG1*) getTreeObject(tmp);
		leg1Root = RBTreeDelete(leg1Root, tmp);
		free(leg1Ptr);
	}
	return (rowMatches);
}

static int
leg1Compare(void *a, void *b)
{
	int ret;
	LEG1 *a1 = (LEG1 *) a;
	LEG1 *b1 = (LEG1 *) b;

	if((ret = a1->arr_aptid - b1->arr_aptid))
		return(ret);
	if((ret = strcmp(a1->FD_airline_desig,b1->FD_airline_desig)))
		return(ret);
	if((ret = strcmp(a1->FD_flight_number,b1->FD_flight_number)))
		return(ret);
	if(a1->UTC_departure_time > b1->UTC_departure_time)
		return(1);
	if(a1->UTC_departure_time < b1->UTC_departure_time)
		return(-1);
	if(a1->UTC_arrival_time > b1->UTC_arrival_time)
		return(1);
	if(a1->UTC_arrival_time < b1->UTC_arrival_time)
		return(-1);
	return(0);
}

static int
resultCompare(void *a, void *b)
{
	int ret;
	CjoinResult *a1 = (CjoinResult *) a;
	CjoinResult *b1 = (CjoinResult *) b;

        if((ret = a1->leg1OrgAptID - b1->leg1OrgAptID))
		return(ret);

        if((ret = a1->leg1DestAptID - b1->leg1DestAptID))
		return(ret);

	if((ret = strcmp(a1->leg1Flight_no, b1->leg1Flight_no)))
		return(ret);

        if((ret = strcmp(a1->leg1Flight_carrier, b1->leg1Flight_carrier)))
		return(ret);

        if(a1->leg1DptTime > b1->leg1DptTime)
		return(1);
        if(a1->leg1DptTime < b1->leg1DptTime)
		return(-1);

        if(a1->leg1ArrTime > b1->leg1ArrTime)
		return(1);
        if(a1->leg1ArrTime < b1->leg1ArrTime)
		return(-1);


        if((ret = a1->leg2OrgAptID - b1->leg2OrgAptID))
		return(ret);

        if((ret = a1->leg2DestAptID - b1->leg2DestAptID))
		return(ret);

	if((ret = strcmp(a1->leg2Flight_no, b1->leg2Flight_no)))
		return(ret);

        if((ret = strcmp(a1->leg2Flight_carrier, b1->leg2Flight_carrier)))
		return(ret);

        if(a1->leg2DptTime > b1->leg2DptTime)
		return(1);
        if(a1->leg2DptTime < b1->leg2DptTime)
		return(-1);

        if(a1->leg2ArrTime > b1->leg2ArrTime)
		return(1);
        if(a1->leg2ArrTime < b1->leg2ArrTime)
		return(-1);
	return(0);
}

static void
showLeg1(LEG1 *leg1Ptr)
{
	char tbuf1[32], tbuf2[32];

	fprintf(stdout,"%4d %4d %3s %6s %s %s\n",
		leg1Ptr->dep_aptid,
		leg1Ptr->arr_aptid,
		leg1Ptr->FD_airline_desig,
		leg1Ptr->FD_flight_number,
		dt_DateTimeToDateTimeString(leg1Ptr->UTC_departure_time, tbuf1, "%Y/%m/%d %H:%M"),
		dt_DateTimeToDateTimeString(leg1Ptr->UTC_arrival_time, tbuf2, "%Y/%m/%d %H:%M"));
}

