#ifndef CSHOPT_STRUCT_INC
#define CSHOPT_STRUCT_INC 1
#include "CSHOpt_define.h"


typedef struct networkArc
{
	int destDutyInd;
	double cost; //includes cost of destination node
	int blockTm; //includes blockTm of destination node
	int startTm; //as (time_t) / 60
	double tempCostForMac; //temporary cost to store additional information for Mac treatment - MAC - 09/23/08 ANG
	int repoFromAptID; //if a repo is needed to reach destDuty, this stores origin's airportID - MAC - 09/23/08 ANG
	int macRepoFltTm; //used whenever repoFromAptID > 0 to store the repo flt time - MAC - 09/23/08 ANG
	int macRepoStop; //used whenever repoFromAptID > 0 to store # of repo stops - MAC - 09/23/08 ANG
	//int planeStartTm; //Aircraft Start Time  RLZ
} NetworkArc;

typedef struct owner
{
	int ownerID;
	int charterLevel; //1, 2 or 3 where the higher the number the less we want to charter 
} Owner;

typedef struct origDemInfo //fei Jan 2011
{
	//copies of this original demand
	int numInd; //number of copies
	int indices[MAX_NUM_DEM_COPY]; //indices of copies in the new demand list, if this is an original demand

	//common properties of the original demand
	int inLockedTour;
	int isAppoint;
	int acInd;
	int crewPairID;
	time_t defaultOut;
	time_t defaultIn;
	int day;
	int uniqueID;//demand ID or maint ID
	int outAptID;
	int outFboID;
	int output;//for output collection
	
	//for inclusions
	int skipIncl; //whether it can be skipped
	int inclusionInd;  //index in the inclusion list of the corresponding ac
	int origSkipIncl; //to determine the penalty to put in LP - 10/18/11 ANG

	//dual for demand covering
	double dual;
} OrigDemInfo;

typedef struct inclInfo //fei Jan 2011
{
	//indices in origDemIndices
	int prevHardIncl[MAX_LEG_INCL+1]; //index of the previous hard inclusion in the inclusion list of an aircraft, default -1
	int nextHardIncl[MAX_LEG_INCL];  //index of the next hard inclusion in the inclusion list of an aircraft, default -1
	int seIndByDay[2][MAX_WINDOW_DURATION]; //start and end indices of each day's inclusions
	int seHardIndByDay[2][MAX_WINDOW_DURATION]; //start and end indices of each day's hard inclusions
	int isIncl[MAX_WINDOW_DURATION]; //there may be inclusions on a day

	//indices in the list of origDemInfo 
	int origDemIndices[MAX_LEG_INCL];  //required maintenance demand legs and locked demand legs for this aircraft

	//crew IDs
	//assume inclCrewID[i] corresponds to origDemIndices[i]
	int inclCrewID[MAX_LEG_INCL];  //crew (if any) that is locked to each inclusion for this plane
} InclInfo;


typedef struct demand
{
	int demandID;
	int feasible;
	int contractID;
	int ownerID;
	int numPax;
	int outAirportID;
	char outAptICAO[5];
	int outFboID;
	int outCountryID;
	int inAirportID;
	char inAptICAO[5];
	int inFboID;
	int inCountryID;
	time_t reqOut; //departure time requested by owner
	time_t reqIn;
	int earlyAdj; //maximum time (minutes) that reqOut/In can be moved up
	int lateAdj;  //maximum time (minutes) that reqOut/In can be moved back
	int earlyMPM;  //earliest departure time in minutes past midnight, local time (used to check against cutoffForFinalRepo)
	int contractFlag; // was flexschedule. for phase I:  1 for Vector, O otherwise
	int aircraftTypeID; // smallest aircraft type that can meet the demand
	int sequencePosn; //sequence position corresponding to smallest aircraft type
	int aircraftID; //this is populated on a demand leg IFF the demand leg is locked to a plane
	int acInd;  //this is populated on a demand leg IFF the demand leg is locked to a plane
	int crewPairID; //this is populated on a demand leg IFF the demand leg is locked to a crewPair
	int isAppoint;  //0 indicates a flight leg;  1 maintenance leg;  2 airport appointment, 3 airport sales/signing appt, 4 fake maintenance 11/05/07 ANG, 5 vendor ac back to base appt
	int turnTime;  // required turntime following this leg (0 for airport appointment)
	int *blockTm; //in minutes
	int *elapsedTm; //in minutes
	int *early; //earliest departure for each aircraft type (time_t / 60) considering vector or peak day window and curfews
	int *late; //latest departure for each aircraft type (time_t / 60) considering vector or peak day window and curfews
	double *cost;
	double *macOprCost; //Cost of putting a regular demand on Mac - MAC - 08/19/08 ANG
	//double *macOwnerCost; //Cost of putting a mac demand on Mac - MAC - 08/19/08 ANG 
							//Eliminated for now to speed up process, set = operatingCost. 
							//Related codes are retained for possible future use/modification.
	double changePenalty; 
	int redPenCrewPairID; //the ID of the existing crewPair to which the demand leg is assigned in the existing scenario / solution
	int redPenACID; //the ID of the aircraft to which the demand leg is assigned in the existing scenario / solution
	int inLockedTour; //indicates demand leg is currently covered by a crewPair/plane with lockTour = 1, so demand can be removed from optimization
	int predDemID; //demandID of preceding trip that is tied to this one:  same contractID and stop less than 90 minutes
	int succDemID; //demandID of succeeding (following) trip that is tied to this one:  same contractID and stop less than 90 minutes
	int tripNumber; //new field read to avoid confusion during simultaneous usage - affects how we assign predDemID and succDemID - 10/27/11 ANG
	NetworkArc **puSArcList;  // list of pickup arcs from start of trip
	int numPUSArcs[MAX_AC_TYPES];
	NetworkArc **puEArcList;  // list of pickup arcs from end of trip
	int numPUEArcs[MAX_AC_TYPES];
	double dual; //dual corresponding to trip-covering constraint
	double *puSDual; //dual corresponding to plane connection constraint for pickup at start of trip
	double *puEDual; //dual corresponding to plane connection constraint for pickup at end of trip
	int incRevStatus[MAX_AC_TYPES]; // if set to -1 the corresponding item in incRev is to be ignored.
	double incRev[MAX_AC_TYPES];
	int recoveryFlag;
	int downgradeRecovery;
	int upgradeRecovery;
	int noCharterFlag; //case 1: no charter for runway length <=5000ft.
	int noFlexFlag; 
	int contingencyidx;
	int contingecnyfkdmdflag; //Bool, 1 for fkdmd, o.w. for regular demand
	int demandnuminzone; //the number of demand in that zone and that period of time, only used for contingencyfkdmdflag. 
	time_t reqOut_actual; //actual out time for airport assignment. purpose: no turn time before airport assignment.
	int maintenanceRecordID; //stored only if .isAppoint == 4 - 04/09/08 ANG - stored for all 09/20/11 ANG
	int maxUpgradeFromRequest; //If a downgrade is requested, the smallestAllowSeq is the downgrade, but should still allow upgrade to the contract_seq
	int isMacDemand; //MAC - 08/19/08 ANG
	int macID; //M-aircraftID - MAC - 09/23/08 ANG
	int macIndex; //Index of aircraft in macInfoList - MacDOC per MAC - 11/02/2010 RLZ for revenue purpose
	int requires135; //04/20/10 ANG
	int isV10Contract; //New Vector - 05/25/10 ANG
	double revPerH; //Revenue Per Hour RLZ 11/01/2010
	int contract_actypeid; //Store revenue info - 06/27/11 ANG
	int legInd; //Store revenue helper - 06/29/11 ANG
	//Flex OS
	int origSkipIncl;//storing the original skipIncl when breaking non-skippable incl into days - 10/18/11 ANG
	int skipIncl;//fei Jan 2011 //for inclusion, whether can be skipped
	int flexLocation; //fei Jan 2011 //for inclusion, whether there are multiple locations
	int origUniqueID;//fei Jan 2011, to identify the copies //same for all copies
	time_t origDefOut;//fei Jan 2011, //same for all copies
	time_t origDefIn;//for storing original endtime information - FlexOS - 06/24/11 ANG
	int origDemInd;//fei Jan 2011, index of the original demand in origDemInfos, if this is a copy //same for all copies
	int isMainFlexLoc; //to indicate 'main copy' when there are multiple locations (when flexLocation = 1) - 07/29/11 ANG
	int output;//for output collection - 08/12/11 ANG
	int reqOCF; //to indicate whether an OCF is required afterwards - MX only - 10/07/11 ANG
	int outHubID;
	int outHubTime;
	int inHubID;
	int inHubTime;
} Demand;

typedef struct duty
{
	double sumDuals;
	int startTm[4]; // start time of each demand leg or trip as (time_t)/60 minutes
	int intTrTm[3]; //inter-trip time = turn-time plus any fboTransitTime OR plus repo elapsed time and second turn-time in minutes
	int repoFltTm[3]; //repoFltTm[i] is repo flight time between demandInd[i] and demandInd[i+1] - MAC - 09/24/08 ANG 
	int repoStop[3]; //repoFltStop[i] is the number of repo stops between demandInd[i] and demandInd[i+1] - MAC - 09/24/08 ANG 
	int endTm; //end time duty (last demand or repo leg) as(time_t)/60 minutes
	int crewStartTm; //start time of crew duty (assuming NO reposition is required to get to duty) as (time_t)/60 minutes  (!= startTm[0] if first demand is maint or apt appt)
	int crewEndTm; //end time of crew duty as (time_t)/60 minutes (!= endTm if last demand is maintenance or airport appt)
	int lastDemInd;
	int blockTm; // in minutes
	int demandInd[4];
	int repoDemandInd; //index of trip that is destination of a final repositioning leg
	int aircraftTypeID;
	int aircraftID; // null unless one or more legs is maintenance or locked
	int acInd;  //-1 unless one or more legs is maintenance or locked
	int crewPairID; // null unless one or more legs is locked
	double cost; //total cost, including actual cost and early/late penalty
	double earlyLatePen; //penalty for starting demand legs early or late
	double actualCost; //not including early/late penalty
	double changePenalty; //includes penalties for changing crew or plane assignments for legs
	double redPenaltyList[4];
	int redPenCrewPairList[4];
	int redPenACList[4];
	NetworkArc **arcList;
	int *countsPerArcList;
	int *arcTallyByDay;  //tracks the last parent arc index for each day
	int *unreachableFlag; // an array of booleans (one for each plane with inclusions or exclusions) that indicates the node can't
						// be reached by the plane (corresponding to that index) 
	int predType;  //predecessor type: 1 = crewPlane arc, 2 = crewPUSArc, 3 = crewPUEArc,  4 = duty node
	int predInd; //index of predecessor duty node OR of crewArc
	double spCost; //actual cost of shortest path to node
	double spRedCost; //reduced cost of shortest path to node
	int spACInd;  //index of aircraft used in current shortest path to node
	int isPureAppoint; // 1: consists only of appt
	double tempCostForMac; //temporary cost for Mac calculation - MAC - 09/23/08 ANG
	int repoFromAptID; //used whenever repoDemandInd > 1 and lastDemInd is not defined - MAC - 09/23/08 ANG
	int macRepoFltTm; //used whenever repoFromAptID > 0 to store the repo flt time - MAC - 09/23/08 ANG
	int macRepoStop; //used whenever repoFromAptID > 0 to store # of repo stops - MAC - 09/23/08 ANG
	int twoDutyFlag; //used to indicate whether the duty is a 2-duty one - 2DutyDay - 04/30/10 ANG
	int secondDutyFirstDmdSeq; //used to store first demand sequence index in second duty, entry in {1,2,3) - 2DutyDay - 04/30/10 ANG

	int firstInclInd;//fei FA
	int lastInclInd;//fei FA
} Duty;

struct listMarker
{
	int startInd;
	int endInd;
};

typedef struct cstraveldata
{
	int crewID;
	int dpt_aptID;
	int arr_aptID;
	DateTime travel_dptTm;
	DateTime travel_arrTm;
    int rqtID;
	//used to write back to cs_travel_flights table
	int pre_rqtID;// request id before updates, 
	int updated; //indicator for updates
	int firstupdated; //indicated requestID has been changed when reading into con_curr_flight_tree
	int inserted; // indicator for inserstion
	int changed;// indicate if the node in the tree has been changed (updated or newly inserted)
	int writtentotable; //indicate if the node in the tree has been written to table before.
}  CsTravelData;

typedef struct crew
{
	int crewID; //unique ID for pilot
	char fileas[30]; //name for this pilot
	int position; //captain or FO
	time_t tourStartTm; //datetime of tour start (not including overtime)
	int startRegDay; //first day of pilot's regular (non-overtime) tour (in terms of days of the planning window - window start = day 0)
	time_t tourEndTm; //datetime of tour end (get home time)(not including overtime)
	int endRegDay; //last day of pilot's regular (non-overtime) tour (in terms of days of the planning window - window start = day 0)
	double startEarly; //number of days early a pilot has volunteered to start
	double stayLate; //number of days late a pilot has volunteered to stay
	int startLoc; //airportID where tour starts, maybe different from base
	int endLoc; //airportID where tour must end
	int lockHome; //if set to 1 by scheduler, pilot is sent to endLoc (after any locked tours)
	int availAirportID;  //airportID of next available location
	time_t availDT;
	int activityCode; //0 indicates pilot must start duty at avail time, 1 indicates that pilot will be resting before avail time but hasn't been notified of next duty start and can start later,
				//2 indicates pilot hasn't started tour at avail time and can start later
	int dutyTime; //For a pilot on duty at the start of the planning window, this is the number of minutes worked up until crew.availDT, else 0.
	int blockTm; //For a pilot on duty at the start of the planning window, this is the number of minutes block time up until crew.availDT, else 0.
	int categoryID;
	int aircraftTypeID;
	int acTypeIndex;
	int exclDemandInd[MAX_LEG_EXCL];
	int numExcl;
	int overtimeMatters;  //flag is 1 if there can be a difference in pre-Tour overtime cost of tours so we must calculate pre-Tour overtime
	time_t origAvailDT; //original fields store information which will be overwritten if crew has a locked tour
	int origActCode;
	int origDutyTm;
	int origBlockTm;
	int inTourTransfer;  //flag if crew has flown leg, but is changing to other AC on commercial flight. 
	int needgettinghome;
    CsTravelData *lastCsTravelLeg;
	int lastActivityLeg_arrAptID; // indicate the arrival AptID for the lastActivityLeg if exists
	int lastActivityLeg_aircraftID; //indicate the aircraftID for lastActivityLeg if exists
	int lastActivityLeg_flag;  //indicate if there exists lastActivityLeg for this crew
	int travel_request_created; //indicate if travel request has been created for a crew
	time_t lastActivityLeg_recout;//indicatee the rec_out time for lastActivityLeg if exists
	int noneed_trl_gettinghome;//indicate if we need to buy commercial tickets to get crew home
	int dqOtherCrewPos; //store other crew index associated to the same crew - DQ - 12/09/2009 ANG
	int isDup; //Whether it is a duplicate, 1 = XLS+ - 06/07/11 ANG, 2 = CJ4 - 06/13/11 ANG
	int nextAcID; //if we want to break CPAC assignment to follow what scheduler did - CPAC Exception - 12/18/2009 ANG
	int day; //day=i if tour start on day-i, -1 otherwise - FATIGUE - 02/05/10 ANG
	int firstOutAptID; //store outAptID for first leg after on1 - FATIGUE - 03/02/10 ANG
	DateTime firstLegOutTime; //store outtime for first leg after on1 - FATIGUE - 03/02/10 ANG
	int vacCrew; //fake vendor aircraft crew; entry indicate the index of vac in vacList - VAC - 12/08/10 ANG
	int vacIndex; //index in vacList - VAC - 12/09/10 ANG
} Crew;

typedef struct travelRequest
{   
	int crewID;//crewID needing commercial travel
	int dept_aptID_travel;//departure airport ID, used in travel info request
	int arr_aptID_travel; //arrival airport ID, used in travel info request
	time_t earliest_dept; //earliest time crew can departurem, used in travel info request
	time_t latest_arr;  //latest time crew must arrive, used in travel info request
	int off_aircraftID; //if off tour or airplane swap
	int on_aircraftID;// if on tour or airplane swap
	int flight_purpose;//1 indicates on tour, 2 off tour, 3 changing plane.
	int rqtindex;//the index in travel request list
	int cancelexstticket;// cancel the existing tickets or not.
	int buyticket; // indicator for buying ticket or not for this pilot.
	int tixquestid_cancelled; //requestid of commercial airline ticket cancelled.
	int groundtravel;//indicate the travel request is a ground travel(=1) or commerical airline travel (=0).
} TravelRequest;

typedef struct crewArc
{
	int acInd; //index of plane for crewPlane arc, or for plane-specific crewPickup arc, -1 for (general fleet) crewPickuparc, -acGroupInd if arc is for plane group
	int demandInd; //index of trip for crewPickup arc; -1 for crewPlane arc
	double cost; //cost to get crew to plane
	int earlyPickupTm[3];  //earliest possible departure time for a leg on that plane, as (time_t)/60, for captain, FO, and worst case / pair
	int dutyTmAtEPU[3]; //minutes of duty time that have been incurred by earlyPU for captain, FO, and worst case / pair
	int blockTmAtEPU[3]; //minutes of block time that have been incurred by earlyPU for captain, FO, and worst case / pair
	int canStrtLtrAtEPU[3]; //indicates that duty hours at EPU can be shortened by starting later (for captain, FO, and worst case / pair)
	NetworkArc *arcList;  //arcs from plane or pickup location to first duties; arc costs include change penalties for crew-plane-firstDuty combination
	int numArcs; //number of arcs to first duties from this crewArc "destination node"

} CrewArc;

typedef struct shortTour
{
	int dutyInd; //index of the final duty node of this tour
	double cost; //cost of shortest path to this final duty node PLUS get-home cost if required
	double redCost; //reduced cost of shortest path to this final duty node PLUS get-home cost if req'd 
					//PLUS dual for connection constraint if plane is dropped off at end
	int dropPlane; //1 indicates that plane is dropped off at end of tour;  value > 1 indicates plane is dropped AND also indicates that last duty is a
					//reposition-only duty before pilot ends tour, and plane is dropped at this time (as time_t/60)
	int finalApptDemInd; //index of maintenance demand that can be covered if plane is left at end of tour
} ShortTour; //tours with the lowest reduced cost for a crewPair

typedef struct Tour
{
	int crewPairInd; //index of crewPair
	double cost; //total cost of tour
	int crewArcType; //1 = crewPlane arc, 2 = crewPUSArc, 3 = crewPUEArc
	int crewArcInd;
	int duties[MAX_WINDOW_DURATION]; //dutyList indices of duties in tour
	int dropPlane; //1 indicates that plane is dropped off at end of tour;  value > 1 indicates plane is dropped AND also indicates that last duty is a
					//reposition-only duty before pilot ends tour, and plane is dropped at this time (as time_t/60)
	int finalApptDemInd; //index of maintenance demand that can be covered if plane is left at end of tour
	double redCost;
} Tour; //Tours that are sent to LP / IP

typedef struct exgTour
{
	int crewPairInd;
	int demandInd[MAX_LEGS];
	double cost;
	int acInd;
	int pickupType;/* 1 = pickup when plane is next avail (pickupInd = -1), 
				2 = pickup at start of demand leg (pickupInd = demand index), 3 = pickup at end of demand leg (pickupInd 
				= demand index), 4 = pickup at end of a repo leg before another repo (pickupInd = leg index*/
	int pickupInd;  
	int dropoffType; /* 1 = not dropped off, 2 = drop off at demand start (dropoff index = demand index), 
						3 = dropoff at demand end, 4 = drop off at repo end (don't know next leg, or next leg is another repo). */
	int dropoffInd;

	int crewPairInd2; // For second crewpair, if any. Used in buildAddlExgTour() - 03/14/08 ANG 
	int demandInd2[MAX_LEGS]; // For second aircraft, if any. Used in buildAddlExgTour2() - 03/27/08 ANG
	double cost2; // For second aircraft, if any. Used in buildAddlExgTour2() - 03/27/08 ANG
	int acInd2; // For second aircraft, if any. Used in buildAddlExgTour2() - 03/25/08 ANG 
	int pickupType2; // For second aircraft, if any. Used in buildAddlExgTour2() - 03/27/08 ANG
	int pickupInd2; // For second aircraft, if any. Used in buildAddlExgTour2() - 03/27/08 ANG
	int dropoffType2; // For second aircraft, if any. Used in buildAddlExgTour2() - 03/26/08 ANG
	int dropoffInd2; // For second aircraft, if any. Used in buildAddlExgTour2() - 03/26/08 ANG
} ExgTour;


typedef struct crewAcTour
{
	int crewPairInd;
	double cost;
	int acInd;	
	int crewArcType; //1 = crewPlane arc, 2 = crewPUSArc, 3 = crewPUEArc
	int crewArcInd;
} crewAcTour;

typedef struct crewPair
{
	int crewPairID; //unique ID for crewPair (for the Tour that overlaps this planning window)
	int captainID; //crewID for captain 
	int flightOffID; //crewID for flight officer
	int crewListInd[2]; //first index is for captain, second if for flight officer
	int *aircraftID; //ordered list of aircraft to which crewPair is assigned during planning window in current scenario
	int countAircraftID;//number of aircraftIDs stored for the crewpair in current scenario - 03/24/08 ANG
	int hasFlownFirst; //equals 1 if crewPair has already flown aircraftID[0] on this tour prior to start of planning window
	int *lockTour;  //ordered list, set to 1 if scheduler locks tour for that crewPair on that aircraftID
	int optAircraftID;  //populated in code if crewPair is already flying this plane and lockTour = 0 for plane, 
					//else populate with plane from first locked leg for crewPair for which lockTour = 0 for plane.
					//we consider only this plane for this crewPair in the optimization
	int acInd;  //for optAircraftID
	int exgAircraftID; //Give bonus for maintaining existing crewpair - 02/11/09 ANG
	int exgAcInd; //for exgAircraftID - 02/11/09 ANG
	int acTypeIndex;
	time_t pairStartTm; //max(tourStartTm - startEarly*24*3600) for the two crew members
	int startDay; //startDay of crewPair's tour (in terms of days of the planning window - window start = day 0)
	time_t pairEndTm; //min(tourEndTm + stayLate*24*3600) for the two crew members
	int endDay;  //end Day of crewPair's tour (in terms of days of the planning window - window start = day 0)
	int endRegDay; //earliest last day of pilots' regular (non-overtime) tours (in terms of days of the planning window - window start = day 0)
	double crewPairBonus;  //bonus applied to tours flown by this crewPair (they are a desired or required pairing)
	int availAptID;  //will be populated if crew members are available at the same TIME AND PLACE WITH SAME HOURS (already together)
	time_t availDT;  //will be populated if crew members are available at the same TIME AND PLACE WITH SAME HOURS (already together)
	int activityCode; //will be populated if crew members are available at the same TIME AND PLACE WITH SAME HOURS (already together)
	int dutyTime; //will be populated if crew members are available at the same TIME AND PLACE WITH SAME HOURS (already together)
	int blockTm;  //will be populated if crew members are available at the same TIME AND PLACE WITH SAME HOURS (already together)
	int schedLegIndList[MAX_LEGS]; //for crewPairs that existed prior to run, these are the indices of the scheduled legs from the current scenario/solution
	int schedLegACIndList[MAX_LEGS]; //for crewPairs that existed prior to run, these are the indices of the scheduled legs from the current scenario/solution
	int inclDemandInd[MAX_LEG_INCL];
	int numIncl;
	CrewArc *crewPlaneList; //arcs to planes that can be picked up by crew when they are next available
	CrewArc **crewPUSList; //arcs to trips before which crew can pick up a plane (when left there by another crew)
	CrewArc **crewPUEList; //arcs to trips after which crew can crew can pick up a plane(when left there by another crew)
	int numPlaneArcs;
	int numPUStartArcs;
	int numPUEndArcs;
	double *getHomeCost;  //the cost of sending a crewPair home after a duty MINUS the cost of sending them home from current location
	int nodeStartIndex; //the first getHomeCost in the array corresponds to dutyList[acTypeIndex][nodeStartIndex; also, the first node that must be considered in tour generation)
	double dual;
	int exgTrInd; //store the index of existing tour of the crewpair - 05/16/08 ANG
	int cpacExcpFlag; //flag to indicate that the crewpair is no longer locked to aircraft - 12/30/2009 ANG
	int vacPair; //fake vendor aircraft pair; entry indicate the index of vac in vacList - VAC - 12/08/10 ANG
} CrewPair;

typedef struct aircraft
{
	int aircraftID;
	char registration[15];
	int aircraftTypeID;
    int legCrewPairFlag;  //default -1; Used to un-pair leg-crewPairID, such as 1-1-0-1-1 -> 1-1-0-0-0
	int sequencePosn;
	int firstCrPrID; //ID of crewPair LOCKED to plane when next available (from input, but can be updated based on locked legs).  
	int firstCrPrInd; 
	int availAirportID;
	int availFboID;
	char availAptICAO[5];
	time_t availDT; //updated in processInput.c to include turn-time following the last leg
	int intlCert;  //1 indicates plane has international certification at start of planning window, 0 indicates it does not
	int availDay; //the day of the planning window that the plane is next available
	int maintFlag; //indicates plane will be available when it comes out of maint/appointment.  1 indicates maintenance record, 2 indicates airport appointment.
	int reqOCF; //indicates plane will need an OCF when comes out of maint (populated when maintFlag=1) - 10/10/11 ANG
	int exclDemandInd[MAX_LEG_EXCL];  //indices of excluded demand for this aircraft
	int lastExcl[MAX_WINDOW_DURATION]; //last excluded demand index up to the end of that day [this is the index of exclDemandInd list, not index of demandList]
	
	int inclDemandInd[MAX_LEG_INCL];  //required maintenance demand legs and locked demand legs for this aircraft
	int inclCrewID[MAX_LEG_INCL];  //crew (if any) that is locked to each inclusion for this plane
	int lastIncl[MAX_WINDOW_DURATION]; //last required demand index up to the end of that day [this is the index of inclDemandInd list, not index of demandList]
	
	int acTypeIndex;
	int acGroupInd;
	int unreachableInd;  //index of unreachableFlag on duty nodes that applies to this plane (-1 if there is no unreachableFlag for this plane on the duty nodes)
	int dutyNodeArcIndex[MAX_WINDOW_DURATION];  //equal to 0 (index for fleet)UNLESS plane has exclusions or inclusions on a later day
	NetworkArc *arcList; //list of outgoing arcs / duty nodes that can be reached by this plane
	int arcTallyByDay[MAX_WINDOW_DURATION];  //tracks the last arc index for each day
	int specConnxnConstr[MAX_WINDOW_DURATION + 1]; //indicates if plane can be picked up before some inclusions / exclusions, 
			//so special connection constraints are required in the formulation. Value of 1 indicates special constraint for pickup at START of trips
			//value of 2 indicates special constraint for pickup at END of trips, and value of 3 indicates special constraint for BOTH.
			//specConnxnConstr[MAX_WINDOW_DURATION] = 1 if special constraints on ANY day, = 2 if
			//plane is locked to two different crews in which case it must be picked up by the 2nd crew.
	int puTripListIndex;  //index of puTripList (list of trips before/after which plane could be picked up by another crew) 
						//that goes with this plane 
	int sepNW;  //1 indicates that a separate network (separate from fleet network) must be generated for this plane
	double dual;
	DateTime rec_outtime; // used in input.c
	//time_t nextLegStartTm; //updated in input.c to capture next schedout/starttime/currentstarttime following the last leg - 02/28/08 ANG
	//RLZ: Not needed for availDT....
	int schedLegIndList[MAX_LEGS]; //to populate legs assigned (not locked) to this aircraft in existing scenario - 03/05/08 ANG
	int schedCrPrIndList[MAX_LEGS]; //to populate crew pair assigned to schedLegIndList in existing scenario - 03/14/08 ANG
	int countCrPrToday; //additional existing tours are only built if countCrPrToday > 1
	int numCrPairs; //storing information for aircraft with multi crew pairs - 04/23/08 ANG
	int cprInd[MAX_CRPR_PER_AC]; //it was first store crewpairID, then convert to crew pair index. storing information for aircraft with multi crew pairs - 04/23/08 ANG
	//int firstApptInd; //first appt index during the pw, need to make a copy
	int multiCrew;  //multiple crewpair
	int isMac; //MAC - 08/19/08 ANG
	int hasOwnerTrip; //this is only used for Mac to indicate if there is an owner trip for this aircraft; used in aircraft grouping - MAC - 09/23/08 ANG
	double macDOC; //MacDOC per MAC - 05/20/2009 ANG
	int macIndex; //Index of aircraft in macInfoList - MacDOC per MAC - 05/20/2009 ANG
	int applyCPACbonus; //indicate if the aircraft needs CPAC bonus - CPAC - 06/17/09 ANG
	int cpIndCPACbonus; //indicate crewPairIndex tied to aircraft for CPAC bonus - CPAC - 06/17/09 ANG
	int isVac; //VAC - 12/08/10 ANG
	int vacIndex; //Index of aircraft in vacInfoList - VAC - 12/08/10 ANG
	int csAcIndex; //Index of aircraft in csAcInfoList - Separate DOC per CS AC - 12/23/10 ANG
	
	int numIncl ;//number of inclusions, >= 0 //fei Jan 2011
	InclInfo *inclInfoP; //fei Jan 2011
	time_t fixedTimeB4; //fei FA: end time of the last skipped maint/app: to fix the problem of moving ac earlier when building existing tours
} Aircraft;

typedef struct acGroups
{
	int numAircraft;
	int *acInd;
} AcGroup;

typedef struct aircraftType
{
	int sequencePosn; //This field should be the rank order for upgrades.  Need to verify if sequencePosition can be used for that.
	int aircraftTypeID;
	int maxUpgrades;
	double operatingCost; //operating cost ($/hr)
	double charterCost; //charter cost ($/hr)
	double taxiCost; //operating cost per taxi out/in cycle
	double standardRevenue;
	int capacity;
	int downgradeRecovery;
	int upgradeRecovery;
	double macOprCost; //operating cost for putting a regular-demand on mac - MAC - 08/19/08 ANG
	//double macOwnerCost; //operating cost for putting a mac-demand on mac - MAC - 08/19/08 ANG
							//Eliminated for now to speed up process, set = operatingCost. 
							//Related codes are retained for possible future use/modification.
} AircraftType;

typedef struct maintenanceRecord
{
	int maintenanceRecordID;
	int aircraftID;
	int airportID;
	time_t startTm;
	time_t endTm;
	int apptType;  //0 indicates maintenance record, 1 indicates airport appointment, 2 indicates sales signing appointment // 3 indicates fake maintenance - 11/05/07 ANG // 4 indicates fake vendor aircraft appointment - VAC - 12/09/10 ANG
	int fboID;
	//Flex OS
	int flexLocation; //fei Jan 2011
	int skip;  //fei Jan 2011
	int earlyAdj;//fei Jan 2011
	int lateAdj;//fei Jan 2011
	int uniqueID;//indentify copies //fei Jan 2011
	time_t defaultOut; //fei Jan 2011
	time_t defaultIn; //FlexOS - 06/24/11 ANG
	int isMainFlexLoc; //to indicate 'main copy' when there are multiple locations (when flexLocation = 1) - 07/29/11 ANG
	int inDmdList; //to indicate whether a record is created in demand list - 08/16/11 ANG
	int reqOCF; //to indicate whether an OCF is required afterwards - MX only - 10/07/11 ANG
} MaintenanceRecord;

typedef struct crewEndTourRecord // 11/12/07 ANG
{
	int recordID;	// equals to either:
					// 1. a fake maintenanceRecordID - if a fake record is indeed added
					// 2. a true maintenanceRecordID - if fake record is NOT added due to overlap with the true record
	int aircraftID; 
	int airportID; // equals base airport
	time_t startTm;
	time_t endTm;
	int recordType; // 0 = no fake record due to overlapping maintenance record, 
					// 1 = fake record added with first crewID only, 
					// 2 = fake record updated with second crewID, 
					// 3 = no fake record added because of maxFakeMaintRec is exceeded
	int crewID1; // crew flown to base
	int crewID2; // another crew flown home if both are going off-tour together and have the same base
	
	//int assignedDemandID; // demandID assigned in processInput()
	//int assignedDemandInd; // demand Index assigned in processInput()

	//fei FA
	int assignedDemandID[MAX_NUM_DEM_COPY]; // demandID assigned in processInput()
	int assignedDemandInd[MAX_NUM_DEM_COPY]; // demand Index assigned in processInput()
	int numAD;

	//double cost; // equal zero if apptType = 0, equals to the positioning cost - populated after optimizer was run
	int covered; // final result - populated after optimizer was run : 1 = is flown home, 0 = is NOT
	int wrongCrew;	// sometimes the crew ends tour early and mislead other crews to perform the fake airport appointment
					// 0 = is not wrong crew, 1 = wrong crew - should ignore results
	int crewPairID; //this is to lock the newly created fake appt to a crewPair
} CrewEndTourRecord;

typedef struct airport
{
	int airportID;
	char ICAO[5];
} Airport;

typedef struct aptMap
{
	int airportID;
	double cost;
	int duration;
	int groundOnly;
} AptMap;

typedef struct airport2 
{	// airportID equals the index of the airport list;
	char ICAO[5];
	double lat;
	double lon;
	int tzid;
	int commFlag; //true if this is a commercial airport with flights in the OAG guide
	AptMap *aptMapping;
	int numMaps;
} Airport2;


typedef struct leg
{
	int demandID; //null for repo leg
	int demandInd;  ////stores demand index which is repopulated after demandList is sorted in processInput
	int aircraftID;
	char registration[10];
	int acInd;
	int crewPairID;
	int crewPairInd;  //stores crewPair index which must be repopulated after new crew pairing is done
	int outAirportID;	
	int outFboID;
	int inAirportID;
	int inFboID;
	time_t schedOut;
	time_t schedIn;
	time_t adjSchedIn;
	int crewLocked; //indicates crew has been locked by a Scheduler
	int planeLocked; //indicates plane has been locked by a Scheduler
	int inLockedTour; //indicates leg is currently covered by a crewPair/plane with lockTour = 1
	int dropped; //1: if optimizer decides to drop this leg from the exg tour RLZ 09/01/2009
	int exgTourException; //1: if this repo leg does need to be written to output should exg tour containing this repo leg gets picked - 03/31/10 ANG
} Leg;


typedef struct optParameters
{
	int estimateCrewLoc; //estimate crew location from Bitwise crew assignment. (follow aircraft)
	int autoFlyHome; //Automatically create fake airport assignments to fly pilots home - 11/08/07 ANG
	int beyondPlanningWindowBenefit; // Benefit (per hour) of tour time overlap beyond the planning window.
	double changeNxtDayPenalty; //penalty for recommending changes to assignment of a leg departing tomorrow
	double changeTodayPenalty; //penalty for recommending changes to assignment of a leg departing today
	int charterMacDmd; //consider chartering Mac demands - MAC - 11/18/11 ANG
	double crewPairBonus; //NOT USED - TO BE REMOVED
	int cutoffForFinalRepo; //minutes after midnight local time before which a trip's earliest start must occur if we are to consider positioning the evening before
	int crewAllAc; //crewing all aircraft and work on contingency plans - Fei DL - 11/08/11 ANG
	int dayEndTime;          // minutes after midnight GMT that is used to divide days in planning window
	int downgrPairPriority1; //downgrade pairing request with priority 1 to priority 2 - 01/21/09 ANG
	int dutyStartFromMidnight;  // minutes after midnight LOCAL TIME that is used to start crew duty - 10/30/07 ANG
	double earlyCostPH; //penalty cost per hour for starting a flexibly scheduled trip early
	int earlierAvailability; // to get earlier availDT for crew: - 10/30/07 ANG
							// 1 - run getDutySoFarAlt(), 0 - otherwise
	int fboTransitTm; //default time (minutes) that must be added to turntime when an aircraft comes into one FBO and departs from another
	int finalPostFltTm; //required post-flight duty minutes, last flight on last duty day
	int firstPreFltTm; //required pre-flight duty minutes, first flight on first duty day
	int firstDayShortDutyStart; //minutes from midnight from which short duty start limit begins - FATIGUE - 02/25/10 ANG
	int firstDayShortDutyEnd; //minutes from midnight from which short duty start limit ends - FATIGUE - 02/25/10 ANG
	int shortDutyHrDif; //shortage in minutes for first duty day - FATIGUE - 02/05/10 ANG
	int fuelStopTm; //default time (minutes) for fuel stops
	int inclContingencyDmd; //include contingency demands - 09/08/08 ANG
	int inclInfeasExgSol; //include infeasible cases in exiting solutions - 05/22/08 ANG
	int includeLockedUmlg; //include all demands in locked charters - 02/12/08 ANG
	int ignoreSwapLoc;    //Ignore SWAP airport assignment -03/26/08 RLZ
	int ignoreMacOS;    //Ignore green blocks put on Mac - MAC - 10/23/08 ANG
	int ignoreMacOSMX;    //Ignore both maintenance AND green blocks put on Mac started after windowStart - MAC - 01/07/09 ANG
	int ignoreMac;    //Ignore green and mx blocks put on Mac - MAC - 01/05/09 ANG
	int ignoreMacDmd;    //Ignore Mac demands - MAC - 01/06/09 ANG
	int ignoreAllOS;    //Ignore green blocks put on any aircraft - 10/28/08 ANG
	int ignorePairConstraints; //Ignore pairing requests - 10/15/10 ANG
	double lateCostPH; //penalty cost per hour for starting a flexibly scheduled trip late
	double macBonus; //bonus of putting Mac owner's trip on the corresponding Mac - MAC - 09/05/08 ANG
	double macPenalty; //increase mac operating cost by this much per hour - MAC - 10/28/08 ANG
	int maintTmForReassign; //length of maintenance (minutes) above which crew will be reassigned
	int maintTurnTime; //default minutes to "out" after maintenance release
	int maxDutyTm; //max duty minutes (14 x 60 = 840)
	int maxSecondDutyTm; //max duty minutes (8 x 60 = 480). If zero, 2DutyDay is not implemented - 2DutyDay - 05/12/10 ANG
	int minSecondRestTm; // min rest time in minutes (16 x 60 = 960), after second duty on the same day - 2DutyDay - 05/21/10 ANG
	int maxFakeMaintRec;//max fake maintenance records can be added to bring pilots home on last day of tour - 11/08/07 ANG
	int maxFlightTm; //max flight minutes (10 x 60 = 600)
	int maxRepoTm; //max flight time (minutes) of allowable repo
//	int maxUpgrades; //maximum number of sequencePositions that a demand leg can be upgraded
	int minCharterTime; //in minutes
	int minRestTm; //min rest minutes (10 x 60 = 600)
	//int minTimeToDuty; //NO LONGER USED - TO BE REMOVED [elapsed time (minutes) from run start time to duty start at a local FBO for a crew that has met or exceeded the minimum overnight rest (if not already assigned in the existing schedule)]
	int minTimeToNotify; //Min to notify a crew for his new duty information, ties to RunTime.
	int minTmToTourStart; //elapsed time (minutes) from run start time to base for pilot starting a tour (if not already assigned in exg schedule)
	int crewTourStartInMin; //minutes after midnight local time a crew can start the tour 
	int numToursPerCrewPerItn; //used when adding new reduced cost columns to each LP iteration
	double overTimeCost; //overtime cost per pilot ($/day)
	double overTimeHalfCost; //overtime cost per pilot ($/half day)
	int pairingModelRHS; // right hand side for pairing solver - 12/10/2009 ANG
	int pairingLevel; // 0 - pair pilots who are not yet flying a plane; 1 - keep all existing pairs and pair only those pilots not yet paired
					//2 - run crew pairing twice (as per 0 and 1 above) and keep BOTH sets of pairs; 3- completely bypass crew pairing.
	int peakDayLevel_1_Adj; // scheduled out adjustment for level 1 peak days (minutes).
	int peakDuration; //duration in minute of the peak flying period considered when pairing pilots to form crews
	int peakGMTStart; //start time (in minutes after midnight GMT) of the peak flying period considered when pairing pilots to form crews (used for all but first day of pairing)
	int peakGMTDuration; //NOT USED - TO BE REMOVED
	int peakOverlapBenefit; // Benefit (per hour) of peak-time overlap within the planning window.
	int peakStart; // start time (in minutes past local midnight) of the peak flying period (used for first day of pairing only)
	int planningWindowDuration; // (max) number of days in planning window for Optimizer
	int planningWindowStart; // number of minutes after current time that planning window starts
	int planningFakeRuntime; // number of minutes after current time that fake runtime starts - 02/19/08 ANG
	int postArrivalTime; //A crew member must spend this many minutes at that the commercial airport after arrival, before ground transpo to FBO.
	int postFlightTm; //required post-flight duty minutes after last flight of the day
	int preBoardTime; //A crew member must arrive at the airport this many minutes in advance of a commercial departure
	int preFlightTm; //required pre-flight duty minutes ("show-time")before first flight of the day
	int printLP; //store LP file from Optimization model - 10/18/10 ANG
	int priorityBenefit[5];	//Benefit of pairing pilots with priority 1..4
	double priorityBftWt; //Scale up/down the priorityBenefit from 0 to 1. default at 0. 10/26/10 RLZ 
	int prohibitStealingPlanes; //prevent crews from picking up a plane from a crew with remaining days left in tour
	int restToDutyTm; //NO LONGER USED - TO BE REMOVED [elapsed time (minutes) from end of minimum overnight rest to start of duty at a local FBO (if not already assigned in the existing schedule)]
	int runWithContingency; // Indicator to show if run with Contingency Plan.
	int runWithoutExgSol; // test 02/01/11 ANG
	int runType; //0 indicates optimization, 1 indicates OAG update, 2 indicates OAG nightly pre-process
	int runOptStats; //Run optimizer statistics - 02/12/08 ANG
	int runOptStatsByDay; //Run optimizer statistics - 12/05/08 ANG - Currently only works for pwduration = 3, slight modification needed for pwduration = 2
	int ssoftFromBW; //create a fake ssoft record, if there is duty line on that day 02/26/2010 RLZ
	int storePairingResult; //Whether final crew pair list is stored into MySQL table - 02/22/10 ANG
	int taxiInTm; //default minutes from "on" to "in"
	int taxiOutTm; //default minutes from "out" to "off"
	double ticketCostFixed; //fixed component of airline ticket price approx.
	double ticketCostVar; //variable component (cost per mile) of airline ticket price
	int travelcutoff; //number of hours after (fake) runtime travel information will be read - modified JTO change - 09/11/09 ANG
	int turnTime; //default minutes from "in" to "out" between legs
	int preOCFtime; //Additional turn time before OCF (in addition to original turnTime) - 10/07/11 ANG
	int postOCFtime; //Additional turn time after OCF (in addition to original turnTime) - 10/07/11 ANG
	double uncovMaintPenalty; //(high) penalty for not covering a maintenance leg/appt
	double uncovDemoPenalty; // penalty for not covering a sales demo appointment
	double uncovFlyHomePenalty; // penalty for not flying a pilot back to his base - 11/06/07 ANG
	double uncovFlyHomePenalty2; // penalty for not flying a pilot back to his base - 04/22/08 ANG
	double uncovVacPenalty; // penalty for not flying vendor aircraft back to its base - VAC - 12/13/10 ANG
	double uncovHardApptPenalty; // penalty for not covering a non-skippable appointment
	double uncovSoftApptPenalty; // penalty for not covering a skippable appointment
	int useOTFcrew; // if we want to utilize OTF crew, default currently 1 - 10/15/09 ANG
	int vectorWin; //We are using this now. [max +/- time adjustment (minutes) for Vector leg] 
	time_t windowEnd; //THIS IS CALCULATED AND NOT EVER SET BY THE USER
	time_t windowStart; //THIS IS CALCULATED AND NOT EVER SET BY THE USER
	int withDQ; // DQ - 12/09/2009 ANG
	int withOag;// THIS IS INDICATOR TO SHOW IF OAG DATA IS INCLUDED
	int withOcf; // if we want to consider OCF automatically - 10/12/11 ANG
	int withCTC;// THIS IS INDICATOR TO SHOW IF CTC DATA IS INTEGRATED
	int withMac; //Indicator if M-aircraft are treated separately; not an option for user, help in debugging - MAC - 09/23/08 ANG
	int withVac; //If vendor aircraft is considered in optimization - VAC - 12/01/10 ANG
	int vacToBase; //If vendor aircraft is required to be back to its base - VAC - 12/13/10 ANG
	double xsCharterPenalty[4]; //penalty for chartering a contract with charter level 1..3
	int dutyNodeAdjInterval;  //In minutes, used in createOneTripNodes to flex the trip
	int maxFirstFlexE;     //     =  2      #maximum number a demand can be flex early for the first
    int maxFirstFlexL;     //     =  2      #maximum number a demand can be flex late for the first
    int noEarlyThanFlex;    //    =  360    #in minutes past mid night local time
	double earlyCostPH_recovery; //penalty cost per hour for starting a recovery trip early
	double lateCostPH_recovery; //penalty cost per hour for starting a recovery trip late
	int recoveryAdj_early;   // flex earlier for recovery demand (minutes)
	int recoveryAdj_late;   // flex later for recovery demand (minutes)
	double downgradePenaltyRatio; //incentive for not  downgradeing in recovery
	int maxCrewExtension; //in minutes, used to justify if we want to make crew fly home the next day, this maximum limit is not violated - 04/21/08 ANG
	int cutoffForShortDuty; //300    #minutes after midnight local time RLZ
    int shortDutyTm;   //600   #in minute, duty time for a crew starting before cutyoffForShortDuty RLZ
	int minRestTmLong;//840   # rest time for early duty
	int exgCPACLock;   //flag for not re assign existing crew pairs - Note : usage might be revised - give bonus to existing crewpair-aircraft combination - 02/19/09 ANG
	int crewDutyAfterCommFlt; //shreshhold to have crew continue working or get rest after commercial travel, unit Hours
	double exgCPACBonus; //Bonus for keeping current crewpair-aircraft assignment - 02/11/09 ANG
	int updateforSimu;// determine if write data in proposed_managedleg into managedleg table, i.e, used for simulation
	int paramsOracle;//determine if update parameters value from oracle database
	int sendhomecutoff;//used to decide if to buy airline tickets to send crew home
	int useSeparateMacDOC; //MacDOC per MAC - 05/21/2009 ANG
	int useSeparateMacBonus; //Mac bonus per MAC - 12/14/10 ANG
	int useSeparateCsAcDOC; //If separate DOC considered for some citationAir aircraft - Separate DOC for CS Aircraft - 12/23/10 ANG
	double pairSameBaseBonus[5]; //bonus for pairing pilots having close by bases [exact same base, 15min flight, 30min flight, 1hr flight, 2hr flight] - 08/05/09 ANG
	int nonRevContractID; // additional Non revenue contract or user chose to do so. this can be expanded to a list. RLZ 11/02/2010 
	//int noInclusionTest; //fei Jan 2011
	int withFlexOS; //FlexOS - 03/14/11 ANG
	int flexMxLoc; //FlexMX - 06/29/11 ANG
	int flexMxTime; //FlexMX - 08/10/11 ANG
	int flexMxDir; //FlexMX - 08/10/11 ANG
	int flexUmxTime; //FlexMX - 09/15/11 ANG
	int testCombineMaint ; //fei FA
	int testOutTmWindow ; //fei FA
} OptParameters;

typedef struct pairConstraints
{
int pairConstraintID; // integer
int crew1ID; //pilot who should be paired with another pilot or group
int crew2ID; //pilot with whom they should be paired; default zero;
int categoryID; //pilot category with whom they should be paired; default -1;
int priority; // 1 - hard constraint, 2,3,4 - soft constraints with decreasing cost.
time_t startTm;
} PairConstraint;

/*
TYPE                     FIRSTID           SECONDID
1 airport-curfew start   airportID         curfewstarttime - minutes after midnight GMT
2 airport-curfew end     airportID         curfewendtime - minutes after midnight GMT
3 aircraftType-airport   aircraftTypeID    airportID
4 demand-aircraft        demandID          aircraftID
5 aircraft-state		 aircraftID		   stateID (currently used to prevent N506CS land on Florida, can be extended to other usage)
6 aircraft-country		 aircraftID		   countryID - only for part 135
*/
typedef enum {
	Excln_Airport_CurfewStart = 1,
	Excln_Airport_CurfewEnd,
	Excln_AircraftType_Airport,
	Excln_Demand_Aircraft,
	Excln_Aircraft_State, //08/27/09 ANG
	Excln_Aircraft_Country, //only for part 135 - 04/20/10 ANG
	ExclType_EndOfList = 255
} ExclusionType;

typedef struct exclusion
{
	int typeID;
	int firstID; //ID of aircraft type, airport,demand, aircraft, crew category, or crew corresponding to first object in exclusion
	int secondID; //ID of airport, aircraft, or owner corresponding to second object in exclusion.  For airport-time exclusions, the second object will be a time, and there will be two exclusions/exclusion types - one for start and one for end
} Exclusion;

typedef struct type_3_exclusion_list {
	int count;
	int aircraftTypeID;
	int *airportIDptr;
} Type_3_Exclusion_List;

typedef struct oagEntry
{
	int connAptID;  //ID of airport where connection is made for one-stop itineraries (null for direct itineraries)
	time_t dptTm;
	time_t arrTm;
	int unAvail;   //= 1 if itinerary is unavailable per Travel Dept
} OagEntry;

typedef struct oDEntry
{
	int commOrAptID;
	int commDestAptID;
	time_t earlyDpt;
	time_t lateArr;
	double cost; 
	OagEntry *oagList;
	int numOag;
	int numOD;
} ODEntry;


typedef struct blockItin
{
	int blockItinID;
	int startAptID;
	int endAptID;
	time_t start;
	time_t end;
} BlockItin;

typedef struct crewAssignmentSoln {
	int crewID;
	int aircraftID;
	int position;
	time_t startTm;
	time_t endTm;
	int *coverDems; //DL Fei
	int numCoverDems; //DL Fei
} ProposedCrewAssg;

typedef struct mgdLegSoln {
	int demandID;
	int captainID;
	int FOID;
	int crewPairInd;
	int schedOutFBOID;
	int schedInFBOID;
	int schedOutAptID;
	int schedInAptID;
	int aircraftID;
	time_t schedOut;
	time_t schedIn;
	int exgTour;
} ProposedMgdLeg;

typedef struct unmgdLegSoln {
	int aircraftID; //VAC - 12/10/10 ANG
	int departureAptID;
	int departureFBOID;
	int demandID;
	time_t schedOut;
	time_t schedIn;
	int arrivalFBOID;
	int arrivalAptID;
} ProposedUnmgdLeg;

typedef struct ownerSigningSoln { //06/24/11 ANG
	int recordID; 
	int aircraftID;
	int airportID;
	int fboID;
	time_t startTime;
	time_t endTime;
	int exgTour;
} ProposedOwnerSigning;

//FOR CSHOag:

typedef struct commercialOrigin
{
	int aptID;
	time_t earlyFlDep;
}  CommOrig;

typedef struct commercialDestn
{
	int aptID;
	time_t lateFlArr;
}  CommDest;

typedef struct origEntry
{
	int aptID;
	time_t earlyStart;
	CommOrig *commOrig;
}  OrigEntry;

typedef struct destEntry
{
	int aptID;
	time_t lateEnd;
	CommDest *commDest;
}  DestEntry;

typedef struct Warning_error_Entry
{   int local_scenarioid;
    char group_name[15];
	int aircraftid;
	int crewid;
	char datatime_str[50];
	int airportid;
    int demandid;
	int crewassgid;
	int crewpairid;
	int actypeid;
	int contractid;
	int minutes;
	int leg1id;
	int leg2id;
	int legindx;
    int acidx;
	int crewpairindx;
	int maintindx;
	int exclusionindex;
	char filename[100];
	char line_number[10];
	int format_number;
	char notes[400];
}  Warning_error_Entry;

typedef struct hubsByZone
{
	int aptID;
	char ICAO[5];
	int timezoneid;
}  HubsByZone;

//typedef struct cstraveldata
//{
//	int crewID;
//	int dpt_aptID;
//	int arr_aptID;
//	DateTime travel_dptTm;
//	DateTime travel_arrTm;
//  int rqtID;
//}  CsTravelData;

typedef struct macInfo
{
	int aircraftID; 
	int aircraftTypeID;
	int contractID; 
	double macDOC;
	double macBonus; //Mac bonus per MAC - 12/14/10 ANG
} MacInfo;

typedef struct vacInfo
{
	int aircraftID; 
	int aircraftTypeID;
	int startAirportID; 
	int endAirportID; 
	int intlCert;
	int numSeats;
	time_t availStartTm;
	time_t availEndTm;
	double vacDOC;
} VacInfo;

typedef struct csAcInfo //Separate DOC per CS AC - 12/23/10 ANG
{
	int aircraftID; 
	int aircraftTypeID;
	double cacDOC;
} CsAcInfo;

typedef struct dqCrew
{
	int crewID; 
	int acTypeID1;
	int acTypeID2; 
	int postID1;
	int postID2; 
	int sameFleet;
} DqCrew;

//Flex logic revised - 12/06/12 ANG
typedef struct irrFlexDemand {
  int demandID;
  int flexHour;
  int flexDir;
} IrrFlexDemand;

#endif // CSHOPT_STRUCT_INC
