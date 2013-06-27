/**
 * @file move.cpp
 * @author Adrian Del Maestro
 *
 * @brief Move class impementaions.
 */

#include "move.h"
#include "path.h"
#include "action.h"
#include "lookuptable.h"
#include "communicator.h"

uint32 MoveBase::totAttempted = 0;
uint32 MoveBase::totAccepted = 0;

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// MOVE BASE CLASS -----------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
MoveBase::MoveBase (Path &_path, ActionBase *_actionPtr, MTRand &_random, 
        string _name, ensemble _operateOnConfig) :
    name(_name),
    operateOnConfig(_operateOnConfig),
	path(_path), 
	actionPtr(_actionPtr), 
	random(_random) {

 	/* Initialize private data to zero */
	numAccepted = numAttempted = numToMove = 0;
	success = false;

	sqrtLambdaTau = sqrt(constants()->lambda() * constants()->tau());
	sqrt2LambdaTau = sqrt(2.0)*sqrtLambdaTau;
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
MoveBase::~MoveBase() {
	originalPos.free();
}


///@cond DEBUG
/*************************************************************************//**
 *  Print the current worm configuration after a move if DEBUG_WORM is
 *  activated.
******************************************************************************/
inline void MoveBase::printMoveState(string state) {
#ifdef DEBUG_WORM
	/* We make a list of all the beads contained in the worm */
	Array <beadLocator,1> wormBeads;	// Used for debugging
	wormBeads.resize(path.worm.length+1);
	wormBeads = XXX;

	/* Output the worldline configuration */
	communicate()->file("debug")->stream() << "Move State: " << state 
		<< " (" << path.getTrueNumParticles() << ")" << endl;
	communicate()->file("debug")->stream() << "head " << path.worm.head[0] << " " << path.worm.head[1]
        << " tail " << path.worm.tail[0] << " " << path.worm.tail[1]
        << " length " << path.worm.length 
        << " gap " << path.worm.gap << endl;

	if (!path.worm.isConfigDiagonal) {
		beadLocator beadIndex;
		beadIndex = path.worm.tail;
		int n = 0;
		do {
			wormBeads(n) = beadIndex;
			beadIndex = path.next(beadIndex);
			++n;
		} while(!all(beadIndex==path.next(path.worm.head)));
	}

	path.printWormConfig(wormBeads);
	path.printLinks<fstream>(communicate()->file("debug")->stream());
	wormBeads.free();
#endif
}

/*************************************************************************//**
 *  We perform a check to make sure that there are no problems with a move,
 *  this is only performed if DEBUG_MOVE is defined.
******************************************************************************/
inline void MoveBase::checkMove(int callNum, double diffA) {
#ifdef DEBUG_MOVE
	/* The first time we call, we calculate the kinetic and potential action */
	if (callNum == 0) {
		oldV = actionPtr->potentialAction();
		oldK = actionPtr->kineticAction();
	}

	/* The second time, we compute the updated values and compare. This would
	 * be used after a keep move */
	if (callNum == 1) {
		newV = actionPtr->potentialAction();
		newK = actionPtr->kineticAction();
		double diffV = newV - oldV;
		if (abs(diffV-diffA) > EPS) {
			communicate()->file("debug")->stream() << format("%-16s%16.6e\t%16.6e\t%16.6e\n") % name 
				% diffV % diffA % (diffV - diffA);
            // communicate()->file("debug")->stream() << path.worm.beads << endl;
            // communicate()->file("debug")->stream() << path.prevLink << endl;
            // communicate()->file("debug")->stream() << path.nextLink << endl;
            cout << name << " PROBLEM WITH KEEP " << diffV << " " << diffA << endl;
			exit(EXIT_FAILURE);
		}
	}

	/* The third time would be used after an undo move */
	if (callNum == 2) {
		newV = actionPtr->potentialAction();
		newK = actionPtr->kineticAction();
		double diffV = newV - oldV;
		double diffK = newK - oldK;
		if ( (abs(diffV) > EPS) || abs(diffK) > EPS) {
			communicate()->file("debug")->stream() << format("%-16s%16.6e\t%16.6e\n") % name
				% diffV % diffK;
            // communicate()->file("debug")->stream() << path.worm.beads << endl;
            // communicate()->file("debug")->stream() << path.prevLink << endl;
            // communicate()->file("debug")->stream() << path.nextLink << endl;
			cout << name << " PROBLEM WITH UNDO " << diffV << " " << diffK << endl;
			exit(EXIT_FAILURE);
		}
	}

	/* This call is used to debug the distributions in the changes in potential
	 * and kinetic energy per particle. diffA is the number of beads touched
	 * in the move here.*/
	if (callNum == -1) {
		newV = actionPtr->potentialAction();
		newK = actionPtr->kineticAction();
		double diffV = newV - oldV;
		communicate()->file("debug")->stream() << format("%-16s%16.6e\t%16.6e\n") % name 
			% ((newK-oldK)/diffA) % ((diffV)/diffA);
	}
#endif
}
///@endcond DEBUG

/*************************************************************************//**
 *  For all move types, if the move is accepted, we simply increment our
 *  accept counters 
******************************************************************************/
void MoveBase::keepMove() {
	numAccepted++;
	totAccepted++;

	/* Restore the shift level for the time step to 1 */
	actionPtr->setShift(1);

	success = true;
}

/*************************************************************************//**
 * Returns a new staging position which will exactly sample the kinetic
 * action. 
 *
 * @param neighborIndex The index of the bead to be updated's neighbor
 * @param endIndex The index of the final bead in the stage
 * @param stageLength The length of the stage
 * @param k The position along the stage
 * @return A NDIM-vector which holds a new random position.
******************************************************************************/
dVec MoveBase::newStagingPosition(const beadLocator &neighborIndex, const beadLocator &endIndex,
		const int stageLength, const int k) {

	PIMC_ASSERT(path.worm.beadOn(neighborIndex));

    /* The rescaled value of lambda used for staging */
    double f1 = 1.0 * (stageLength - k - 1);
    double f2 = 1.0 / (1.0*(stageLength - k));
    double sqrtLambdaKTau = sqrt2LambdaTau * sqrt(f1 * f2);

	/* We find the new 'midpoint' position which exactly samples the kinetic 
	 * density matrix */
	neighborPos = path(neighborIndex);
	newRanPos = path(endIndex) - neighborPos;
	path.boxPtr->putInBC(newRanPos);
	newRanPos *= f2;
	newRanPos += neighborPos;

	/* This is the random kick around that midpoint */
	for (int i = 0; i < NDIM; i++)
		newRanPos[i] = random.randNorm(newRanPos[i],sqrtLambdaKTau);

    path.boxPtr->putInside(newRanPos);

    return newRanPos;
}

/*************************************************************************//**
 * Generates a new position, which exactly samples the free particle
 * density matrix.
 *
 * Compute a position which is selected from a guassian distribution
 * with a mean at a neighboring position, and variance equal to 
 * 2 * lambda * tau.  
 * @param neighborIndex the beadLocator for a neighboring bead
 * @return A randomly generated position which exactly samples 1/2 the
 * kinetic action.
******************************************************************************/
dVec MoveBase::newFreeParticlePosition(const beadLocator &neighborIndex) {

	PIMC_ASSERT(path.worm.beadOn(neighborIndex));

	/* The Gaussian distributed random position */
	for (int i = 0; i < NDIM; i++)
		newRanPos[i] = random.randNorm(path(neighborIndex)[i],sqrt2LambdaTau);

    path.boxPtr->putInside(newRanPos);

    return newRanPos;
}

/*************************************************************************//**
 * Returns a new bisection position which will exactly sample the kinetic
 * action. 
 *
 * @param beadIndex The bead that we want a new position for
 * @param lshift The number of time slices between beads moved at this level
 * @return A NDIM-vector which holds a new random position.
******************************************************************************/
dVec MoveBase::newBisectionPosition(const beadLocator &beadIndex, 
        const int lshift) { 

    /* The size of the move */
    double delta = sqrtLambdaTau*sqrt(1.0*lshift);

	/* We first get the index and position of the 'previous' neighbor bead */
	nBeadIndex = path.prev(beadIndex,lshift);

	/* We now get the midpoint between the previous and next beads */
	newRanPos = path.getSeparation(path.next(beadIndex,lshift),nBeadIndex);
	newRanPos *= 0.5;
	newRanPos += path(nBeadIndex);

	/* This is the gausian distributed random kick around that midpoint */
	for (int i = 0; i < NDIM; i++)
		newRanPos[i] = random.randNorm(newRanPos[i],delta);

    /* Put in PBC and return */
    path.boxPtr->putInside(newRanPos);
	return newRanPos;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// CENTEROFMASS MOVE CLASS ---------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
CenterOfMassMove::CenterOfMassMove (Path &_path, ActionBase *_actionPtr, 
		MTRand &_random, string _name,  ensemble _operateOnConfig) :
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zera */
	numAccepted = numAttempted = numToMove = 0;

	/* We setup the original position array which will store the shift
	 * of a single particle at a time so it can be undone later */
	originalPos.resize(1);
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
CenterOfMassMove::~CenterOfMassMove() {
}

/*************************************************************************//**
 *  Performs a Center of Mass move.
 * 
 *  For each active particle, we move the center of mass of its wordline
 *  by a fixed random amount.
******************************************************************************/
bool CenterOfMassMove::attemptMove() {

	success = false;

	/* We don't bother performing a center of mass move, unless we have at 
	 * least one bead on slice 0 */
	if (path.numBeadsAtSlice(0) == 0)
		return false;

	/* The initial bead to be moved */
	startBead = 0,random.randInt(path.numBeadsAtSlice(0)-1);

	checkMove(0,0.0);

    int wlLength = 0;
	beadLocator beadIndex;
	/* Check to see if the start bead is on a worm and that it is shorter than
     * the number of time slices.  If it is, we start at the worm tail and end
     * at its head. */
	if ( path.worm.foundBead(path,startBead) )  {
        if (path.worm.length >= constants()->numTimeSlices())
            return false;
		startBead = path.worm.tail;
		endBead   = path.worm.head;
	}
	/* Otherwise, we loop around until we find the initial bead, and finish at 
     * its predecesser in imaginary time */
	else {
		endBead = path.prev(startBead);

        /* if our worldline is longer than numTimeSlices, we don't attempt the
         * move */
        beadIndex = startBead;
        do {
            ++wlLength;
            beadIndex = path.next(beadIndex);
        } while (!all(beadIndex==path.next(endBead)));

        if (wlLength > constants()->numTimeSlices())
            return false;
    }

	/* Increment the number of center of mass moves and the total number
	 * of moves */
	numAttempted++;
	totAttempted++;

	/* The random shift that will be applied to the center of mass*/
	for (int i = 0; i < NDIM; i++) 
		originalPos(0)[i] = constants()->Delta()*(-0.5 + random.rand());

	/* Here we test to see if any beads are placed outside the box (if we
	 * don't have periodic boundary conditions).  If that is the case,
	 * we don't continue */
	dVec pos;
	if (int(sum(path.boxPtr->periodic)) != NDIM) {
		beadIndex = startBead;
		do {
			pos = path(beadIndex) + originalPos(0);
			path.boxPtr->putInBC(pos);
			for (int i = 0; i < NDIM; i++) {
				if ((pos[i] < -0.5*path.boxPtr->side[i]) || (pos[i] >= 0.5*path.boxPtr->side[i]))
					return false;
			}
			beadIndex = path.next(beadIndex);
		} while (!all(beadIndex==path.next(endBead)));
	}

    /* Determine the old potential action of the path */
     oldAction = actionPtr->potentialAction(startBead,endBead);

    /* Go through the worldline and update the position of all the beads */
	beadIndex = startBead;
	do {
		pos = path(beadIndex) + originalPos(0);
		path.boxPtr->putInBC(pos);
		path.updateBead(beadIndex,pos);
		beadIndex = path.next(beadIndex);
	} while (!all(beadIndex==path.next(endBead)));

    /* Get the new potential action of the path */
    newAction = actionPtr->potentialAction(startBead,endBead);

	/* The metropolis acceptance step */
	if (random.rand() < exp(-(newAction - oldAction)))  {
		keepMove();
		checkMove(1,newAction-oldAction);
	}
	else {
		undoMove();
		checkMove(2,0.0);
	}

	return success;
}

/*************************************************************************//**
 *  Undo the move by restoring the original particle positions.
******************************************************************************/
void CenterOfMassMove::undoMove() {

	dVec pos;
	beadLocator beadIndex;
	beadIndex = startBead;
	do {
		pos = path(beadIndex) - originalPos(0);
		path.boxPtr->putInBC(pos);
		path.updateBead(beadIndex,pos);
		beadIndex = path.next(beadIndex);
	} while (!all(beadIndex==path.next(endBead)));

	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// STAGING MOVE CLASS --------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
StagingMove::StagingMove (Path &_path, ActionBase *_actionPtr, 
        MTRand &_random, string _name, ensemble _operateOnConfig) : 
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zera */
	numAccepted = numAttempted = numToMove = 0;

	/* Resize the original position array */
	originalPos.resize(constants()->Mbar()-1);
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
StagingMove::~StagingMove() {
    // empty destructor
}

/*************************************************************************//**
 *  Perform the staging move.
 * 
 *  The staging move is non-local in imaginary time and exactly samples the
 *  kinetic density matrix.
******************************************************************************/
bool StagingMove::attemptMove() {

	success = false;

    checkMove(0,0.0);

	/* We don't bother staging if we only have a worm, as this will be taken care
	 * of with other moves.  It is also possible that we could get stuck in an 
	 * infinite loop here.*/
	if (path.getTrueNumParticles()==0)
		return false;

	/* Randomly select the start bead of the stage */
	startBead[0] = random.randInt(path.numTimeSlices-1);

    /* We need to worry about the possibility of an empty slice for small
     * numbers of particles. */
    if (path.numBeadsAtSlice(startBead[0]) == 0)
        return false;
    startBead[1] = random.randInt(path.numBeadsAtSlice(startBead[0])-1);

	/* Now we have to make sure that we are moving an active trajectory, 
	 * otherwise we exit immediatly */
	beadLocator beadIndex;
	beadIndex = startBead;
	for (int k = 0; k < (constants()->Mbar()); k++) {
		if (!path.worm.beadOn(beadIndex) || all(beadIndex==path.worm.head))
			return false;
		beadIndex = path.next(beadIndex);
	}
	endBead = beadIndex;

	/* If we haven't found the worm head, and all beads are on, try to perform the move */

	/* Increment the attempted counters */
	numAttempted++;
	totAttempted++;

    /* Get the current action for the path segment to be updated */
    oldAction = actionPtr->potentialAction(startBead,path.prev(endBead));

    /* Perorm the staging update, generating the new path and updating bead
     * positions, while storing the old one */
	beadIndex = startBead;
	int k = 0;
	dVec pos;
	do {
		beadIndex = path.next(beadIndex);
		originalPos(k) = path(beadIndex);
		path.updateBead(beadIndex,
				newStagingPosition(path.prev(beadIndex),endBead,constants()->Mbar(),k));
		++k;
	} while (!all(beadIndex==path.prev(endBead)));

    /* Get the new action for the updated path segment */
    newAction = actionPtr->potentialAction(startBead,path.prev(endBead));

	/* The actual Metropolis test */
	if ( random.rand() < exp(-(newAction-oldAction)) ) {
		keepMove();
        checkMove(1,newAction-oldAction);
    }
	else {
		undoMove();
        checkMove(2,0.0);
    }
	
	return success;
}

/*************************************************************************//**
 *  Undo the staging move, restoring the original position of each
 *  particle that has been tampered with.
******************************************************************************/
void StagingMove::undoMove() {

	int k = 0;
	beadLocator beadIndex;
	beadIndex = startBead;
	do {
		beadIndex = path.next(beadIndex);
		path.updateBead(beadIndex,originalPos(k));
		++k;
	} while (!all(beadIndex==path.prev(endBead)));

	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// BISECTION MOVE CLASS ------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 * Constructor.
 *
 * Initialize the number of levels and all local data structures.
******************************************************************************/
BisectionMove::BisectionMove(Path &_path, ActionBase *_actionPtr, 
        MTRand &_random, string _name, ensemble _operateOnConfig) : 
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zera */
	numAccepted = numAttempted = numToMove = 0;

	/* Initialize the acceptance by level counters */
	numAcceptedLevel.resize(constants()->b()+1);
	numAttemptedLevel.resize(constants()->b()+1);
	numAcceptedLevel  = 0;
	numAttemptedLevel = 0;

    /* The number of levels used in bisection */
	numLevels = constants()->b();

	/* We setup the original and new positions as well as the include array
     * which stores updates to the beads involved in the bisection */
	numActiveBeads = ipow(2,numLevels)-1;
    include.resize(numActiveBeads);
    originalPos.resize(numActiveBeads);
    newPos.resize(numActiveBeads);
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
BisectionMove::~BisectionMove() {
    include.free();
}


/*****************************************************************************/
/// Bisection Move : attempt Move
///
/// Here we actually perform the bisection move, which attempts to make a single
/// large move at the midpoint between two slices, then futher bisects until
/// all particles have been moved.  The only adjustable parameter for is the 
/// number of levels, which sets the number of particles that we will try 
/// to move.  The move can be fully rejected at any level, and we then
/// go back and restore the positions of all moved particles.
///
/// For a nice description of the algorithm see:
/// C. Chakravarty et al. J. Chem. Phys. 109, 2123 (1998).
/*****************************************************************************/
bool BisectionMove::attemptMove() {

	success = false;

    /* We cannot perform this move at present when using a non-local action */
    if (!actionPtr->local)
        return false;

	/* Randomly select the start bead of the bisection */
	startBead[0] = random.randInt(path.numTimeSlices-1);

    /* We need to worry about the possibility of an empty slice for small
     * numbers of particles. */
    if (path.numBeadsAtSlice(startBead[0]) == 0)
        return false;
    startBead[1] = random.randInt(path.numBeadsAtSlice(startBead[0])-1);

	/* Now we have to make sure that we are moving an active trajectory, 
	 * otherwise we exit immediatly */
	beadLocator beadIndex;
	beadIndex = startBead;
	for (int k = 0; k < (numActiveBeads+1); k++) {
		if (!path.worm.beadOn(beadIndex) || all(beadIndex==path.worm.head))
			return false;
		beadIndex = path.next(beadIndex);
	}
	endBead = beadIndex;

    checkMove(0,0.0);

    /* Increment the number of displacement moves and the total number
     * of moves */
    numAttempted++;
    totAttempted++;
    numAttemptedLevel(numLevels)++;
    include = true;

    /* Now we perform the actual bisection down to level 1 */
    oldDeltaAction = 0.0;
    for (level = numLevels; level > 0; level--) {

        /* Compute the distance between time slices and set the tau
         * shift for the action */
        shift = ipow(2,level-1);
        actionPtr->setShift(shift);

        /* Reset the actions for this level*/
        oldAction = newAction = 0.0;

        beadIndex = path.next(startBead,shift);
        int k = 1;
        do {
            int n = k*shift-1;

            if (include(n)) {
                originalPos(n) = path(beadIndex);
                oldAction += actionPtr->potentialAction(beadIndex);

                /* Generate the new position and compute the action */
                newPos(n) = newBisectionPosition(beadIndex,shift);
                path.updateBead(beadIndex,newPos(n));
                newAction += actionPtr->potentialAction(beadIndex);

                /* Set the include bit */
                include(n) = false;
            }
            /* At level 1 we need to compute the full action */
            else if (level==1) {
                newAction += actionPtr->potentialAction(beadIndex);
                path.updateBead(beadIndex,originalPos(n));
                oldAction += actionPtr->potentialAction(beadIndex);
                path.updateBead(beadIndex,newPos(n));
            }

            ++k;
            beadIndex = path.next(beadIndex,shift);
        } while (!all(beadIndex==endBead));

        /* Record the total action difference at this level */
        deltaAction = (newAction - oldAction);

        /* Now we do the metropolis step, if we accept the move, we 
         * keep going to the next level, however, if we reject it,
         * we return all slices that have already been moved back
         * to their original positions and break out of the level
         * loop. */

        /* The actual Metropolis test */
        if (random.rand() < exp(-deltaAction + oldDeltaAction)) {
            if (level == 1) {
                keepMove();
                checkMove(1,deltaAction);
            }
        }
        else {
            undoMove();
            checkMove(2,0.0);
            break;
        }

        oldDeltaAction = deltaAction;

    } // end over level

	return success;
}

/*****************************************************************************/
/// Bisection Move : keep Move
///
/// If the move is accepted, we simply increment our accept counters 
/*****************************************************************************/
void BisectionMove::keepMove() {

	numAccepted++;
	numAcceptedLevel(numLevels)++;
	totAccepted++;

	/* Restore the shift level for the time step to 1 */
	actionPtr->setShift(1);

	success = true;
}

/*****************************************************************************/
/// Bisection Move : undo Move
///
/// We undo the bisection move, restoring the original position of each
/// particle that has been tampered with, it might not be all of them.
/*****************************************************************************/
void BisectionMove::undoMove() {

    /* Go through the list of updated beads and return them to their original
     * positions */
	int k = 0;
	beadLocator beadIndex;
	beadIndex = startBead;
	do {
		beadIndex = path.next(beadIndex);
        if (!include(k)) {
            path.updateBead(beadIndex,originalPos(k));
        }
		++k;
	} while (!all(beadIndex==path.prev(endBead)));

	actionPtr->setShift(1);
	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// OPEN MOVE CLASS -----------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 * Constructor.
******************************************************************************/
OpenMove::OpenMove (Path &_path, ActionBase *_actionPtr, MTRand &_random, 
        string _name, ensemble _operateOnConfig) : 
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zero */
	numAccepted = numAttempted = numToMove = 0;

	/* Initialize the acceptance by level counters */
	numAcceptedLevel.resize(constants()->b()+1);
	numAttemptedLevel.resize(constants()->b()+1);
	numAcceptedLevel  = 0;
	numAttemptedLevel = 0;
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
OpenMove::~OpenMove() {
}

/*************************************************************************//**
 * Perform an open move.
 *
 * Here we actually attempt to open up the world line configuration with
 * the resulting creation of a worm.  We select a particle and then time 
 * slice at random, and provided the current configuration is diagonal
 * (worm free) we just remove a portion of the particle's worldline.
******************************************************************************/
bool OpenMove::attemptMove() {

	success = false;

    /* Get the length of the proposed gap to open up. We only allow even 
     * gaps. */
    gapLength = 2*(1 + random.randInt(constants()->Mbar()/2-1));
    numLevels = int (ceil(log(1.0*gapLength) / log(2.0)-EPS));

    /* Randomly select the head bead, and make sure it is turned on, we only
     * allow the head or tail to live on even slices */
    /* THIS IS EXTREMELY IMPORTANT FOR DETAILED BALANCE */
    headBead[0] = 2*random.randInt(path.numTimeSlices/2-1);
    headBead[1] = random.randInt(path.numBeadsAtSlice(headBead[0])-1);

    /* Find the tail bead */
    tailBead = path.next(headBead,gapLength);

    /* Determine how far apart they are */
    dVec sep;
    sep = path.getSeparation(headBead,tailBead);

    /* We make sure that the proposed worm is not too costly */
    if ( !path.worm.tooCostly(sep,gapLength) ) {

        checkMove(0,0.0);

        /* We use the 'true' number of particles here because we are still diagonal, 
         * so it corresponds to the number of worldlines*/
        double norm = (constants()->C() * constants()->Mbar() * path.worm.getNumBeadsOn())
                / actionPtr->rho0(headBead,tailBead,gapLength);

        /* We rescale to take into account different attempt probabilities */
        norm *= constants()->attemptProb("close")/constants()->attemptProb("open");

        /* Weight for ensemble */
        norm *= actionPtr->ensembleWeight(-gapLength+1);
        double muShift = gapLength*constants()->mu()*constants()->tau();

        /* Increment the number of open moves and the total number of moves */
        numAttempted++;
        totAttempted++;
        numAttemptedLevel(numLevels)++;

        /* The temporary head and tail are special beads */
        path.worm.special1 = headBead;
        path.worm.special2 = tailBead;

        /* If we have a local action, perform a single slice rejection move */
        if (actionPtr->local) {

            double actionShift = (-log(norm) + muShift)/gapLength;

            /* We now compute the potential energy of the beads that would be
             * removed if the worm is inserted */
            oldAction = 0.0;
            double deltaAction = 0.0;
            double PNorm = 1.0;
            double P;
            double factor = 0.5;

            beadLocator beadIndex;
            beadIndex = headBead;
            do {
                deltaAction -= actionPtr->barePotentialAction(beadIndex) - factor*actionShift;
                P = min(exp(-deltaAction)/PNorm,1.0);

                /* We do a single slice Metropolis test and exit the move if we
                 * wouldn't remove the single bead */
                if ( random.rand() >= P ) {
                    undoMove();
                    return success;
                }
                PNorm *= P;

                factor = 1.0; 
                beadIndex = path.next(beadIndex);
            } while (!all(beadIndex==tailBead));

            /* Add the part from the tail */
            deltaAction -= actionPtr->barePotentialAction(tailBead) - 0.5*actionShift;
            deltaAction -= actionPtr->potentialActionCorrection(headBead,tailBead);

            /* Now perform the final metropolis acceptance test based on removing a
             * chunk of worldline. */
            if ( random.rand() < (exp(-deltaAction)/PNorm) ) {
                keepMove();
                checkMove(1,deltaAction - gapLength*actionShift);
            }
            else {
                undoMove();
                checkMove(2,0.0);
            }
        }
        /* otherwise, perform a full trajctory update */
        else {

            /* We now compute the potential energy of the beads that would be
             * removed if the worm is inserted */
            oldAction = actionPtr->potentialAction(headBead,tailBead);

            /* Now perform the metropolis acceptance test based on removing a chunk of
             * worldline. */
            if ( random.rand() < norm*exp(oldAction - muShift) ) {
                keepMove();
                checkMove(1,-oldAction);
            }
            else {
                undoMove();
                checkMove(2,0.0);
            }
        }

    } // too costly

	return success;
}

/*************************************************************************//**
 * We preserve the move, update the acceptance ratios and the location
 * of the worm, and must delete the beads and links in the gap.
******************************************************************************/
void OpenMove::keepMove() {

	/* update the acceptance counters */
	numAccepted++;
	totAccepted++;
	numAcceptedLevel(numLevels)++;
	
	/* Remove the beads and links from the gap */
	beadLocator beadIndex;
	beadIndex = path.next(headBead);
	while (!all(beadIndex==tailBead)) {
		beadIndex = path.delBeadGetNext(beadIndex);
	} 

	/* Update all the properties of the worm */
	path.worm.update(path,headBead,tailBead);

	/* The configuration with a worm is not diagonal */
 	path.worm.isConfigDiagonal = false;

	printMoveState("Opened up a worm.");
	success = true;
}

/*************************************************************************//**
 * Undo the open move, restoring the beads and links.
******************************************************************************/
void OpenMove::undoMove() {

	/* Reset the worm parameters */
	path.worm.reset();
 	path.worm.isConfigDiagonal = true;
	
	printMoveState("Failed to open up a worm.");
	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// CLOSE MOVE CLASS ----------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
CloseMove::CloseMove (Path &_path, ActionBase *_actionPtr, 
		MTRand &_random, string _name, ensemble _operateOnConfig) : 
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zero */
	numAccepted = numAttempted = numToMove = 0;

	/* Initialize the acceptance by level counters */
	numAcceptedLevel.resize(constants()->b()+1);
	numAttemptedLevel.resize(constants()->b()+1);
	numAcceptedLevel  = 0;
	numAttemptedLevel = 0;
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
CloseMove::~CloseMove() {
	oldBeadOn.free();
}

/*************************************************************************//**
 * Perform a close move.
 * 
 * We attempt to close up the world line configuration if a worm 
 * is already present. This consists of both filling in the beadOn 
 * array as well as generating new positions for the particle beads. 
 * After a sucessful close, we update the number of particles. 
******************************************************************************/
bool CloseMove::attemptMove() {

	success = false;
	/* We first make sure we are in an off-diagonal configuration, and that that
	 * gap is neither too large or too small and that the worm cost is reasonable.
	 * Otherwise, we simply exit the move */
    if ( (path.worm.gap > constants()->Mbar()) || (path.worm.gap == 0)
            || path.worm.tooCostly() )
		return false;

    checkMove(0,0.0);

	/* Otherwise, proceed with the close move */
	numLevels = int (ceil(log(1.0*path.worm.gap) / log(2.0)-EPS));

	/* Increment the number of close moves and the total number of moves */
	numAttempted++;
	numAttemptedLevel(numLevels)++;
	totAttempted++;

	/* Get the head and new 'tail' slices for the worldline length
	 * to be closed.  These beads are left untouched */
	headBead = path.worm.head;
	tailBead = path.worm.tail;

	/* Compute the part of the acceptance probability that does not
	 * depend on the change in potential energy */
	double norm = actionPtr->rho0(path.worm.head,path.worm.tail,path.worm.gap) /  
		(constants()->C() * constants()->Mbar() * 
         (path.worm.getNumBeadsOn() + path.worm.gap - 1));

	/* We rescale to take into account different attempt probabilities */
	norm *= constants()->attemptProb("open")/constants()->attemptProb("close");

	/* Weight for ensemble */
	norm *= actionPtr->ensembleWeight(path.worm.gap-1);

	/* The change in the number sector */
	double muShift = path.worm.gap*constants()->mu()*constants()->tau();

    /* If we have a local action, perform single slice updates */
    if (actionPtr->local) {

        double actionShift = (log(norm) + muShift)/path.worm.gap;

        double deltaAction = 0.0;
        double PNorm = 1.0;
        double P;

        /* Compute the potential action for the new trajectory, inclucing a piece
         * coming from the head and tail. */
        beadLocator beadIndex;
        beadIndex = path.worm.head;

        deltaAction += actionPtr->barePotentialAction(beadIndex) - 0.5*actionShift;
        P = min(exp(-deltaAction)/PNorm,1.0);

        /* We perform a metropolis test on the tail bead */
        if ( random.rand() >= P ) {
            undoMove();
            return success;
        }
        PNorm *= P;

        for (int k = 0; k < (path.worm.gap-1); k++) {
            beadIndex = path.addNextBead(beadIndex,
                    newStagingPosition(beadIndex,path.worm.tail,path.worm.gap,k));
            deltaAction += actionPtr->barePotentialAction(beadIndex) - actionShift;
            P = min(exp(-deltaAction)/PNorm,1.0);

            /* We perform a metropolis test on the single bead */
            if ( random.rand() >= P ) {
                undoMove();
                return success;
            }
            PNorm *= P;
        }
        path.next(beadIndex) = path.worm.tail;
        path.prev(path.worm.tail) = beadIndex;

        /* If we have made it this far, we compute the total action as well as the
         * action correction */
        deltaAction += actionPtr->barePotentialAction(path.worm.tail) - 0.5*actionShift;
        deltaAction += actionPtr->potentialActionCorrection(path.worm.head,path.worm.tail);

        /* Perform the metropolis test */
        if ( random.rand() < (exp(-deltaAction)/PNorm) ) {
            keepMove();
            checkMove(1,deltaAction + log(norm) + muShift);
        }
        else {
            undoMove();
            checkMove(2,0);
        }
    }
    /* Otherwise, perform a full trajectory update */
    else
    {
        /* Generate a new new trajectory */
        beadLocator beadIndex;
        beadIndex = path.worm.head;
        for (int k = 0; k < (path.worm.gap-1); k++) {
            beadIndex = path.addNextBead(beadIndex,
                    newStagingPosition(beadIndex,path.worm.tail,path.worm.gap,k));
        }
        path.next(beadIndex) = path.worm.tail;
        path.prev(path.worm.tail) = beadIndex;

        /* Compute the action for the new trajectory */
        newAction = actionPtr->potentialAction(headBead,tailBead);

        /* Perform the metropolis test */
        if ( random.rand() < norm*exp(-newAction + muShift) )  {
            keepMove();
            checkMove(1,newAction);
        }
        else {
            undoMove();
            checkMove(2,0.0);
        }
    
    }

	return success;
}

/*************************************************************************//**
 * We preserve the move, update the acceptance ratios and fully close the
 * worm.
******************************************************************************/
void CloseMove::keepMove() {

	/* update the acceptance counters */
	numAccepted++;
	numAcceptedLevel(numLevels)++;
	totAccepted++;

	/* Update all the properties of the closed worm and our newly
	 * diagonal configuration. */
	path.worm.reset();
 	path.worm.isConfigDiagonal = true;
	
	printMoveState("Closed up a worm.");
	success = true;
}

/*************************************************************************//**
 * We undo the close move, restoring the worm and gap.
******************************************************************************/
void CloseMove::undoMove() {

	/* Delete all the beads that were added. */
	beadLocator beadIndex;
	beadIndex = path.next(path.worm.head);
	while (!all(beadIndex==path.worm.tail) && (!all(beadIndex==XXX)))
		beadIndex = path.delBeadGetNext(beadIndex);

	path.next(path.worm.head) = XXX;
	path.prev(path.worm.tail) = XXX;

	/* Make sure we register the off-diagonal configuration */
 	path.worm.isConfigDiagonal = false;
	
	printMoveState("Failed to close up a worm.");
	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// INSERT MOVE CLASS ---------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
InsertMove::InsertMove (Path &_path, ActionBase *_actionPtr, MTRand &_random, 
        string _name, ensemble _operateOnConfig) : 
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zero */
	numAccepted = numAttempted = numToMove = 0;

	/* Initialize the acceptance by level counters */
	numAcceptedLevel.resize(constants()->b()+1);
	numAttemptedLevel.resize(constants()->b()+1);
	numAcceptedLevel  = 0;
	numAttemptedLevel = 0;
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
InsertMove::~InsertMove() {
}

/*************************************************************************//**
 * Perform an insert move.
 * 
 * Attempt to insert a worm, which acts like a new worldline without
 * periodic boundary conditions.  It is only possible if we are already
 * in a diagonal configuration.  We have to be careful to properly grow
 * our bead and link arrays. The actual number of particles doesn't increase,
 * just the number of active worldlines.
******************************************************************************/
bool InsertMove::attemptMove() {

    /* Get the length of the proposed worm to insert */
    wormLength = 2*(1 + random.randInt(constants()->Mbar()/2-1));
    numLevels = int(ceil(log(1.0*wormLength) / log(2.0)-EPS));

    checkMove(0,0.0);

    /* Increment the number of insert moves and the total number of moves */
    numAttempted++;
    totAttempted++;
    numAttemptedLevel(numLevels)++;

    /* Move normalization factor */
    double norm = constants()->C() * constants()->Mbar() * path.numTimeSlices 
        * path.boxPtr->volume;
    double muShift = wormLength*constants()->tau()*constants()->mu();
    
    /* We rescale to take into account different attempt probabilities */
    norm *= constants()->attemptProb("remove") / constants()->attemptProb("insert");

    /* Weight for ensemble */
    norm *= actionPtr->ensembleWeight(wormLength);

    /* We pick a random tail slice, and add a new bead */
    int slice = 2*(random.randInt(constants()->numTimeSlices()/2-1));

    /* Choose a random position inside the box for the proposed tail */
    tailBead = path.addBead(slice,path.boxPtr->randPosition(random));
    path.worm.special2 = tailBead;

    /* If we have a local action, perform a single slice rejection move */
    if (actionPtr->local) {

        double actionShift = (log(norm) + muShift)/wormLength;

        double deltaAction = 0.0;
        double PNorm = 1.0;
        double P;

        /* Generate the action for the proposed worm */
        beadLocator beadIndex;
        beadIndex = tailBead;
        deltaAction += actionPtr->barePotentialAction(beadIndex) - 0.5*actionShift;
        P = min(exp(-deltaAction)/PNorm,1.0);

        /* We perform a metropolis test on the tail bead */
        if ( random.rand() >= P ) {
            undoMove();
            return success;
        }
        PNorm *= P;

        /* Now go through the non head/tail beads */
        for (int k = 1; k < wormLength; k++) {

            beadIndex = path.addNextBead(beadIndex,newFreeParticlePosition(beadIndex));
            deltaAction += actionPtr->barePotentialAction(beadIndex) - actionShift;
            P = min(exp(-deltaAction)/PNorm,1.0);

            /* We perform a metropolis test on the single bead */
            if ( random.rand() >= P ) {
                undoMove();
                return success;
            }
            PNorm *= P;
        }
        headBead = path.addNextBead(beadIndex,newFreeParticlePosition(beadIndex));
        path.worm.special1 = headBead;

        /* If we have made it this far, we compute the total action as well as the
         * action correction */
        deltaAction += actionPtr->barePotentialAction(headBead) - 0.5*actionShift;
        deltaAction += actionPtr->potentialActionCorrection(tailBead,headBead);

        /* Perform a final Metropolis test for inserting the full worm*/
        if ( random.rand() < (exp(-deltaAction)/PNorm) ) {
            keepMove();
            checkMove(1,deltaAction + actionShift*wormLength);
        }
        else {
            undoMove();
            checkMove(2,0.0);
        }
    }
    /* Otherwise, perform a full trajectory move */
    else {

        /* Generate the path for the proposed worm, setting the new head as special */
        beadLocator beadIndex;
        beadIndex = tailBead;
        for (int k = 0; k < wormLength; k++) 
            beadIndex = path.addNextBead(beadIndex,newFreeParticlePosition(beadIndex));
        headBead = beadIndex;
        path.worm.special1 = headBead;

        /* Compute the new path action */
        newAction = actionPtr->potentialAction(tailBead,headBead);

        /* Perform the metropolis test */
        if ( random.rand() < norm*exp(-newAction + muShift) ) {
            keepMove();
            checkMove(1,newAction);
        }
        else {
            undoMove();
            checkMove(2,0.0);
        }
    }

	return success;
}

/*************************************************************************//**
 *  We preserve the insert move, update the acceptance ratios and the worm
 *  location.
******************************************************************************/
void InsertMove::keepMove() {

	/* update the acceptance counters */
	numAccepted++;
	totAccepted++;
	numAcceptedLevel(numLevels)++;

	/* Update all the properties of the inserted worm */
	path.worm.update(path,headBead,tailBead);

	/* The configuration with a worm is off-diagonal */
 	path.worm.isConfigDiagonal = false;

	printMoveState("Inserted a worm.");
	success = true;
}

/*************************************************************************//**
 * We undo the insert move, restoring the original worldlines.
******************************************************************************/
void InsertMove::undoMove() {

	/* To undo an insert, we simply remove all the beads that we have
	 * added and reset the worm state.*/
	beadLocator beadIndex;
	beadIndex = tailBead;
	do {
		beadIndex = path.delBeadGetNext(beadIndex);
	} while (!all(beadIndex==XXX));

	path.worm.reset();

	/* Reset the configuration to diagonal */
 	path.worm.isConfigDiagonal = true;

	printMoveState("Failed to insert a worm.");
	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// REMOVE MOVE CLASS ---------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
RemoveMove::RemoveMove (Path &_path, ActionBase *_actionPtr, MTRand &_random, 
        string _name, ensemble _operateOnConfig) : 
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zero */
	numAccepted = numAttempted = numToMove = 0;

	/* Initialize the acceptance by level counters */
	numAcceptedLevel.resize(constants()->b()+1);
	numAttemptedLevel.resize(constants()->b()+1);
	numAcceptedLevel  = 0;
	numAttemptedLevel = 0;
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
RemoveMove::~RemoveMove() {
}

/*************************************************************************//**
 * Perform a remove move.
 * 
 * Attempt to remove a worm thus restoring a diagonal configuration.
 * It is only possible if we are already have an off-diagonal configuration.  
 * We have to be careful to properly shrink our bead and link arrays.  Again
 * the number of true particles doesn't change here, just the number of
 * active worldlines.
******************************************************************************/
bool RemoveMove::attemptMove() {

	/* We first make sure we are in an off-diagonal configuration, and the worm isn't
	 * too short or long, also that we don't remove our last particle */
    if ( (path.worm.length > constants()->Mbar()) || (path.worm.length < 1)
			|| (path.getTrueNumParticles() < 1) ) 
        return false;

    numLevels = int(ceil(log(1.0*path.worm.length) / log(2.0)-EPS));

    checkMove(0,0.0);

    /* Increment the number of insert moves and the total number of moves */
    numAttempted++;
    numAttemptedLevel(numLevels)++;
    totAttempted++;

    /* The normalization constant and action shift due to the chemical potential */
    double norm = 1.0 / (constants()->C() * constants()->Mbar() * path.numTimeSlices 
            * path.boxPtr->volume);
    double muShift = path.worm.length * constants()->mu() * constants()->tau();
    
    /* We rescale to take into account different attempt probabilities */
    norm *= constants()->attemptProb("insert") / constants()->attemptProb("remove");

    /* Weight for ensemble */
    norm *= actionPtr->ensembleWeight(-path.worm.length);

    /* If we have a local action, perform a single slice rejection move */
    if (actionPtr->local) {

        double actionShift = (-log(norm) + muShift)/path.worm.length;

        oldAction = 0.0;
        double deltaAction = 0.0;
        double PNorm = 1.0;
        double P;

        /* First do the head */
        beadLocator beadIndex;
        beadIndex = path.worm.head;
        double factor = 0.5;
        do {
            deltaAction -= actionPtr->barePotentialAction(beadIndex) - factor*actionShift;
            P = min(exp(-deltaAction)/PNorm,1.0);

            /* We do a single slice Metropolis test and exit the move if we
             * wouldn't remove the single bead */
            if ( random.rand() >= P ) {
                undoMove();
                return success;
            }
            PNorm *= P;

            factor = 1.0; 
            beadIndex = path.prev(beadIndex);
        } while (!all(beadIndex==path.worm.tail));

        /* Add the part from the tail */
        deltaAction -= actionPtr->barePotentialAction(path.worm.tail) - 0.5*actionShift;
        deltaAction -= actionPtr->potentialActionCorrection(path.worm.tail,path.worm.head);

        /* Perform final metropolis test */
        if ( random.rand() < (exp(-deltaAction)/PNorm) ) {
            keepMove();
            checkMove(1,deltaAction + log(norm) - muShift);
        }
        else {
            undoMove();
            checkMove(2,0.0);
        }
    }
    /* otherwise, perform a full trajectory update */
    else {
        /* Compute the potential action of the path */
        oldAction = actionPtr->potentialAction(path.worm.tail,path.worm.head);

        /* Perform the metropolis test */
        if ( random.rand() < norm*exp(oldAction - muShift) ) {
            keepMove();
            checkMove(1,-oldAction);
        }

        else {
            undoMove();
            checkMove(2,0.0);
        }
    }

	return success;
}

/*************************************************************************//**
 * We preserve the remove move, update the acceptance ratios and shrink
 * the data sets.
******************************************************************************/
void RemoveMove::keepMove() {

	/* update the acceptance counters */
	numAccepted++;
	numAcceptedLevel(numLevels)++;
	totAccepted++;

	printMoveState("About to remove a worm.");

	/* We delete the worm from our data sets and reset all
	 * worm properties.  */
	beadLocator beadIndex;
	beadIndex = path.worm.head;
	do {
		beadIndex = path.delBeadGetPrev(beadIndex);
	} while (!all(beadIndex==XXX));

	path.worm.reset();

	/* The configuration without a worm is diagonal */
 	path.worm.isConfigDiagonal = true;

	printMoveState("Removed a worm.");
	success = true;
}

/*************************************************************************//**
 *  We undo the remove move, but since we haven't actually touched the 
 *  wordlines, we don't have to do anything here.
******************************************************************************/
void RemoveMove::undoMove() {

	/* Reset the configuration to off-diagonal */
 	path.worm.isConfigDiagonal = false;

	printMoveState("Failed to remove a worm.");
	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ADVANCE HEAD MOVE CLASS ---------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
AdvanceHeadMove::AdvanceHeadMove (Path &_path, ActionBase *_actionPtr, 
		MTRand &_random, string _name, ensemble _operateOnConfig) : 
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zero */
	numAccepted = numAttempted = numToMove = 0;

	/* Initialize the acceptance by level counters */
	numAcceptedLevel.resize(constants()->b()+1);
	numAttemptedLevel.resize(constants()->b()+1);
	numAcceptedLevel  = 0;
	numAttemptedLevel = 0;
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
AdvanceHeadMove::~AdvanceHeadMove() {
}

/*************************************************************************//**
 * Perform an advance head move.
 * 
 * Attempt to advance a worm head in imaginary time by a random number
 * of slices.  We generate the new bead positions from the free particle
 * density matrix.  It is only possible if we already have an off-diagonal 
 * configuration.  
******************************************************************************/
bool AdvanceHeadMove::attemptMove() {

	success = false;

    /* Get the length of the proposed advancement by computing
     * a random number of levels to be used in the bisection algorithm. */
    advanceLength = 2*(1 + random.randInt(constants()->Mbar()/2-1));
    numLevels = int (ceil(log(1.0*advanceLength) / log(2.0)-EPS));

    checkMove(0,0.0);

    /* Increment the number of advance moves and the total number of moves */
    numAttempted++;
    numAttemptedLevel(numLevels)++;
    totAttempted++;

    double muShift = advanceLength*constants()->tau()*constants()->mu();
    double norm = constants()->attemptProb("recede head") / 
        constants()->attemptProb("advance head");

    /* Weight for ensemble */
    norm *= actionPtr->ensembleWeight(advanceLength);

    /* Make the old head a special bead, and undefine the head */
    path.worm.special1 = path.worm.head;
    path.worm.head = XXX;

    /* If we have a local action, perform a single slice rejection move */
    if (actionPtr->local) {

        double actionShift = (log(norm) + muShift)/advanceLength;

        double deltaAction = 0.0;
        double PNorm = 1.0;
        double P;

        /* Generate the new path, and compute its action, assigning the new head */
        beadLocator beadIndex;
        beadIndex = path.worm.special1;
        deltaAction = actionPtr->barePotentialAction(beadIndex) - 0.5*actionShift;

        P = min(exp(-deltaAction)/PNorm,1.0);
        if ( random.rand() >= P ) {
            undoMove();
            return success;
        }
        PNorm *= P;

        for (int k = 0; k < (advanceLength-1); k++) {
            beadIndex = path.addNextBead(beadIndex,newFreeParticlePosition(beadIndex));
            deltaAction += actionPtr->barePotentialAction(beadIndex) - actionShift;

            P = min(exp(-deltaAction)/PNorm,1.0);

            /* We perform a metropolis test on the single bead */
            if ( random.rand() >= P ) {
                undoMove();
                return success;
            }
            PNorm *= P;
        }
        headBead = path.addNextBead(beadIndex,newFreeParticlePosition(beadIndex));

        /* Assign the new head and compute its action */
        path.worm.head = headBead;
        deltaAction += actionPtr->potentialAction(headBead) - 0.5*actionShift;
        deltaAction += actionPtr->potentialActionCorrection(path.worm.special1,path.worm.head);

        /* Perform the metropolis test */
        if ( random.rand() < (exp(-deltaAction)/PNorm)) {
            keepMove();
            checkMove(1,deltaAction + advanceLength*actionShift);
        }
        else { 
            undoMove();
            checkMove(2,0.0);
        }
    }
    /* Otherwise, perform a full trajctory update */
    else {

        /* Generate the new path, assigning the new head */
        beadLocator beadIndex;
        beadIndex = path.worm.special1;
        for (int k = 0; k < advanceLength; k++)
            beadIndex = path.addNextBead(beadIndex,newFreeParticlePosition(beadIndex));
        headBead = beadIndex;
        path.worm.head = headBead;

        /* Compute the action for the updated path */
        newAction = actionPtr->potentialAction(path.worm.special1,headBead);

        /* Perform the metropolis test */
        if ( random.rand() < norm*exp(-newAction + muShift) ) {
            keepMove();
            checkMove(1,newAction);
        }
        else {
            undoMove();
            checkMove(2,0.0);
        }
    }

	return success;
}

/*************************************************************************//**
 * We preserve the advance head move, update the acceptance ratios and the worm
 * coordinates.
******************************************************************************/
void AdvanceHeadMove::keepMove() {
	
	/* update the acceptance counters */
	numAccepted++;
	totAccepted++;
	numAcceptedLevel(numLevels)++;

	/* Update all the properties of the inserted worm */
	path.worm.update(path,headBead,path.worm.tail);

	/* The configuration with a worm is off-diagonal */
 	path.worm.isConfigDiagonal = false;

	printMoveState("Advanced a worm.");
	success = true;
}

/*************************************************************************//**
 * We undo the advance move, turning off the re-initialized beads and making
 * sure to restore the orginal worm.
******************************************************************************/
void AdvanceHeadMove::undoMove() {

	/* Copy back the head bead */
	path.worm.head = path.worm.special1;

	/* We remove all the beads and links that have been added. */
	beadLocator beadIndex;
	beadIndex = path.next(path.worm.head);
    while (!all(beadIndex==XXX))
        beadIndex = path.delBeadGetNext(beadIndex);
    path.next(path.worm.head) = XXX;

	/* Reset the configuration to off-diagonal */
 	path.worm.isConfigDiagonal = false;

	/* Unset the special marker */
	path.worm.special1 = XXX;

	printMoveState("Failed to advance a worm.");
	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// ADVANCE TAIL MOVE CLASS ---------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
AdvanceTailMove::AdvanceTailMove (Path &_path, ActionBase *_actionPtr, 
		MTRand &_random, string _name, ensemble _operateOnConfig) : 
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zero */
	numAccepted = numAttempted = numToMove = 0;

	/* Initialize the acceptance by level counters */
	numAcceptedLevel.resize(constants()->b()+1);
	numAttemptedLevel.resize(constants()->b()+1);
	numAcceptedLevel  = 0;
	numAttemptedLevel = 0;
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
AdvanceTailMove::~AdvanceTailMove() {
}

/*************************************************************************//**
 * Perform an advance tail move.
 * 
 * Here we attempt to advance the tail of a worm in imaginary time by a 
 * random number of slices. This is accomplished by removing beads and the
 * result is a shorter worm.  It is only possible if we already have an 
 * off-diagonal configuration.  
******************************************************************************/
bool AdvanceTailMove::attemptMove() {

	success = false;

    /* Get the number of time slices we will try to shrink the worm by */
    advanceLength = 2*(1 + random.randInt(constants()->Mbar()/2-1));
    numLevels = int (ceil(log(1.0*advanceLength) / log(2.0)-EPS));

    /* We now make sure that this is shorter than the length of the worm, 
     * otherewise we reject the move immediatly */
    if (advanceLength < path.worm.length) {

        /* The proposed new tail */
        tailBead = path.next(path.worm.tail,advanceLength);

        /* The action shift due to the chemical potential */
        double muShift = advanceLength*constants()->tau()*constants()->mu();

        double norm = constants()->attemptProb("recede tail") / 
            constants()->attemptProb("advance tail");

        /* Weight for ensemble */
        norm *= actionPtr->ensembleWeight(-advanceLength);

        checkMove(0,0.0);

        /* Increment the number of recede moves and the total number of moves */
        numAttempted++;
        numAttemptedLevel(numLevels)++;
        totAttempted++;

        /* Mark the proposed tail as special */
        path.worm.special1 = tailBead;

        /* If we have a local action, perform a single slice rejection move */
        if (actionPtr->local) {

            double actionShift = (-log(norm) + muShift)/advanceLength;

            oldAction = 0.0;
            double deltaAction = 0.0;
            double PNorm = 1.0;
            double P;
            double factor = 0.5;

            beadLocator beadIndex;
            beadIndex = path.worm.tail;
            do {
                deltaAction -= actionPtr->barePotentialAction(beadIndex) - factor*actionShift;
                P = min(exp(-deltaAction)/PNorm,1.0);

                /* We do a single slice Metropolis test and exit the move if we
                 * wouldn't remove the single bead */
                if ( random.rand() >= P ) {
                    undoMove();
                    return success;
                }
                PNorm *= P;

                factor = 1.0; 
                beadIndex = path.next(beadIndex);
            } while (!all(beadIndex==tailBead));

            /* Add the part from the tail */
            deltaAction -= actionPtr->barePotentialAction(beadIndex) - 0.5*actionShift;
            deltaAction -= actionPtr->potentialActionCorrection(path.worm.tail,tailBead);

            /* Perform final metropolis test */
            if ( random.rand() < (exp(-deltaAction)/PNorm) ) {
                keepMove();
                checkMove(1,deltaAction - advanceLength*actionShift);
            }
            else {
                undoMove();
                checkMove(2,0.0);
            }
        }
        /* Otherwise, perform a full trajectory update */
        else {
            /* Compute the old action for the path to be removed */
            oldAction = actionPtr->potentialAction(path.worm.tail,tailBead);

            /* Perform the metropolis test */
            if ( random.rand() < norm*exp(oldAction - muShift) )  {
                keepMove();
                checkMove(1,-oldAction);
            }
            else {
                undoMove();
                checkMove(2,0.0);
            }
        }

    } // advanceLength

	return success;
}

/*************************************************************************//**
 * We preserve the advance tail move by deleting the appropriate links and
 * beads and updating the properties of the worm.
******************************************************************************/
void AdvanceTailMove::keepMove() {

	/* update the acceptance counters */
	numAccepted++;
	totAccepted++;
	numAcceptedLevel(numLevels)++;

	/* Delete beads and links */
	beadLocator beadIndex;
	beadIndex = path.prev(tailBead);
	do {
		beadIndex = path.delBeadGetPrev(beadIndex);
	} while (!all(beadIndex==XXX));

	/* Update all the changed properties of the inserted worm */
	path.worm.update(path,path.worm.head,tailBead);

	/* The configuration is still off-diagonal */
 	path.worm.isConfigDiagonal = false;

	printMoveState("Advanced a worm tail.");
	success = true;
}

/*************************************************************************//**
 * We undo the advance tail move, but since we haven't actually touched the 
 * wordlines, we don't have to do anything here.
******************************************************************************/
void AdvanceTailMove::undoMove() {

	/* Make sure the configuration is still off-diagonal */
 	path.worm.isConfigDiagonal = false;

	/* Unset the special marker */
	path.worm.special1 = XXX;

	printMoveState("Failed to advance a worm tail.");
	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// RECEDE HEAD MOVE CLASS ----------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
RecedeHeadMove::RecedeHeadMove (Path &_path, ActionBase *_actionPtr, 
		MTRand &_random, string _name, ensemble _operateOnConfig) : 
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zero */
	numAccepted = numAttempted = numToMove = 0;

	/* Initialize the acceptance by level counters */
	numAcceptedLevel.resize(constants()->b()+1);
	numAttemptedLevel.resize(constants()->b()+1);
	numAcceptedLevel  = 0;
	numAttemptedLevel = 0;
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
RecedeHeadMove::~RecedeHeadMove() {
}

/*************************************************************************//**
 * Perform a recede head move.
 * 
 * Attempt to propagate a worm backwards in imaginary time by
 * randomly selecting a number of links then attempting to remove them.
 * The number of true particles doesn't change here.
******************************************************************************/
bool RecedeHeadMove::attemptMove() {

	success = false;

    /* Get the number of time slices we will try to shrink the worm by */
    recedeLength = 2*(1 + random.randInt(constants()->Mbar()/2-1));
    numLevels = int (ceil(log(1.0*recedeLength) / log(2.0)-EPS));

    /* We now make sure that this is shorter than the length of the worm, 
     * otherewise we reject the move immediatly */
    if (recedeLength < path.worm.length) {

        /* The proposed new head */
        headBead = path.prev(path.worm.head,recedeLength);

        /* The action shift due to the chemical potential */
        double muShift = recedeLength*constants()->tau()*constants()->mu();

        double norm = constants()->attemptProb("advance head") / 
            constants()->attemptProb("recede head");

        /* Weight for ensemble */
        norm *= actionPtr->ensembleWeight(-recedeLength);

        checkMove(0,0.0);

        /* Increment the number of recede moves and the total number of moves */
        numAttempted++;
        numAttemptedLevel(numLevels)++;
        totAttempted++;

        /* Set the proposed head as special */
        path.worm.special1 = headBead;

        /* If we have a local action, perform a single slice rejection move */
        if (actionPtr->local) {

            double actionShift = (-log(norm) + muShift)/recedeLength;

            oldAction = 0.0;

            double deltaAction = 0.0;
            double PNorm = 1.0;
            double P;
            double factor = 0.5;

            beadLocator beadIndex;
            beadIndex = path.worm.head;
            do {
                deltaAction -= actionPtr->barePotentialAction(beadIndex) - factor*actionShift;
                P = min(exp(-deltaAction)/PNorm,1.0);
                if ( random.rand() >= P ) {
                    undoMove();
                    return success;
                }
                PNorm *= P;

                factor = 1.0; 
                beadIndex = path.prev(beadIndex);
            } while (!all(beadIndex==headBead));

            deltaAction -= actionPtr->barePotentialAction(headBead) - 0.5*actionShift;
            deltaAction -= actionPtr->potentialActionCorrection(path.worm.special1,path.worm.head);

            /* Perform final metropolis test */
            if ( random.rand() < (exp(-deltaAction)/PNorm) ) {
                keepMove();
                checkMove(1,deltaAction - recedeLength*actionShift);
            }
            else {
                undoMove();
                checkMove(2,0.0);
            }
        }
        /* Otherwise, perform a full trajectory update */
        else {
            /* Get the original action for the path */
            oldAction = actionPtr->potentialAction(headBead,path.worm.head);

            /* Perform the metropolis test */
            if ( random.rand() < norm*exp(oldAction - muShift) ) {
                keepMove();
                checkMove(1,-oldAction);
            }
            else {
                undoMove();
                checkMove(2,0.0);
            }
        }

    } // recedeLength

	return success;
}

/*************************************************************************//**
 * We preserve the recede move by deleting the appropriate links and
 * particles and updating the properties of the worm.
******************************************************************************/
void RecedeHeadMove::keepMove() {

	/* update the acceptance counters */
	numAccepted++;
	totAccepted++;
	numAcceptedLevel(numLevels)++;

	/* Delete beads and links */
	beadLocator beadIndex;
	beadIndex = path.next(headBead);
	do {
		beadIndex = path.delBeadGetNext(beadIndex);
	} while (!all(beadIndex==XXX));

	/* Update all the changed properties of the inserted worm */
	path.worm.update(path,headBead,path.worm.tail);

	/* The configuration is still off-diagonal */
 	path.worm.isConfigDiagonal = false;

	printMoveState("Receded a worm head.");
	success = true;
}

/*************************************************************************//**
 * We undo the recede move, but since we haven't actually touched the 
 * wordlines, we don't have to do anything here.
******************************************************************************/
void RecedeHeadMove::undoMove() {

	/* Make sure the configuration is still off-diagonal */
 	path.worm.isConfigDiagonal = false;

	/* Unset the special mark */
	path.worm.special1 = XXX;

	printMoveState("Failed to recede a worm head.");
	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// RECEDE TAIL MOVE CLASS ----------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
RecedeTailMove::RecedeTailMove (Path &_path, ActionBase *_actionPtr, 
		MTRand &_random, string _name, ensemble _operateOnConfig) : 
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zero */
	numAccepted = numAttempted = numToMove = 0;

	/* Initialize the acceptance by level counters */
	numAcceptedLevel.resize(constants()->b()+1);
	numAttemptedLevel.resize(constants()->b()+1);
	numAcceptedLevel  = 0;
	numAttemptedLevel = 0;
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
RecedeTailMove::~RecedeTailMove() {
}

/*************************************************************************//**
 * Perform a recede tail move.
 * 
 * Attempt to propagate a worm tail backwards in imaginary time by
 * randomly selecting a number of links then attempting to generate new positions
 * which exactly sample the free particle density matrix.
******************************************************************************/
bool RecedeTailMove::attemptMove() {

	success = false;

    /* Get the length of the proposed advancement by computing
     * a random number of levels to be used in the bisection algorithm. */
    recedeLength = 2*(1 + random.randInt(constants()->Mbar()/2-1));
    numLevels = int (ceil(log(1.0*recedeLength) / log(2.0)-EPS));

    checkMove(0,0.0);

    /* Increment the number of advance moves and the total number of moves */
    numAttempted++;
    numAttemptedLevel(numLevels)++;
    totAttempted++;

    double muShift = recedeLength*constants()->tau()*constants()->mu();
    double norm = constants()->attemptProb("advance tail") / 
        constants()->attemptProb("recede tail");

    /* Weight for ensemble */
    norm *= actionPtr->ensembleWeight(recedeLength);

    /* Make the current tail special, and undefine the tail */
    path.worm.special1 = path.worm.tail;
    path.worm.tail = XXX;

    /* If we have a local action, perform a single slice rejection move */
    if (actionPtr->local) {

        double actionShift = (log(norm) + muShift)/recedeLength;
        double deltaAction = 0.0;
        double PNorm = 1.0;
        double P;

        /* Compute the action for the proposed path, and assign the new tail */
        beadLocator beadIndex;
        beadIndex = path.worm.special1;

        deltaAction += actionPtr->barePotentialAction(beadIndex) - 0.5*actionShift;
        P = min(exp(-deltaAction)/PNorm,1.0);

        /* We perform a metropolis test on the tail bead */
        if ( random.rand() >= P ) {
            undoMove();
            return success;
        }
        PNorm *= P;

        for (int k = 0; k < (recedeLength-1); k++) {
            beadIndex = path.addPrevBead(beadIndex,newFreeParticlePosition(beadIndex));
            deltaAction += actionPtr->barePotentialAction(beadIndex) - actionShift;
            P = min(exp(-deltaAction)/PNorm,1.0);

            /* We perform a metropolis test on the single bead */
            if ( random.rand() >= P ) {
                undoMove();
                return success;
            }
            PNorm *= P;
        }
        tailBead = path.addPrevBead(beadIndex,newFreeParticlePosition(beadIndex));

        /* Assign the new tail bead and compute its action */
        path.worm.tail = tailBead;
        deltaAction += actionPtr->barePotentialAction(tailBead) - 0.5*actionShift;
        deltaAction += actionPtr->potentialActionCorrection(tailBead,path.worm.special1);

        /* Perform the metropolis test */
        if ( random.rand() < (exp(-deltaAction)/PNorm)) {
            keepMove();
            checkMove(1,deltaAction + recedeLength*actionShift);
        }
        else {
            undoMove();
            checkMove(2,0.0);
        }
    }
    /* Otherwise, we perform a full trajectory updates */
    else {
        /* Generate the new path, assigning the new tail */
        beadLocator beadIndex;
        beadIndex = path.worm.special1;
        for (int k = 0; k < recedeLength; k++)
            beadIndex = path.addPrevBead(beadIndex,newFreeParticlePosition(beadIndex));
        tailBead = beadIndex;
        path.worm.tail = tailBead;

        /* Get the action for the proposed path */
        newAction = actionPtr->potentialAction(tailBead,path.worm.special1);

        /* Perform the metropolis test */
        if ( random.rand() < norm*exp(-newAction + muShift)) {
            keepMove();
            checkMove(1,newAction);
        }
        else {
            undoMove();
            checkMove(2,0.0);
        }
    }

	return success;
}

/*************************************************************************//**
 * We preserve the recede tail move, update the acceptance ratios and the worm
 * coordinates.
******************************************************************************/
void RecedeTailMove::keepMove() {
	
	/* update the acceptance counters */
	numAccepted++;
	totAccepted++;
	numAcceptedLevel(numLevels)++;

	/* Update all the properties of the inserted worm */
	path.worm.update(path,path.worm.head,tailBead);

	/* The configuration with a worm is off-diagonal */
 	path.worm.isConfigDiagonal = false;

	printMoveState("Receded a worm tail.");
	success = true;
}

/*************************************************************************//**
 * We undo the recede tail move, turning off the re-initialized beads and making
 * sure to restore the orginal worm.
******************************************************************************/
void RecedeTailMove::undoMove() {

	/* Copy back the tail bead */
	path.worm.tail = path.worm.special1;

	/* We remove all the beads and links that have been added. */
	beadLocator beadIndex;
	beadIndex = path.prev(path.worm.tail);
	while (!all(beadIndex==XXX)) 
        beadIndex = path.delBeadGetPrev(beadIndex);
	path.prev(path.worm.tail) = XXX;

	/* Reset the configuration to off-diagonal */
 	path.worm.isConfigDiagonal = false;

	/* Unset the special mark */
	path.worm.special1 = XXX;

	printMoveState("Failed to recede a worm tail.");
	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// SWAP MOVE BASE CLASS ------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
SwapMoveBase::SwapMoveBase (Path &_path, ActionBase *_actionPtr, MTRand &_random,
        string _name, ensemble _operateOnConfig) : 
    MoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
SwapMoveBase::~SwapMoveBase() {
}

/*************************************************************************//**
 * Get the normalization constant for a swap move.
 * 
 * We compute the normalization constant used in both the pivot selection
 * probability as well as the overall acceptance probabilty.  
 * @see Eq. (2.23) of PRE 74, 036701 (2006).
******************************************************************************/
double SwapMoveBase::getNorm(const beadLocator &beadIndex) {

	double Sigma = 0.0;
	double crho0;

	/* We sum up the free particle density matrices for each bead
	 * in the list */
	Sigma = actionPtr->rho0(beadIndex,path.lookup.fullBeadList(0),swapLength);
	cumulant.at(0) = Sigma;
	for (int n = 1; n < path.lookup.fullNumBeads; n++) {
		crho0 = actionPtr->rho0(beadIndex,path.lookup.fullBeadList(n),swapLength);
		Sigma += crho0;
		cumulant.at(n) = cumulant.at(n-1) + crho0;
	}

	/* Normalize the cumulant */
	for (int n = 0; n < path.lookup.fullNumBeads; n++)
        cumulant.at(n) /= Sigma;


	return Sigma;
}

/*************************************************************************//**
 * Select the pivot bead for a swap move.
 * 
 * Here we select a pivot bead from a list with the probability given by 
 * Eq. (2.22) of PRE 74, 036701 (2006).  We use the trick in Ceperly's 
 * lecture notes where we evaluate the cumulative distribution function
 * then generate a uniform random variable and find where it lies in 
 * the ordered CDF list.
******************************************************************************/
beadLocator SwapMoveBase::selectPivotBead() {
	
	/* Generate a uniform deviate, and figure out where it fits in our cumulant
	 * array using a binary search */
	double u = random.rand();
    int index = std::lower_bound(cumulant.begin(),cumulant.end(),u)
            - cumulant.begin();
	/* Copy over the pivot bead and return it */
	beadLocator pivotBead;
	pivotBead = path.lookup.fullBeadList(index);
	
	return pivotBead;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// SWAP HEAD MOVE CLASS ------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
SwapHeadMove::SwapHeadMove (Path &_path, ActionBase *_actionPtr, 
		MTRand &_random, string _name, ensemble _operateOnConfig) : 
    SwapMoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zero */
	numAccepted = numAttempted = numToMove = 0;

	/* Initialize the acceptance by level counters */
	numAcceptedLevel.resize(constants()->b()+1);
	numAttemptedLevel.resize(constants()->b()+1);
	numAcceptedLevel  = 0;
	numAttemptedLevel = 0;

	/* Update the sizes of the original position array */
	originalPos.resize(constants()->Mbar()-1);
	originalPos = 0.0;
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
SwapHeadMove::~SwapHeadMove() {
}

/*************************************************************************//**
 * Perform a swap head move.
 * 
 * Attempt to perform a swap move that samples particle permuations due to the
 * indistinguishability of bosons by reattaching the worm head to another
 * worldline..
******************************************************************************/
bool SwapHeadMove::attemptMove() {

	success = false;

	/* We first make sure we are in an off-diagonal configuration */
	if (!path.worm.isConfigDiagonal) {

		/* Initialize */
		pivot = XXX;
		swap = XXX;

		/* Now we figure out how many beads will be involved with the swap bisection. */
		swapLength = constants()->Mbar();
		numLevels = int (ceil(log(1.0*swapLength) / log(2.0)-EPS));

		/* Now form the list of beads which are in the neighbourhood of
		 * the head, but at the advanced time slice */
		int pivotSlice = path.worm.head[0] + swapLength;
		if (pivotSlice >= constants()->numTimeSlices())
			pivotSlice -= constants()->numTimeSlices();

		/* Update the full interaction list */
		path.lookup.updateFullInteractionList(path.worm.head,pivotSlice);

		/* We can only try to make a move if we have at least one bead to swap with */
		if (path.lookup.fullNumBeads > 0) {

			cumulant.resize(path.lookup.fullNumBeads);

			/* We compute the normalization factors using the head bead */
			SigmaHead = getNorm(path.worm.head);

			/* Get the pivot bead */
			pivot = selectPivotBead();

			/* Now we try to find the swap bead.  If we find the worm tail, we immediatly
			 * exit the move */
			beadLocator beadIndex;
			beadIndex = pivot;
			for (int k = 0; k < swapLength; k++) {
				if (all(beadIndex==path.worm.tail))
					return false;
				beadIndex = path.prev(beadIndex);
			}
			swap = beadIndex;

			/* We only continue if the swap is not the tail, and the swap and pivot
			 * grid boxes coincide. */
			if ( !all(path.worm.tail==swap) && path.lookup.gridNeighbors(pivot,swap) ) {

                checkMove(0,0.0);

				/* Increment the number of swap moves and the total number of moves */
				numAttempted++;
				totAttempted++;
				numAttemptedLevel(numLevels)++;

				/* Now we create a new list of beads (using the same beadList)
				 * which contains all beads in neighborhood of the swap bead
				 * grid box, but at a time slice advanced by Mbar. We only need to
				 * do this if head and swap are in different grid boxes. */ 
				if (!path.lookup.gridShare(path.worm.head,swap)) {
					path.lookup.updateFullInteractionList(swap,pivotSlice);
					cumulant.resize(path.lookup.fullNumBeads);
				}

				/* Get the normalization factor for the new list */
				SigmaSwap = getNorm(swap);

				/* We now perform a pre-metropolis step on the selected bead. If this 
				 * is not accepted, it is extremely likely that the potential change
				 * will have any effect, so we don't bother with it. */
                double PNorm = min(SigmaHead/SigmaSwap,1.0);
				if (random.rand() < PNorm) {

					/* Mark the special beads */
					path.worm.special1 = swap;
					path.worm.special2 = pivot;

					/* Store the original positions */
					int k = 0;
					beadIndex = swap;
					do {
						/* Store the original positions */
						if (!all(beadIndex==swap) && !all(beadIndex==pivot)) {
							originalPos(k) = path(beadIndex);
							++k;
						}
						beadIndex = path.next(beadIndex);
					} while (!all(beadIndex==path.next(pivot)));

                    /* now compute the original action for the path to be
                     * updated */
                    oldAction = actionPtr->potentialAction(swap,pivot);

					/* Because the staging algorithm requires that we move foward
					 * and backward in imaginary time (for periodic boundary conditions)
					 * we perform the relinking now, and change back to the original 
					 * linking only if we reject the move */

					/* Store temporary bead locators for all the linkages that
					 * will be changed */
					nextSwap = path.next(swap);

					/* Update the links.  Here we exploit the non-constant return reference 
					 * semantics of the next/prev methods */
					path.next(path.worm.head) = nextSwap;
					path.next(swap)           = XXX; 
					path.prev(nextSwap)       = path.worm.head;

					/* Change the former head to a special bead, and assign the new head */
					path.worm.special1 = path.worm.head;
					path.worm.head = swap;

					/* Propose a new trajectory */
					beadIndex = path.worm.special1;
					k = 0;
					do {
						if (!all(beadIndex==path.worm.special1) && !all(beadIndex==pivot)) {
							path.updateBead(beadIndex,
									newStagingPosition(path.prev(beadIndex),pivot,swapLength,k));
							++k;
						}
						beadIndex = path.next(beadIndex);
					} while (!all(beadIndex==path.next(pivot)));

                    /* Compute the potential action for the updated path */
                    newAction = actionPtr->potentialAction(path.worm.special1,pivot);

					if ( random.rand() < exp(-(newAction - oldAction))) {
						keepMove();
                        checkMove(1,newAction-oldAction);
                    }
					else {
						undoMove();
                        checkMove(2,0.0);
                    }

				} // PNorm 

			} // if we didn't find the tail and grid boxes coincide

		} // numListBeads = 0

	} // isConfigDiagonal

	return success;
}

/*************************************************************************//**
 * Preserve the swap head move.
******************************************************************************/
void SwapHeadMove::keepMove() {

	/* update the acceptance counters */
	numAccepted++;
	totAccepted++;
	numAcceptedLevel(numLevels)++;

	/* Now we update the properties of our worm.  Head is replaced with
	 * swap and we must update the length and gap */
	path.worm.update(path,swap,path.worm.tail);

	printMoveState("Performed a head swap.");
	success = true;
}

/*************************************************************************//**
 * If we reject the swap head move, all we need to do is restore the 
 * original positions and undo the new linkages.
******************************************************************************/
void SwapHeadMove::undoMove() {

	/* Reassign the head bead */
	path.worm.head = path.worm.special1;

	/* Return all links to their un-swapped values */
	path.next(path.worm.head) = XXX;
	path.next(swap) = nextSwap;
	path.prev(nextSwap) = swap;

	/* Return the path to its original coordinates */
	beadLocator beadIndex;
	beadIndex = path.next(swap);
	int k = 0;
	do {
		path.updateBead(beadIndex,originalPos(k));
		++k;
		beadIndex = path.next(beadIndex);
	} while (!all(beadIndex==pivot));

	/* Make sure the configuration is still off-diagonal */
 	path.worm.isConfigDiagonal = false;

	/* Unset the special beads */
	path.worm.special1 = XXX;
	path.worm.special2 = XXX;

	printMoveState("Failed to perform a head swap.");
	success = false;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// SWAP TAIL MOVE CLASS ------------------------------------------------------
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

/*************************************************************************//**
 *  Constructor.
******************************************************************************/
SwapTailMove::SwapTailMove (Path &_path, ActionBase *_actionPtr, 
		MTRand &_random, string _name, ensemble _operateOnConfig) : 
    SwapMoveBase(_path,_actionPtr,_random,_name,_operateOnConfig) {

	/* Initialize private data to zero */
	numAccepted = numAttempted = numToMove = 0;

	/* Initialize the acceptance by level counters */
	numAcceptedLevel.resize(constants()->b()+1);
	numAttemptedLevel.resize(constants()->b()+1);
	numAcceptedLevel  = 0;
	numAttemptedLevel = 0;

	/* Update the sizes of the original position array */
	originalPos.resize(constants()->Mbar()-1);
	originalPos = 0.0;
}

/*************************************************************************//**
 *  Destructor.
******************************************************************************/
SwapTailMove::~SwapTailMove() {
}

/*************************************************************************//**
 * Perform a swap tail move.
 * 
 * Try to perform a swap tail move that samples particle permuations due to the
 * indistinguishability of bosons by reattaching the tail to a worldline.
******************************************************************************/
bool SwapTailMove::attemptMove() {

	success = false;

	/* We first make sure we are in an off-diagonal configuration */
	if (!path.worm.isConfigDiagonal) {

		/* Initialize */
		pivot = XXX;
		swap = XXX;

		/* Now we figure out how many beads will be involved with the swap bisection. */
		swapLength = constants()->Mbar();
		numLevels = int (ceil(log(1.0*swapLength) / log(2.0)-EPS));

		/* Now form the list of beads which are in the neighbourhood of
		 * the tail, but at the regressed time slice */
		int pivotSlice = path.worm.tail[0] - swapLength;
		if (pivotSlice < 0)
			pivotSlice += constants()->numTimeSlices();

		path.lookup.updateFullInteractionList(path.worm.tail,pivotSlice);

		/* We can only try to make a move if we have at least one bead to swap with */
		if (path.lookup.fullNumBeads > 0) {

			cumulant.resize(path.lookup.fullNumBeads);

			/* We compute the normalization factors using the tail bead */
			SigmaTail = getNorm(path.worm.tail);

			/* Get the pivot bead */
			pivot = selectPivotBead();

			/* Now we try to find the swap bead.  If we find the worm head, we immediatly
			 * exit the move */
			beadLocator beadIndex;
			beadIndex = pivot;
			for (int k = 0; k < swapLength; k++) {
				if (all(beadIndex==path.worm.head))
					return false;
				beadIndex = path.next(beadIndex);
			}
			swap = beadIndex;

			/* We only continue if we don't find the head, and the pivot and swap grid
			 * boxes coincide, otherwise we reject the move. */
			if ( !all(path.worm.head==swap) && path.lookup.gridNeighbors(pivot,swap) ) {

                checkMove(0,0.0);

				/* Increment the number of swap moves and the total number of moves */
				numAttempted++;
				totAttempted++;
				numAttemptedLevel(numLevels)++;

				/* Now we create a new list of beads (using the same beadList)
				 * which contains all beads in neighborhood of the swap bead
				 * grid box, but at a time slice advanced by Mbar. We only 
				 * do this if tail and swap are in different grid boxes. */ 
				if (!path.lookup.gridShare(path.worm.tail,swap)) {
					path.lookup.updateFullInteractionList(swap,pivotSlice);
					cumulant.resize(path.lookup.fullNumBeads);
				}

				/* Get the normalization factor for the new list */
				SigmaSwap = getNorm(swap);

				/* We now perform a pre-metropolis step on the selected bead. If this 
				 * is not accepted, it is extremely unlikely that the potential change
				 * will have any effect, so we don't bother with it. */
                double PNorm = min(SigmaTail/SigmaSwap,1.0);
				if (random.rand() < PNorm) {

					/* Mark the swap and pivot as special */
					path.worm.special1 = swap;
					path.worm.special2 = pivot;

					int k = 0;
					oldAction = newAction = 0.0;

                    /* Store the old trajectory and compute its action */
					beadIndex = swap;
					do {
						if (!all(beadIndex==swap) && !all(beadIndex==pivot)) {
							originalPos(k) = path(beadIndex);
							++k;
						}
						beadIndex = path.prev(beadIndex);
					} while (!all(beadIndex==path.prev(pivot)));

                    oldAction = actionPtr->potentialAction(pivot,swap);

					/* Because the staging algorithm requires that we move foward
					 * and backward in imaginary time (for periodic boundary conditions)
					 * we perform the relinking now, and change back to the original 
					 * linking only if we reject the move */

					/* Store temporary bead locators for all the linkages that
					 * will be changed */
					prevSwap = path.prev(swap);

					/* Update the links.  Here we exploit the non-constant return reference 
					 * semantics of the next/prev methods */
					path.prev(path.worm.tail) = prevSwap;
					path.prev(swap)           = XXX;
					path.next(prevSwap)       = path.worm.tail;

					/* Change the former tail to a special bead, and assign the new tail */
					path.worm.special1 = path.worm.tail;
					path.worm.tail = swap;

                    /* Propose a new path trajectory and compute its action */
					k = 0;
					beadIndex = path.worm.special1;
					do {
						if (!all(beadIndex==path.worm.special1) && !all(beadIndex==pivot)) {
							path.updateBead(beadIndex,
									newStagingPosition(path.next(beadIndex),pivot,swapLength,k));
							++k;
						}
						beadIndex = path.prev(beadIndex);
					} while (!all(beadIndex==path.prev(pivot)));

                    newAction = actionPtr->potentialAction(pivot,path.worm.special1);

                    /* Perform the Metropolis test */
					if ( random.rand() < exp(-(newAction - oldAction))) {
						keepMove();
                        checkMove(1,newAction-oldAction);
                    }
					else {
						undoMove();
                        checkMove(2,0.0);
                    }

				} // PNorm 

			} // if we didn't find the tail and the grid boxes coincide

		} // numListBeads = 0

	} // isConfigDiagonal

	return success;
}

/*************************************************************************//**
 * Preserve the swap tail move.
******************************************************************************/
void SwapTailMove::keepMove() {

	/* update the acceptance counters */
	numAccepted++;
	totAccepted++;
	numAcceptedLevel(numLevels)++;

	/* Now we update the properties of our worm.  Tail is replaced with
	 * swap and we must update the length and gap */
	path.worm.update(path,path.worm.head,swap);

	/* Restore the shift level for the time step to 1 */
	actionPtr->setShift(1);

	printMoveState("Performed a tail swap.");
	success = true;
}

/*************************************************************************//**
 * If we reject the swap tail move, all we need to do is restore the 
 * original positions and undo the new linkages.
******************************************************************************/
void SwapTailMove::undoMove() {

	/* Copy back the tail bead */
	path.worm.tail = path.worm.special1;

	/* Return all links to their un-swapped values */
	path.prev(path.worm.tail) = XXX;
	path.prev(swap)           = prevSwap;
	path.next(prevSwap)       = swap;

	beadLocator beadIndex;
	beadIndex = path.prev(swap);
	int k = 0;
	do {
		path.updateBead(beadIndex,originalPos(k));
		++k;
		beadIndex = path.prev(beadIndex);
	} while (!all(beadIndex==pivot));

	/* Unset the special beads */
	path.worm.special1 = XXX;
	path.worm.special2 = XXX;

	/* Make sure the configuration is still off-diagonal */
 	path.worm.isConfigDiagonal = false;

	printMoveState("Failed to perform a tail swap.");
	success = false;
}
