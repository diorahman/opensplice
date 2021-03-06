/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to 2011 PrismTech
 *   Limited and its licensees. All rights reserved. See file:
 *
 *                     $OSPL_HOME/LICENSE 
 *
 *   for full copyright notice and license terms. 
 *
 */

#include "jni_publisherQos.h"
#include <jni.h>
#include "v_kernel.h"
#include "jni__handler.h"
#include "v_publisherQos.h"
#include "os_heap.h"


const c_char*
jni_publisherQosGetPartition(
    JNIEnv* env, 
    jobject jqos)
{
    jfieldID partitionPolicyFieldID, partitionNameFieldID;
    jclass pubQosClass, partPolClass;
    jobject partitionPolicy;
    jstring partitionName;
    const c_char* partition;
    c_char* fullClassName;
    
    partition = NULL;
    
    if(jqos != NULL){    
        fullClassName = jni_getFullName("policy/PublisherQos");
        pubQosClass = (*env)->FindClass(env, fullClassName);
        os_free(fullClassName);
        assert(pubQosClass != NULL);
        
        fullClassName = jni_getFullName("policy/PartitionQosPolicy");
        partPolClass = (*env)->FindClass(env, fullClassName);
        os_free(fullClassName);
        assert(partPolClass != NULL);
            
        fullClassName = jni_getFullRepresentation("policy/PartitionQosPolicy");
        partitionPolicyFieldID = (*env)->GetFieldID(env, pubQosClass, "partition", 
                                    fullClassName); 
        os_free(fullClassName);
        partitionPolicy = (*env)->GetObjectField(env, jqos, partitionPolicyFieldID);
        partitionNameFieldID = (*env)->GetFieldID(env, partPolClass, "name", "Ljava/lang/String;");
        
        partitionName = (jstring) ((*env)->GetObjectField(env, partitionPolicy, partitionNameFieldID)); 
        partition = (*env)->GetStringUTFChars(env, partitionName, 0);
    }
    return partition;
}
