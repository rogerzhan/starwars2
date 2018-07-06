// CONSTANTS

#define INFINITY 999999  //flight time is in minutes, so this is effectively infinite for flight time as well as get-home cost
#define MAX_WINDOW_DURATION 3 //Max number of days in the planning window
#define MAX_LEG_EXCL 30 //maximum number of demand legs from which a single aircraft (not a fleet) or pilot is excluded
#define MAX_LEG_INCL 12 //maximum number of appointment PLUS locked legs to which a single aircraft or crew pair is tied
#define MAX_LEGS 20 //maximum number of legs within the planning window in the current scenario
#define MAX_AC_TYPES 5  //maximum number of aircraft types
#define MAX_AIRPORTS 9000 //used for aptExcl array
#define TOTAL_AIRPORTS_NUM 9000  // Use to read data from airportLatLonTabByAptId. 
#define MAX_PLANES_PER_FLEET 30 
#define DutyAllocChunk 1000  //used for dynamic memory allocation of duty nodes
#define ArcAllocChunk 100 //used for dynamic memory allocation of network (duty-to-duty) arcs
#define TourAllocChunk 100 //used for dynamic memory allocation of Tours
#define PAST_WINDOW 99 //used to indicate that a pilot's endDay or endRegDay is beyond the end of the planning window
#define maxRepoConnxns 50 //maximum number of existing tours in the planning window which involve a crew swap between two positioning legs
#define CPLEX_TIME_LIMIT 300 // seconds. Has to be read in as a parameter.
#define MAX_CPLEX_ITERATIONS 60 // number of times shortest path is executed
#define TIME_STEP 60 // (in minutes). Used for computing peak overlap benefit.
#define OD_HASH_SIZE 150000 //hash table parameter for OD table, which indicates OAG entries that have been queried
#define MIN_DIRECTS_PER_HOUR 1 //if the number of direct commercial (oag)itineraries per hour is less than this number, we will consider 1-stop itineraries
#define MAX_OAG_PER_OD 2000 //maximum number of commercial itineraries retrieved for a commercial orig-dest pair
#define MAX_ACTIVITY_LEGS 40 // maximum number of activity legs in thw time window for wherearetheplanesSQL, (pws-5 days,pwe+1 day)
#define DAYS_BEOFRE_PWS 3//days before planning windowstart, to include more crewassignment duties. 
#define ODEntryAllocChunk 100   //used for dynamic memory allocation of oDTable
#define MIN_LAYOVER 2700 //lower limit for connection time between two commercial flights  //Jintao's change
#define MAX_LAYOVER 7200  //upper limit for connection time between two commercial flights  //Jintao's change
#define MAX_DAY_DIVISION 2//3 //upper limit to seperate a day to create contingency demand
#define ZONE_NUM 6  // number of zones with which we define whole United States
#define ACTYPE_NUM 5// number of demand per type in every zone
#define FAKE_DMD_DURATION // length of time we want to keep an aircraft as contingency
#define MAX_FAKE_DMD_NUMBER // the max number of contingency aicraft in one zone we can make
#define MAX_FAKE_DMD_NUM // the max number of contgiency fake demand we can add
#define MAX_CRPR_PER_AC 5 //maximum crew pairs per aircraft in existing schedule - 04/23/08 ANG