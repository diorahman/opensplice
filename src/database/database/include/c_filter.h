/*
 *                         OpenSplice DDS
 *
 *   This software and documentation are Copyright 2006 to 2009 PrismTech 
 *   Limited and its licensees. All rights reserved. See file:
 *
 *                     $OSPL_HOME/LICENSE 
 *
 *   for full copyright notice and license terms. 
 *
 */
#ifndef C_FILTER_H
#define C_FILTER_H

#include "c_base.h"
#include "os_if.h"

#ifdef OSPL_BUILD_DB
#define OS_API OS_API_EXPORT
#else
#define OS_API OS_API_IMPORT
#endif
/* !!!!!!!!NOTE From here no more includes are allowed!!!!!!! */

C_CLASS(c_filter);

OS_API c_filter
c_filterNew(
    c_type type,
    q_expr predicate,
    c_value params[]);
    
OS_API c_bool
c_filterEval(
    c_filter f,
    c_object o);

C_CLASS(c_qPred);
OS_API c_bool
c_qPredEval(
    c_qPred q,
    c_object o);

#undef OS_API

#endif /* C_FILTER_H */