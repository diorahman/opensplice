#include "v_groupInstance.h"
#include "v_state.h"
#include "v_writer.h"
#include "v__group.h"
#include "v__lifespanAdmin.h"
#include "c_collection.h"
#include "v_time.h"
#include "v_public.h"
#include "v__topic.h"
#include "v_message.h"
#include "v__messageQos.h"

#include "os_report.h"
#define _EXTENT_
#ifdef _EXTENT_
#include "c_extent.h"
#endif

#define CHECK_REFCOUNT(var, i)

#ifndef NDEBUG
#define CHECK_COUNT(instance) v_groupInstanceCheckCount(instance)
#else
#define CHECK_COUNT(instance)
#endif

#ifndef NDEBUG
static void
v_groupInstanceCheckCount(
    v_groupInstance instance)
{
    int disposedFound = 0;
    int writeFound = 0;
    v_groupSample currentSample;
    v_registration reg, unreg;
    int inconsistent = 0;

    currentSample = v_groupInstanceTail(instance);
    while (currentSample != NULL) {
        if (v_messageStateTest(v_groupSampleMessage(currentSample),L_WRITE)) {
            writeFound++;
        }
        if (v_messageStateTest(v_groupSampleMessage(currentSample),L_DISPOSED)) {
            disposedFound++;
        }
        currentSample = currentSample->newer;
    }
    assert (writeFound == instance->messageCount);
    assert ((writeFound + disposedFound) == instance->count);


    /* Check the list again but now also in the opposite direction to verify
     * the instance consistentcy.
     */
    disposedFound = 0; writeFound = 0;
    currentSample = v_groupInstanceHead(instance);
    while (currentSample != NULL) {
        if (v_messageStateTest(v_groupSampleMessage(currentSample),L_WRITE)) {
            writeFound++;
        }
        if (v_messageStateTest(v_groupSampleMessage(currentSample),L_DISPOSED)) {
            disposedFound++;
        }
        currentSample = v_groupSample(currentSample->older);
    }
    assert (writeFound == instance->messageCount);
    assert ((writeFound + disposedFound) == instance->count);


    /* now check registrations and unregisterMessages lists.
     * first do cross check! messages in registrations may not
     * occur in unregisterMessages. */
    inconsistent = 0;
    reg = instance->registrations;
    while (reg != NULL) {
        unreg = instance->unregisterMessages;
        while ((unreg != NULL) && (!inconsistent)) {
            if ((unreg->message == reg->message) ||
                (v_gidCompare(unreg->message->writerGID,
                              reg->message->writerGID) == C_EQ)) {
                inconsistent = 1;
            }
            assert(!inconsistent);
            unreg = unreg->next;
        }
        reg = reg->next;
    }
    /* check for duplicates in both lists */
    reg = instance->registrations;
    while (reg != NULL) {
        unreg = reg->next;
        if (unreg != NULL) {
            if ((unreg->message == reg->message) ||
                (v_gidCompare(unreg->message->writerGID,
                              reg->message->writerGID) == C_EQ)) {
                inconsistent = 1;
            }
        }
        assert(!inconsistent);
        reg = reg->next;
    }
    unreg = instance->unregisterMessages;
    while (unreg != NULL) {
        reg = unreg->next;
        if (reg != NULL) {
            if ((unreg->message == reg->message) ||
                (v_gidCompare(unreg->message->writerGID,
                              reg->message->writerGID) == C_EQ)) {
                inconsistent = 1;
            }
        }
        assert(!inconsistent);
        unreg = unreg->next;
    }
}

#endif

static v_groupInstance
v_groupAllocInstance(
    v_group _this)
{
    v_kernel kernel;
    v_groupInstance instance;

    assert(_this);
    assert(C_TYPECHECK(_this,v_group));

    if (_this->cachedInstance == NULL) {
#ifdef _EXTENT_
        instance = v_groupInstance(c_extentCreate(_this->instanceExtent));
#else
        type = c_subType(_this->instances);
        instance = v_groupInstance(c_new(type));
#endif
        kernel = v_objectKernel(_this);
        v_object(instance)->kernel = kernel;
        v_objectKind(instance) = K_GROUPINSTANCE;
        instance->readerInstanceCache = v_groupCacheNew(kernel,
                                                        V_CACHE_INSTANCE);
        instance->group = (c_voidp)_this;
    } else {
        instance = _this->cachedInstance;
        _this->cachedInstance = NULL;
        assert(instance->group == (c_voidp)_this);
    }

    return instance;
}

void
v_groupInstanceInit (
    v_groupInstance _this,
    v_message message)
{
    c_array instanceKeyList;
    c_array messageKeyList;
    c_long i, nrOfKeys;
    /*
     * copy the key value of the message into the newly created instance.
     */
    messageKeyList = v_topicMessageKeyList(v_groupTopic(_this->group));
    instanceKeyList = v_groupKeyList(_this->group);
    nrOfKeys = c_arraySize(messageKeyList);
    assert(nrOfKeys == c_arraySize(instanceKeyList));
    for (i=0;i<nrOfKeys;i++) {
        c_fieldCopy(messageKeyList[i],message,
                    instanceKeyList[i],_this);
    }
    c_free(instanceKeyList);

    _this->epoch              = C_TIME_ZERO;
    _this->registrations      = NULL;
    _this->unregisterMessages = NULL;
    _this->oldest             = NULL;
    _this->count              = 0;
    _this->state              = 0;

    v_stateSet(_this->state, L_EMPTY);
    assert(v_groupInstanceHead(_this) == NULL);
    v_groupInstanceSetHead(_this,NULL);
}

v_groupInstance
v_groupInstanceNew(
    v_group group,
    v_message message)
{
    v_groupInstance _this;

    assert(C_TYPECHECK(group,v_group));
    assert(C_TYPECHECK(message,v_message));

    _this = v_groupAllocInstance(group);
    v_groupInstanceInit(_this,message);

    return _this;
}

static c_bool
lifespanAdminRemove(
    v_groupSample sample,
    c_voidp arg)
{
    v_lifespanAdminRemove(v_lifespanAdmin(arg), v_lifespanSample(sample));
    return TRUE;
}

void
v_groupInstanceFree(
    v_groupInstance instance)
{
    v_group group;
    v_groupSample sample;
    c_array instanceKeyList;
    c_long i, nrOfKeys;

    assert(C_TYPECHECK(instance,v_groupInstance));
    assert((instance->count == 0) == (v_groupInstanceHead(instance) == NULL));
    assert((instance->count == 0) == (v_groupInstanceTail(instance) == NULL));
    CHECK_COUNT(instance);

    if (c_refCount(instance) == 1) {
        c_free(instance->registrations);
        instance->registrations = NULL;
        c_free(instance->unregisterMessages);
        instance->unregisterMessages = NULL;

        /* make sure it is removed from any purge list! */
        instance->epoch = C_TIME_ZERO;

        v_cacheDeinit(v_cache(instance->readerInstanceCache));
        group = v_group(instance->group);
        if (group->cachedInstance == NULL) {
            /*
               Explicit free the sample when this v_groupInstance is
               cached. Because otherwise the reference is overwritten
               when the v_groupInstance is re-used.
               Note: the sample itself is also cached in its own module.
            */
            sample = v_groupInstanceHead(instance);
            c_free(sample);
            v_groupInstanceSetHead(instance,NULL);
            /*
               Also free the key values for this instance, otherwise the
               reference(s) is/are overwritten when the v_groupInstance
               is re-used.
            */
            instanceKeyList = v_groupKeyList(instance->group);
            nrOfKeys = c_arraySize(instanceKeyList);
            for (i = 0; i < nrOfKeys; i++) {
                c_fieldFreeRef(instanceKeyList[i], instance);
            }
            c_free(instanceKeyList);
            group->cachedInstance = c_keep(instance);
        }
    }
    c_free(instance);
}

void
v_groupInstanceDisconnect(
    v_groupInstance instance)
{
    assert(C_TYPECHECK(instance,v_groupInstance));
    assert((instance->count == 0) == (v_groupInstanceHead(instance) == NULL));
    assert((instance->count == 0) == (v_groupInstanceTail(instance) == NULL));
    CHECK_COUNT(instance);

    /* just tear-down the instance pipeline */
    v_cacheDeinit(v_cache(instance->readerInstanceCache));
}

static v_message
createRegistration(
    v_groupInstance instance,
    v_message message)
{
    v_topic topic;
    v_message regmsg;

    if (v_messageState(message) & (~L_REGISTER)) {
        /* The message is an implicit registration!
         * Therefore create a new pure registration method.
         * for late-joining applications to avoid unwanted
         * initial data:)
         */
        topic = v_groupTopic(v_groupInstanceGroup(instance));
        regmsg = v_topicMessageNew(topic);
        memcpy(regmsg, message, C_SIZEOF(v_message));
        v_topicMessageCopyKeyValues(topic, regmsg, message);
        regmsg->qos = c_keep(message->qos);
        v_nodeState(regmsg) = L_REGISTER;
    } else {
        regmsg = c_keep(message);
    }
    return regmsg;
}

v_writeResult
v_groupInstanceRegister (
    v_groupInstance instance,
    v_message message,
    v_message *regMsg)
{
    v_registration *registration;
    v_registration found;
    v_writeResult result;

    assert(instance != NULL);
    assert(C_TYPECHECK(instance,v_groupInstance));
    assert(message != NULL);
    assert(C_TYPECHECK(message,v_message));
    CHECK_COUNT(instance);

    /* First look if an unregister message exists for the message writer.
     * If an unregister message is found then verify if the register
     * message is newer than the unregister message. If the register
     * message is newer then remove the unregister message and insert the
     * register message and otherwise the register message is 'old news' and
     * can be discarded.
     * If there was no unregister message then there could still be a
     * register message so first look for an existing registration before
     * inserting it.
     * In case there already exists a registration for the given writer
     * then keep the newest.
     * Under normal condition register messages are always followd by an
     * unregister message but message can be reordered due to transport.
     */
    registration = &instance->unregisterMessages;
    found = NULL;
    while ((*registration != NULL) && (found == NULL)) {
        if (v_gidCompare((*registration)->message->writerGID,
                         message->writerGID) == C_EQ) {
            found = *registration;
            (*registration) = found->next;
            found->next = NULL;
        } else {
            registration = &((*registration)->next);
        }
    }
    if (found == NULL) {
        /* No unregister message found so start looking for a register
         * message.
         */
        registration = &instance->registrations;
        while ((*registration != NULL) && (found == NULL)) {
            if (v_gidCompare((*registration)->message->writerGID,
                             message->writerGID) == C_EQ) {
                found = *registration;
            } else {
                registration = &((*registration)->next);
            }
        }
        if (found == NULL) {
            /* No existing registration found so insert a new one for
             * this writer.
             */
            found = c_new(v_kernelType(v_objectKernel(instance),
                                       K_REGISTRATION));
            found->message = createRegistration(instance,message);
            found->next = instance->registrations;
            instance->registrations = found;
            *regMsg = c_keep(found->message);
            result = V_WRITE_REGISTERED;
        } else {
            /* if register message is newer than found and is explicitly
             * registered so replace the old one because the new one may
             * have different QoS values.
             * Note that probably in this case the unregister message is
             * belonging to the previous register message is not yet received.
             * It will be discarded when it arrives.
             * Note that the reader are not aware of this situation and
             * do miss a generation count increase!
             */
            if (v_messageStateTest(message, L_REGISTER)) {
                if(c_timeCompare(message->writeTime,
                                 found->message->writeTime) == C_GT) {
                    c_free(found->message);
                    found->message = createRegistration(instance,message);
                    found->message = c_keep(message);
                }
            }
            result = V_WRITE_SUCCESS;
        }
    } else {
        /* check writeTime of unregister message and given message.
           If given message is older, return WRITE_SUCCESS,
           so no explicit register message is send.
        */
        if (c_timeCompare(found->message->writeTime,
                          message->writeTime) == C_GT) {
            /* reinsert as unregister message */
            found->next = instance->unregisterMessages;
            instance->unregisterMessages = found;
            result = V_WRITE_SUCCESS;
        } else {
            c_free(found->message);
            found->message = createRegistration(instance,message);
            found->next = instance->registrations;
            instance->registrations = found;
            *regMsg = c_keep(found->message);
            result = V_WRITE_REGISTERED;
        }
    }
    if (instance->registrations != NULL) {
        v_stateClear(instance->state, L_NOWRITERS);
    }

    CHECK_COUNT(instance);
    return result;
}


static void
v_groupInstanceRemoveUnregistrationIfObsolete(
    v_groupInstance instance,
    v_gid writerGID)
{
	v_registration *unregistration;
	v_registration unregistrationFound;
	v_groupSample sample;
	v_groupSample sampleFound;

	/* First find the corresponding unregister message, if available */
    unregistration = &instance->unregisterMessages;
    unregistrationFound = NULL;
    while ((*unregistration != NULL) && (unregistrationFound == NULL)) {
        if (v_gidCompare((*unregistration)->message->writerGID, writerGID) == C_EQ) {
            unregistrationFound = *unregistration;
        } else {
            unregistration = &((*unregistration)->next);
        }
    }
    /* Only continue in case the unregistration exists */
    if (unregistrationFound != NULL) {
    	/* Now figure out if the instance contains any messages
         * from this gid */
	    sample = v_groupSample(instance->oldest);
	    sampleFound = NULL;
	    while ((sample != NULL) && (sampleFound == NULL)) {
	    	if (v_gidCompare(v_groupSampleTemplate(sample)->message->writerGID, writerGID) == C_EQ) {
	    		sampleFound = sample;
	    	} else {
	            sample = sample->newer;
	    	}
	    }
	    if (sampleFound == NULL) {
	    	/* No sample found for this gid so remove the
                 * corresponding unregister message */
	    	*unregistration = unregistrationFound->next;
	    	unregistrationFound->next = NULL;
	    	c_free(unregistrationFound);
	    }
    }
}


v_writeResult
v_groupInstanceUnregister (
    v_groupInstance instance,
    v_message message)
{
    v_writeResult result;
    v_registration *registration;
    v_registration found;

    assert(instance != NULL);
    assert(C_TYPECHECK(instance,v_groupInstance));
    assert(message != NULL);
    assert(C_TYPECHECK(message,v_message));
    CHECK_COUNT(instance);

    registration = &instance->registrations;
    found = NULL;
    while (((*registration) != NULL) && (found == NULL)) {
        if (v_gidCompare((*registration)->message->writerGID,
                         message->writerGID) == C_EQ) {
            found = *registration;
            (*registration) = found->next;
            found->next = NULL;
        } else {
            /* must be in else clause,
             * since then clause could have NULL-ified..
             */
            registration = &((*registration)->next);
        }
    }
    if (found != NULL) {
        if (c_timeCompare(found->message->writeTime,
                          message->writeTime) == C_GT) {
            /* reinsert registration */
            result = V_WRITE_SUCCESS;
            found->next = instance->registrations;
            instance->registrations = found;
        } else {
            c_free(found->message);
            found->message = c_keep(message);
            found->next = instance->unregisterMessages;
            instance->unregisterMessages = found;
            result = V_WRITE_UNREGISTERED;
        }
    } else {
        /* registration not found, should not happen */
        /* assert(FALSE);*/
        result = V_WRITE_ERROR;
    }
    if (instance->registrations == NULL) {
        /* no more writers, so set state to NOWRITERS */
        v_stateSet(instance->state, L_NOWRITERS);
    }

    if (result == V_WRITE_UNREGISTERED) {
    	/* Unregister succeeded, remove unregistermessage in case there
         * are no messages in the instance at all.
         */
    	v_groupInstanceRemoveUnregistrationIfObsolete(instance, message->writerGID);
    }

    CHECK_COUNT(instance);
    return result;
}

c_bool
v_groupInstanceWalkRegistrations (
    v_groupInstance instance,
    v_groupInstanceWalkRegistrationAction action,
    c_voidp arg)
{
    v_registration registration;
    c_bool proceed = TRUE;

    assert(instance != NULL);
    assert(C_TYPECHECK(instance,v_groupInstance));
    assert(action != NULL);
    CHECK_COUNT(instance);

    registration = instance->registrations;
    while ((registration != NULL) && (proceed == TRUE)) {
        proceed = action(registration,arg);
        registration = registration->next;
    }
    CHECK_COUNT(instance);

    return proceed;
}

c_bool
v_groupInstanceWalkUnregisterMessages (
    v_groupInstance instance,
    v_groupInstanceWalkRegistrationAction action,
    c_voidp arg)
{
    v_registration registration;
    c_bool proceed = TRUE;

    assert(instance != NULL);
    assert(C_TYPECHECK(instance,v_groupInstance));
    assert(action != NULL);
    CHECK_COUNT(instance);

    registration = instance->unregisterMessages;

    while ((registration != NULL) && (proceed == TRUE)) {
        proceed = action(registration,arg);
        registration = registration->next;
    }
    CHECK_COUNT(instance);

    return proceed;
}

v_message
v_groupInstanceCreateMessage(
    v_groupInstance _this)
{
    c_array instanceKeyList;
    c_array messageKeyList;
    c_long i, nrOfKeys;
    v_group group;
    v_message message = NULL;

    if (_this != NULL) {
        group = v_groupInstanceGroup(_this);
        message = v_topicMessageNew(v_groupTopic(group));
        if (message != NULL) {
            messageKeyList = v_topicMessageKeyList(v_groupTopic(group));
            instanceKeyList = v_groupKeyList(group);
            assert(c_arraySize(messageKeyList) == c_arraySize(instanceKeyList));
            nrOfKeys = c_arraySize(messageKeyList);
            for (i=0;i<nrOfKeys;i++) {
                c_fieldCopy(instanceKeyList[i],_this,
                            messageKeyList[i],message);
            }
            c_free(instanceKeyList);
        }
    }
    return message;
}

c_bool
v_groupInstanceWalkSamples (
    v_groupInstance instance,
    v_groupInstanceWalkSampleAction action,
    c_voidp arg)
{
    v_groupSample sample;
    c_bool proceed = TRUE;

    assert(instance != NULL);
    assert(C_TYPECHECK(instance,v_groupInstance));
    assert(action != NULL);

    sample = v_groupSample(instance->oldest);
    while ((sample != NULL) && (proceed == TRUE)) {
        proceed = action(sample,arg);
        sample = sample->newer;
    }
    return proceed;
}

v_writeResult
v_groupInstanceInsert(
    v_groupInstance instance,
    v_message message)
{
    v_group group;
    v_groupSample sample;
    v_groupSample oldest;
    v_groupSample ptr;
    v_state state;
    c_equality equality;

    assert(message != NULL);
    assert(instance != NULL);
    assert(C_TYPECHECK(message,v_message));
    assert(C_TYPECHECK(instance,v_groupInstance));
    assert((instance->count == 0) == (v_groupInstanceHead(instance) == NULL));
    assert((instance->count == 0) == (v_groupInstanceTail(instance) == NULL));
    CHECK_COUNT(instance);

    if (message == NULL) {
        return V_WRITE_PRE_NOT_MET;
    }

    if (v_messageStateTest(message,L_UNREGISTER) ||
        v_messageStateTest(message,L_REGISTER)) {
        return V_WRITE_SUCCESS;
    }

    group = v_group(instance->group);
    sample = v_groupSampleNew(group,message);

    if (v_stateTest(instance->state, L_EMPTY)) {
        assert(v_groupInstanceHead(instance) == NULL);
        assert(v_groupInstanceTail(instance) == NULL);
        assert(instance->count == 0);
        v_groupInstanceSetHead(instance,sample);
        v_stateClear(instance->state, L_EMPTY);
        ptr = NULL;
        equality = C_EQ;
    } else {
        assert(v_groupInstanceHead(instance) != NULL);
        assert(v_groupInstanceTail(instance) != NULL);
        assert(instance->count != 0);
        ptr = v_groupInstanceHead(instance);
        equality = v_timeCompare(message->writeTime,
                                 v_groupSampleMessage(ptr)->writeTime);
        if (equality == C_LT) {
            /* Received an older message, so need to find the right spot
               in the instance. */
            while (ptr->older != NULL) {
                equality = v_timeCompare(message->writeTime,
                                 v_groupSampleMessage(ptr->older)->writeTime);
                if (equality != C_LT) {
                    break;
                }
                ptr = ptr->older;
            }
            sample->newer = ptr;
            sample->older = ptr->older;
            ptr->older = c_keep(sample); /* added keep */
        } else {
            sample->older = v_groupInstanceHead(instance);
            v_groupInstanceSetHead(instance,sample);
        }
    }
    assert(c_refCount(sample) == 2);
    if (sample->older != NULL) {
        sample->older->newer = sample;
    } else {
        v_groupInstanceSetTail(instance,sample);
    }
    sample->instance = instance;
    state = v_nodeState(message);

    if (v_stateTest(state,L_DISPOSED)) {
        instance->count++;
    }
    if (v_stateTest(state, L_WRITE)) {
        if (instance->messageCount < group->depth) {
            instance->count++;
            instance->messageCount++;
            v_lifespanAdminInsert(group->lifespanAdmin,
                                  v_lifespanSample(sample));
        } else {
            /* We have reached max history depth for this instance.
             * So the tail samples must be removed until we have removed a
             * write sample.
             * NOTE: the last sample might be the sample that just has been
             * inserted in the case of order by source timestamp!
             * Note2: after removing the oldest (write) message this algorthm
             * continues to remove any Dispose messages until the next oldest
             * (write) message is found. This avoids unecessary resource
             * consumption by superfluous dispose messages.
             * The instance->messageCount does not need an update as the
             * inserted write sample will also push out a write sample from
             * the history buffer.
             */
            /* Algorithm: walk the history from oldest towards newest.
             * first:     skip all data-less samples since they are not counted.
             * second:    check if the oldest data sample is not the sample
             *            just inserted. If that is true then the inserted
             *            sample is accepted and can be inserted into the
             *            lifespan admin.
             * third:     skip all data-less samples between the oldest data
             *            sample and the next newer data sample.
             * last:      delete all (skipped) samples older than the found
             *            second last oldest data sample.
             */
            ptr = NULL;
            oldest = instance->oldest;
            /* First skip any trailing dispose messages. */
            while (!v_messageStateTest(v_groupSampleMessage(oldest),L_WRITE)) {
                instance->count--;
                ptr = oldest;
                oldest = oldest->newer;
            }
            /* If the last, to be removed data message, is not the inserted
             * sample then add the sample to the lifespan admin.
             */
            if (sample != oldest) {
                v_lifespanAdminInsert(group->lifespanAdmin,
                                      v_lifespanSample(sample));
                v_groupInstanceRemoveUnregistrationIfObsolete(instance,
                                                              v_groupSampleMessage(oldest)->writerGID);
                v_lifespanAdminRemove(group->lifespanAdmin,v_lifespanSample(oldest));
            }
            if (v_messageStateTest(v_groupSampleMessage(oldest),L_DISPOSED)) {
                /* A double write-dipose sample found! */
                instance->count--;
            }
            ptr = oldest;
            oldest = oldest->newer;
            /* First skip any (new) trailing dispose messages.
             * oldest can never become NULL as there are at least
             * 2 WRITE messages in the history buffer.
             * Since the inserted message is a write message and we
             * have reached max history depth.
             */
            while (!v_messageStateTest(v_groupSampleMessage(oldest),L_WRITE)) {
                instance->count--;
                ptr = oldest;
                oldest = oldest->newer;
            }
            v_groupInstanceSetTail(instance,oldest);
            oldest->older = NULL;
            ptr->newer = NULL;

            c_free(ptr); /* free the whole previous list. */
        }
    }
    if (v_messageStateTest(v_groupSampleMessage(v_groupInstanceHead(instance)), L_DISPOSED)) {
        v_stateSet(instance->state, L_DISPOSED);
    } else {
        if (v_stateTest(instance->state, L_DISPOSED)) {
            instance->epoch = C_TIME_ZERO;
            v_stateClear(instance->state, L_DISPOSED);
        }
    }
    c_free(sample);

    CHECK_COUNT(instance);
    assert((instance->count == 0) == (v_groupInstanceTail(instance) == NULL));
    assert((instance->count == 0) == (v_groupInstanceHead(instance) == NULL));

    return V_WRITE_SUCCESS;
}

void
v_groupInstanceRemove (
    v_groupSample sample)
{
    v_groupInstance instance;
    v_state state;

    assert(C_TYPECHECK(sample,v_groupSample));

    if (sample != NULL) {
        instance = v_groupInstance(sample->instance);
        CHECK_COUNT(instance);
        if (sample->newer != NULL) {
            v_groupSample(sample->newer)->older = c_keep(sample->older);
        } else {
            assert(v_groupInstanceHead(instance) == sample);
            v_groupInstanceSetHead(instance,sample->older);
        }
        if (sample->older != NULL) {
            v_groupSample(sample->older)->newer = sample->newer;
        } else {
            assert(v_groupInstanceTail(instance) == sample);
            v_groupInstanceSetTail(instance,sample->newer);
        }
        state = v_nodeState(v_groupSampleMessage(sample));
        if (v_stateTest(state, L_WRITE)) {
            instance->count--;
            instance->messageCount--;
        }
        if (v_stateTest(state, L_DISPOSED)) {
            instance->count--;
        }
        CHECK_COUNT(instance);
        c_free(sample);
        if (instance->oldest == NULL) {
            v_stateSet(instance->state, L_EMPTY);
        }
        CHECK_COUNT(instance);
        assert((instance->count == 0) == (v_groupInstanceTail(instance) == NULL));
        assert((instance->count == 0) == (v_groupInstanceHead(instance) == NULL));
    }
}

void
v_groupInstancePurge(
    v_groupInstance instance)
{
    v_group group;
    c_long disposeCount;

    assert(C_TYPECHECK(instance,v_groupInstance));
    assert(v_stateTest(instance->state, L_DISPOSED | L_NOWRITERS));

    disposeCount = instance->count - instance->messageCount;
    assert(disposeCount >= 0);
    group = v_group(instance->group);
    while ((instance->oldest != NULL) && (disposeCount > 0)) {
        v_lifespanAdminRemove(group->lifespanAdmin,
                              v_lifespanSample(instance->oldest));
        v_groupInstanceRemove(instance->oldest);
        disposeCount = instance->count - instance->messageCount;
    }
    assert(disposeCount == 0);
    assert(instance->messageCount <= instance->count);
    v_stateClear(instance->state, L_DISPOSED);
}


c_time
v_groupInstanceDisposeTime (
    v_groupInstance instance)
{
    assert(instance != NULL);
    assert(C_TYPECHECK(instance,v_groupInstance));

    return v_groupSampleMessage(v_groupInstanceHead(instance->oldest))->writeTime;
}

void
v_groupInstanceGetRegisterMessages(
    v_groupInstance instance,
    c_ulong systemId,
    c_iter *messages)
{
    v_registration reg;

    assert(instance != NULL);
    assert(C_TYPECHECK(instance,v_groupInstance));
    CHECK_COUNT(instance);

    reg = instance->registrations;
    while (reg != NULL) {
        if (v_gidSystemId(reg->message->writerGID) == systemId) {
            *messages = c_iterInsert(*messages, c_keep(reg->message));
        }
        reg = reg->next;
    }
    CHECK_COUNT(instance);
}

v_message
v_groupInstanceGetRegisterMessageOfWriter(
    v_groupInstance instance,
    v_gid writerGid)
{
    v_registration reg;
    v_message found;

    assert(instance != NULL);
    assert(C_TYPECHECK(instance,v_groupInstance));
    CHECK_COUNT(instance);

    found = NULL;
    reg = instance->registrations;
    while ((reg != NULL) && (found == NULL)) {
        if (v_gidCompare(reg->message->writerGID, writerGid) == C_EQ) {
            found = c_keep(reg->message);
        }
        reg = reg->next;
    }
    CHECK_COUNT(instance);

    return found;

}

void
v_groupInstancePurgeTimed(
    v_groupInstance instance,
    c_time purgeTime)
{
    v_group group;
    v_message message;
    c_bool proceed;

    assert(C_TYPECHECK(instance,v_groupInstance));

    group = v_group(instance->group);

    if (instance->oldest != NULL) {
        proceed = TRUE;

        while ((proceed == TRUE) && (instance->oldest)) {
            message = v_groupSampleMessage(instance->oldest);

            if (v_timeCompare(message->writeTime, purgeTime) == C_LT) {
                v_lifespanAdminRemove(group->lifespanAdmin,
                                      v_lifespanSample(instance->oldest));
                v_groupInstanceRemove(instance->oldest);
            } else {
                proceed = FALSE;
            }
        }
    }
    return;
}
