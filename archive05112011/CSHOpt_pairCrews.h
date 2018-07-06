#include <time.h>

// Structure that defines the data that is sent to the pilot pairing optimizer.
struct matArc {
	int ac;		            // Aircraft ID
	int apt;                // Swap airport (used only for display)
	time_t swapTm;          // Swap time (used only for display)
	int p1;		            // Pilot 1
	int p2;		            // Pilot 2
	double benefit;	        // Arc benefit
	struct matArc *next;	// next in linked list.
};

typedef struct matArc MatchingArc;

int pairCrews (void);


