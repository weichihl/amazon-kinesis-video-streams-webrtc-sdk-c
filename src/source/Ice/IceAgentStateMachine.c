/**
 * Implementation of a ice agent states machine callbacks
 */
#define LOG_CLASS "IceAgentState"
#include "../Include_i.h"

#define ICE_FSM_ENTER() //ENTERS()
#define ICE_FSM_LEAVE() //LEAVES()

PCHAR iceAgentFsmToString(UINT64 state)
{
    PCHAR stateStr = NULL;
    switch (state) {
        case ICE_AGENT_STATE_NONE:
            stateStr = ICE_AGENT_STATE_NONE_STR;
            break;
        case ICE_AGENT_STATE_NEW:
            stateStr = ICE_AGENT_STATE_NEW_STR;
            break;
        case ICE_AGENT_STATE_CHECK_CONNECTION:
            stateStr = ICE_AGENT_STATE_CHECK_CONNECTION_STR;
            break;
        case ICE_AGENT_STATE_CONNECTED:
            stateStr = ICE_AGENT_STATE_CONNECTED_STR;
            break;
        case ICE_AGENT_STATE_NOMINATING:
            stateStr = ICE_AGENT_STATE_NOMINATING_STR;
            break;
        case ICE_AGENT_STATE_READY:
            stateStr = ICE_AGENT_STATE_READY_STR;
            break;
        case ICE_AGENT_STATE_DISCONNECTED:
            stateStr = ICE_AGENT_STATE_DISCONNECTED_STR;
            break;
        case ICE_AGENT_STATE_FAILED:
            stateStr = ICE_AGENT_STATE_FAILED_STR;
            break;
    }

    return stateStr;
}

/**
 * @brief advance the fsm of the ice agent.
 * 
*/
STATUS iceAgentFsmStep(PIceAgent pIceAgent)
{
    ICE_FSM_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 oldState;

    CHK(pIceAgent != NULL, STATUS_NULL_ARG);

    oldState = pIceAgent->iceAgentState;

    CHK_STATUS(stepStateMachine(pIceAgent->pStateMachine));

    // if any failure happened and state machine is not in failed state, stepStateMachine again into failed state.
    if (pIceAgent->iceAgentState != ICE_AGENT_STATE_FAILED && STATUS_FAILED(pIceAgent->iceAgentStatus)) {
        CHK_STATUS(stepStateMachine(pIceAgent->pStateMachine));
    }

    if (oldState != pIceAgent->iceAgentState) {
        if (pIceAgent->iceAgentCallbacks.connectionStateChangedFn != NULL) {
            DLOGD("Ice agent state changed from %s to %s.", iceAgentFsmToString(oldState), iceAgentFsmToString(pIceAgent->iceAgentState));
            pIceAgent->iceAgentCallbacks.connectionStateChangedFn(pIceAgent->iceAgentCallbacks.customData, pIceAgent->iceAgentState);
        }
    } else {
        // state machine retry is not used. resetStateMachineRetryCount just to avoid
        // state machine retry grace period overflow warning.
        CHK_STATUS(resetStateMachineRetryCount(pIceAgent->pStateMachine));
    }

CleanUp:

    CHK_LOG_ERR(retStatus);

    ICE_FSM_LEAVE();
    return retStatus;
}
/**
 * @brief check the transition of fsm is valid or not.
 * 
*/
STATUS iceAgentFsmAccept(PIceAgent pIceAgent, UINT64 state)
{
    ICE_FSM_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    BOOL locked = FALSE;

    CHK(pIceAgent != NULL, STATUS_NULL_ARG);

    MUTEX_LOCK(pIceAgent->lock);
    locked = TRUE;

    // Step the state machine
    CHK_STATUS(acceptStateMachineState(pIceAgent->pStateMachine, state));

CleanUp:

    if (locked) {
        MUTEX_UNLOCK(pIceAgent->lock);
    }

    ICE_FSM_LEAVE();
    return retStatus;
}

/*
 * This function is supposed to be called from within IceAgentStateMachine callbacks. Assume holding IceAgent->lock
 */
/**
 * @brief   check the timeout of connection. right now, the interval of timer is 2*(the interval of keeping alive).
 * 
 * @param[]
*/
STATUS iceAgentFsmCheckDisconnection(PIceAgent pIceAgent, PUINT64 pNextState)
{
    ICE_FSM_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    UINT64 currentTime;

    CHK(pIceAgent != NULL && pNextState != NULL, STATUS_NULL_ARG);

    currentTime = GETTIME();
    // check the timeout of connection.
    if (!pIceAgent->detectedDisconnection && 
        IS_VALID_TIMESTAMP(pIceAgent->lastDataReceivedTime) &&
        pIceAgent->lastDataReceivedTime + KVS_ICE_ENTER_STATE_DISCONNECTION_GRACE_PERIOD <= currentTime) {
        *pNextState = ICE_AGENT_STATE_DISCONNECTED;
    // back to ready from disconnected.
    } else if (pIceAgent->detectedDisconnection) {
        if (IS_VALID_TIMESTAMP(pIceAgent->lastDataReceivedTime) &&
            pIceAgent->lastDataReceivedTime + KVS_ICE_ENTER_STATE_DISCONNECTION_GRACE_PERIOD > currentTime) {
            // recovered from disconnection
            DLOGD("recovered from disconnection");
            pIceAgent->detectedDisconnection = FALSE;
            if (pIceAgent->iceAgentCallbacks.connectionStateChangedFn != NULL) {
                pIceAgent->iceAgentCallbacks.connectionStateChangedFn(pIceAgent->iceAgentCallbacks.customData, pIceAgent->iceAgentState);
            }
        } else if (currentTime >= pIceAgent->disconnectionGracePeriodEndTime) {
            CHK(FALSE, STATUS_ICE_FAILED_TO_RECOVER_FROM_DISCONNECTION);
        }
    }

CleanUp:

    ICE_FSM_LEAVE();
    return retStatus;
}

///////////////////////////////////////////////////////////////////////////
// State machine callback functions
///////////////////////////////////////////////////////////////////////////
STATUS iceAgentFsmNew(UINT64 customData, UINT64 time)
{
    ICE_FSM_ENTER();
    UNUSED_PARAM(time);
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;

    CHK(pIceAgent != NULL, STATUS_NULL_ARG);
    // return early if we are already in ICE_AGENT_STATE_GATHERING
    CHK(pIceAgent->iceAgentState != ICE_AGENT_STATE_NEW, retStatus);

    pIceAgent->iceAgentState = ICE_AGENT_STATE_NEW;

CleanUp:

    ICE_FSM_LEAVE();
    return retStatus;
}

STATUS iceAgentFsmFromNew(UINT64 customData, PUINT64 pState)
{
    ICE_FSM_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;
    UINT64 state;

    CHK(pIceAgent != NULL && pState != NULL, STATUS_NULL_ARG);

    // go directly to next state
    state = ICE_AGENT_STATE_CHECK_CONNECTION;

    *pState = state;

CleanUp:

    ICE_FSM_LEAVE();
    return retStatus;
}

/**
 * @brief 
*/
STATUS iceAgentFsmCheckConnection(UINT64 customData, UINT64 time)
{
    ICE_FSM_ENTER();
    UNUSED_PARAM(time);
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;

    CHK(pIceAgent != NULL, STATUS_NULL_ARG);
    // #YC_TBD, why this happen. basically leaving from new will set the state as check-connection.
    // 
    if (pIceAgent->iceAgentState != ICE_AGENT_STATE_CHECK_CONNECTION) {
        CHK_STATUS(iceAgentSetupFsmCheckConnection(pIceAgent));
        pIceAgent->iceAgentState = ICE_AGENT_STATE_CHECK_CONNECTION;
    }

    CHK_STATUS(iceAgentCheckCandidatePairConnection(pIceAgent));

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        pIceAgent->iceAgentStatus = retStatus;

        // fix up retStatus so we can successfully transition to failed state.
        retStatus = STATUS_SUCCESS;
    }

    ICE_FSM_LEAVE();
    return retStatus;
}

STATUS iceAgentFsmFromCheckConnection(UINT64 customData, PUINT64 pState)
{
    ICE_FSM_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;
    UINT64 state = ICE_AGENT_STATE_CHECK_CONNECTION; // original state
    BOOL connectedCandidatePairFound = FALSE;
    UINT64 currentTime = GETTIME();
    PDoubleListNode pCurNode = NULL;
    PIceCandidatePair pIceCandidatePair = NULL;
    BOOL locked = FALSE;

    CHK(pIceAgent != NULL && pState != NULL, STATUS_NULL_ARG);

    MUTEX_LOCK(pIceAgent->lock);
    locked = TRUE;

    // move to failed state if any error happened.
    CHK_STATUS(pIceAgent->iceAgentStatus);

    // connected pair found ? go to ICE_AGENT_STATE_CONNECTED : timeout ? go to error : remain in ICE_AGENT_STATE_CHECK_CONNECTION
    // pop the first succeeded candidate pair, and switch to connected state.
    CHK_STATUS(doubleListGetHeadNode(pIceAgent->iceCandidatePairs, &pCurNode));
    while (pCurNode != NULL && !connectedCandidatePairFound) {
        pIceCandidatePair = (PIceCandidatePair) pCurNode->data;
        pCurNode = pCurNode->pNext;

        if (pIceCandidatePair->state == ICE_CANDIDATE_PAIR_STATE_SUCCEEDED) {
            connectedCandidatePairFound = TRUE;
            state = ICE_AGENT_STATE_CONNECTED;
        }
    }

    // return error if no connected pair found within timeout
    if (!connectedCandidatePairFound && currentTime >= pIceAgent->stateEndTime) {
        retStatus = STATUS_ICE_NO_CONNECTED_CANDIDATE_PAIR;
    }

CleanUp:
    // #YC_TBD,
    if (STATUS_FAILED(retStatus)) {
        state = ICE_AGENT_STATE_FAILED;
        pIceAgent->iceAgentStatus = retStatus;
        // fix up retStatus so we can successfully transition to failed state.
        retStatus = STATUS_SUCCESS;
    }

    if (pState != NULL) {
        *pState = state;
    }

    if (locked) {
        MUTEX_UNLOCK(pIceAgent->lock);
    }

    ICE_FSM_LEAVE();
    return retStatus;
}

/**
 * @brief the handler of fsm when ice agent is at connected state.
*/
STATUS iceAgentFsmConnected(UINT64 customData, UINT64 time)
{
    ICE_FSM_ENTER();
    UNUSED_PARAM(time);
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;

    CHK(pIceAgent != NULL, STATUS_NULL_ARG);

    CHK_STATUS(iceAgentSetupFsmConnected(pIceAgent));

    pIceAgent->iceAgentState = ICE_AGENT_STATE_CONNECTED;

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        pIceAgent->iceAgentStatus = retStatus;

        // fix up retStatus so we can successfully transition to failed state.
        retStatus = STATUS_SUCCESS;
    }

    ICE_FSM_LEAVE();
    return retStatus;
}

/**
 * @brief the handler of fsm when exiting from the connected state.
*/
STATUS iceAgentFsmFromConnected(UINT64 customData, PUINT64 pState)
{
    ICE_FSM_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;
    UINT64 state = ICE_AGENT_STATE_NOMINATING; // original state
    BOOL locked = FALSE;

    CHK(pIceAgent != NULL && pState != NULL, STATUS_NULL_ARG);

    MUTEX_LOCK(pIceAgent->lock);
    locked = TRUE;

    // move to failed state if any error happened.
    CHK_STATUS(pIceAgent->iceAgentStatus);

    // return early if changing to disconnected state
    //CHK(state != ICE_AGENT_STATE_DISCONNECTED, retStatus);

    // Go directly to nominating state from connected state.
    //state = ICE_AGENT_STATE_NOMINATING;

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        state = ICE_AGENT_STATE_FAILED;
        pIceAgent->iceAgentStatus = retStatus;
        // fix up retStatus so we can successfully transition to failed state.
        retStatus = STATUS_SUCCESS;
    }

    if (pState != NULL) {
        *pState = state;
    }

    if (locked) {
        MUTEX_UNLOCK(pIceAgent->lock);
    }

    ICE_FSM_LEAVE();
    return retStatus;
}

/**
 * @brief the handler of state machine when ice agent is at nominating state.
 * 
*/
STATUS iceAgentFsmNominating(UINT64 customData, UINT64 time)
{
    ICE_FSM_ENTER();
    UNUSED_PARAM(time);
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;

    CHK(pIceAgent != NULL, STATUS_NULL_ARG);
    // prepare the stun use-candidate packet if we are controlling ice agent.
    if (pIceAgent->iceAgentState != ICE_AGENT_STATE_NOMINATING) {
        CHK_STATUS(iceAgentSetupFsmNominating(pIceAgent));
        pIceAgent->iceAgentState = ICE_AGENT_STATE_NOMINATING;
    }

    if (pIceAgent->isControlling) {
        // send the stun use-candidate packet.
        CHK_STATUS(iceAgentSendCandidateNomination(pIceAgent));
    } else {
        // if not controlling, keep sending connection checks and wait for peer
        // to nominate a pair.
        CHK_STATUS(iceAgentCheckCandidatePairConnection(pIceAgent));
    }

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        pIceAgent->iceAgentStatus = retStatus;

        // fix up retStatus so we can successfully transition to failed state.
        retStatus = STATUS_SUCCESS;
    }

    ICE_FSM_LEAVE();
    return retStatus;
}


/**
 * @brief the handler of fsm when exiting from nominating state.
 * 
 * @param[in] customData the context.
 * @param[out] pState the next state.
*/
STATUS iceAgentFsmFromNominating(UINT64 customData, PUINT64 pState)
{
    ICE_FSM_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;
    UINT64 state = ICE_AGENT_STATE_NOMINATING; // original state
    UINT64 currentTime = GETTIME();
    BOOL nominatedAndValidCandidatePairFound = FALSE, locked = FALSE;
    PDoubleListNode pCurNode = NULL;
    PIceCandidatePair pIceCandidatePair = NULL;

    CHK(pIceAgent != NULL && pState != NULL, STATUS_NULL_ARG);

    MUTEX_LOCK(pIceAgent->lock);
    locked = TRUE;

    // move to failed state if any error happened.
    CHK_STATUS(pIceAgent->iceAgentStatus);

    CHK_STATUS(doubleListGetHeadNode(pIceAgent->iceCandidatePairs, &pCurNode));
    while (pCurNode != NULL) {
        pIceCandidatePair = (PIceCandidatePair) pCurNode->data;
        pCurNode = pCurNode->pNext;

        if (pIceCandidatePair->nominated && pIceCandidatePair->state == ICE_CANDIDATE_PAIR_STATE_SUCCEEDED) {
            nominatedAndValidCandidatePairFound = TRUE;
            break;
        }
    }

    if (nominatedAndValidCandidatePairFound) {
        state = ICE_AGENT_STATE_READY;
    } else if (currentTime >= pIceAgent->stateEndTime) {
        CHK(FALSE, STATUS_ICE_FAILED_TO_NOMINATE_CANDIDATE_PAIR);
    }

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        state = ICE_AGENT_STATE_FAILED;
        pIceAgent->iceAgentStatus = retStatus;
        // fix up retStatus so we can successfully transition to failed state.
        retStatus = STATUS_SUCCESS;
    }

    if (pState != NULL) {
        *pState = state;
    }

    if (locked) {
        MUTEX_UNLOCK(pIceAgent->lock);
    }

    ICE_FSM_LEAVE();
    return retStatus;
}

STATUS iceAgentFsmReady(UINT64 customData, UINT64 time)
{
    ICE_FSM_ENTER();
    UNUSED_PARAM(time);
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;

    CHK(pIceAgent != NULL, STATUS_NULL_ARG);

    if (pIceAgent->iceAgentState != ICE_AGENT_STATE_READY) {
        CHK_STATUS(iceAgentSetupFsmReady(pIceAgent));
        pIceAgent->iceAgentState = ICE_AGENT_STATE_READY;
    }

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        pIceAgent->iceAgentStatus = retStatus;

        // fix up retStatus so we can successfully transition to failed state.
        retStatus = STATUS_SUCCESS;
    }

    ICE_FSM_LEAVE();
    return retStatus;
}

STATUS iceAgentFsmFromReady(UINT64 customData, PUINT64 pState)
{
    ICE_FSM_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;
    UINT64 state = ICE_AGENT_STATE_READY; // original state
    BOOL locked = FALSE;
    PDoubleListNode pCurNode = NULL, pNodeToDelete = NULL;
    PIceCandidate pIceCandidate = NULL;

    CHK(pIceAgent != NULL && pState != NULL, STATUS_NULL_ARG);

    MUTEX_LOCK(pIceAgent->lock);
    locked = TRUE;

    // move to failed state if any error happened.
    CHK_STATUS(pIceAgent->iceAgentStatus);

    CHK_STATUS(iceAgentFsmCheckDisconnection(pIceAgent, &state));

    // Free TurnConnections that are shutdown
    CHK_STATUS(doubleListGetHeadNode(pIceAgent->localCandidates, &pCurNode));
    while (pCurNode != NULL) {
        pIceCandidate = (PIceCandidate) pCurNode->data;
        pNodeToDelete = pCurNode;
        pCurNode = pCurNode->pNext;

        if (pIceCandidate->iceCandidateType == ICE_CANDIDATE_TYPE_RELAYED && turnConnectionIsShutdownComplete(pIceCandidate->pTurnConnection)) {
            CHK_LOG_ERR(freeTurnConnection(&pIceCandidate->pTurnConnection));
            MEMFREE(pIceCandidate);
            CHK_STATUS(doubleListDeleteNode(pIceAgent->localCandidates, pNodeToDelete));
        }
    }

    // return early if changing to disconnected state
    CHK(state != ICE_AGENT_STATE_DISCONNECTED, retStatus);

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        state = ICE_AGENT_STATE_FAILED;
        pIceAgent->iceAgentStatus = retStatus;
        // fix up retStatus so we can successfully transition to failed state.
        retStatus = STATUS_SUCCESS;
    }

    if (pState != NULL) {
        *pState = state;
    }

    if (locked) {
        MUTEX_UNLOCK(pIceAgent->lock);
    }

    ICE_FSM_LEAVE();
    return retStatus;
}

STATUS iceAgentFsmDisconnected(UINT64 customData, UINT64 time)
{
    ICE_FSM_ENTER();
    UNUSED_PARAM(time);
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;

    CHK(pIceAgent != NULL, STATUS_NULL_ARG);
    CHK(pIceAgent->iceAgentState != ICE_AGENT_STATE_DISCONNECTED, retStatus);

    // not stopping timer task from previous state as we want it to continue and perhaps recover.
    // not setting pIceAgent->iceAgentState as it stores the previous state and we want to step back into it to continue retry
    DLOGD("Ice agent detected disconnection. current state %s. Last data received time %" PRIu64 " s",
          iceAgentFsmToString(pIceAgent->iceAgentState), pIceAgent->lastDataReceivedTime / HUNDREDS_OF_NANOS_IN_A_SECOND);
    pIceAgent->detectedDisconnection = TRUE;

    // after detecting disconnection, store disconnectionGracePeriodEndTime and when it is reached and ice still hasnt recover
    // then go to failed state.
    pIceAgent->disconnectionGracePeriodEndTime = GETTIME() + KVS_ICE_ENTER_STATE_FAILED_GRACE_PERIOD;

    if (pIceAgent->iceAgentCallbacks.connectionStateChangedFn != NULL) {
        pIceAgent->iceAgentCallbacks.connectionStateChangedFn(pIceAgent->iceAgentCallbacks.customData, ICE_AGENT_STATE_DISCONNECTED);
    }

    // step out of disconnection state to retry. Do not use stepIceAgentState machine because lock is not re-entrant.
    CHK_STATUS(stepStateMachine(pIceAgent->pStateMachine));

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        pIceAgent->iceAgentStatus = retStatus;

        // fix up retStatus so we can successfully transition to failed state.
        retStatus = STATUS_SUCCESS;
    }

    ICE_FSM_LEAVE();
    return retStatus;
}


STATUS iceAgentFsmFromDisconnected(UINT64 customData, PUINT64 pState)
{
    ICE_FSM_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;
    UINT64 state = pIceAgent->iceAgentState; // state before ICE_AGENT_STATE_DISCONNECTED
    BOOL locked = FALSE;

    CHK(pIceAgent != NULL && pState != NULL, STATUS_NULL_ARG);

    MUTEX_LOCK(pIceAgent->lock);
    locked = TRUE;

    // move to failed state if any error happened.
    CHK_STATUS(pIceAgent->iceAgentStatus);

CleanUp:

    if (STATUS_FAILED(retStatus)) {
        state = ICE_AGENT_STATE_FAILED;
        pIceAgent->iceAgentStatus = retStatus;
        // fix up retStatus so we can successfully transition to failed state.
        retStatus = STATUS_SUCCESS;
    }

    if (pState != NULL) {
        *pState = state;
    }

    if (locked) {
        MUTEX_UNLOCK(pIceAgent->lock);
    }

    ICE_FSM_LEAVE();
    return retStatus;
}


STATUS iceAgentFsmFailed(UINT64 customData, UINT64 time)
{
    ICE_FSM_ENTER();
    UNUSED_PARAM(time);
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;
    const PCHAR errMsgPrefix = (PCHAR) "IceAgent fatal error:";

    CHK(pIceAgent != NULL, STATUS_NULL_ARG);
    CHK(pIceAgent->iceAgentState != ICE_AGENT_STATE_FAILED, retStatus);

    pIceAgent->iceAgentState = ICE_AGENT_STATE_FAILED;

    // log some debug info about the failure once.
    switch (pIceAgent->iceAgentStatus) {
        case STATUS_ICE_NO_AVAILABLE_ICE_CANDIDATE_PAIR:
            DLOGE("%s No ice candidate pairs available to make connection.", errMsgPrefix);
            break;
        default:
            DLOGE("IceAgent failed with 0x%08x", pIceAgent->iceAgentStatus);
            break;
    }

CleanUp:

    ICE_FSM_LEAVE();
    return retStatus;
}


STATUS iceAgentFsmFomFailed(UINT64 customData, PUINT64 pState)
{
    ICE_FSM_ENTER();
    STATUS retStatus = STATUS_SUCCESS;
    PIceAgent pIceAgent = (PIceAgent) customData;

    CHK(pIceAgent != NULL && pState != NULL, STATUS_NULL_ARG);

    // FAILED state is terminal.
    *pState = ICE_AGENT_STATE_FAILED;

CleanUp:

    ICE_FSM_LEAVE();
    return retStatus;
}


/**
 * Static definitions of the states
 */
#define ICE_AGENT_STATE_UNLIMIT_RETRY       (INFINITE_RETRY_COUNT_SENTINEL)
#define ICE_AGENT_STATE_NEW_REQUIRED        (ICE_AGENT_STATE_NONE | ICE_AGENT_STATE_NEW)
#define ICE_AGENT_STATE_CHECK_CONN_REQUIRED (ICE_AGENT_STATE_NEW | ICE_AGENT_STATE_CHECK_CONNECTION)
#define ICE_AGENT_STATE_CONNECTED_REQUIRED  (ICE_AGENT_STATE_CHECK_CONNECTION | ICE_AGENT_STATE_CONNECTED)
#define ICE_AGENT_STATE_NOMINATING_REQUIRED (ICE_AGENT_STATE_CONNECTED | ICE_AGENT_STATE_NOMINATING)
#define ICE_AGENT_STATE_READY_REQUIRED      (ICE_AGENT_STATE_CONNECTED | ICE_AGENT_STATE_NOMINATING | ICE_AGENT_STATE_READY | ICE_AGENT_STATE_DISCONNECTED)
#define ICE_AGENT_STATE_DISCONNECTED_REQUIRED                                                                                                        \
    (ICE_AGENT_STATE_CHECK_CONNECTION | ICE_AGENT_STATE_CONNECTED | ICE_AGENT_STATE_NOMINATING | ICE_AGENT_STATE_READY | ICE_AGENT_STATE_DISCONNECTED)
#define ICE_AGENT_STATE_FAILED_REQUIRED                                                                                                              \
    (ICE_AGENT_STATE_CHECK_CONNECTION | ICE_AGENT_STATE_CONNECTED | ICE_AGENT_STATE_NOMINATING | ICE_AGENT_STATE_READY |                             \
     ICE_AGENT_STATE_DISCONNECTED | ICE_AGENT_STATE_FAILED)

StateMachineState ICE_AGENT_STATE_MACHINE_STATES[] = {
    {ICE_AGENT_STATE_NEW, ICE_AGENT_STATE_NEW_REQUIRED, iceAgentFsmFromNew, iceAgentFsmNew, ICE_AGENT_STATE_UNLIMIT_RETRY,
     STATUS_ICE_INVALID_STATE},
    {ICE_AGENT_STATE_CHECK_CONNECTION, ICE_AGENT_STATE_CHECK_CONN_REQUIRED, iceAgentFsmFromCheckConnection, iceAgentFsmCheckConnection,
     ICE_AGENT_STATE_UNLIMIT_RETRY, STATUS_ICE_INVALID_STATE},
    {ICE_AGENT_STATE_CONNECTED, ICE_AGENT_STATE_CONNECTED_REQUIRED, iceAgentFsmFromConnected, iceAgentFsmConnected,
     ICE_AGENT_STATE_UNLIMIT_RETRY, STATUS_ICE_INVALID_STATE},
    {ICE_AGENT_STATE_NOMINATING, ICE_AGENT_STATE_NOMINATING_REQUIRED, iceAgentFsmFromNominating, iceAgentFsmNominating,
     ICE_AGENT_STATE_UNLIMIT_RETRY, STATUS_ICE_INVALID_STATE},
    {ICE_AGENT_STATE_READY, ICE_AGENT_STATE_READY_REQUIRED, iceAgentFsmFromReady, iceAgentFsmReady, ICE_AGENT_STATE_UNLIMIT_RETRY,
     STATUS_ICE_INVALID_STATE},
    {ICE_AGENT_STATE_DISCONNECTED, ICE_AGENT_STATE_DISCONNECTED_REQUIRED, iceAgentFsmFromDisconnected, iceAgentFsmDisconnected,
     ICE_AGENT_STATE_UNLIMIT_RETRY, STATUS_ICE_INVALID_STATE},
    {ICE_AGENT_STATE_FAILED, ICE_AGENT_STATE_FAILED_REQUIRED, iceAgentFsmFomFailed, iceAgentFsmFailed, ICE_AGENT_STATE_UNLIMIT_RETRY,
     STATUS_ICE_INVALID_STATE},
};

UINT32 ICE_AGENT_STATE_MACHINE_STATE_COUNT = ARRAY_SIZE(ICE_AGENT_STATE_MACHINE_STATES);